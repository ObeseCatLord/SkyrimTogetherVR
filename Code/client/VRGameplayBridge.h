#pragma once

#include <cstddef>
#include <cstdint>

#include <vr_common/VRGameplayBridge.h>

namespace SkyrimTogetherVR::GameplayBridgeClient
{
struct Diagnostics
{
    bool Initialized{false};
    bool Ready{false};
    bool Retired{false};
    std::uint32_t OwnerThreadId{0};
    std::uint32_t EndpointState{0};
    std::uint64_t LifecycleEpoch{0};
    GameplayBridge::CapabilityMask RequestedCapabilities{0};
    GameplayBridge::CapabilityMask AvailableCapabilities{0};
    GameplayBridge::CapabilityMask ActiveCapabilities{0};
    std::uint64_t ProducedEventCount{0};
    std::uint64_t ConsumedEventCount{0};
    std::uint64_t SubmittedCommandCount{0};
    std::uint64_t ExecutedCommandCount{0};
    std::uint64_t RejectedCommandCount{0};
    std::uint64_t StaleCommandCount{0};
    std::uint64_t DiscardedEventCount{0};
    std::uint64_t RejectedSubmissionCount{0};
    std::uint64_t EventRingDroppedPushCount{0};
    std::uint64_t CommandRingDroppedPushCount{0};
};

bool Initialize() noexcept;
bool Activate(std::uint32_t aOwnerThreadId) noexcept;
[[nodiscard]] bool RetireSession(GameplayBridge::EpochRetireReason aReason) noexcept;
void Retire() noexcept;

[[nodiscard]] bool IsReady() noexcept;
[[nodiscard]] Diagnostics GetDiagnostics() noexcept;
void UpdateSessionIdentity(std::uint64_t aServerInstanceNonce, std::uint64_t aConnectionGeneration) noexcept;
[[nodiscard]] std::uint64_t GetLifecycleEpoch() noexcept;
[[nodiscard]] GameplayBridge::CapabilityMask GetActiveCapabilities() noexcept;

[[nodiscard]] bool TryConsumeEvent(GameplayBridge::EventRecord& arEvent) noexcept;
[[nodiscard]] bool TrySubmitCommand(GameplayBridge::CommandRecord& arCommand) noexcept;
// Submits one logical multi-record transaction under a shared ActionId. The
// caller retains semantic IDs such as TextId; actor-action staging IDs are
// normalized to the allocated ActionId before publication.
[[nodiscard]] bool TrySubmitCommandBatch(GameplayBridge::CommandRecord* apCommands,
                                         std::size_t aCommandCount) noexcept;
[[nodiscard]] GameplayBridge::CommandPumpResult PumpCommands(std::uint32_t aMaxCommands) noexcept;
} // namespace SkyrimTogetherVR::GameplayBridgeClient
