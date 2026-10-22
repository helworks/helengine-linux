#include "linux_cpu_profile_sink.hpp"
#include "IUpdateable.hpp"
#include <iostream>
#include <stdexcept>

/// <summary>Provides a real generated-interface updateable without starting a scene or GPU.</summary>
class ProfileTestUpdateable final : public ::IUpdateable {
public:
    /// <summary>Returns a fixed update order for this diagnostic item.</summary>
    uint8_t get_UpdateOrder() override { return Order; }
    /// <summary>Preserves the interface's mutable order contract.</summary>
    void set_UpdateOrder(uint8_t value) override { Order=value; }
    /// <summary>Leaves timing injection under the test's explicit control.</summary>
    void Update() override {}
private:
    /// <summary>Stores the diagnostic item's update order.</summary>
    uint8_t Order=0;
};

/// <summary>Checks stage validation, actual invocation accounting, frame reset and pointer re-registration.</summary>
int main() {
    using helengine::linux::LinuxCpuProfileSink;
    // Plot names must remain alive until Tracy's process-wide asynchronous transport shuts down.
    auto& sink=*new LinuxCpuProfileSink;
    ProfileTestUpdateable item;
    bool rejected=false;
    try { sink.RecordStage(static_cast<::RuntimeCpuProfileStage>(255),1); } catch (const std::out_of_range&) { rejected=true; }
    if (!rejected) { throw std::runtime_error("Unknown profiling stage was silently accepted."); }
    rejected=false;
    try { sink.RecordUpdateable(&item,0,7,12,1); } catch (const std::logic_error&) { rejected=true; }
    if (!rejected) { throw std::runtime_error("Unregistered profiling identity was silently accepted."); }
    sink.RegisterType(&item,7,"ProfileTestUpdateable");
    sink.RecordStage(::RuntimeCpuProfileStage::ObjectManagerUpdate,42);
    sink.RecordUpdateable(&item,0,7,12,11);
    sink.RecordUpdateable(&item,0,7,12,19);
    sink.ClockFault();
    sink.FlushFrame();
    auto summary=sink.GetLastFrameSummary();
    if (summary.ExecutedComponents!=1 || summary.Calls!=2 || summary.ComponentMicroseconds!=30 || summary.StageMicroseconds!=42 || summary.ClockFaults!=1) { throw std::runtime_error("CPU profiling frame totals disagree with the measured calls."); }
    sink.RegisterType(&item,8,"ReplacementAtSameAddress");
    rejected=false;
    try { sink.RecordUpdateable(&item,0,7,13,100); } catch (const std::logic_error&) { rejected=true; }
    if (!rejected) { throw std::runtime_error("CPU profiling accepted an obsolete type registration."); }
    sink.RecordUpdateable(&item,0,8,13,3);
    sink.FlushFrame();
    summary=sink.GetLastFrameSummary();
    if (summary.ExecutedComponents!=1 || summary.Calls!=1 || summary.ComponentMicroseconds!=3 || summary.StageMicroseconds!=0 || summary.ClockFaults!=1) { throw std::runtime_error("CPU profiling did not reset the frame or register the replacement identity."); }
    sink.FlushFrame();
    summary=sink.GetLastFrameSummary();
    if (summary.ExecutedComponents || summary.Calls || summary.ComponentMicroseconds) { throw std::runtime_error("Idle profiling frame retained old duration samples."); }
    if (sink.ReadMicroseconds()==0) { throw std::runtime_error("Monotonic profiling clock is invalid."); }
    std::cout << "generated_cpu_profile_sink=passed\n";
}
