#pragma once
#include "linux_run_options.hpp"
#include <memory>
#include <vector>

class RuntimeSceneCatalog;
class StandardPlatformInputConfiguration;
class StandardPlatformActionBinding;
class Core;
namespace helengine::vulkan { class VulkanRenderManager3D; }
namespace helengine::linux {
using helengine::vulkan::VulkanRenderManager3D;
/// <summary>Runs the real generated Core, packaged scene catalog and native renderer in a normal SDL host or explicit offscreen validation mode.</summary>
class LinuxPlayer final {
public:
    /// <summary>Identifies scale-bearing completed-output receipts independently of the supervisor JSON protocol.</summary>
    static constexpr int NativeProtocolVersion = 2;
    /// <summary>Loads the selected real scene, runs simulation/render frames and reports genuine submitted work; errors remain fatal.</summary>
    static int Run(const LinuxRunOptions& options);
    /// <summary>Resolves an explicit scene ID or the embedded startup path independently from incidental catalog ordering.</summary>
    static std::string ResolveSceneId(::RuntimeSceneCatalog* catalog,const std::string& requestedSceneId,const std::string& startupPath);
private:
    /// <summary>Builds the generated scene catalog without changing packaged IDs or filtering menu entries.</summary>
    static ::RuntimeSceneCatalog* CreateSceneCatalog();
    /// <summary>Translates the generated standard action manifest into the existing portable input configuration.</summary>
    static ::StandardPlatformInputConfiguration* CreateStandardInput(std::vector<std::unique_ptr<::StandardPlatformActionBinding>>& bindingOwners);
    /// <summary>Reads optional generated menu diagnostics during explicit input replay, without invoking menu actions or modifying generated state.</summary>
    static std::string SnapshotMenuState(::Core* core);
    /// <summary>Rejects failed software tracing sessions and optionally requires a complete real 320x240 sample pass plus central GPU output variation.</summary>
    static void ValidateSoftwareTracer(::Core* core,bool requireProgress,const std::vector<uint8_t>& pixels,uint32_t width,uint32_t height);
    /// <summary>Requires central scene pixels to respond to the existing shadow toggle and restore exactly without advancing simulation.</summary>
    static void ValidateShadowReception(::Core* core,VulkanRenderManager3D& renderer,uint32_t width,uint32_t height);
};
}
