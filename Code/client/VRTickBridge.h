#pragma once

#include <cstdint>

namespace SkyrimTogetherVR::TickBridge
{
struct Diagnostics
{
    bool Ready{false};
    bool PermitPending{false};
    std::uint32_t ActivationThreadId{0};
    std::uint64_t ProducedSequence{0};
    std::uint64_t ConsumedSequence{0};
    std::uint64_t PermitPendingSinceMs{0};
    std::uint64_t OwnerHeartbeatMs{0};
    std::uint64_t OwnerUpdateCompletedMs{0};
};

bool Initialize() noexcept;
void Activate() noexcept;
void Retire() noexcept;
std::uint32_t GetActivationThreadId() noexcept;
Diagnostics GetDiagnostics() noexcept;
void RecordOwnerHeartbeat() noexcept;
void RecordOwnerUpdateCompleted(std::uint64_t aSequence) noexcept;
bool TryConsumeUpdatePermit(std::uint64_t& arSequence) noexcept;
std::uint32_t __cdecl Dispatch(std::uint64_t aEpoch, std::uint64_t aSequence, std::uint32_t aExecutorThreadId) noexcept;
std::uint32_t __cdecl CaptureBodyPose(std::uint64_t aEpoch, std::uint64_t aSequence, std::uint32_t aExecutorThreadId) noexcept;
} // namespace SkyrimTogetherVR::TickBridge
