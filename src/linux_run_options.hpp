#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "linux_output_options.hpp"

namespace helengine::linux {
/// <summary>Validates explicit player content, scene selection and isolated frame-limited execution.</summary>
class LinuxRunOptions final {
public:
    /// <summary>Parses known structured player flags and rejects invalid/missing values before creating any window or GPU resource.</summary>
    LinuxRunOptions(int argc,char** argv);
    /// <summary>Stores the mandatory actual cooked content directory; no current-directory fallback is invented.</summary>
    std::string ContentRoot;
    /// <summary>Registers independently sized tagged outputs in main-layout order.</summary>
    std::vector<LinuxOutputOptions> Outputs;
    /// <summary>Disables focus and the entire input region of every output.</summary>
    bool Noninteractive = false;
    /// <summary>Binds native role titles and completion receipts to the launching session.</summary>
    std::string Session;
    /// <summary>Identifies the supervisor that must remain the direct parent throughout guarded execution.</summary>
    uint32_t ParentPid = 0;
    /// <summary>Stores an optional stable scene ID from the packaged runtime catalog.</summary>
    std::string SceneId;
    /// <summary>Stores an optional explicit validation script whose SDL events enter the ordinary generated input capture path.</summary>
    std::string InputScript;
    /// <summary>Selects real generated-core rendering without creating a window or reading desktop input.</summary>
    bool Offscreen = false;
    /// <summary>Requests counted Vulkan validation explicitly.</summary>
    bool Validation = false;
    /// <summary>Requires an actual software tracing sample pass and central GPU image variation instead of accepting a visible HUD as scene validation.</summary>
    bool RequireTracerProgress=false;
    /// <summary>Checks real shadow receiver pixels by drawing one frozen Core frame with shadows enabled, disabled, then restored.</summary>
    bool RequireShadowReception=false;
    /// <summary>Limits execution for reproducible smoke tests; zero means run until the SDL quit event.</summary>
    uint32_t Frames = 0;
};
}
