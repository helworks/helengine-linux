#include "linux_render_manager_3d.hpp"
#include "linux_render_manager_2d.hpp"
#include "vulkan_renderer_2d.hpp"
#include "linux_runtime_model.hpp"
#include "ModelSubmeshResolver.hpp"
#include "RuntimeSubmesh.hpp"
#include "generated_disposable_deleter.hpp"
#include "RuntimeMaterial.hpp"
#include "ShaderRuntimeMaterial.hpp"
#include "IDrawable3D.hpp"
#include <numeric>
#include "Core.hpp"
#include "ObjectManager.hpp"
#include "ICamera.hpp"
#include "Entity.hpp"
#include "ModelAsset.hpp"
#include "IRenderQueue3D.hpp"
#include <algorithm>
#include <stdexcept>

namespace helengine::linux {
LinuxRenderManager3D::LinuxRenderManager3D(VulkanRenderer2D& renderer,LinuxRenderManager2D& bridge) : Renderer(renderer), Bridge(bridge), MeshRenderer(renderer.GetContext()), NeutralShadowAtlas(MeshRenderer.CreateDepthShadow(1,1)), NeutralPointShadow(MeshRenderer.CreatePointShadow(1)) { }

LinuxRenderManager3D::~LinuxRenderManager3D() {
    MeshRenderer.DiscardFrame();
    for (auto* model : Models) { model->Dispose(); delete model; }
    for (const auto& material : Materials) { material.first->Dispose(); delete material.first; }
}

::RuntimeModel* LinuxRenderManager3D::BuildModelFromRaw(::ModelAsset* data) {
    if (Disposed) { throw std::logic_error("Cannot upload a model through a disposed Linux renderer."); }
    if (!data || !data->Positions || !data->Normals || !data->TexCoords || data->Positions->Length<=0 || data->Positions->Length!=data->Normals->Length || data->Positions->Length!=data->TexCoords->Length) { throw std::invalid_argument("Linux model upload requires matching nonempty position, normal and UV attributes."); }
    std::vector<VulkanMeshRenderer::Vertex> vertices(static_cast<size_t>(data->Positions->Length));
    for (int32_t index=0;index<data->Positions->Length;++index) {
        const auto position=(*data->Positions)[index],normal=(*data->Normals)[index]; const auto uv=(*data->TexCoords)[index];
        vertices[index]={{position.X.ToSingle(),position.Y.ToSingle(),position.Z.ToSingle()},{normal.X.ToSingle(),normal.Y.ToSingle(),normal.Z.ToSingle()},{uv.X.ToSingle(),uv.Y.ToSingle()}};
    }
    std::vector<uint32_t> indices;
    if (data->Indices32 && data->Indices32->Length>0) { indices.assign(data->Indices32->Data,data->Indices32->Data+data->Indices32->Length); }
    else if (data->Indices16 && data->Indices16->Length>0) { indices.assign(data->Indices16->Data,data->Indices16->Data+data->Indices16->Length); }
    else { indices.resize(vertices.size()); std::iota(indices.begin(),indices.end(),0U); }
    std::unique_ptr<Array<::RuntimeSubmesh*>> submeshes(::ModelSubmeshResolver::BuildRuntimeSubmeshes(data));
    std::vector<std::unique_ptr<::RuntimeSubmesh>> temporarySubmeshes;
    for (int32_t index=0;index<submeshes->Length;++index) { temporarySubmeshes.emplace_back((*submeshes)[index]); }
    for (int32_t index=0;index<submeshes->Length;++index) { if ((*submeshes)[index]->get_PrimitiveTopology()!=::ModelPrimitiveTopology::TriangleList) { throw std::runtime_error("Linux indexed model pipeline does not yet support authored line-list topology."); } }
    const size_t handle=MeshRenderer.UploadMesh(vertices,indices);
    GeneratedDisposableOwner<LinuxRuntimeModel> model(new LinuxRuntimeModel(handle));
    try { model->set_Id(data->get_Id()); model->SetBounds(data->BoundsMin,data->BoundsMax); model->SetSubmeshes(submeshes.get()); for (auto& submesh : temporarySubmeshes) { submesh.release(); } Models.emplace(model.get()); }
    catch (...) { MeshRenderer.ReleaseMesh(handle); throw; }
    return model.release();
}

void LinuxRenderManager3D::ReleaseModel(::RuntimeModel* model) {
    auto* native=dynamic_cast<LinuxRuntimeModel*>(model);
    if (!native || !Models.contains(model)) { throw std::invalid_argument("Linux model release requires a live model owned by this renderer."); }
    MeshRenderer.ReleaseMesh(native->Mesh); Models.erase(model); ::RenderManager3D::ReleaseModel(model);
}

void LinuxRenderManager3D::ReleaseMaterial(::RuntimeMaterial* material) {
    if (!material) { throw std::invalid_argument("Linux material release requires a generated runtime material."); }
    const auto found=Materials.find(material);
    if (found!=Materials.end()) { MeshRenderer.ReleaseMaterial(found->second.Pipeline); Materials.erase(found); }
    ::RenderManager3D::ReleaseMaterial(material);
}

void LinuxRenderManager3D::Dispose() {
    if (Disposed) { return; }
    MeshRenderer.DiscardFrame();
    while (!Models.empty()) { ReleaseModel(*Models.begin()); }
    while (!Materials.empty()) { ReleaseMaterial(Materials.begin()->first); }
    ::RenderManager3D::Dispose(); Disposed=true;
}

void LinuxRenderManager3D::Visit(::IDrawable3D* drawable) {
    if (!CurrentCamera) { throw std::logic_error("Linux generated draw visitation requires an active camera."); }
    if (!drawable || !drawable->get_Parent() || !drawable->get_Parent()->get_IsHierarchyEnabled() || !drawable->get_Model()) { return; }
    auto* model=dynamic_cast<LinuxRuntimeModel*>(drawable->get_Model());
    if (!model || !Models.contains(model)) { throw std::runtime_error("Generated drawable references a model owned by another renderer or already released."); }
    auto* submeshes=model->get_Submeshes(); auto* materials=drawable->get_Materials();
    if (!materials || materials->Length<=0 || (materials->Length!=1 && materials->Length<submeshes->Length)) { throw std::runtime_error("Linux drawable requires authored runtime materials for every submesh."); }
    const auto world=drawable->get_Parent()->get_WorldTransformMatrix();
    for (int32_t index=0;index<submeshes->Length;++index) {
        auto* material=dynamic_cast<::ShaderRuntimeMaterial*>((*materials)[materials->Length==1?0:index]);
        if (!material) { throw std::runtime_error("Linux drawable requires a shader-backed material."); }
        const auto found=Materials.find(material->ResolveRootMaterial());
        if (found==Materials.end()) { throw std::runtime_error("Linux drawable root material has no live authored Vulkan pipeline."); }
        std::vector<VulkanMeshRenderer::BufferBinding> buffers; std::vector<VulkanMeshRenderer::ImageBinding> images;
        for (const auto& binding : found->second.Bindings) {
            if (binding.Layout.Type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) { buffers.push_back({binding.Layout.Slot,BuildUniform(material,binding,world)}); }
            else {
                VkDescriptorImageInfo descriptor;
                if (binding.Name=="shadowAtlasTexture" || binding.Name=="shadowAtlasSampler") { descriptor=MeshRenderer.GetShadowDescriptor(CurrentShadowAtlas); }
                else if (binding.Name=="pointShadowSampler") { descriptor=MeshRenderer.GetShadowDescriptor(NeutralPointShadow); }
                else if (binding.Name=="pointShadowTexture0" || binding.Name=="pointShadowTexture1" || binding.Name=="pointShadowTexture2" || binding.Name=="pointShadowTexture3") { descriptor=MeshRenderer.GetShadowDescriptor(CurrentPointShadows[static_cast<size_t>(binding.Name.back()-'0')]); }
                else if (binding.Layout.Type==VK_DESCRIPTOR_TYPE_SAMPLER) { descriptor=Renderer.GetTextureDescriptor(0); }
                else { auto* texture=ResolveMaterialTexture(material,binding.Name); if (!texture) { throw std::runtime_error("Linux material has no required inherited sampled texture: "+binding.Name); } descriptor=Bridge.GetTextureDescriptor(texture); }
                images.push_back({binding.Layout.Slot,descriptor,binding.Layout.ViewType});
            }
        }
        auto* submesh=(*submeshes)[index]; MeshRenderer.QueueDraw(model->Mesh,found->second.Pipeline,buffers,images,submesh->get_IndexStart(),submesh->get_IndexCount()); ++LastDrawCalls;
    }
}

void LinuxRenderManager3D::Draw() {
    if (Disposed || !get_OwnerCore() || !get_OwnerCore()->get_ObjectManager()) { throw std::logic_error("Linux camera dispatch requires initialized generated Core/ObjectManager and a live renderer."); }
    std::vector<::ICamera*> cameras;
    auto* registered = get_OwnerCore()->get_ObjectManager()->get_Cameras();
    if (registered) { for (int32_t index=0;index<registered->get_Count();++index) { auto* camera = (*registered)[index]; if (camera && camera->get_Parent() && camera->get_Parent()->get_IsHierarchyEnabled() && IsCameraVisibleInWindow(camera,get_MainWindowHandle())) { cameras.push_back(camera); } } }
    std::stable_sort(cameras.begin(),cameras.end(),[](auto* left,auto* right) { return left->get_CameraDrawOrder() < right->get_CameraDrawOrder(); });
    LastDrawCalls=0; LastVisibleMeshSamples=0; bool initialized=false;
    for (auto* camera : cameras) {
        if (camera->get_RenderTarget()) { throw std::runtime_error("Linux native offscreen camera RenderTarget resources are not implemented."); }
        auto settings=camera->get_ClearSettings(); auto color=settings.ClearColor;
        const std::array<float,4> clear {color.X.ToSingle(),color.Y.ToSingle(),color.Z.ToSingle(),color.W.ToSingle()};
        auto viewport=camera->get_Viewport(); float x=viewport.X.ToSingle(),y=viewport.Y.ToSingle(),w=viewport.Z.ToSingle(),h=viewport.W.ToSingle();
        if (w>0 && h>0 && w<=1 && h<=1) { x*=Renderer.GetWidth(); y*=Renderer.GetHeight(); w*=Renderer.GetWidth(); h*=Renderer.GetHeight(); }
        if (w<=0 || h<=0) { x=y=0; w=Renderer.GetWidth(); h=Renderer.GetHeight(); }
        auto* queue=camera->get_RenderQueue3D(); const bool has3d=queue && queue->get_Count()>0;
        if (has3d) {
            if (!settings.ClearDepthEnabled || settings.ClearDepth!=1.0f) { throw std::runtime_error("Linux 3D camera requires explicit depth clear 1 until authored depth retention is implemented."); }
            auto position=camera->get_Parent()->get_Position(); auto orientation=camera->get_Parent()->get_Orientation();
            auto forward=::float4::RotateVector(::float3(0.0f,0.0f,-1.0f),orientation); auto up=::float4::RotateVector(::float3(0.0f,1.0f,0.0f),orientation); auto target=position+forward;
            ::float4x4 view,projection; ::float4x4::CreateLookAt__ref0_ref1_ref2_out3(position,target,up,view);
            ::float4x4::CreatePerspectiveFieldOfView__out4(camera->get_FieldOfView(),w/h,camera->get_NearPlaneDistance(),camera->get_FarPlaneDistance(),projection);
            ::float4x4::Multiply__ref0_ref1_out2(view,projection,CurrentViewProjection); CurrentCameraPosition=position; CurrentCamera=camera;
            try { PrepareCameraLights(camera); PrepareCameraShadows(camera); queue->VisitOrdered(this); CurrentCamera=nullptr; MeshRenderer.RenderFrame(Renderer.GetOutputImageView(),Renderer.GetWidth(),Renderer.GetHeight(),clear,initialized&&!settings.ClearColorEnabled,{{static_cast<int32_t>(x),static_cast<int32_t>(y)},{static_cast<uint32_t>(w),static_cast<uint32_t>(h)}}); }
            catch (...) { CurrentCamera=nullptr; MeshRenderer.DiscardFrame(); throw; }
            LastVisibleMeshSamples+=MeshRenderer.GetLastVisibleSamples();
            Renderer.BeginFrame(clear,true);
        } else { MeshRenderer.RenderFrame(Renderer.GetOutputImageView(),Renderer.GetWidth(),Renderer.GetHeight(),clear,initialized&&!settings.ClearColorEnabled); Renderer.BeginFrame(clear,true); }
        Bridge.RenderCamera(camera); LastDrawCalls+=static_cast<int32_t>(Renderer.GetQueuedDrawCount()); LastPixels=Renderer.EndFrameAndReadback(); initialized=true;
    }
    if (!initialized) { Renderer.BeginFrame({0,0,0,1}); LastPixels=Renderer.EndFrameAndReadback(); }
}
}
