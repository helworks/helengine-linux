#pragma once
#include "IRuntimeCpuProfileSink.hpp"
#include <array>
#include <deque>
#include <string>
#include <unordered_map>

namespace helengine::linux {
/// <summary>Routes the existing generated Core CPU probes to Tracy with stable per-registration component and entity plot identities.</summary>
class LinuxCpuProfileSink final : public ::IRuntimeCpuProfileSink {
public:
    /// <summary>Reports exact integer sample totals independently of Tracy transport availability.</summary>
    struct FrameSummary {
        /// <summary>Counts component registrations which executed during the completed frame.</summary>
        uint64_t ExecutedComponents=0;
        /// <summary>Counts measured update calls, including valid zero-duration samples.</summary>
        uint64_t Calls=0;
        /// <summary>Sums measured component durations in the shared contract's microseconds.</summary>
        uint64_t ComponentMicroseconds=0;
        /// <summary>Sums reported stages, which may contain nested intervals.</summary>
        uint64_t StageMicroseconds=0;
        /// <summary>Counts rejected clock samples over the profiling session.</summary>
        uint64_t ClockFaults=0;
    };
    /// <summary>Reads the monotonic host clock in the generated contract's microsecond units.</summary>
    uint64_t ReadMicroseconds() override;
    /// <summary>Accumulates a validated Core or UI stage outside Tracy transport.</summary>
    void RecordStage(::RuntimeCpuProfileStage stage,uint64_t elapsedMicroseconds) override;
    /// <summary>Accumulates one measured update call without allocating after the identity's first execution.</summary>
    void RecordUpdateable(::IUpdateable* item,int32_t index,uint32_t typeHash,uint32_t ownerSceneEntityId,uint64_t elapsedMicroseconds) override;
    /// <summary>Creates a new registration identity, distinguishing allocations which reuse an old native address.</summary>
    void RegisterType(::IUpdateable* item,uint32_t typeHash,HeCppString typeName) override;
    /// <summary>Counts invalid clocks without converting them into successful zero-duration samples.</summary>
    void ClockFault() override;
    /// <summary>Publishes frame samples after measured work and resets durations while retaining stable Tracy name storage.</summary>
    void FlushFrame();
    /// <summary>Returns the completed frame's exact totals for diagnostics and transport-independent validation.</summary>
    FrameSummary GetLastFrameSummary() const;
private:
    /// <summary>Keeps registration names and counters stable for the lifetime of the sink.</summary>
    struct ComponentRow {
        /// <summary>Stores the resolved concrete type legend.</summary>
        std::string TypeName;
        /// <summary>Checks that recorded samples belong to the currently registered concrete type.</summary>
        uint32_t TypeHash=0;
        /// <summary>Names a unique registration, even when its native address is reused.</summary>
        std::string PlotName;
        /// <summary>Borrows a stable entity aggregate row after the first execution.</summary>
        size_t EntityIndex=SIZE_MAX;
        /// <summary>Accumulates the current frame's measured duration.</summary>
        uint64_t Microseconds=0;
        /// <summary>Counts measured calls independently of clock precision.</summary>
        uint64_t Calls=0;
    };
    /// <summary>Keeps logical owning-entity counters and plot names alive after scene unloading.</summary>
    struct EntityRow {
        /// <summary>Names the owning scene entity and native entity identity.</summary>
        std::string PlotName;
        /// <summary>Accumulates current-frame component durations.</summary>
        uint64_t Microseconds=0;
        /// <summary>Counts executed component calls owned by this entity.</summary>
        uint64_t Calls=0;
    };
    /// <summary>Retains old registration plot names because Tracy queues their pointers asynchronously.</summary>
    std::deque<ComponentRow> Components;
    /// <summary>Retains entity plot names at stable addresses.</summary>
    std::deque<EntityRow> Entities;
    /// <summary>Selects the most recent registration at each borrowed updateable address.</summary>
    std::unordered_map<::IUpdateable*,size_t> ComponentIndices;
    /// <summary>Groups components by actual owning entity address and authored id.</summary>
    std::unordered_map<std::string,size_t> EntityIndices;
    /// <summary>Stores the fixed shared stage range, validated against its generated final enum value.</summary>
    std::array<uint64_t,18> Stages{};
    /// <summary>Counts invalid clocks across frames.</summary>
    uint64_t ClockFaultCount=0;
    /// <summary>Stores the last fully published frame's diagnostic totals.</summary>
    FrameSummary LastFrame;
};
}
