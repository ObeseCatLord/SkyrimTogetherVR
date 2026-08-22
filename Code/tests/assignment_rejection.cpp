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

TEST_CASE("production VR profile builder negotiates canonical final-equipment transactions", "[skyrim-vr][authentication]")
{
    using namespace SkyrimTogether::Protocol;

    CHECK(kGameplayProtocolRevision == 21);
    const auto requestedRelays = kFunctionalVRRelayCapabilities |
                                 ToMask(GameplayCapability::VREquipmentRelay);
    const auto connectionOnly = BuildVRProductionCapabilities(
        VRProductionProfile::ConnectionOnly, requestedRelays, true);
    const auto avatarSync = BuildVRProductionCapabilities(
        VRProductionProfile::AvatarSync, requestedRelays, true);
    const auto gameplay = BuildVRProductionCapabilities(
        VRProductionProfile::Gameplay, requestedRelays, true);
    const auto gameplayBaseline = BuildVRProductionCapabilities(VRProductionProfile::Gameplay, 0, true);
    const auto gameplayWithoutExactActions = BuildVRProductionCapabilities(VRProductionProfile::Gameplay);
    const auto negotiatedGameplay = kServerCapabilities & gameplayBaseline;

    CHECK(connectionOnly == kVRConnectionOnlyProfileCapabilities);
    CHECK(avatarSync == (kVRAvatarSyncProfileCapabilities |
                         kFunctionalVRRelayCapabilities |
                         kVRExactAnimationActionCapabilities));
    CHECK(gameplay == (kVRGameplayProfileCapabilities |
                       kFunctionalVRRelayCapabilities));
    CHECK(gameplayBaseline == kVRGameplayProfileCapabilities);
    CHECK(gameplayWithoutExactActions == kVRAvatarSyncProfileCapabilities);
    CHECK(negotiatedGameplay == gameplayBaseline);
    CHECK(HasCapability(kServerCapabilities, GameplayCapability::FinalEquipmentTransactions));
    CHECK(HasCapability(kClientCapabilities, GameplayCapability::FinalEquipmentTransactions));
    CHECK(HasCapability(kServerCapabilities, GameplayCapability::RevisionedCanonicalRecovery));
    CHECK(HasCapability(kClientCapabilities, GameplayCapability::RevisionedCanonicalRecovery));
    CHECK(HasCapability(gameplayBaseline, GameplayCapability::RevisionedCanonicalRecovery));
    CHECK_FALSE(HasCapability(kServerCapabilities, GameplayCapability::VREquipmentRelay));
    CHECK_FALSE(HasCapability(kClientCapabilities, GameplayCapability::VREquipmentRelay));
    CHECK_FALSE(HasCapability(avatarSync, GameplayCapability::VREquipmentRelay));
    CHECK_FALSE(HasCapability(gameplayBaseline, GameplayCapability::VREquipmentRelay));
    CHECK_FALSE(HasCapability(negotiatedGameplay, GameplayCapability::VREquipmentRelay));
    CHECK_FALSE(HasCapability(gameplay, GameplayCapability::VREquipmentRelay));
    CHECK_FALSE(HasCapability(avatarSync, GameplayCapability::FinalEquipmentTransactions));
    CHECK(HasCapability(gameplayBaseline, GameplayCapability::FinalEquipmentTransactions));
    CHECK(HasCapability(negotiatedGameplay, GameplayCapability::FinalEquipmentTransactions));
    CHECK(HasCapability(gameplay, GameplayCapability::FinalEquipmentTransactions));
    CHECK(HasCapability(gameplayBaseline, GameplayCapability::ExactAnimationActions));
    CHECK(HasCapability(gameplay, GameplayCapability::ExactAnimationActions));
    CHECK_FALSE(HasCapability(gameplayWithoutExactActions, GameplayCapability::ExactAnimationActions));

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
    CHECK(CanAdmitGameplayClient(gameplayBaseline));
    CHECK(CanReceiveAssignmentRejection(gameplay));
    CHECK(CanOwnNpc(gameplay));
    CHECK(CanAdmitGameplayClient(gameplay));
    CHECK_FALSE(IsVrGameplayClient(gameplayWithoutExactActions));
    CHECK(CanAdmitGameplayClient(gameplayWithoutExactActions));
}

TEST_CASE("canonical final-equipment recipients exclude desktop and direct-relay peers", "[skyrim-vr][capabilities]")
{
    using namespace SkyrimTogether::Protocol;

    const auto canonicalRecipient = BuildVRProductionCapabilities(VRProductionProfile::Gameplay, 0, true);
    const auto desktopRecipient = kCoreCapabilities;
    const auto directRelayPeer = canonicalRecipient | ToMask(GameplayCapability::VREquipmentRelay);

    CHECK(HasCapability(canonicalRecipient, GameplayCapability::FinalEquipmentTransactions));
    CHECK_FALSE(HasCapability(desktopRecipient, GameplayCapability::FinalEquipmentTransactions));
    CHECK_FALSE(HasCapability(canonicalRecipient, GameplayCapability::VREquipmentRelay));
    CHECK_FALSE(CanAdmitGameplayClient(directRelayPeer));
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
    const auto unidentifiedGameplayCore = desktop | ToMask(GameplayCapability::NativeGameplayCore);
    const auto unidentifiedGameplay = desktop | ToMask(GameplayCapability::VrGameplayClient);
    const auto unidentifiedFinalEquipment =
        desktop | ToMask(GameplayCapability::FinalEquipmentTransactions);
    const auto unidentifiedOwnership = desktop | kVRNpcOwnershipCapabilities;
    const auto incompleteGameplay = kVRGameplayProfileCapabilities & ~kVRNpcOwnershipCapabilities;
    const auto gameplayWithoutNativeCore =
        kVRGameplayProfileCapabilities & ~ToMask(GameplayCapability::NativeGameplayCore);
    const auto gameplayWithoutCore = kVRGameplayProfileCapabilities & ~kCoreCapabilities;
    const auto gameplayWithoutFinalEquipment =
        kVRGameplayProfileCapabilities & ~ToMask(GameplayCapability::FinalEquipmentTransactions);
    const auto gameplayWithoutExactActions =
        kVRGameplayProfileCapabilities & ~ToMask(GameplayCapability::ExactAnimationActions);
    const auto finalEquipmentWithoutGameplayIntent =
        kVRAvatarSyncProfileCapabilities | ToMask(GameplayCapability::FinalEquipmentTransactions);
    const auto unsupportedRelay = kVRAvatarSyncProfileCapabilities |
                                  ToMask(GameplayCapability::VREquipmentRelay);

    CHECK_FALSE(IsVrClient(unidentifiedGameplay));
    CHECK_FALSE(IsVrGameplayClient(unidentifiedGameplay));
    CHECK_FALSE(CanOwnNpc(unidentifiedRelay));
    CHECK_FALSE(CanOwnNpc(unidentifiedOwnership));
    CHECK_FALSE(CanAdmitGameplayClient(unidentifiedRelay));
    CHECK_FALSE(CanAdmitGameplayClient(unidentifiedGameplayCore));
    CHECK_FALSE(CanAdmitGameplayClient(unidentifiedGameplay));
    CHECK_FALSE(CanAdmitGameplayClient(unidentifiedFinalEquipment));
    CHECK_FALSE(CanAdmitGameplayClient(unidentifiedOwnership));
    CHECK_FALSE(CanAdmitGameplayClient(incompleteGameplay));
    CHECK_FALSE(CanAdmitGameplayClient(gameplayWithoutNativeCore));
    CHECK_FALSE(CanAdmitGameplayClient(gameplayWithoutCore));
    CHECK_FALSE(CanAdmitGameplayClient(gameplayWithoutFinalEquipment));
    CHECK_FALSE(CanAdmitGameplayClient(gameplayWithoutExactActions));
    CHECK_FALSE(CanReceiveAssignmentRejection(gameplayWithoutExactActions));
    CHECK_FALSE(CanAdmitGameplayClient(finalEquipmentWithoutGameplayIntent));
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
