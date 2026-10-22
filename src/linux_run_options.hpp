#pragma once
#include <cstdint>
#include <string>

namespace helengine::linux {
/// <summary>Validates explicit player content, scene selection and isolated frame-limited execution.</summary>
class LinuxRunOptions final {
public:
    /// <summary>Parses known structured player flags and rejects invalid/missing values before creating any window or GPU resource.</summary>
    LinuxRunOptions(int argc,char** argv);
    /// <summary>Stores the mandatory actual cooked content directory; no current-directory fallback is invented.</summary>
    std::string ContentRoot;
    /// <summary>Stores an optional stable scene ID from the packaged runtime catalog.</summary>
    std::string SceneId;
    /// <summary>Selects real generated-core rendering without creating a window or reading desktop input.</summary>
    bool Offscreen = false;
    /// <summary>Requests counted Vulkan validation explicitly.</summary>
    bool Validation = false;
    /// <summary>Limits execution for reproducible smoke tests; zero means run until the SDL quit event.</summary>
    uint32_t Frames = 0;
};
}
