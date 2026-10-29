#pragma once
class Core;
class IRuntimeCpuProfileSink;

namespace helengine::linux {
/// <summary>Returns a process-lifetime sink for opt-in frame capture or a Tracy-instrumented player.</summary>
::IRuntimeCpuProfileSink* GetLinuxCpuProfileSink();
/// <summary>Publishes component/entity/stage counters after the measured Core frame and emits its Tracy frame marker.</summary>
void FlushLinuxProfilerFrame(::Core* core);
}
