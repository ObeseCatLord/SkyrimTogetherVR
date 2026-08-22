#include <catch2/catch.hpp>

#include <Services/DeferredNativeGameplayCoreClose.h>
#include <Services/VRRehydrationState.h>
#include <Structs/VRHiggsRelayState.h>

#include <array>
#include <string_view>

TEST_CASE("VR rehydration requires every lifecycle milestone", "[skyrim-vr][lifecycle]")
{
    constexpr std::array states{
        VRRehydrationState::Offline,
        VRRehydrationState::Stable,
        VRRehydrationState::Connecting,
        VRRehydrationState::Authenticated,
        VRRehydrationState::Bootstrap,
        VRRehydrationState::Assigned,
        VRRehydrationState::DomainsActive,
        VRRehydrationState::Ready,
    };

    for (std::size_t index = 1; index < states.size(); ++index)
        CHECK(CanTransitionVRRehydrationState(states[index - 1], states[index]));

    CHECK_FALSE(CanTransitionVRRehydrationState(
        VRRehydrationState::Authenticated, VRRehydrationState::Assigned));
    CHECK_FALSE(CanTransitionVRRehydrationState(
        VRRehydrationState::Bootstrap, VRRehydrationState::Ready));
    CHECK_FALSE(CanTransitionVRRehydrationState(
        VRRehydrationState::Ready, VRRehydrationState::Bootstrap));
}

TEST_CASE("VR rehydration profiles have explicit terminal paths", "[skyrim-vr][lifecycle]")
{
    CHECK_FALSE(VRRehydrationProfileRequiresAvatar(VRRehydrationProfile::ConnectionOnly));
    CHECK(VRRehydrationProfileRequiresAvatar(VRRehydrationProfile::AvatarSync));
    CHECK(VRRehydrationProfileRequiresAvatar(VRRehydrationProfile::Gameplay));
    CHECK(std::string_view{VRRehydrationProfileName(VRRehydrationProfile::ConnectionOnly)} == "connection_only");
    CHECK(CanTransitionVRRehydrationState(VRRehydrationState::Authenticated, VRRehydrationState::Ready));
    CHECK(CanTransitionVRRehydrationState(VRRehydrationState::Retiring, VRRehydrationState::Stable));
    CHECK(CanTransitionVRRehydrationState(VRRehydrationState::Stable, VRRehydrationState::Connecting));
    CHECK_FALSE(CanTransitionVRRehydrationState(VRRehydrationState::Retiring, VRRehydrationState::Connecting));
}

TEST_CASE("deferred native-parity closes are bound to one authenticated session", "[skyrim-vr][lifecycle]")
{
    constexpr DeferredNativeGameplayCoreCloseToken token{
        0x10,
        0x20,
    };

    CHECK(IsCurrentDeferredNativeGameplayCoreClose(token, true, 0x10, 0x20));
    CHECK_FALSE(IsCurrentDeferredNativeGameplayCoreClose(token, false, 0x10, 0x20));
    CHECK_FALSE(IsCurrentDeferredNativeGameplayCoreClose(token, true, 0x11, 0x20));
    CHECK_FALSE(IsCurrentDeferredNativeGameplayCoreClose(token, true, 0x10, 0x21));
    CHECK_FALSE(IsCurrentDeferredNativeGameplayCoreClose({}, true, 0x10, 0x20));
}

TEST_CASE("HIGGS relay negotiation requires an operational bridge snapshot", "[skyrim-vr][lifecycle]")
{
    VRHiggsRelayState state{};
    CHECK_FALSE(IsVRHiggsRelayOperational(state));

    state.BridgeLoaded = true;
    state.Detected = true;
    state.InterfaceAvailable = true;
    state.CallbacksRegistered = true;
    state.SnapshotAvailable = true;
    CHECK_FALSE(IsVRHiggsRelayOperational(state));

    state.SnapshotSequence = 1;
    CHECK(IsVRHiggsRelayOperational(state));

    state.CallbacksRegistered = false;
    CHECK_FALSE(IsVRHiggsRelayOperational(state));
}

TEST_CASE("VR rehydration permits retirement and explicit retry but not implicit recovery", "[skyrim-vr][lifecycle]")
{
    CHECK(CanTransitionVRRehydrationState(VRRehydrationState::Ready, VRRehydrationState::Retiring));
    CHECK(CanTransitionVRRehydrationState(VRRehydrationState::Retiring, VRRehydrationState::Stable));
    CHECK(CanTransitionVRRehydrationState(VRRehydrationState::Bootstrap, VRRehydrationState::Failed));
    CHECK(IsVRRehydrationTerminal(VRRehydrationState::Failed));
    CHECK(CanTransitionVRRehydrationState(VRRehydrationState::Failed, VRRehydrationState::Stable));
    CHECK_FALSE(CanTransitionVRRehydrationState(VRRehydrationState::Failed, VRRehydrationState::Connecting));
    CHECK_FALSE(CanTransitionVRRehydrationState(VRRehydrationState::Failed, VRRehydrationState::Retiring));
}

TEST_CASE("VR rehydration exposes finite deadlines for active connection stages", "[skyrim-vr][lifecycle]")
{
    CHECK(VRRehydrationDeadlineSeconds(VRRehydrationState::Connecting) > 0.0);
    CHECK(VRRehydrationDeadlineSeconds(VRRehydrationState::Authenticated) > 0.0);
    CHECK(VRRehydrationDeadlineSeconds(VRRehydrationState::Bootstrap) > 0.0);
    CHECK(VRRehydrationDeadlineSeconds(VRRehydrationState::Assigned) > 0.0);
    CHECK(VRRehydrationDeadlineSeconds(VRRehydrationState::DomainsActive) > 0.0);
    CHECK(VRRehydrationDeadlineSeconds(VRRehydrationState::Ready) == 0.0);
    CHECK(std::string_view{VRRehydrationStateName(VRRehydrationState::Ready)} == "ready");
}
