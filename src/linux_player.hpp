#pragma once
#include "linux_run_options.hpp"
#include <memory>
#include <vector>

class RuntimeSceneCatalog;
class StandardPlatformInputConfiguration;
class StandardPlatformActionBinding;
namespace helengine::linux {
/// <summary>Runs the real generated Core, packaged scene catalog and native renderer in a normal SDL host or explicit offscreen validation mode.</summary>
class LinuxPlayer final {
public:
    /// <summary>Loads the selected real scene, runs simulation/render frames and reports genuine submitted work; errors remain fatal.</summary>
    static int Run(const LinuxRunOptions& options);
    /// <summary>Resolves an explicit scene ID or the embedded startup path independently from incidental catalog ordering.</summary>
    static std::string ResolveSceneId(::RuntimeSceneCatalog* catalog,const std::string& requestedSceneId,const std::string& startupPath);
private:
    /// <summary>Builds the generated scene catalog without changing packaged IDs or filtering menu entries.</summary>
    static ::RuntimeSceneCatalog* CreateSceneCatalog();
    /// <summary>Translates the generated standard action manifest into the existing portable input configuration.</summary>
    static ::StandardPlatformInputConfiguration* CreateStandardInput(std::vector<std::unique_ptr<::StandardPlatformActionBinding>>& bindingOwners);
};
}
