#pragma once
#include "IRuntimeCpuProfileSink.hpp"
#include <deque>
#include <string>
#include <unordered_map>

namespace helengine::linux {
/// <summary>Exports existing Core microsecond probes and optionally forwards them to the unchanged Tracy sink.</summary>
class LinuxFrameProfileSink final : public ::IRuntimeCpuProfileSink {
public:
    /// <summary>Borrows the process-lifetime Tracy sink, or accepts null for ordinary Release capture.</summary>
    explicit LinuxFrameProfileSink(::IRuntimeCpuProfileSink* delegate=nullptr);
    /// <summary>Stores the precise last Core clock read; completed probes use that endpoint without resampling.</summary>
    uint64_t ReadMicroseconds() override;
    /// <summary>Exports a measured Core/UI stage ending at the most recent clock read.</summary>
    void RecordStage(::RuntimeCpuProfileStage stage,uint64_t elapsedMicroseconds) override;
    /// <summary>Exports a measured component call with its current registration and owning entity identity.</summary>
    void RecordUpdateable(::IUpdateable* item,int32_t index,uint32_t typeHash,uint32_t ownerSceneEntityId,uint64_t elapsedMicroseconds) override;
    /// <summary>Retains bounded concrete type names and creates a fresh identity when an address is reused.</summary>
    void RegisterType(::IUpdateable* item,uint32_t typeHash,HeCppString typeName) override;
    /// <summary>Rejects broken clock measurements explicitly, so no successful trace can conceal invalid time.</summary>
    void ClockFault() override;
private:
    /// <summary>Limits retained identities and names across the whole capture session.</summary>
    static constexpr size_t RegistrationCapacity=65536;
    /// <summary>Retains concrete names at stable addresses until recorder serialization.</summary>
    std::deque<std::string> Names;
    /// <summary>Retains type hashes for stale registration rejection.</summary>
    std::deque<uint32_t> Hashes;
    /// <summary>Selects the most recent registration for each borrowed address.</summary>
    std::unordered_map<::IUpdateable*,size_t> Registrations;
    /// <summary>Borrows optional Tracy transport without assuming ownership.</summary>
    ::IRuntimeCpuProfileSink* Delegate;
    /// <summary>Stores the epoch endpoint supplied by the last Core probe clock read.</summary>
    uint64_t LastRead=0;
    /// <summary>Validates subtraction before forwarding a completed interval.</summary>
    uint64_t Start(uint64_t elapsedMicroseconds) const;
};
}
