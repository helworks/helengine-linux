#pragma once
class Core;
class IRuntimeCpuProfileSink;

namespace helengine::linux {
/// <summary>Returns a process-lifetime sink only for a Tracy-instrumented native player.</summary>
::IRuntimeCpuProfileSink* GetLinuxCpuProfileSink();
/// <summary>Publishes component/entity/stage counters after the measured Core frame and emits its Tracy frame marker.</summary>
void FlushLinuxProfilerFrame(::Core* core);
}
