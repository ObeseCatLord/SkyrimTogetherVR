#pragma once

#include "vr_common/VRGameplayBridge.h"

#include <cstddef>
#include <cstdint>

namespace SkyrimTogetherVR::GameplayAdapter::BridgeBatchPolicy
{
// A logical batch must be captured under one session and entity generation.
// Action and sequence fields intentionally vary within a batch, but the
// session and entity fields may not.
[[nodiscard]] constexpr bool HasSingleBatchIdentity(
    const GameplayBridge::EventRecord* ap_records, const std::size_t a_count) noexcept
{
    if (!ap_records || a_count == 0)
        return false;
    const auto& identity = ap_records[0].Header.Identity;
    for (std::size_t index = 1; index < a_count; ++index) {
        const auto& candidate = ap_records[index].Header.Identity;
        if (candidate.ServerInstanceNonce != identity.ServerInstanceNonce ||
            candidate.ConnectionGeneration != identity.ConnectionGeneration ||
            candidate.LifecycleEpoch != identity.LifecycleEpoch ||
            candidate.EntityId != identity.EntityId ||
            candidate.EntityGeneration != identity.EntityGeneration)
            return false;
    }
    return true;
}

[[nodiscard]] constexpr bool ShouldLogAggregate(const std::uint64_t a_count) noexcept
{
    return a_count != 0 && (a_count & (a_count - 1)) == 0;
}

[[nodiscard]] constexpr bool CanCoalescePendingBatch(
    const std::size_t a_pendingBatchSize, const std::size_t a_incomingBatchSize) noexcept
{
    return a_pendingBatchSize == 1 && a_incomingBatchSize == 1;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::BridgeBatchPolicy
