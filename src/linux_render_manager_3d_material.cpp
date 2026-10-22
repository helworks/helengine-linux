#include "linux_render_manager_3d.hpp"
#include "linux_render_manager_2d.hpp"
#include "vulkan_renderer_2d.hpp"
#include "ShaderAsset.hpp"
#include "ShaderProgramAsset.hpp"
#include "ShaderBinaryAsset.hpp"
#include "ShaderBindingAsset.hpp"
#include "ShaderMaterialAsset.hpp"
#include "ShaderRuntimeMaterial.hpp"
#include "ShaderRuntimeMaterialLoader.hpp"
#include "MaterialLayout.hpp"
#include "MaterialLayoutBuilder.hpp"
#include "MaterialPropertyBlock.hpp"
#include "MaterialRenderState.hpp"
#include "StandardMaterialTextureBindingDefaults.hpp"
#include "generated_disposable_deleter.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace helengine::linux {
::RuntimeMaterial* LinuxRenderManager3D::BuildMaterialFromRawAsset(::ContentManager* content,HeCppString path) { return ::ShaderRuntimeMaterialLoader::BuildMaterialFromRawAsset(this,content,path); }

::ShaderCompileTarget LinuxRenderManager3D::get_ShaderCompileTarget() { return ::ShaderCompileTarget::Vulkan; }

::ShaderProgramAsset* LinuxRenderManager3D::FindProgram(::ShaderAsset* shader,HeCppString name,::ShaderStage stage) {
    if (shader && shader->Programs) { for (int32_t index=0;index<shader->Programs->Length;++index) { auto* program=(*shader->Programs)[index]; if (program && program->Name==name && program->Stage==stage) { return program; } } }
    throw std::runtime_error("Linux material has no authored shader program for the requested stage: "+name.raw());
}

std::vector<uint32_t> LinuxRenderManager3D::ReadShader(::ShaderAsset* shader,HeCppString name,::ShaderStage stage,HeCppString variant) {
    if (!shader || shader->TargetName!="vulkan" || !shader->Binaries) { throw std::invalid_argument("Linux materials require the cooked Vulkan shader package."); }
    for (int32_t index=0;index<shader->Binaries->Length;++index) {
        auto* binary=(*shader->Binaries)[index];
        if (!binary || binary->TargetName!=shader->TargetName || binary->ProgramName!=name || binary->Stage!=stage || binary->Variant!=variant) { continue; }
        if (!binary->Bytecode || binary->Bytecode->Length<20 || binary->Bytecode->Length%4) { throw std::runtime_error("Authored Vulkan shader binary has an invalid SPIR-V byte length."); }
        std::vector<uint32_t> words(binary->Bytecode->Length/4); std::memcpy(words.data(),binary->Bytecode->Data,binary->Bytecode->Length); return words;
    }
    throw std::runtime_error("Linux material has no exact cooked shader binary for program/variant: "+name.raw()+"/"+variant.raw());
}

void LinuxRenderManager3D::AddBindings(LinuxMaterialResource& resource,::ShaderProgramAsset* program,VkShaderStageFlags stage) {
    if (!program->Bindings) { return; }
    for (int32_t index=0;index<program->Bindings->Length;++index) {
        auto* binding=(*program->Bindings)[index];
        if (!binding || binding->Name.empty() || binding->Slot<0 || binding->Set!=0 || binding->Size<0) { throw std::runtime_error("Linux Vulkan shader reflection requires named set-zero resources with nonnegative slots and sizes."); }
        VkDescriptorType type;
        switch (binding->Type) {
            case ::ShaderResourceType::ConstantBuffer: type=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; break;
            case ::ShaderResourceType::Texture2D: type=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; break;
            case ::ShaderResourceType::Sampler: type=VK_DESCRIPTOR_TYPE_SAMPLER; break;
            default: throw std::runtime_error("Linux authored material resource type is not implemented: "+binding->Name.raw());
        }
        const auto found=std::find_if(resource.Bindings.begin(),resource.Bindings.end(),[binding](const auto& value) { return value.Layout.Slot==static_cast<uint32_t>(binding->Slot); });
        if (found!=resource.Bindings.end()) {
            if (found->Name!=binding->Name.raw() || found->Layout.Type!=type || found->Layout.MinimumBytes!=static_cast<size_t>(binding->Size)) { throw std::runtime_error("Authored shader stages disagree about a shared Vulkan descriptor."); }
            found->Layout.Stages|=stage;
        } else { resource.Bindings.push_back({binding->Name.raw(),{static_cast<uint32_t>(binding->Slot),type,stage,static_cast<size_t>(binding->Size),1}}); }
    }
}

::RuntimeMaterial* LinuxRenderManager3D::BuildMaterialFromRaw(::ShaderMaterialAsset* material,::ShaderAsset* shader) {
    if (Disposed || !material || !shader || material->ShaderAssetId.empty() || material->ShaderAssetId!=shader->get_Id() || !material->RenderState) { throw std::invalid_argument("Linux material upload requires matching authored shader identity, render state and a live renderer."); }
    auto* vertex=FindProgram(shader,material->VertexProgram,::ShaderStage::Vertex);
    auto* pixel=FindProgram(shader,material->PixelProgram,::ShaderStage::Pixel);
    LinuxMaterialResource resource; resource.ShaderId=shader->get_Id().raw(); AddBindings(resource,vertex,VK_SHADER_STAGE_VERTEX_BIT); AddBindings(resource,pixel,VK_SHADER_STAGE_FRAGMENT_BIT);
    std::vector<VulkanMeshRenderer::BindingLayout> bindings; for (const auto& binding : resource.Bindings) { bindings.push_back(binding.Layout); }
    VulkanMeshRenderer::MaterialState state {static_cast<uint32_t>(material->RenderState->get_BlendMode()),static_cast<uint32_t>(material->RenderState->get_CullMode()),material->RenderState->get_DepthTestEnabled(),material->RenderState->get_DepthWriteEnabled()};
    GeneratedDisposableOwner<::ShaderRuntimeMaterial> runtime(new ::ShaderRuntimeMaterial());
    runtime->set_Id(material->get_Id()); runtime->SetLayout(::MaterialLayoutBuilder::Build(material,shader)); runtime->SetRenderState(material->RenderState); runtime->ApplyConstantBufferDefaults(material->ConstantBuffers);
    runtime->set_CastsShadows(material->CastsShadows); runtime->set_ReceivesShadows(material->ReceivesShadows); runtime->set_SupportsNormalMapping(!material->NormalTextureAssetId.empty()); runtime->set_SupportsEmissive(!material->EmissiveTextureAssetId.empty());
    ::StandardMaterialTextureBindingDefaults::Apply(runtime.get(),&Bridge);
    resource.Pipeline=MeshRenderer.UploadMaterial(ReadShader(shader,material->VertexProgram,::ShaderStage::Vertex,material->Variant),vertex->EntryPoint.raw(),ReadShader(shader,material->PixelProgram,::ShaderStage::Pixel,material->Variant),pixel->EntryPoint.raw(),bindings,state);
    try { Materials.emplace(runtime.get(),resource); } catch (...) { MeshRenderer.ReleaseMaterial(resource.Pipeline); throw; }
    return runtime.release();
}

void LinuxRenderManager3D::InvalidateShaderResources(HeCppString shaderId,::ShaderAsset*) {
    for (const auto& resource : Materials) { if (resource.second.ShaderId==shaderId.raw()) { throw std::runtime_error("Release live Linux materials before invalidating their authored shader pipelines."); } }
}

::RuntimeTexture* LinuxRenderManager3D::ResolveMaterialTexture(::ShaderRuntimeMaterial* material,const std::string& name) const {
    while (material) {
        const int32_t index=material->get_Layout()->FindTextureBindingIndex(name);
        if (index>=0) { auto* texture=material->get_Properties()->GetTexture(index); if (texture) { return texture; } }
        material=dynamic_cast<::ShaderRuntimeMaterial*>(material->get_ParentMaterial());
    }
    return nullptr;
}

void LinuxRenderManager3D::AppendShaderMatrix(std::vector<uint8_t>& output,::float4x4 matrix) {
    const float values[] {matrix.M11.ToSingle(),matrix.M21.ToSingle(),matrix.M31.ToSingle(),matrix.M41.ToSingle(),matrix.M12.ToSingle(),matrix.M22.ToSingle(),matrix.M32.ToSingle(),matrix.M42.ToSingle(),matrix.M13.ToSingle(),matrix.M23.ToSingle(),matrix.M33.ToSingle(),matrix.M43.ToSingle(),matrix.M14.ToSingle(),matrix.M24.ToSingle(),matrix.M34.ToSingle(),matrix.M44.ToSingle()};
    const auto* bytes=reinterpret_cast<const uint8_t*>(values); output.insert(output.end(),bytes,bytes+sizeof(values));
}

std::vector<uint8_t> LinuxRenderManager3D::BuildUniform(::ShaderRuntimeMaterial* material,const LinuxMaterialResource::Binding& binding,::float4x4 world) {
    if (binding.Name=="TransformBuffer") {
        if (binding.Layout.MinimumBytes!=224) { throw std::runtime_error("Linux generated transform upload requires the existing 224-byte TransformBuffer shader contract."); }
        ::float4x4 wvp,normal; ::float4x4::Multiply__ref0_ref1_out2(world,CurrentViewProjection,wvp); ::float4x4::InverseTranspose__ref0_out1(world,normal);
        std::vector<uint8_t> bytes; bytes.reserve(224); AppendShaderMatrix(bytes,world); AppendShaderMatrix(bytes,wvp); AppendShaderMatrix(bytes,normal);
        const float tail[] {CurrentCameraPosition.X.ToSingle(),CurrentCameraPosition.Y.ToSingle(),CurrentCameraPosition.Z.ToSingle(),0,material->get_ReceivesShadows()?1.0f:0.0f,material->get_SupportsEmissive()?1.0f:0.0f,0,0};
        const auto* source=reinterpret_cast<const uint8_t*>(tail); bytes.insert(bytes.end(),source,source+sizeof(tail)); return bytes;
    }
    if (binding.Name=="ForwardLightBuffer" || binding.Name=="ShadowBuffer") { throw std::runtime_error("Linux authored lighting/shadow constants require their actual generated light/resource implementation: "+binding.Name); }
    Array<uint8_t>* data=nullptr;
    if (!material->TryResolveConstantBufferData__out1(binding.Name,data) || !data) { throw std::runtime_error("Linux material has no required inherited constant buffer: "+binding.Name); }
    std::unique_ptr<Array<uint8_t>> owned(data);
    if (data->Length<0 || static_cast<size_t>(data->Length)<binding.Layout.MinimumBytes) { throw std::runtime_error("Linux authored material buffer is smaller than its reflected GPU layout: "+binding.Name); }
    return std::vector<uint8_t>(data->Data,data->Data+data->Length);
}
}
