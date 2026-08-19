#include <catch2/catch.hpp>

#include <vr_gameplay_bridge/FaderRecoveryPolicy.h>

namespace
{
using namespace SkyrimTogetherVR::GameplayAdapter::FaderRecoveryPolicy;

PlayerContext MakeContext(const float aX = 0.0F)
{
    return {
        .Player = 0x1000,
        .Base = 0x2000,
        .Cell = 0x3000,
        .PlayerFormId = 0x14,
        .BaseFormId = 0x7,
        .CellFormId = 0x1234,
        .PositionX = aX,
        .PositionY = 20.0F,
        .PositionZ = 30.0F,
    };
}

Observation ExactFader(const bool aActive = true)
{
    return {
        .ServerInstanceNonce = 0xA0,
        .ConnectionGeneration = 0xB0,
        .UiAvailable = true,
        .ExactHudAndFader = true,
        .FaderOnStack = true,
        .FaderActive = aActive,
        .TransitionActive = false,
        .CanQueueHide = true,
        .Context = MakeContext(),
    };
}

void ChangeCell(Observation& arObservation)
{
    arObservation.Context.Cell = 0x4000;
    arObservation.Context.CellFormId = 0x4321;
}
} // namespace

TEST_CASE("Fader recovery never changes offline presentation state")
{
    StateMachine state;
    auto observation = ExactFader();
    observation.ServerInstanceNonce = 0;
    observation.ConnectionGeneration = 0;

    REQUIRE(state.Observe(observation, 0) == Action::None);
    REQUIRE(state.Observe(observation, kFaderHardTimeoutMs + kStableContextDwellMs) == Action::None);

    observation.ServerInstanceNonce = 0xA0;
    observation.ConnectionGeneration = 0xB0;
    REQUIRE(state.Observe(observation, kFaderHardTimeoutMs + kStableContextDwellMs + 1) == Action::None);
    REQUIRE(state.Observe(observation, 3 * kFaderHardTimeoutMs) == Action::None);
}

TEST_CASE("Fader recovery resets its hard timeout after a long loading transition")
{
    StateMachine state;
    auto observation = ExactFader();
    observation.TransitionActive = true;

    REQUIRE(state.Observe(observation, 0) == Action::None);
    REQUIRE(state.Observe(observation, 15000) == Action::None);

    observation.TransitionActive = false;
    observation.LifecycleGeneration = 1;
    REQUIRE(state.Observe(observation, 15001) == Action::None);
    REQUIRE(state.Observe(observation, 15001 + kFaderHardTimeoutMs - 1) == Action::None);
    REQUIRE(state.Observe(observation, 15001 + kFaderHardTimeoutMs) == Action::Candidate);
    REQUIRE(state.Observe(observation, 15001 + kFaderHardTimeoutMs + kStableContextDwellMs) == Action::Hide);
}

TEST_CASE("Fader recovery handles an active stranded Fader only after the hard timeout")
{
    StateMachine state;
    auto observation = ExactFader();

    REQUIRE(state.Observe(observation, 0) == Action::None);
    ChangeCell(observation);
    REQUIRE(state.Observe(observation, 1) == Action::None);
    REQUIRE(state.Observe(observation, kFaderHardTimeoutMs - 1) == Action::None);
    REQUIRE(state.Observe(observation, kFaderHardTimeoutMs) == Action::Candidate);
    REQUIRE(state.Observe(observation, kFaderHardTimeoutMs + kStableContextDwellMs - 1) == Action::None);
    REQUIRE(state.Observe(observation, kFaderHardTimeoutMs + kStableContextDwellMs) == Action::Hide);
}

TEST_CASE("Fader recovery rejects mixed menu states")
{
    StateMachine state;
    auto observation = ExactFader();
    observation.ExactHudAndFader = false;

    REQUIRE(state.Observe(observation, 0) == Action::None);
    REQUIRE(state.Observe(observation, 30000) == Action::None);
}

TEST_CASE("Fader stack classification ignores inactive backing entries")
{
    MenuStackAccumulator stack;
    stack.Observe(MenuIdentity::Other, false, false);
    stack.Observe(MenuIdentity::Other, true, false);
    stack.Observe(MenuIdentity::Hud, true, true);
    stack.Observe(MenuIdentity::Fader, true, true);

    REQUIRE(stack.ExactHudAndFader());
}

TEST_CASE("Fader stack classification rejects any additional live menu")
{
    MenuStackAccumulator stack;
    stack.Observe(MenuIdentity::Hud, true, true);
    stack.Observe(MenuIdentity::Fader, true, true);
    stack.Observe(MenuIdentity::Other, true, true);

    REQUIRE_FALSE(stack.ExactHudAndFader());
}

TEST_CASE("Fader stack classification rejects duplicate live expected menus")
{
    MenuStackAccumulator stack;
    stack.Observe(MenuIdentity::Hud, true, true);
    stack.Observe(MenuIdentity::Hud, true, true);
    stack.Observe(MenuIdentity::Fader, true, true);

    REQUIRE_FALSE(stack.ExactHudAndFader());
}

TEST_CASE("Fader recovery handles inactive stranded Faders only after the hard timeout")
{
    StateMachine state;
    auto observation = ExactFader(false);

    REQUIRE(state.Observe(observation, 0) == Action::None);
    ChangeCell(observation);
    REQUIRE(state.Observe(observation, 1) == Action::None);
    REQUIRE(state.Observe(observation, kFaderHardTimeoutMs - 1) == Action::None);
    REQUIRE(state.Observe(observation, kFaderHardTimeoutMs) == Action::Candidate);
    REQUIRE(state.Observe(observation, kFaderHardTimeoutMs + kStableContextDwellMs) == Action::Hide);
}

TEST_CASE("Fader recovery restarts stable dwell when identity or position changes")
{
    StateMachine state;
    auto observation = ExactFader();

    REQUIRE(state.Observe(observation, 0) == Action::None);
    ChangeCell(observation);
    REQUIRE(state.Observe(observation, kFaderHardTimeoutMs) == Action::Candidate);

    observation.Context.Cell = 0x5000;
    observation.Context.CellFormId = 0x5432;
    REQUIRE(state.Observe(observation, kFaderHardTimeoutMs + 200) == Action::Candidate);

    observation.Context = MakeContext(32.0F);
    REQUIRE(state.Observe(observation, kFaderHardTimeoutMs + 400) == Action::Candidate);
    REQUIRE(state.Observe(observation, kFaderHardTimeoutMs + 400 + kStableContextDwellMs) == Action::Hide);
}

TEST_CASE("Fader recovery suppresses a one-shot hide that never closes")
{
    StateMachine state;
    auto observation = ExactFader();
    const auto hideAt = kFaderHardTimeoutMs + kStableContextDwellMs;

    REQUIRE(state.Observe(observation, 0) == Action::None);
    ChangeCell(observation);
    REQUIRE(state.Observe(observation, kFaderHardTimeoutMs) == Action::Candidate);
    REQUIRE(state.Observe(observation, hideAt) == Action::Hide);
    REQUIRE(state.Observe(observation, hideAt + kHideVerificationTimeoutMs - 1) == Action::None);
    REQUIRE(state.Observe(observation, hideAt + kHideVerificationTimeoutMs) == Action::Suppressed);
    REQUIRE(state.Observe(observation, hideAt + 10000) == Action::None);
}

TEST_CASE("Fader recovery rearms only after a real close and reopen")
{
    StateMachine state;
    auto observation = ExactFader();
    const auto firstHideAt = kFaderHardTimeoutMs + kStableContextDwellMs;

    REQUIRE(state.Observe(observation, 0) == Action::None);
    ChangeCell(observation);
    REQUIRE(state.Observe(observation, kFaderHardTimeoutMs) == Action::Candidate);
    REQUIRE(state.Observe(observation, firstHideAt) == Action::Hide);

    observation.FaderOnStack = false;
    observation.ExactHudAndFader = false;
    REQUIRE(state.Observe(observation, firstHideAt + 1) == Action::Verified);

    observation = ExactFader();
    ChangeCell(observation);
    const auto reopenedAt = firstHideAt + 2;
    REQUIRE(state.Observe(observation, reopenedAt) == Action::None);
    REQUIRE(state.Observe(observation, reopenedAt + kFaderHardTimeoutMs) == Action::None);
    observation.Context.Cell = 0x5000;
    observation.Context.CellFormId = 0x5432;
    REQUIRE(state.Observe(observation, reopenedAt + kFaderHardTimeoutMs + 1) == Action::Candidate);
    REQUIRE(state.Observe(observation, reopenedAt + kFaderHardTimeoutMs + kStableContextDwellMs + 1) == Action::Hide);
}

TEST_CASE("Fader recovery treats an authenticated lifecycle transition as recovery evidence")
{
    StateMachine state;
    auto observation = ExactFader();
    const auto firstHideAt = kFaderHardTimeoutMs + kStableContextDwellMs;

    REQUIRE(state.Observe(observation, 0) == Action::None);
    ChangeCell(observation);
    REQUIRE(state.Observe(observation, kFaderHardTimeoutMs) == Action::Candidate);
    REQUIRE(state.Observe(observation, firstHideAt) == Action::Hide);
    REQUIRE(state.Observe(observation, firstHideAt + kHideVerificationTimeoutMs) == Action::Suppressed);

    observation.LifecycleGeneration = 1;
    const auto resetAt = firstHideAt + kHideVerificationTimeoutMs + 1;
    REQUIRE(state.Observe(observation, resetAt) == Action::None);
    REQUIRE(state.Observe(observation, resetAt + kFaderHardTimeoutMs) == Action::Candidate);
    REQUIRE(state.Observe(observation, resetAt + kFaderHardTimeoutMs + kStableContextDwellMs) == Action::Hide);
}

TEST_CASE("Fader recovery preserves load evidence across temporary transport retirement")
{
    StateMachine state;
    auto observation = ExactFader();

    REQUIRE(state.Observe(observation, 0) == Action::None);

    observation.ServerInstanceNonce = 0;
    observation.ConnectionGeneration = 0;
    observation.LifecycleGeneration = 1;
    REQUIRE(state.Observe(observation, 100) == Action::None);

    observation.ServerInstanceNonce = 0xA0;
    observation.ConnectionGeneration = 0xB1;
    REQUIRE(state.Observe(observation, 200) == Action::None);
    REQUIRE(state.Observe(observation, 200 + kFaderHardTimeoutMs) == Action::Candidate);
    REQUIRE(state.Observe(observation, 200 + kFaderHardTimeoutMs + kStableContextDwellMs) == Action::Hide);
}

TEST_CASE("Fader recovery preserves offline load evidence across an unavailable UI snapshot")
{
    StateMachine state;
    auto observation = ExactFader();

    REQUIRE(state.Observe(observation, 0) == Action::None);

    observation.ServerInstanceNonce = 0;
    observation.ConnectionGeneration = 0;
    observation.LifecycleGeneration = 1;
    REQUIRE(state.Observe(observation, 100) == Action::None);

    observation.UiAvailable = false;
    observation.FaderOnStack = false;
    observation.ExactHudAndFader = false;
    REQUIRE(state.Observe(observation, 150) == Action::None);

    observation = ExactFader();
    observation.ConnectionGeneration = 0xB1;
    observation.LifecycleGeneration = 1;
    REQUIRE(state.Observe(observation, 200) == Action::None);
    REQUIRE(state.Observe(observation, 200 + kFaderHardTimeoutMs) == Action::Candidate);
    REQUIRE(state.Observe(observation, 200 + kFaderHardTimeoutMs + kStableContextDwellMs) == Action::Hide);
}

TEST_CASE("Fader recovery clears pending load evidence when the Fader closes offline")
{
    StateMachine state;
    auto observation = ExactFader();
    REQUIRE(state.Observe(observation, 0) == Action::None);

    observation.ServerInstanceNonce = 0;
    observation.ConnectionGeneration = 0;
    observation.LifecycleGeneration = 1;
    REQUIRE(state.Observe(observation, 100) == Action::None);

    observation.FaderOnStack = false;
    observation.ExactHudAndFader = false;
    REQUIRE(state.Observe(observation, 200) == Action::None);

    observation = ExactFader();
    observation.ConnectionGeneration = 0xB1;
    observation.LifecycleGeneration = 1;
    REQUIRE(state.Observe(observation, 300) == Action::None);
    REQUIRE(state.Observe(observation, 300 + 2 * kFaderHardTimeoutMs) == Action::None);
}

TEST_CASE("Fader recovery waits for additional live menus to close")
{
    StateMachine state;
    auto observation = ExactFader();
    REQUIRE(state.Observe(observation, 0) == Action::None);

    observation.LifecycleGeneration = 1;
    observation.ExactHudAndFader = false;
    REQUIRE(state.Observe(observation, 100) == Action::None);
    REQUIRE(state.Observe(observation, 100 + kFaderHardTimeoutMs) == Action::None);

    observation.ExactHudAndFader = true;
    REQUIRE(state.Observe(observation, 100 + kFaderHardTimeoutMs + 1) == Action::Candidate);
    REQUIRE(state.Observe(observation, 100 + kFaderHardTimeoutMs + 1 + kStableContextDwellMs) == Action::Hide);
}

TEST_CASE("Fader recovery requires a cell transition and ignores a new player identity")
{
    StateMachine state;
    auto observation = ExactFader();

    REQUIRE(state.Observe(observation, 0) == Action::None);
    observation.Context.Player = 0x1100;
    observation.Context.PlayerFormId = 0x15;
    REQUIRE(state.Observe(observation, 1) == Action::None);
    REQUIRE(state.Observe(observation, kFaderHardTimeoutMs + 1) == Action::None);

    ChangeCell(observation);
    REQUIRE(state.Observe(observation, kFaderHardTimeoutMs + 2) == Action::Candidate);
}

TEST_CASE("Fader recovery resets and rearms on direct authenticated session rollover")
{
    StateMachine state;
    auto observation = ExactFader();

    REQUIRE(state.Observe(observation, 0) == Action::None);
    observation.ServerInstanceNonce = 0xC0;
    observation.ConnectionGeneration = 0xD0;
    constexpr std::uint64_t rolloverAt = 500;
    REQUIRE(state.Observe(observation, rolloverAt) == Action::None);
    REQUIRE(state.Observe(observation, rolloverAt + kFaderHardTimeoutMs - 1) == Action::None);
    REQUIRE(state.Observe(observation, rolloverAt + kFaderHardTimeoutMs) == Action::Candidate);
    REQUIRE(state.Observe(observation, rolloverAt + kFaderHardTimeoutMs + kStableContextDwellMs) == Action::Hide);
}

TEST_CASE("Fader recovery leaves an intentional long active fade alone without transition evidence")
{
    StateMachine state;
    const auto observation = ExactFader(true);

    REQUIRE(state.Observe(observation, 0) == Action::None);
    REQUIRE(state.Observe(observation, 10 * kFaderHardTimeoutMs) == Action::None);
}
