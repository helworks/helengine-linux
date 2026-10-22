#pragma once
#include "RenderManager3D.hpp"
#include "IRenderVisitor3D.hpp"
#include "IShaderRenderManager3D.hpp"
#include "linux_material_resource.hpp"
#include "float4x4.hpp"
#include "float3.hpp"
#include "ShaderStage.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>

class ShaderProgramAsset;
class ShaderRuntimeMaterial;
class RuntimeTexture;

namespace helengine::linux {
class VulkanRenderer2D;
class LinuxRenderManager2D;
/// <summary>Coordinates generated cameras and the implemented Vulkan 2D pass; rejects unsupported 3D resources explicitly until their native pipelines are implemented.</summary>
class LinuxRenderManager3D final : public ::RenderManager3D, public ::IRenderVisitor3D, public ::IShaderRenderManager3D {
public:
    /// <summary>Borrows the same-device renderer and generated 2D bridge used by each camera.</summary>
    LinuxRenderManager3D(VulkanRenderer2D& renderer,LinuxRenderManager2D& bridge);
    /// <summary>Releases still-owned native models and material wrappers before the borrowed graphics owner is destroyed.</summary>
    ~LinuxRenderManager3D() override;
    /// <summary>Converts configurable vertex values only at the fixed GPU32 boundary and preserves authored model identity, bounds and submesh ranges.</summary>
    ::RuntimeModel* BuildModelFromRaw(::ModelAsset* data) override;
    /// <summary>Uses the generated generic shader material loader and canonical cooked asset identities.</summary>
    ::RuntimeMaterial* BuildMaterialFromRawAsset(::ContentManager* content,HeCppString path) override;
    /// <summary>Builds the generated material layout and immutable native pipeline from authored program binaries and reflected binding metadata.</summary>
    ::RuntimeMaterial* BuildMaterialFromRaw(::ShaderMaterialAsset* material,::ShaderAsset* shader) override;
    /// <summary>Selects the existing cooked Vulkan shader package without instantiating any managed GPU backend.</summary>
    ::ShaderCompileTarget get_ShaderCompileTarget() override;
    /// <summary>Rejects invalidation while a live native material still owns a pipeline from the shader.</summary>
    void InvalidateShaderResources(HeCppString shaderId,::ShaderAsset* shader) override;
    /// <summary>Releases native mesh memory and the generated model's copied submesh ownership.</summary>
    void ReleaseModel(::RuntimeModel* model) override;
    /// <summary>Releases the native root pipeline before disposing generated inherited material metadata.</summary>
    void ReleaseMaterial(::RuntimeMaterial* material) override;
    /// <summary>Releases all native resources after discarding unsent draw packets, with idempotent explicit shutdown.</summary>
    void Dispose() override;
    /// <summary>Queues the drawable's authored material/submesh pairs for the active generated camera.</summary>
    void Visit(::IDrawable3D* drawable) override;
    /// <summary>Visits active cameras in authored order, renders generated 2D commands and synchronizes one complete frame.</summary>
    void Draw() override;
    /// <summary>Reports real submitted draw calls for menu/core smoke tests and generated diagnostics.</summary>
    int32_t get_LastDrawCallCount() override { return LastDrawCalls; }
    /// <summary>Returns the latest actual GPU readback without saving a desktop screenshot or generated image file.</summary>
    const std::vector<uint8_t>& GetLastPixels() const { return LastPixels; }
private:
    /// <summary>Finds the exact authored program and stage rather than substituting a default shader.</summary>
    static ::ShaderProgramAsset* FindProgram(::ShaderAsset* shader,HeCppString name,::ShaderStage stage);
    /// <summary>Copies the exact target/program/stage/variant binary into aligned SPIR-V words.</summary>
    static std::vector<uint32_t> ReadShader(::ShaderAsset* shader,HeCppString name,::ShaderStage stage,HeCppString variant);
    /// <summary>Combines matching per-stage bindings while preserving resource names, sets, unified slots and reflected byte sizes.</summary>
    static void AddBindings(LinuxMaterialResource& resource,::ShaderProgramAsset* program,VkShaderStageFlags stage);
    /// <summary>Resolves a material texture through generated parent inheritance without inventing an image for absent required bindings.</summary>
    ::RuntimeTexture* ResolveMaterialTexture(::ShaderRuntimeMaterial* material,const std::string& name) const;
    /// <summary>Copies a generated row-vector matrix into the column-major fixed GPU32 shader storage boundary.</summary>
    static void AppendShaderMatrix(std::vector<uint8_t>& output,::float4x4 matrix);
    /// <summary>Builds one immutable reflected uniform payload from generated transforms or authored material property bytes.</summary>
    std::vector<uint8_t> BuildUniform(::ShaderRuntimeMaterial* material,const LinuxMaterialResource::Binding& binding,::float4x4 world);
    /// <summary>Snapshots hierarchy-enabled authored lights visible to the active camera into the existing fixed 288-byte forward-light shader contract.</summary>
    void PrepareCameraLights(::ICamera* camera);
    /// <summary>Borrows the native Vulkan renderer.</summary>
    VulkanRenderer2D& Renderer;
    /// <summary>Borrows the generated 2D command bridge.</summary>
    LinuxRenderManager2D& Bridge;
    /// <summary>Owns indexed geometry and authored Vulkan pipelines on the same device as the UI target.</summary>
    VulkanMeshRenderer MeshRenderer;
    /// <summary>Tracks generated model wrappers exclusively owned by this bridge.</summary>
    std::unordered_set<::RuntimeModel*> Models;
    /// <summary>Maps root material wrapper identity to its immutable native shader resource.</summary>
    std::unordered_map<::RuntimeMaterial*,LinuxMaterialResource> Materials;
    /// <summary>Borrows the camera only while synchronously visiting its ordered draw queue.</summary>
    ::ICamera* CurrentCamera=nullptr;
    /// <summary>Retains the generated view-projection matrix without changing the engine's selected arithmetic policy.</summary>
    ::float4x4 CurrentViewProjection;
    /// <summary>Retains the current authored camera position for the fixed GPU shader constants.</summary>
    ::float3 CurrentCameraPosition;
    /// <summary>Stores fixed GPU32 ambient and four forward light slots for the current camera only.</summary>
    std::vector<uint8_t> CurrentForwardLights;
    /// <summary>Prevents uploads after explicit renderer shutdown.</summary>
    bool Disposed=false;
    /// <summary>Stores the actual latest offscreen framebuffer bytes for isolated verification.</summary>
    std::vector<uint8_t> LastPixels;
    /// <summary>Stores the submitted count from the previous completed frame.</summary>
    int32_t LastDrawCalls = 0;
};
}
