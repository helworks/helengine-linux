#include "linux_cpu_profile_sink.hpp"
#include "Component.hpp"
#include "IUpdateable.hpp"
#include <tracy/Tracy.hpp>
#include <chrono>
#include <cstdlib>
#include <cxxabi.h>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <typeinfo>

namespace helengine::linux {
uint64_t LinuxCpuProfileSink::ReadMicroseconds() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

void LinuxCpuProfileSink::RecordStage(::RuntimeCpuProfileStage stage,uint64_t elapsedMicroseconds) {
    static_assert(static_cast<size_t>(::RuntimeCpuProfileStage::CommandListTrailingClips)==17,"Update the profiler stage names for the generated Core contract.");
    const auto index=static_cast<size_t>(stage);
    if (index==0 || index>=Stages.size()) { throw std::out_of_range("Unknown Core CPU profiling stage."); }
    Stages[index]+=elapsedMicroseconds;
}

void LinuxCpuProfileSink::RegisterType(::IUpdateable* item,uint32_t typeHash,HeCppString typeName) {
    if (!item) { throw std::invalid_argument("CPU profiling registration requires an updateable."); }
    const char* nativeName=typeid(*item).name();
    int status=0;
    std::unique_ptr<char,decltype(&std::free)> readableName(abi::__cxa_demangle(nativeName,nullptr,nullptr,&status),&std::free);
    Components.emplace_back();
    auto& row=Components.back();
    row.TypeName=status==0 && readableName ? readableName.get() : nativeName && nativeName[0] ? nativeName : typeName.raw();
    row.TypeHash=typeHash;
    ComponentIndices[item]=Components.size()-1;
}

void LinuxCpuProfileSink::RecordUpdateable(::IUpdateable* item,int32_t index,uint32_t typeHash,uint32_t ownerSceneEntityId,uint64_t elapsedMicroseconds) {
    (void)index;
    const auto found=ComponentIndices.find(item);
    if (found==ComponentIndices.end()) { throw std::logic_error("Core CPU profiling received an unregistered updateable."); }
    auto& row=Components[found->second];
    if (row.TypeHash!=typeHash) { throw std::logic_error("Core CPU profiling sample type differs from its current registration."); }
    if (row.PlotName.empty()) {
        std::ostringstream name;
        name << "Component.Update/" << row.TypeName << "/entity=" << ownerSceneEntityId << "/registration=" << found->second;
        row.PlotName=name.str();
        auto* component=dynamic_cast<::Component*>(item);
        if (component && component->Parent) {
            std::ostringstream entityName;
            entityName << "Entity.Update/id=" << ownerSceneEntityId << "/instance=" << static_cast<const void*>(component->Parent);
            const auto key=entityName.str();
            const auto entity=EntityIndices.find(key);
            if (entity==EntityIndices.end()) { row.EntityIndex=Entities.size(); EntityIndices.emplace(key,row.EntityIndex); Entities.emplace_back(); Entities.back().PlotName=key; }
            else { row.EntityIndex=entity->second; }
        }
    }
    row.Microseconds+=elapsedMicroseconds; ++row.Calls;
    if (row.EntityIndex!=SIZE_MAX) { auto& entity=Entities[row.EntityIndex]; entity.Microseconds+=elapsedMicroseconds; ++entity.Calls; }
}

void LinuxCpuProfileSink::ClockFault() { ++ClockFaultCount; }

void LinuxCpuProfileSink::FlushFrame() {
    static constexpr const char* names[]={"unused","Core.Stage/EarlyInput.ms","Core.Stage/FrameCounters.ms","Core.Stage/ObjectManagerUpdate.ms","Core.Stage/Audio.ms","Core.Stage/Physics.ms","Core.Stage/LateInput.ms","Core.Stage/PointerInteraction.ms","Core.Stage/FrameBoundary.ms","Core.Stage/RenderManager3D.ms","UI.Stage/CommandListSetup.ms","UI.Stage/Clipping.ms","UI.Stage/Sprites.ms","UI.Stage/TextLayout.ms","UI.Stage/TextEffectsSetup.ms","UI.Stage/GlyphEmission.ms","UI.Stage/RoundedRects.ms","UI.Stage/TrailingClips.ms"};
    FrameSummary result;
    result.ClockFaults=ClockFaultCount;
    for (auto& row : Components) {
        if (!row.PlotName.empty()) { TracyPlot(row.PlotName.c_str(),static_cast<double>(row.Microseconds)/1000.0); }
        if (row.Calls) { ++result.ExecutedComponents; }
        result.Calls+=row.Calls; result.ComponentMicroseconds+=row.Microseconds;
        row.Calls=0; row.Microseconds=0;
    }
    uint64_t executedEntities=0;
    for (auto& row : Entities) {
        TracyPlot(row.PlotName.c_str(),static_cast<double>(row.Microseconds)/1000.0);
        if (row.Calls) { ++executedEntities; }
        row.Calls=0; row.Microseconds=0;
    }
    for (size_t index=1;index<Stages.size();++index) { TracyPlot(names[index],static_cast<double>(Stages[index])/1000.0); result.StageMicroseconds+=Stages[index]; Stages[index]=0; }
    TracyPlot("Core.UpdateableInstances.Executed",static_cast<int64_t>(result.ExecutedComponents));
    TracyPlot("Core.Entities.Updated",static_cast<int64_t>(executedEntities));
    TracyPlot("Core.UpdateableCalls",static_cast<int64_t>(result.Calls));
    TracyPlot("Core.Profile.ClockFaults",static_cast<int64_t>(ClockFaultCount));
    LastFrame=result;
}

LinuxCpuProfileSink::FrameSummary LinuxCpuProfileSink::GetLastFrameSummary() const { return LastFrame; }
}
