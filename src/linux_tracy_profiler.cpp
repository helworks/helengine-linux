#include "linux_tracy_profiler.hpp"
#if defined(TRACY_ENABLE)
#include "linux_cpu_profile_sink.hpp"
#include "Core.hpp"
#include "ObjectManager.hpp"
#include <tracy/Tracy.hpp>
#include <stdexcept>

namespace helengine::linux {
::IRuntimeCpuProfileSink* GetLinuxCpuProfileSink() {
    // Tracy retains name pointers asynchronously, so the sink intentionally lives until process exit.
    static auto* sink=new LinuxCpuProfileSink;
    return sink;
}

void FlushLinuxProfilerFrame(::Core* core) {
    if (!core) { throw std::invalid_argument("Profiling a frame requires the active Core."); }
    {
        ZoneScopedN("Profiler.EmitComponentSamples");
        static_cast<LinuxCpuProfileSink*>(GetLinuxCpuProfileSink())->FlushFrame();
        auto* objects=core->get_ObjectManager();
        TracyPlot("Core.Entities.Registered",static_cast<int64_t>(objects->get_Entities()->get_Count()));
        TracyPlot("Core.Updateables.Registered",static_cast<int64_t>(objects->get_Updateables()->get_Count()));
        TracyPlot("Render.Drawables2D.Registered",static_cast<int64_t>(objects->get_Drawables2D()->get_Count()));
        TracyPlot("Render.Drawables3D.Registered",static_cast<int64_t>(objects->get_Drawables3D()->get_Count()));
        TracyPlot("Render.Cameras.Registered",static_cast<int64_t>(objects->get_Cameras()->get_Count()));
    }
    FrameMark;
}
}
#else
namespace helengine::linux {
::IRuntimeCpuProfileSink* GetLinuxCpuProfileSink() { return nullptr; }
void FlushLinuxProfilerFrame(::Core*) {}
}
#endif
