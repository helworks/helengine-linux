#pragma once
#include "RenderManager3D.hpp"
#include <vector>

namespace helengine::linux {
class VulkanRenderer2D;
class LinuxRenderManager2D;
/// <summary>Coordinates generated cameras and the implemented Vulkan 2D pass; rejects unsupported 3D resources explicitly until their native pipelines are implemented.</summary>
class LinuxRenderManager3D final : public ::RenderManager3D {
public:
    /// <summary>Borrows the same-device renderer and generated 2D bridge used by each camera.</summary>
    LinuxRenderManager3D(VulkanRenderer2D& renderer,LinuxRenderManager2D& bridge);
    /// <summary>Rejects unsupported native meshes at asset load with the source identity instead of accepting a blank render.</summary>
    ::RuntimeModel* BuildModelFromRaw(::ModelAsset* data) override;
    /// <summary>Visits active cameras in authored order, renders generated 2D commands and synchronizes one complete frame.</summary>
    void Draw() override;
    /// <summary>Reports real submitted draw calls for menu/core smoke tests and generated diagnostics.</summary>
    int32_t get_LastDrawCallCount() override { return LastDrawCalls; }
    /// <summary>Returns the latest actual GPU readback without saving a desktop screenshot or generated image file.</summary>
    const std::vector<uint8_t>& GetLastPixels() const { return LastPixels; }
private:
    /// <summary>Borrows the native Vulkan renderer.</summary>
    VulkanRenderer2D& Renderer;
    /// <summary>Borrows the generated 2D command bridge.</summary>
    LinuxRenderManager2D& Bridge;
    /// <summary>Stores the actual latest offscreen framebuffer bytes for isolated verification.</summary>
    std::vector<uint8_t> LastPixels;
    /// <summary>Stores the submitted count from the previous completed frame.</summary>
    int32_t LastDrawCalls = 0;
};
}
