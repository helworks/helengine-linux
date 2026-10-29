#include "linux_frame_profile_sink.hpp"
#include "IUpdateable.hpp"
#include "vulkan_frame_trace.hpp"
#include <chrono>
#include <cstdlib>
#include <cxxabi.h>
#include <memory>
#include <stdexcept>
#include <typeinfo>

namespace helengine::linux {
LinuxFrameProfileSink::LinuxFrameProfileSink(::IRuntimeCpuProfileSink* delegate) : Delegate(delegate) {}
uint64_t LinuxFrameProfileSink::ReadMicroseconds() {
    const auto now=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now<=0 || static_cast<uint64_t>(now)<LastRead) { ClockFault(); }
    LastRead=static_cast<uint64_t>(now);
    return LastRead;
}
uint64_t LinuxFrameProfileSink::Start(uint64_t elapsedMicroseconds) const {
    if (!LastRead || elapsedMicroseconds>LastRead) { throw std::logic_error("Frame CPU probe requires a valid measured clock endpoint."); }
    return LastRead-elapsedMicroseconds;
}
void LinuxFrameProfileSink::RecordStage(::RuntimeCpuProfileStage stage,uint64_t elapsedMicroseconds) {
    static_assert(static_cast<size_t>(::RuntimeCpuProfileStage::CommandListTrailingClips)==17,"Update trace stage names for the generated Core contract.");
    static constexpr const char* names[]={"unused","Core.EarlyInput","Core.FrameCounters","Core.ObjectManagerUpdate","Core.Audio","Core.Physics","Core.LateInput","Core.PointerInteraction","Core.FrameBoundary","Core.RenderManager3D","UI.CommandListSetup","UI.Clipping","UI.Sprites","UI.TextLayout","UI.TextEffectsSetup","UI.GlyphEmission","UI.RoundedRects","UI.TrailingClips"};
    const auto index=static_cast<size_t>(stage);
    if (index==0 || index>=18) { throw std::out_of_range("Unknown Core CPU profiling stage."); }
    helengine::vulkan::VulkanFrameTrace::RecordCompletedSample(names[index],Start(elapsedMicroseconds),LastRead);
    if (Delegate) { Delegate->RecordStage(stage,elapsedMicroseconds); }
}
void LinuxFrameProfileSink::RegisterType(::IUpdateable* item,uint32_t typeHash,HeCppString typeName) {
    if (!item) { throw std::invalid_argument("Frame CPU registration requires an updateable."); }
    if (Names.size()==RegistrationCapacity) { throw std::overflow_error("Frame CPU registration capacity exceeded."); }
    const char* nativeName=typeid(*item).name();
    int status=0;
    std::unique_ptr<char,decltype(&std::free)> readableName(abi::__cxa_demangle(nativeName,nullptr,nullptr,&status),&std::free);
    const std::string name=status==0 && readableName ? readableName.get() : nativeName && nativeName[0] ? nativeName : typeName.raw();
    if (name.empty() || name.size()>4096) { throw std::invalid_argument("Frame CPU type name is absent or exceeds 4096 bytes."); }
    Names.emplace_back(name); Hashes.push_back(typeHash); Registrations[item]=Names.size()-1;
    if (Delegate) { Delegate->RegisterType(item,typeHash,typeName); }
}
void LinuxFrameProfileSink::RecordUpdateable(::IUpdateable* item,int32_t index,uint32_t typeHash,uint32_t ownerSceneEntityId,uint64_t elapsedMicroseconds) {
    const auto found=Registrations.find(item);
    if (found==Registrations.end() || Hashes[found->second]!=typeHash) { throw std::logic_error("Frame CPU sample requires its current type registration."); }
    if (index<0) { throw std::out_of_range("Frame CPU update index must be nonnegative."); }
    helengine::vulkan::VulkanFrameTrace::RecordCompletedSample(Names[found->second].c_str(),Start(elapsedMicroseconds),LastRead,typeHash,found->second+1,ownerSceneEntityId,index);
    if (Delegate) { Delegate->RecordUpdateable(item,index,typeHash,ownerSceneEntityId,elapsedMicroseconds); }
}
void LinuxFrameProfileSink::ClockFault() {
    if (Delegate) { Delegate->ClockFault(); }
    throw std::logic_error("Frame CPU profiling detected a broken monotonic clock; capture rejected.");
}
}
