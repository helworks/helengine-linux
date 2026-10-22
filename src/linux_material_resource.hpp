#pragma once
#include "vulkan_mesh_renderer.hpp"
#include <string>

namespace helengine::linux {
/// <summary>Retains an authored Vulkan pipeline and exact reflected resource identities independently of generated runtime material inheritance.</summary>
class LinuxMaterialResource final {
public:
    /// <summary>Describes one declared uniform, texture or sampler by its authored identity and unified binding slot.</summary>
    struct Binding {
        /// <summary>Preserves the authored resource name used to resolve inherited material properties.</summary>
        std::string Name;
        /// <summary>Preserves the Vulkan layout reflected from the shader program definition.</summary>
        VulkanMeshRenderer::BindingLayout Layout;
    };
    /// <summary>Identifies the renderer-owned native pipeline for this root material.</summary>
    size_t Pipeline;
    /// <summary>Tracks the source shader so explicit invalidation can reject live material users.</summary>
    std::string ShaderId;
    /// <summary>Retains descriptor identities for immutable per-draw resource snapshots.</summary>
    std::vector<Binding> Bindings;
};
}
