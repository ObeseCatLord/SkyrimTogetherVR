#pragma once

#include "pch.h"

namespace SkyrimTogetherVR::GameplayAdapter
{
using namespace SkyrimTogetherVR::GameplayBridge;

class BridgeEndpoint
{
public:
    static BridgeEndpoint& Get() noexcept;

    [[nodiscard]] bool Attach() noexcept;
    [[nodiscard]] bool IsAttached() const noexcept;
    [[nodiscard]] GameplayBridgeMapping* Mapping() const noexcept;
    [[nodiscard]] bool IsOperational() const noexcept;
    [[nodiscard]] CommandPumpResult ValidateCommandPump(
        std::uint32_t a_callerProcessId,
        std::uint32_t a_callerThreadId,
        std::uint64_t a_lifecycleEpoch) noexcept;
    [[nodiscard]] bool TryPushEvent(const EventRecord& a_record) noexcept;
    [[nodiscard]] BridgeIdentity SnapshotIdentity(std::uint64_t a_sequenceId) const noexcept;
    [[nodiscard]] std::uint64_t NextEventSequence() noexcept;

    void PublishCapabilities() noexcept;
    void Fault(const char* a_reason) noexcept;

private:
    [[nodiscard]] bool ValidateMapping() const noexcept;
    [[nodiscard]] bool ValidateHeader(const MappingHeader& a_header) const noexcept;
    [[nodiscard]] static bool IsSupportedState(EndpointState a_state) noexcept;

    GameplayBridgeMapping* _mapping{};
    std::atomic<std::uint64_t> _eventSequence{};
    std::atomic_bool _attachAttempted{};
};
} // namespace SkyrimTogetherVR::GameplayAdapter
