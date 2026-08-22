#include <catch2/catch.hpp>

#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>
#include <glm/glm.hpp>

#include <Services/VRLocalGameplayService.h>
#include <vr_gameplay_bridge/LocalGameplayCapture.h>

namespace
{
namespace CapturePolicy = SkyrimTogetherVR::GameplayAdapter::LocalGameplayCapture::ActivationPolicy;
namespace ServicePolicy = SkyrimTogetherVR::VRLocalGameplayPolicy;

constexpr std::uint32_t kOwnedNpcReferenceFormId = 0xFF001234;
constexpr std::uint32_t kOwnedNpcServerId = 0x8021;
constexpr std::uint32_t kLocalPlayerServerId = 0x1337;
}

TEST_CASE("Pre-activation capture is restricted to the local player", "[skyrim-vr][activation][npc]")
{
    REQUIRE(CapturePolicy::ShouldCapturePreActivation(
        true, CapturePolicy::kPlayerReferenceFormId, true, true, true, true, false));
    const auto player = CapturePolicy::EncodeActivator(true, CapturePolicy::kPlayerReferenceFormId);
    REQUIRE(player.TargetHandle.Value == SkyrimTogetherVR::GameplayBridge::kLocalPlayerHandle.Value);
    REQUIRE(player.TargetLocalFormId == CapturePolicy::kPlayerReferenceFormId);

    REQUIRE_FALSE(CapturePolicy::ShouldCapturePreActivation(
        true, kOwnedNpcReferenceFormId, false, true, true, true, false));
    // The decoder retains bounded non-player identities for wire compatibility,
    // but this detour no longer originates them from arbitrary engine threads.
    const auto ownedNpc = CapturePolicy::EncodeActivator(false, kOwnedNpcReferenceFormId);
    REQUIRE(ownedNpc.TargetHandle.Value == 0);
    REQUIRE(ownedNpc.TargetLocalFormId == kOwnedNpcReferenceFormId);
    REQUIRE(CapturePolicy::IsValidEncodedActivator(ownedNpc.TargetHandle, ownedNpc.TargetLocalFormId));
}

TEST_CASE("Pre-activation rejects non-actors and malformed targets", "[skyrim-vr][activation][npc]")
{
    REQUIRE_FALSE(CapturePolicy::ShouldCapturePreActivation(
        false, kOwnedNpcReferenceFormId, false, true, true, true, false));
    REQUIRE_FALSE(CapturePolicy::ShouldCapturePreActivation(
        true, kOwnedNpcReferenceFormId, false, false, true, true, false));
    REQUIRE_FALSE(CapturePolicy::ShouldCapturePreActivation(
        true, 0, false, true, true, true, false));
}

TEST_CASE("Activate resolves the encoded actor and never falls back to the player", "[skyrim-vr][activation][npc]")
{
    const auto playerServerId = ServicePolicy::ResolveActivateActivatorServerId(
        SkyrimTogetherVR::GameplayBridge::kLocalPlayerHandle, ServicePolicy::kPlayerReferenceFormId,
        kLocalPlayerServerId);
    REQUIRE(playerServerId == kLocalPlayerServerId);

    const SkyrimTogetherVR::GameplayBridge::AdapterHandle nonPlayerHandle{};
    const auto ownedNpcServerId = ServicePolicy::ResolveActivateActivatorServerId(
        nonPlayerHandle, kOwnedNpcReferenceFormId, kOwnedNpcServerId);
    REQUIRE(ownedNpcServerId == kOwnedNpcServerId);

    const auto unresolvedNpcServerId = ServicePolicy::ResolveActivateActivatorServerId(
        nonPlayerHandle, kOwnedNpcReferenceFormId, 0);
    REQUIRE(unresolvedNpcServerId == 0);
    REQUIRE(unresolvedNpcServerId != kLocalPlayerServerId);

    REQUIRE_FALSE(ServicePolicy::IsValidActivateActivator(
        SkyrimTogetherVR::GameplayBridge::kLocalPlayerHandle, kOwnedNpcReferenceFormId));
    REQUIRE_FALSE(ServicePolicy::IsValidActivateActivator(nonPlayerHandle, ServicePolicy::kPlayerReferenceFormId));
}
