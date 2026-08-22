#include <catch2/catch.hpp>

#include <vr_gameplay_bridge/ActivationHooks.h>
#include <vr_gameplay_bridge/ActorWorldManager.h>

namespace
{
namespace Policy = SkyrimTogetherVR::GameplayAdapter::ActivationHookPolicy;
}

TEST_CASE("Activation hook pins the verified Skyrim VR pre-mutation target", "[skyrim-vr][activation]")
{
    constexpr std::array<std::uint8_t, 32> expectedPrologue{
        0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x48, 0x20, 0x44,
        0x88, 0x40, 0x18, 0x55, 0x56, 0x57, 0x48, 0x8D,
        0x68, 0xB1, 0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00,
        0x00, 0x48, 0xC7, 0x45, 0x17, 0xFE, 0xFF, 0xFF,
    };

    REQUIRE(Policy::HasPinnedTargetConfiguration());
    REQUIRE(Policy::kActivateRefVrRva == 0x002A8300);
    REQUIRE(Policy::kActivateRefVrPrologue == expectedPrologue);
    REQUIRE(Policy::UsesDirectRvaHookTarget());
    REQUIRE(Policy::kCuratedActivateRefOverrideId == 19796);
    REQUIRE(Policy::kCuratedActivateRefOverrideRva == Policy::kActivateRefVrRva);
    REQUIRE(Policy::kLegacyBaseCsvActivateRefVrRva == 0x002B8310);
    REQUIRE(Policy::kActivateRefVrRva != Policy::kLegacyBaseCsvActivateRefVrRva);
}

TEST_CASE("Activation capture only accepts valid non-book local interactions", "[skyrim-vr][activation]")
{
    REQUIRE(Policy::ShouldCapturePreActivation(true, true, true, true, false));
    REQUIRE_FALSE(Policy::ShouldCapturePreActivation(false, true, true, true, false));
    REQUIRE_FALSE(Policy::ShouldCapturePreActivation(true, false, true, true, false));
    REQUIRE_FALSE(Policy::ShouldCapturePreActivation(true, true, false, true, false));
    REQUIRE_FALSE(Policy::ShouldCapturePreActivation(true, true, true, false, false));
    REQUIRE_FALSE(Policy::ShouldCapturePreActivation(true, true, true, true, true));
}

TEST_CASE("Activation policy captures before one non-blocking original call", "[skyrim-vr][activation]")
{
    REQUIRE(Policy::MustCaptureBeforeOriginal());
    REQUIRE(Policy::MustPublishAfterSuccessfulOriginal(true));
    REQUIRE_FALSE(Policy::MustPublishAfterSuccessfulOriginal(false));
    REQUIRE(Policy::IsExactlyOneOriginalCallPolicy(true, 1));
    REQUIRE_FALSE(Policy::IsExactlyOneOriginalCallPolicy(false, 1));
    REQUIRE_FALSE(Policy::IsExactlyOneOriginalCallPolicy(true, 0));
    REQUIRE_FALSE(Policy::IsExactlyOneOriginalCallPolicy(true, 2));
}

TEST_CASE("Canonical open-state application settles observed direction", "[skyrim-vr][activation]")
{
    namespace Open = SkyrimTogetherVR::GameplayAdapter::OpenStatePolicy;

    REQUIRE(Open::IsApplicableAuthoritativeOpenState(Open::kOpen));
    REQUIRE(Open::IsApplicableAuthoritativeOpenState(Open::kOpening));
    REQUIRE(Open::IsApplicableAuthoritativeOpenState(Open::kClosed));
    REQUIRE(Open::IsApplicableAuthoritativeOpenState(Open::kClosing));
    REQUIRE_FALSE(Open::IsApplicableAuthoritativeOpenState(Open::kNone));
    REQUIRE(Open::IsOpenDirection(Open::kOpening));
    REQUIRE_FALSE(Open::IsOpenDirection(Open::kClosing));
    REQUIRE_FALSE(Open::RequiresNativeSet(Open::kOpen, true));
    REQUIRE(Open::RequiresNativeSet(Open::kOpening, true));
    REQUIRE_FALSE(Open::RequiresNativeSet(Open::kClosed, false));
    REQUIRE(Open::RequiresNativeSet(Open::kClosing, false));
}

TEST_CASE("Activation detour replaces the post-state event sink and retains uncertain hooks", "[skyrim-vr][activation]")
{
    REQUIRE_FALSE(Policy::UsesTesActivateEventSink());
    REQUIRE(Policy::ShouldRetainTrampolineOnDetachFailure());
    REQUIRE_FALSE(Policy::ShouldClearHookStateAfterDetach(false));
    REQUIRE(Policy::ShouldClearHookStateAfterDetach(true));
    REQUIRE_FALSE(Policy::ShouldLogAggregate(0));
    REQUIRE(Policy::ShouldLogAggregate(1));
    REQUIRE(Policy::ShouldLogAggregate(2));
    REQUIRE_FALSE(Policy::ShouldLogAggregate(3));
}
