#include <catch2/catch.hpp>

#include <TiltedCore/Meta.hpp>

#include <Services/VRDeathRespawnService.h>

namespace
{
namespace Policy = SkyrimTogetherVR::RespawnGoldReconciliation;

constexpr Policy::SessionScope kScope{
    0x1122334455667788ull,
    17,
    23,
};
} // namespace

TEST_CASE("VR respawn gold retries only bounded pre-application failures", "[skyrim-vr][respawn]")
{
    Policy::PendingGoldLoss work{};
    REQUIRE(Policy::Begin(work, kScope, 500));

    REQUIRE(Policy::BeginAttempt(work, 500));
    REQUIRE(Policy::BindAction(work, 101));
    const auto first = Policy::ResolveResult(work, kScope, 101, Policy::ResultKind::RetryableFailure);
    REQUIRE(first.Next == Policy::Disposition::Retry);
    REQUIRE(first.RetryDelaySeconds == Policy::kInitialRetryDelaySeconds);
    REQUIRE(work.RemainingGold == 500);
    REQUIRE(work.ActionId == 0);

    REQUIRE(Policy::BeginAttempt(work, 500));
    REQUIRE(Policy::BindAction(work, 102));
    const auto second = Policy::ResolveResult(work, kScope, 102, Policy::ResultKind::RetryableFailure);
    REQUIRE(second.Next == Policy::Disposition::Retry);
    REQUIRE(second.RetryDelaySeconds == Policy::kInitialRetryDelaySeconds * 2.0);
    REQUIRE(work.Attempts == 2);
}

TEST_CASE("VR respawn gold terminal failures retain the committed loss for session retirement", "[skyrim-vr][respawn]")
{
    Policy::PendingGoldLoss work{};
    REQUIRE(Policy::Begin(work, kScope, 250));

    REQUIRE(Policy::BeginAttempt(work, 250));
    REQUIRE(Policy::BindAction(work, 201));
    const auto malformed = Policy::ResolveResult(work, kScope, 201, Policy::ResultKind::TerminalFailure);
    REQUIRE(malformed.Next == Policy::Disposition::RetireSession);
    REQUIRE(work.Terminal);
    REQUIRE(work.HasPendingLoss());
    REQUIRE(work.RemainingGold == 250);

    Policy::PendingGoldLoss timedOut{};
    REQUIRE(Policy::Begin(timedOut, kScope, 250));
    REQUIRE(Policy::BeginAttempt(timedOut, 250));
    REQUIRE(Policy::BindAction(timedOut, 202));
    const auto timeout = Policy::ResolveResult(timedOut, kScope, 202, Policy::ResultKind::TimedOut);
    REQUIRE(timeout.Next == Policy::Disposition::RetireSession);
    REQUIRE(timedOut.Terminal);
    REQUIRE(timedOut.RemainingGold == 250);
}

TEST_CASE("VR respawn gold ignores results from another lifecycle scope", "[skyrim-vr][respawn]")
{
    Policy::PendingGoldLoss work{};
    REQUIRE(Policy::Begin(work, kScope, 100));
    REQUIRE(Policy::BeginAttempt(work, 100));
    REQUIRE(Policy::BindAction(work, 301));

    auto staleScope = kScope;
    ++staleScope.LifecycleEpoch;
    const auto stale = Policy::ResolveResult(work, staleScope, 301, Policy::ResultKind::Success);
    REQUIRE(stale.Next == Policy::Disposition::Ignored);
    REQUIRE(work.ActionId == 301);
    REQUIRE(work.RemainingGold == 100);
    REQUIRE_FALSE(work.Completed);
}

TEST_CASE("VR respawn gold completes a bridge action exactly once", "[skyrim-vr][respawn]")
{
    Policy::PendingGoldLoss work{};
    REQUIRE(Policy::Begin(work, kScope, 100));
    REQUIRE(Policy::BeginAttempt(work, 100));
    REQUIRE(Policy::BindAction(work, 401));

    const auto completed = Policy::ResolveResult(work, kScope, 401, Policy::ResultKind::Success);
    REQUIRE(completed.Next == Policy::Disposition::Completed);
    REQUIRE(work.Completed);
    REQUIRE_FALSE(work.HasPendingLoss());
    REQUIRE(work.RemainingGold == 0);

    const auto duplicate = Policy::ResolveResult(work, kScope, 401, Policy::ResultKind::Success);
    REQUIRE(duplicate.Next == Policy::Disposition::Ignored);
    REQUIRE(work.RemainingGold == 0);
    REQUIRE(work.Completed);
}
