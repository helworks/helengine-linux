#include "linux_tracy_profiler.hpp"
#include "linux_frame_profile_sink.hpp"
#include <cstdlib>
#include <string_view>
#if defined(TRACY_ENABLE)
#include "linux_cpu_profile_sink.hpp"
#include "Core.hpp"
#include "ObjectManager.hpp"
#include <tracy/Tracy.hpp>
#include <stdexcept>

namespace helengine::linux {
/// <summary>Returns the existing process-lifetime Tracy sink independently of optional frame capture.</summary>
static ::IRuntimeCpuProfileSink* GetLinuxTracyCpuProfileSink() {
    // Tracy retains name pointers asynchronously, so the sink intentionally lives until process exit.
    static auto* sink=new LinuxCpuProfileSink;
    return sink;
}

void FlushLinuxProfilerFrame(::Core* core) {
    if (!core) { throw std::invalid_argument("Profiling a frame requires the active Core."); }
    {
        ZoneScopedN("Profiler.EmitComponentSamples");
        static_cast<LinuxCpuProfileSink*>(GetLinuxTracyCpuProfileSink())->FlushFrame();
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
/// <summary>Returns the existing process-lifetime Tracy sink independently of optional frame capture.</summary>
static ::IRuntimeCpuProfileSink* GetLinuxTracyCpuProfileSink() { return nullptr; }
void FlushLinuxProfilerFrame(::Core*) {}
}
#endif

namespace helengine::linux {
::IRuntimeCpuProfileSink* GetLinuxCpuProfileSink() {
    static const char* value=std::getenv("HELENGINE_FRAME_TRACE");
    static const bool enabled=value && std::string_view(value)=="1";
    if (!enabled) { return GetLinuxTracyCpuProfileSink(); }
    // Stable registration names must outlive every buffered frame and Tracy's asynchronous transport.
    static auto* sink=new LinuxFrameProfileSink(GetLinuxTracyCpuProfileSink());
    return sink;
}
}
