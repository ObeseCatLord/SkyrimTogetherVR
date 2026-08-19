#include <catch2/catch.hpp>

#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>
#include <glm/glm.hpp>

#include <Services/VRActorReplicationService.h>

#include <array>
#include <limits>

namespace
{
namespace Recovery = SkyrimTogetherVR::ActorReplicationRecovery;
namespace GameplayBridge = SkyrimTogetherVR::GameplayBridge;

constexpr std::uint8_t kMaximumRetries = 3;

constexpr std::array kRetryablePreMutationStatuses{
    GameplayBridge::CommandStatus::Inactive,
    GameplayBridge::CommandStatus::StaleEntity,
    GameplayBridge::CommandStatus::InvalidHandle,
    GameplayBridge::CommandStatus::MissingForm,
    GameplayBridge::CommandStatus::MissingCell,
    GameplayBridge::CommandStatus::EngineRejected,
    GameplayBridge::CommandStatus::QueueOverflow,
};
} // namespace

TEST_CASE("VR actor spawn recovery terminalizes retained snapshot ambiguity", "[skyrim-vr][actor-replication]")
{
    for (const auto status : kRetryablePreMutationStatuses) {
        CHECK(Recovery::IsRetryablePreMutationStatus(status));
        CHECK(Recovery::ClassifySpawnResult(status, 0, kMaximumRetries) ==
              Recovery::Disposition::Terminal);
    }

    CHECK(Recovery::ClassifySpawnTimeout(0, kMaximumRetries) == Recovery::Disposition::Terminal);
}

TEST_CASE("VR actor spawn action history is initial-submission only", "[skyrim-vr][actor-replication]")
{
    CHECK(Recovery::ShouldReplaySpawnActionHistory(0));

    for (std::uint8_t attempts{1}; attempts <= kMaximumRetries; ++attempts)
        CHECK_FALSE(Recovery::ShouldReplaySpawnActionHistory(attempts));

    CHECK_FALSE(Recovery::ShouldReplaySpawnActionHistory(std::numeric_limits<std::uint8_t>::max()));
}

TEST_CASE("VR final equipment retries explicit pre-mutation results only", "[skyrim-vr][actor-replication]")
{
    for (const auto status : kRetryablePreMutationStatuses) {
        CHECK(Recovery::ClassifyEquipmentResult(status, true, 0, kMaximumRetries) ==
              Recovery::Disposition::Retry);
    }

    CHECK(Recovery::ClassifyEquipmentTimeout(true, 0, kMaximumRetries) ==
          Recovery::Disposition::Terminal);
    CHECK(Recovery::IsBeforeEquipmentFinalMutation(0, 3));
    CHECK(Recovery::IsBeforeEquipmentFinalMutation(1, 3));
}

TEST_CASE("VR actor replication terminalizes malformed stale and post-commit outcomes", "[skyrim-vr][actor-replication]")
{
    constexpr std::array terminalStatuses{
        GameplayBridge::CommandStatus::Malformed,
        GameplayBridge::CommandStatus::StaleSession,
        GameplayBridge::CommandStatus::StaleEpoch,
        GameplayBridge::CommandStatus::Unsupported,
        GameplayBridge::CommandStatus::WrongThread,
        GameplayBridge::CommandStatus::Degraded,
    };

    for (const auto status : terminalStatuses) {
        CHECK_FALSE(Recovery::IsRetryablePreMutationStatus(status));
        CHECK(Recovery::ClassifySpawnResult(status, 0, kMaximumRetries) ==
              Recovery::Disposition::Terminal);
        CHECK(Recovery::ClassifyEquipmentResult(status, true, 0, kMaximumRetries) ==
              Recovery::Disposition::Terminal);
    }

    CHECK_FALSE(Recovery::IsBeforeEquipmentFinalMutation(2, 3));
    for (const auto status : kRetryablePreMutationStatuses) {
        CHECK(Recovery::ClassifyEquipmentResult(status, false, 0, kMaximumRetries) ==
              Recovery::Disposition::Terminal);
    }
    CHECK(Recovery::ClassifyEquipmentTimeout(false, 0, kMaximumRetries) ==
          Recovery::Disposition::Terminal);
}

TEST_CASE("VR ambiguous timeouts stay terminal regardless of retry budget", "[skyrim-vr][actor-replication]")
{
    for (std::uint8_t attempts{}; attempts < kMaximumRetries; ++attempts) {
        CHECK(Recovery::ClassifySpawnTimeout(attempts, kMaximumRetries) ==
              Recovery::Disposition::Terminal);
        CHECK(Recovery::ClassifyEquipmentTimeout(true, attempts, kMaximumRetries) ==
              Recovery::Disposition::Terminal);
    }

    CHECK(Recovery::ClassifySpawnTimeout(kMaximumRetries, kMaximumRetries) ==
          Recovery::Disposition::Terminal);
    CHECK(Recovery::ClassifyEquipmentTimeout(true, kMaximumRetries, kMaximumRetries) ==
          Recovery::Disposition::Terminal);
}

TEST_CASE("VR actor spawn recovery lifecycle reset drops generation-bound retry state", "[skyrim-vr][actor-replication]")
{
    Recovery::SpawnRecoveryState recovery{};
    recovery.EntityIdentity.ServerInstanceNonce = 0x1122334455667788ull;
    recovery.EntityIdentity.ConnectionGeneration = 7;
    recovery.EntityIdentity.LifecycleEpoch = 9;
    recovery.EntityIdentity.EntityId = 42;
    recovery.EntityIdentity.EntityGeneration = 3;
    recovery.ResyncAttempts = kMaximumRetries;
    recovery.HasEntityIdentity = true;

    CHECK(recovery.MatchesCurrentEntity(recovery.EntityIdentity));
    recovery.Reset();
    CHECK(recovery.ResyncAttempts == 0);
    CHECK_FALSE(recovery.HasEntityIdentity);
    CHECK_FALSE(recovery.MatchesCurrentEntity({}));
    CHECK(Recovery::ClassifySpawnTimeout(recovery.ResyncAttempts, kMaximumRetries) ==
          Recovery::Disposition::Terminal);
}

TEST_CASE("VR final equipment success never schedules a duplicate committed apply", "[skyrim-vr][actor-replication]")
{
    constexpr std::uint16_t kResultCount = 3;
    constexpr auto kFinalResult = static_cast<std::uint16_t>(kResultCount - 1);

    CHECK(GameplayBridge::IsSuccessfulCommandResult(
        GameplayBridge::CommandStatus::Success, GameplayBridge::GameplayDomain::Equipment,
        GameplayBridge::GameplayAction::EquipmentSnapshotEnd));
    CHECK_FALSE(Recovery::IsRetryablePreMutationStatus(GameplayBridge::CommandStatus::Success));
    CHECK(Recovery::ClassifySpawnResult(GameplayBridge::CommandStatus::Success, 0, kMaximumRetries) ==
          Recovery::Disposition::Terminal);
    CHECK_FALSE(Recovery::IsBeforeEquipmentFinalMutation(kFinalResult, kResultCount));
    CHECK(Recovery::ClassifyEquipmentTimeout(false, 0, kMaximumRetries) ==
          Recovery::Disposition::Terminal);
    CHECK(Recovery::ClassifyEquipmentResult(GameplayBridge::CommandStatus::EngineRejected, false, 0,
                                            kMaximumRetries) == Recovery::Disposition::Terminal);
}

TEST_CASE("VR Protocol 17 admission keeps queued sequences FIFO", "[skyrim-vr][actor-replication]")
{
    constexpr std::uint64_t firstAdmission = 41;
    constexpr std::uint64_t secondAdmission = 42;

    CHECK_FALSE(Recovery::IsEarlierAdmissionOrder(firstAdmission, secondAdmission));
    CHECK(Recovery::IsEarlierAdmissionOrder(secondAdmission, firstAdmission));
    CHECK(Recovery::CanRefreshUnadmittedAcceptance(false));
    CHECK_FALSE(Recovery::CanRefreshUnadmittedAcceptance(true));
}

TEST_CASE("VR FIFO admission timeout uses cumulative head age, not retry cadence", "[skyrim-vr][actor-replication]")
{
    constexpr double timeout = 10.0;
    constexpr double retryCadence = 0.25;
    double admissionAge{};
    double retryElapsed{};

    for (std::size_t retry{}; retry < 40; ++retry) {
        admissionAge += retryCadence;
        retryElapsed += retryCadence;
        if (retryElapsed >= retryCadence)
            retryElapsed = 0.0;
    }

    CHECK(retryElapsed == 0.0);
    CHECK(Recovery::HasCumulativeAdmissionTimedOut(admissionAge, timeout));
    CHECK(Recovery::ShouldRetireUnadmittedAdmissionHead(false, true, admissionAge, timeout));
    CHECK_FALSE(Recovery::ShouldRetireUnadmittedAdmissionHead(true, true, admissionAge, timeout));
    CHECK_FALSE(Recovery::ShouldRetireUnadmittedAdmissionHead(false, false, admissionAge, timeout));
}

TEST_CASE("VR Protocol 17 admission suppresses exact duplicates", "[skyrim-vr][actor-replication]")
{
    constexpr auto domain = static_cast<std::uint16_t>(GameplayBridge::GameplayDomain::Inventory);
    CHECK(Recovery::IsDuplicateAdmissionIdentity(17, domain, 100, 0x1234, 0,
                                                  17, domain, 100, 0x1234, 0));
    CHECK_FALSE(Recovery::IsDuplicateAdmissionIdentity(17, domain, 100, 0x1234, 0,
                                                        17, domain, 101, 0x1234, 0));
}

TEST_CASE("VR Protocol 17 ledger retirement permits server ID reuse", "[skyrim-vr][actor-replication]")
{
    constexpr std::uint32_t serverId = 9001;
    constexpr std::uint32_t playerId = 77;

    CHECK(Recovery::IsRetiredLedgerIdentity(serverId, serverId, playerId));
    CHECK(Recovery::IsRetiredLedgerIdentity(playerId, serverId, playerId));
    CHECK_FALSE(Recovery::IsRetiredLedgerIdentity(123, serverId, playerId));
}

TEST_CASE("VR spawn staging detects server and player identity replacement", "[skyrim-vr][actor-replication]")
{
    CHECK_FALSE(Recovery::IsServerIdIdentityReplacement(77, 77));
    CHECK(Recovery::IsServerIdIdentityReplacement(77, 78));
    CHECK(Recovery::IsServerIdIdentityReplacement(0, 78));

    CHECK_FALSE(Recovery::IsPlayerIdIdentityReplacement(77, 9001, 9001));
    CHECK(Recovery::IsPlayerIdIdentityReplacement(77, 9001, 9002));
    CHECK_FALSE(Recovery::IsPlayerIdIdentityReplacement(0, 9001, 9002));

    CHECK_FALSE(Recovery::IsLocalServerIdIdentityReplacement(0, 9001));
    CHECK_FALSE(Recovery::IsLocalServerIdIdentityReplacement(9001, 9001));
    CHECK(Recovery::IsLocalServerIdIdentityReplacement(9001, 9002));
}

TEST_CASE("VR spawn identity distinguishes canonical refresh from entity replacement", "[skyrim-vr][actor-replication]")
{
    GameplayBridge::BridgeIdentity existing{};
    existing.ServerInstanceNonce = 11;
    existing.ConnectionGeneration = 22;
    existing.LifecycleEpoch = 33;
    existing.EntityId = 44;
    existing.EntityGeneration = 5;

    auto refresh = existing;
    refresh.SequenceId = 100;
    refresh.ActionId = 200;
    CHECK(Recovery::IsSameSpawnEntityIdentity(existing, refresh));
    CHECK_FALSE(Recovery::IsSpawnEntityIdentityReplacement(9001, existing, 9001, refresh));

    auto recycledGeneration = existing;
    recycledGeneration.EntityGeneration = 6;
    CHECK(Recovery::IsSpawnEntityIdentityReplacement(9001, existing, 9002, recycledGeneration));

    auto recycledLifecycle = existing;
    recycledLifecycle.LifecycleEpoch = 34;
    CHECK(Recovery::IsSpawnEntityIdentityReplacement(9001, existing, 9001, recycledLifecycle));

    auto unrelatedNpc = existing;
    unrelatedNpc.EntityId = 45;
    CHECK_FALSE(Recovery::IsSpawnEntityIdentityReplacement(9001, existing, 9002, unrelatedNpc));
}

TEST_CASE("VR canonical resync exhaustion remains recoverable with bounded backoff", "[skyrim-vr][actor-replication]")
{
    CHECK_FALSE(Recovery::ShouldRotateCanonicalResyncRequest(2, kMaximumRetries));
    CHECK(Recovery::ShouldRotateCanonicalResyncRequest(kMaximumRetries, kMaximumRetries));
    CHECK(Recovery::CanonicalResyncBackoffMultiplier(0) == 1);
    CHECK(Recovery::CanonicalResyncBackoffMultiplier(1) == 2);
    CHECK(Recovery::CanonicalResyncBackoffMultiplier(4) == 16);
    CHECK(Recovery::CanonicalResyncBackoffMultiplier(42) == 16);
    CHECK(Recovery::ShouldLogCanonicalResyncExhaustion(1));
    CHECK_FALSE(Recovery::ShouldLogCanonicalResyncExhaustion(3));
    CHECK(Recovery::ShouldLogCanonicalResyncExhaustion(4));
}

TEST_CASE("VR canonical resync keeps quarantine on staging allocation failure", "[skyrim-vr][actor-replication]")
{
    CHECK_FALSE(Recovery::CanLiftCanonicalResyncQuarantine(false));
    CHECK(Recovery::CanLiftCanonicalResyncQuarantine(true));
}
