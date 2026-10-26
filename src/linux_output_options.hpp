#pragma once
#include <cstdint>
#include <string>

namespace helengine::linux {
/// <summary>Describes one explicitly tagged native output before Core initialization.</summary>
class LinuxOutputOptions final {
public:
    /// <summary>Identifies camera routing and bounded native completion receipts.</summary>
    std::string Tag;
    /// <summary>Requests a positive logical window width.</summary>
    uint32_t Width = 0;
    /// <summary>Requests a positive logical window height.</summary>
    uint32_t Height = 0;
    /// <summary>Requires presentation to preserve rendered alpha.</summary>
    bool Transparent = false;
};
}
