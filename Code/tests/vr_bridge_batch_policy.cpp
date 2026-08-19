#include <catch2/catch.hpp>

#include <vr_gameplay_bridge/BridgeBatchPolicy.h>
#include <vr_common/VRGameplayBridge.h>

#include <array>
#include <cstdint>

namespace
{
namespace BatchPolicy = SkyrimTogetherVR::GameplayAdapter::BridgeBatchPolicy;
namespace GameplayBridge = SkyrimTogetherVR::GameplayBridge;

[[nodiscard]] GameplayBridge::EventRecord MakeRecord(
    const std::uint64_t a_nonce, const std::uint64_t a_generation,
    const std::uint64_t a_epoch) noexcept
{
    GameplayBridge::EventRecord record{};
    record.Header.Identity.ServerInstanceNonce = a_nonce;
    record.Header.Identity.ConnectionGeneration = a_generation;
    record.Header.Identity.LifecycleEpoch = a_epoch;
    return record;
}
} // namespace

TEST_CASE("VR bridge batch policy requires one lifecycle and entity identity", "[skyrim-vr][gameplay-bridge][batch]")
{
    std::array records{
        MakeRecord(11, 7, 3),
        MakeRecord(11, 7, 3),
        MakeRecord(11, 7, 3),
    };

    CHECK(BatchPolicy::HasSingleBatchIdentity(records.data(), records.size()));

    records[2].Header.Identity.LifecycleEpoch = 4;
    CHECK_FALSE(BatchPolicy::HasSingleBatchIdentity(records.data(), records.size()));
    records[2].Header.Identity.LifecycleEpoch = 3;
    records[2].Header.Identity.EntityGeneration = 1;
    CHECK_FALSE(BatchPolicy::HasSingleBatchIdentity(records.data(), records.size()));
    CHECK_FALSE(BatchPolicy::HasSingleBatchIdentity(nullptr, 0));
}

TEST_CASE("VR bridge batch rejection logging is aggregate and rate limited", "[skyrim-vr][gameplay-bridge][batch]")
{
    CHECK_FALSE(BatchPolicy::ShouldLogAggregate(0));
    CHECK(BatchPolicy::ShouldLogAggregate(1));
    CHECK(BatchPolicy::ShouldLogAggregate(2));
    CHECK_FALSE(BatchPolicy::ShouldLogAggregate(3));
    CHECK(BatchPolicy::ShouldLogAggregate(4));
}

TEST_CASE("VR bridge batch policy never coalesces transaction members", "[skyrim-vr][gameplay-bridge][batch]")
{
    CHECK(BatchPolicy::CanCoalescePendingBatch(1, 1));
    CHECK_FALSE(BatchPolicy::CanCoalescePendingBatch(2, 1));
    CHECK_FALSE(BatchPolicy::CanCoalescePendingBatch(1, 2));
    CHECK_FALSE(BatchPolicy::CanCoalescePendingBatch(3, 4));
}
