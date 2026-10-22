#pragma once
#include "RuntimeModel.hpp"
#include <cstddef>

namespace helengine::linux {
/// <summary>Associates generated model bounds and copied submesh metadata with one native indexed mesh owned by the Linux renderer.</summary>
class LinuxRuntimeModel final : public ::RuntimeModel {
public:
    /// <summary>Constructs the generated runtime model and records its validated uploaded mesh handle.</summary>
    explicit LinuxRuntimeModel(size_t mesh) : Mesh(mesh) { }
    /// <summary>Identifies the renderer-owned mesh; the wrapper never deletes GPU resources independently.</summary>
    size_t Mesh;
};
}
