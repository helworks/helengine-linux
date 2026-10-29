#include "linux_frame_profile_sink.hpp"
#include "frame_profile_test_updateable.hpp"
#include "vulkan_frame_trace.hpp"
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>

/// <summary>Verifies exact measured endpoints, identity reuse, contract rejection and valid zero-duration samples.</summary>
int main() {
    using helengine::linux::LinuxFrameProfileSink;
    using helengine::vulkan::VulkanFrameTrace;
    LinuxFrameProfileSink sink;
    ProfileTestUpdateable item;
    bool rejected=false;
    try { sink.RecordStage(::RuntimeCpuProfileStage::EarlyInput,0); } catch (const std::logic_error&) { rejected=true; }
    if (!rejected) { throw std::runtime_error("Missing clock endpoint accepted."); }
    sink.RegisterType(&item,7,"ProfileTestUpdateable");
    VulkanFrameTrace::BeginFrame(1);
    const auto begin=sink.ReadMicroseconds();
    uint64_t end=begin;
    while (end-begin<37) { end=sink.ReadMicroseconds(); }
    // An arbitrary delay after the measured read must not move the exported endpoint.
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    sink.RecordUpdateable(&item,3,7,12,37);
    sink.RecordStage(::RuntimeCpuProfileStage::ObjectManagerUpdate,37);
    sink.RegisterType(&item,8,"ReplacementAtSameAddress");
    rejected=false;
    try { sink.RecordUpdateable(&item,3,7,12,1); } catch (const std::logic_error&) { rejected=true; }
    if (!rejected) { throw std::runtime_error("Obsolete registration accepted."); }
    sink.RecordUpdateable(&item,4,8,13,0);
    rejected=false;
    try { sink.RecordStage(static_cast<::RuntimeCpuProfileStage>(255),1); } catch (const std::out_of_range&) { rejected=true; }
    if (!rejected) { throw std::runtime_error("Unknown stage accepted."); }
    rejected=false;
    try { sink.RecordStage(::RuntimeCpuProfileStage::EarlyInput,UINT64_MAX); } catch (const std::logic_error&) { rejected=true; }
    if (!rejected) { throw std::runtime_error("Clock underflow accepted."); }
    std::ostringstream output;
    VulkanFrameTrace::EndFrame(output,0,0,0,0,64,64,true);
    const auto json=output.str();
    const std::regex samples(R"sample("start":(-?[0-9]+),"end":(-?[0-9]+),"typeHash":([0-9]+),"registrationId":([0-9]+),"entityId":([0-9]+),"updateIndex":(-?[0-9]+))sample");
    std::sregex_iterator row(json.begin(),json.end(),samples),finish;
    int count=0;
    int64_t firstEnd=0;
    for (;row!=finish;++row,++count) {
        const auto start=std::stoll((*row)[1]);
        const auto stop=std::stoll((*row)[2]);
        if (count==0) { firstEnd=stop; }
        if (stop!=firstEnd || stop-start!=(count==2 ? 0 : 37000)) { throw std::runtime_error("Bridge altered the measured endpoint or duration."); }
        if (count==0 && ((*row)[3]!="7" || (*row)[4]!="1" || (*row)[5]!="12" || (*row)[6]!="3")) { throw std::runtime_error("Original identity lost."); }
        if (count==2 && ((*row)[3]!="8" || (*row)[4]!="2" || (*row)[5]!="13" || (*row)[6]!="4")) { throw std::runtime_error("Replacement identity lost."); }
    }
    if (count!=3 || json.find("Core.ObjectManagerUpdate")==std::string::npos || json.find("cpuSampleResolutionNs\":1000")==std::string::npos) { throw std::runtime_error("CPU samples or resolution metadata missing."); }
    rejected=false;
    try { sink.ClockFault(); } catch (const std::logic_error&) { rejected=true; }
    if (!rejected) { throw std::runtime_error("Broken clock accepted."); }
    std::cout << "generated_frame_profile_sink=passed\n";
}
