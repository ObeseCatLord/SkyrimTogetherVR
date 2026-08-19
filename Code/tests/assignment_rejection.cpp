#include <catch2/catch.hpp>

#include <Structs/GameplayCapabilities.h>

#include <entt/entt.hpp>

TEST_CASE("assignment rejection is limited to VR gameplay clients", "[skyrim-vr][assignment]")
{
    using namespace SkyrimTogether::Protocol;

    CHECK_FALSE(CanReceiveAssignmentRejection(kCoreCapabilities));
    CHECK_FALSE(CanReceiveAssignmentRejection(kCoreCapabilities | ToMask(GameplayCapability::VRPoseRelay)));
    CHECK_FALSE(CanReceiveAssignmentRejection(kCoreCapabilities | ToMask(GameplayCapability::VrClient)));
    CHECK_FALSE(CanReceiveAssignmentRejection(
        kCoreCapabilities | ToMask(GameplayCapability::VrClient) |
        ToMask(GameplayCapability::VrGameplayClient)));
}

TEST_CASE("production VR profile builder emits legal capability masks", "[skyrim-vr][authentication]")
{
    using namespace SkyrimTogether::Protocol;

    CHECK(kGameplayProtocolRevision == 15);
    const auto requestedRelays = kFunctionalVRRelayCapabilities |
                                 ToMask(GameplayCapability::VREquipmentRelay);
    const auto connectionOnly = BuildVRProductionCapabilities(
        VRProductionProfile::ConnectionOnly, requestedRelays, true);
    const auto avatarSync = BuildVRProductionCapabilities(
        VRProductionProfile::AvatarSync, requestedRelays, true);
    const auto gameplay = BuildVRProductionCapabilities(
        VRProductionProfile::Gameplay, requestedRelays, true);

    CHECK(connectionOnly == kVRConnectionOnlyProfileCapabilities);
    CHECK(avatarSync == (kVRAvatarSyncProfileCapabilities |
                         kFunctionalVRRelayCapabilities |
                         kVRExactAnimationActionCapabilities));
    CHECK(gameplay == (kVRGameplayProfileCapabilities |
                       kFunctionalVRRelayCapabilities |
                       kVRExactAnimationActionCapabilities));
    CHECK_FALSE(HasCapability(avatarSync, GameplayCapability::VREquipmentRelay));

    CHECK(IsVrClient(connectionOnly));
    CHECK_FALSE(IsVrGameplayClient(connectionOnly));
    CHECK_FALSE(CanOwnNpc(connectionOnly));
    CHECK(CanAdmitGameplayClient(connectionOnly));

    CHECK(IsVrClient(avatarSync));
    CHECK_FALSE(IsVrGameplayClient(avatarSync));
    CHECK_FALSE(CanOwnNpc(avatarSync));
    CHECK(CanAdmitGameplayClient(avatarSync));

    CHECK(IsVrClient(gameplay));
    CHECK(IsVrGameplayClient(gameplay));
    CHECK(CanReceiveAssignmentRejection(gameplay));
    CHECK(CanOwnNpc(gameplay));
    CHECK(CanAdmitGameplayClient(gameplay));
}

TEST_CASE("admission rejects malformed VR capability tuples without breaking desktop", "[skyrim-vr][capabilities]")
{
    using namespace SkyrimTogether::Protocol;

    const auto desktop = kCoreCapabilities;

    CHECK_FALSE(IsVrClient(desktop));
    CHECK_FALSE(IsVrGameplayClient(desktop));
    CHECK(CanOwnNpc(desktop));
    CHECK(CanAdmitGameplayClient(desktop));

    const auto unidentifiedRelay = desktop | ToMask(GameplayCapability::VRPoseRelay);
    const auto unidentifiedParity = desktop | ToMask(GameplayCapability::NativeGameplayParity);
    const auto unidentifiedGameplay = desktop | ToMask(GameplayCapability::VrGameplayClient);
    const auto unidentifiedOwnership = desktop | kVRNpcOwnershipCapabilities;
    const auto incompleteGameplay = kVRGameplayProfileCapabilities & ~kVRNpcOwnershipCapabilities;
    const auto gameplayWithoutParity =
        kVRGameplayProfileCapabilities & ~ToMask(GameplayCapability::NativeGameplayParity);
    const auto gameplayWithoutCore = kVRGameplayProfileCapabilities & ~kCoreCapabilities;
    const auto unsupportedRelay = kVRAvatarSyncProfileCapabilities |
                                  ToMask(GameplayCapability::VREquipmentRelay);

    CHECK_FALSE(IsVrClient(unidentifiedGameplay));
    CHECK_FALSE(IsVrGameplayClient(unidentifiedGameplay));
    CHECK_FALSE(CanOwnNpc(unidentifiedRelay));
    CHECK_FALSE(CanOwnNpc(unidentifiedOwnership));
    CHECK_FALSE(CanAdmitGameplayClient(unidentifiedRelay));
    CHECK_FALSE(CanAdmitGameplayClient(unidentifiedParity));
    CHECK_FALSE(CanAdmitGameplayClient(unidentifiedGameplay));
    CHECK_FALSE(CanAdmitGameplayClient(unidentifiedOwnership));
    CHECK_FALSE(CanAdmitGameplayClient(incompleteGameplay));
    CHECK_FALSE(CanAdmitGameplayClient(gameplayWithoutParity));
    CHECK_FALSE(CanAdmitGameplayClient(gameplayWithoutCore));
    CHECK_FALSE(CanAdmitGameplayClient(unsupportedRelay));
}

TEST_CASE("World reserves EnTT entity zero before network assignments", "[skyrim-vr][assignment]")
{
    entt::registry registry;

    const auto worldReservation = registry.create();
    const auto firstNetworkCandidate = registry.create();

    CHECK(entt::to_integral(worldReservation) == 0u);
    CHECK(entt::to_integral(firstNetworkCandidate) != 0u);
}
