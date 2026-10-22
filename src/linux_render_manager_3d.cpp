#include "linux_render_manager_3d.hpp"
#include "linux_render_manager_2d.hpp"
#include "vulkan_renderer_2d.hpp"
#include "Core.hpp"
#include "ObjectManager.hpp"
#include "ICamera.hpp"
#include "Entity.hpp"
#include "ModelAsset.hpp"
#include "IRenderQueue3D.hpp"
#include <algorithm>
#include <stdexcept>

namespace helengine::linux {
LinuxRenderManager3D::LinuxRenderManager3D(VulkanRenderer2D& renderer,LinuxRenderManager2D& bridge) : Renderer(renderer), Bridge(bridge) { }

::RuntimeModel* LinuxRenderManager3D::BuildModelFromRaw(::ModelAsset* data) {
    throw std::runtime_error("Linux native mesh/material rendering is not implemented for asset: " + (data ? data->get_Id().raw() : std::string("<null>")));
}

void LinuxRenderManager3D::Draw() {
    if (!get_OwnerCore() || !get_OwnerCore()->get_ObjectManager()) { throw std::logic_error("Linux camera dispatch requires initialized generated Core/ObjectManager."); }
    std::vector<::ICamera*> cameras;
    auto* registered = get_OwnerCore()->get_ObjectManager()->get_Cameras();
    if (registered) { for (int32_t index=0;index<registered->get_Count();++index) { auto* camera = (*registered)[index]; if (camera && camera->get_Parent() && camera->get_Parent()->get_IsHierarchyEnabled() && IsCameraVisibleInWindow(camera,get_MainWindowHandle())) { cameras.push_back(camera); } } }
    std::stable_sort(cameras.begin(),cameras.end(),[](auto* left,auto* right) { return left->get_CameraDrawOrder() < right->get_CameraDrawOrder(); });
    for (auto* camera : cameras) { if (camera->get_RenderQueue3D() && camera->get_RenderQueue3D()->get_Count()) { throw std::runtime_error("Active generated camera requires the incomplete Linux native 3D draw pipeline."); } }
    std::array<float,4> clear {0,0,0,1};
    for (auto* camera : cameras) { auto settings = camera->get_ClearSettings(); if (settings.ClearColorEnabled) { auto color=settings.ClearColor; clear={color.X.ToSingle(),color.Y.ToSingle(),color.Z.ToSingle(),color.W.ToSingle()}; break; } }
    Renderer.BeginFrame(clear);
    for (auto* camera : cameras) { Bridge.RenderCamera(camera); }
    LastDrawCalls=static_cast<int32_t>(Renderer.GetQueuedDrawCount()); LastPixels=Renderer.EndFrameAndReadback();
}
}
