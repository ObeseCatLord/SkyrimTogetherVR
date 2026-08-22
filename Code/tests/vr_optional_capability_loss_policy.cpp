#include <catch2/catch.hpp>

#include <Services/OptionalVRCapabilityLossPolicy.h>

TEST_CASE("optional VR capability monitoring only faults negotiated relays", "[skyrim-vr][transport]")
{
    using namespace SkyrimTogether::Protocol;
    using namespace SkyrimTogetherVR;

    const auto higgs = ToMask(GameplayCapability::VRHiggsRelay);
    const auto planck = ToMask(GameplayCapability::PlanckPhysicsInterface002);

    CHECK(ExtractNegotiatedOptionalVRCapabilities(kCoreCapabilities | higgs | planck) == (higgs | planck));
    CHECK(ExtractNegotiatedOptionalVRCapabilities(kCoreCapabilities) == 0);

    CHECK(DetectOptionalVRCapabilityLoss(0, false, false) == OptionalVRCapabilityLoss::None);
    CHECK(DetectOptionalVRCapabilityLoss(higgs, false, true) == OptionalVRCapabilityLoss::VRHiggsRelay);
    CHECK(DetectOptionalVRCapabilityLoss(planck, true, false) == OptionalVRCapabilityLoss::PlanckPhysicsInterface002);
    CHECK(DetectOptionalVRCapabilityLoss(higgs | planck, true, true) == OptionalVRCapabilityLoss::None);
    CHECK(GetUnavailableNegotiatedOptionalVRCapabilities(higgs | planck, false, false) == (higgs | planck));
}

TEST_CASE("deferred optional VR capability close is bound to one accepted generation", "[skyrim-vr][transport]")
{
    using namespace SkyrimTogetherVR;

    constexpr DeferredOptionalVRCapabilityCloseToken token{
        0x10,
        0x20,
    };

    CHECK(IsCurrentDeferredOptionalVRCapabilityClose(token, true, 0x10, 0x20));
    CHECK_FALSE(IsCurrentDeferredOptionalVRCapabilityClose(token, false, 0x10, 0x20));
    CHECK_FALSE(IsCurrentDeferredOptionalVRCapabilityClose(token, true, 0x11, 0x20));
    CHECK_FALSE(IsCurrentDeferredOptionalVRCapabilityClose(token, true, 0x10, 0x21));
    CHECK_FALSE(IsCurrentDeferredOptionalVRCapabilityClose({}, true, 0x10, 0x20));
}
