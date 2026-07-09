#include "Forms/TESObjectCELL.h"
#include "Forms/TESWorldSpace.h"
#include "Services/PapyrusService.h"
#include <Services/PartyService.h>

#include <Services/CharacterService.h>
#include <Services/QuestService.h>
#include <Services/TransportService.h>
#include <Services/VRInventoryService.h>
#include <Services/VRMovementService.h>
#include <Services/VRPoseService.h>
#include <vr_common/VRHandoffPath.h>

#include <Games/References.h>
#include <Games/Misc/SubtitleManager.h>

#include <Forms/TESNPC.h>
#include <Forms/TESQuest.h>

#include <BranchInfo.h>
#include <Components.h>

#include <Systems/InterpolationSystem.h>
#include <Systems/AnimationSystem.h>
#include <Systems/CacheSystem.h>
#include <Systems/FaceGenSystem.h>

#include <Events/ActorAddedEvent.h>
#include <Events/ActorRemovedEvent.h>
#include <Events/UpdateEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/MountEvent.h>
#include <Events/InitPackageEvent.h>
#include <Events/BeastFormChangeEvent.h>
#include <Events/AddExperienceEvent.h>
#include <Events/DialogueEvent.h>
#include <Events/SubtitleEvent.h>
#include <Events/MoveActorEvent.h>
#include <Events/PartyJoinedEvent.h>

#include <Structs/ActionEvent.h>
#include <Messages/CancelAssignmentRequest.h>
#include <Messages/AssignCharacterRequest.h>
#include <Messages/AssignCharacterResponse.h>
#include <Messages/ServerReferencesMoveRequest.h>
#include <Messages/ClientReferencesMoveRequest.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/RequestFactionsChanges.h>
#include <Messages/NotifyFactionsChanges.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/NotifySpawnData.h>
#include <Messages/RequestOwnershipTransfer.h>
#include <Messages/NotifyOwnershipTransfer.h>
#include <Messages/RequestOwnershipClaim.h>
#include <Messages/MountRequest.h>
#include <Messages/NotifyMount.h>
#include <Messages/NewPackageRequest.h>
#include <Messages/NotifyNewPackage.h>
#include <Messages/RequestRespawn.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/SyncExperienceRequest.h>
#include <Messages/NotifySyncExperience.h>
#include <Messages/DialogueRequest.h>
#include <Messages/NotifyDialogue.h>
#include <Messages/SubtitleRequest.h>
#include <Messages/NotifySubtitle.h>
#include <Messages/NotifyActorTeleport.h>
#include <Messages/NotifyRelinquishControl.h>

#include <World.h>
#include <Games/TES.h>
#include <Misc/BSFixedString.h>
#include <NetImmerse/NiAVObject.h>

#include <cmath>
#include <filesystem>
#include <fstream>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS
#define TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC
#define TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SKELETON_WRITES
#define TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SKELETON_WRITES 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE 0
#endif

namespace
{
#if TP_SKYRIM_VR
constexpr char kRemoteAvatarStatusFileName[] = "SkyrimTogetherVR.avatar";

struct RemoteAvatarApplyResult
{
    bool LocalMovementAvailable{false};
    bool RemoteMovementAvailable{false};
    bool SameCellAvailable{false};
    bool SameCell{false};
    bool SameWorldSpaceAvailable{false};
    bool SameWorldSpace{false};
    bool SameSpaceForApply{false};
    bool ActorAvailable{false};
    bool RootAvailable{false};
    bool HeadNodeFound{false};
    bool LeftHandNodeFound{false};
    bool RightHandNodeFound{false};
    bool HmdCopied{false};
    bool LeftHandCopied{false};
    bool RightHandCopied{false};
    bool VrikDetected{false};
    bool VrikInterfaceAvailable{false};
    bool VrikLeftFingersValid{false};
    bool VrikRightFingersValid{false};
    bool VrikCameraOffsetsValid{false};
    bool MovementApplied{false};
    bool HmdFallbackMovementApplied{false};
    uint32_t InvalidTransformCount{0};
    uint32_t InvalidVrikCount{0};
    bool InvalidMovement{false};
};

struct RemoteAvatarStatusSnapshot
{
    uint32_t RemotePlayerCount{0};
    uint32_t RemotePoseMatchCount{0};
    uint32_t ComponentUpsertCount{0};
    uint32_t ActorTargetAttemptCount{0};
    uint32_t SameSpaceCount{0};
    uint32_t SameCellCount{0};
    uint32_t SameWorldSpaceCount{0};
    uint32_t ActorTargetSkippedNoLocalMovementCount{0};
    uint32_t ActorTargetSkippedNoRemoteMovementCount{0};
    uint32_t ActorTargetSkippedDifferentCellCount{0};
    uint32_t ActorTargetSkippedDifferentWorldSpaceCount{0};
    uint32_t MissingFormIdCount{0};
    uint32_t MissingActorCount{0};
    uint32_t MissingRootCount{0};
    uint32_t HeadNodeFoundCount{0};
    uint32_t LeftHandNodeFoundCount{0};
    uint32_t RightHandNodeFoundCount{0};
    uint32_t HmdCopiedCount{0};
    uint32_t LeftHandCopiedCount{0};
    uint32_t RightHandCopiedCount{0};
    uint32_t VrikDetectedCount{0};
    uint32_t VrikInterfaceAvailableCount{0};
    uint32_t VrikLeftFingersValidCount{0};
    uint32_t VrikRightFingersValidCount{0};
    uint32_t VrikCameraOffsetsValidCount{0};
    uint32_t MovementAppliedCount{0};
    uint32_t HmdFallbackMovementCount{0};
    uint32_t InvalidTransformCount{0};
    uint32_t InvalidVrikCount{0};
    uint32_t InvalidMovementCount{0};
    uint32_t SpellOriginValidCount{0};
    uint32_t SpellDestinationValidCount{0};
    uint32_t ArrowOriginValidCount{0};
    uint32_t ArrowDestinationValidCount{0};
    uint32_t BowAimValidCount{0};
    uint32_t BowRotationValidCount{0};
    uint32_t LeftWeaponOffsetValidCount{0};
    uint32_t RightWeaponOffsetValidCount{0};
    uint32_t PrimaryMagicOffsetValidCount{0};
    uint32_t PrimaryMagicAimValidCount{0};
    uint32_t SecondaryMagicOffsetValidCount{0};
    uint32_t SecondaryMagicAimValidCount{0};
    uint32_t StalePoseRemovedCount{0};
    uint32_t RemoteEquipmentMatchCount{0};
    uint32_t EquipmentComponentUpsertCount{0};
    uint32_t StaleEquipmentRemovedCount{0};
    uint32_t EquipmentWeaponDrawQueuedCount{0};
    uint32_t EquipmentMissingFormIdCount{0};
    uint32_t EquipmentMissingActorCount{0};
    uint32_t LastEquipmentPlayerId{0};
    uint32_t LastEquipmentFormId{0};
    uint32_t LastEquipmentSequence{0};
    bool LastEquipmentWeaponDrawn{false};
    bool LastEquipmentWeaponFullyDrawn{false};
    bool LastEquipmentDesiredWeaponDrawn{false};
    bool LastEquipmentWeaponDrawQueued{false};
    uint32_t LastPlayerId{0};
    uint32_t LastFormId{0};
    uint32_t LastPoseSequence{0};
    bool LastSpellOriginValid{false};
    bool LastSpellDestinationValid{false};
    bool LastArrowOriginValid{false};
    bool LastArrowDestinationValid{false};
    bool LastBowAimValid{false};
    bool LastBowRotationValid{false};
    bool LastLeftWeaponOffsetValid{false};
    bool LastRightWeaponOffsetValid{false};
    bool LastPrimaryMagicOffsetValid{false};
    bool LastPrimaryMagicAimValid{false};
    bool LastSecondaryMagicOffsetValid{false};
    bool LastSecondaryMagicAimValid{false};
    RemoteAvatarApplyResult LastApply{};
};

bool HasGameId(const GameId& acId) noexcept
{
    return static_cast<bool>(acId);
}

bool IsFiniteFloat(float aValue) noexcept
{
    return std::isfinite(aValue);
}

bool IsFiniteVector(const glm::vec3& acValue) noexcept
{
    return IsFiniteFloat(acValue.x) && IsFiniteFloat(acValue.y) && IsFiniteFloat(acValue.z);
}

bool IsRemotePoseNodeSafe(const VRPoseNodeData& acPose) noexcept
{
    if (!acPose.Valid)
        return false;

    if (!IsFiniteVector(acPose.Position) || !IsFiniteVector(acPose.AxisX) || !IsFiniteVector(acPose.AxisY) || !IsFiniteVector(acPose.AxisZ))
        return false;

    return IsFiniteFloat(acPose.Scale) && acPose.Scale > 0.0f && acPose.Scale <= 10.0f;
}

bool IsRemoteFingerCurlSafe(const VRFingerCurlData& acFingers) noexcept
{
    return acFingers.Valid && IsFiniteFloat(acFingers.Thumb) && IsFiniteFloat(acFingers.Index) &&
           IsFiniteFloat(acFingers.Middle) && IsFiniteFloat(acFingers.Ring) && IsFiniteFloat(acFingers.Pinky);
}

bool IsRemoteVrikCameraOffsetsSafe(const VRVrikData& acVrik) noexcept
{
    return acVrik.CameraOffsetsValid && IsFiniteVector(acVrik.CameraOffset) &&
           IsFiniteVector(acVrik.FinalCameraOffset) && IsFiniteVector(acVrik.FinalSmoothingOffset);
}

bool DesiredRemoteWeaponDrawn(const VREquipmentUpdate& acEquipment) noexcept
{
    return acEquipment.WeaponDrawn || acEquipment.WeaponFullyDrawn;
}

RemoteAvatarApplyResult CheckRemoteAvatarSpace(const VRMovementService& acMovementService, uint32_t aPlayerId) noexcept
{
    RemoteAvatarApplyResult result{};

    if (!acMovementService.HasLocalMovement())
        return result;

    result.LocalMovementAvailable = true;

    const auto& remoteMovements = acMovementService.GetRemoteMovements();
    const auto movementIt = remoteMovements.find(aPlayerId);
    if (movementIt == remoteMovements.end())
        return result;

    result.RemoteMovementAvailable = true;

    const auto& localMovement = acMovementService.GetLocalMovement();
    const auto& remoteMovement = movementIt->second;
    result.SameCellAvailable = HasGameId(localMovement.CellId) && HasGameId(remoteMovement.CellId);
    result.SameCell = result.SameCellAvailable && localMovement.CellId == remoteMovement.CellId;
    result.SameWorldSpaceAvailable = HasGameId(localMovement.WorldSpaceId) && HasGameId(remoteMovement.WorldSpaceId);
    result.SameWorldSpace = result.SameWorldSpaceAvailable && localMovement.WorldSpaceId == remoteMovement.WorldSpaceId;

    if (result.SameCellAvailable)
        result.SameSpaceForApply = result.SameCell && (!result.SameWorldSpaceAvailable || result.SameWorldSpace);
    else
        result.SameSpaceForApply = result.SameWorldSpaceAvailable && result.SameWorldSpace;

    return result;
}

std::filesystem::path GetHandoffDirectory()
{
    return SkyrimTogetherVR::Handoff::GetDirectory();
}

void WriteRemoteAvatarStatus(const RemoteAvatarStatusSnapshot& acStatus)
{
    std::error_code ec;
    const auto handoffDir = GetHandoffDirectory();
    std::filesystem::create_directories(handoffDir, ec);

    std::ofstream file(handoffDir / kRemoteAvatarStatusFileName, std::ios::trunc);
    if (!file)
        return;

    file << "ready=1\n";
    file << "actorTargetsEnabled=" << (TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS ? "1" : "0") << "\n";
    file << "actorSkeletonWritesEnabled=" << (TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SKELETON_WRITES ? "1" : "0") << "\n";
    file << "actorMovementAuthorityEnabled=0\n";
    file << "remotePlayerCount=" << acStatus.RemotePlayerCount << "\n";
    file << "remotePoseMatchCount=" << acStatus.RemotePoseMatchCount << "\n";
    file << "componentUpsertCount=" << acStatus.ComponentUpsertCount << "\n";
    file << "actorTargetAttemptCount=" << acStatus.ActorTargetAttemptCount << "\n";
    file << "sameSpaceCount=" << acStatus.SameSpaceCount << "\n";
    file << "sameCellCount=" << acStatus.SameCellCount << "\n";
    file << "sameWorldSpaceCount=" << acStatus.SameWorldSpaceCount << "\n";
    file << "actorTargetSkippedNoLocalMovementCount=" << acStatus.ActorTargetSkippedNoLocalMovementCount << "\n";
    file << "actorTargetSkippedNoRemoteMovementCount=" << acStatus.ActorTargetSkippedNoRemoteMovementCount << "\n";
    file << "actorTargetSkippedDifferentCellCount=" << acStatus.ActorTargetSkippedDifferentCellCount << "\n";
    file << "actorTargetSkippedDifferentWorldSpaceCount=" << acStatus.ActorTargetSkippedDifferentWorldSpaceCount << "\n";
    file << "missingFormIdCount=" << acStatus.MissingFormIdCount << "\n";
    file << "missingActorCount=" << acStatus.MissingActorCount << "\n";
    file << "missingRootCount=" << acStatus.MissingRootCount << "\n";
    file << "headNodeFoundCount=" << acStatus.HeadNodeFoundCount << "\n";
    file << "leftHandNodeFoundCount=" << acStatus.LeftHandNodeFoundCount << "\n";
    file << "rightHandNodeFoundCount=" << acStatus.RightHandNodeFoundCount << "\n";
    file << "hmdCopiedCount=" << acStatus.HmdCopiedCount << "\n";
    file << "leftHandCopiedCount=" << acStatus.LeftHandCopiedCount << "\n";
    file << "rightHandCopiedCount=" << acStatus.RightHandCopiedCount << "\n";
    file << "vrikDetectedCount=" << acStatus.VrikDetectedCount << "\n";
    file << "vrikInterfaceAvailableCount=" << acStatus.VrikInterfaceAvailableCount << "\n";
    file << "vrikLeftFingersValidCount=" << acStatus.VrikLeftFingersValidCount << "\n";
    file << "vrikRightFingersValidCount=" << acStatus.VrikRightFingersValidCount << "\n";
    file << "vrikCameraOffsetsValidCount=" << acStatus.VrikCameraOffsetsValidCount << "\n";
    file << "movementAppliedCount=" << acStatus.MovementAppliedCount << "\n";
    file << "hmdFallbackMovementCount=" << acStatus.HmdFallbackMovementCount << "\n";
    file << "invalidTransformCount=" << acStatus.InvalidTransformCount << "\n";
    file << "invalidVrikCount=" << acStatus.InvalidVrikCount << "\n";
    file << "invalidMovementCount=" << acStatus.InvalidMovementCount << "\n";
    file << "spellOriginValidCount=" << acStatus.SpellOriginValidCount << "\n";
    file << "spellDestinationValidCount=" << acStatus.SpellDestinationValidCount << "\n";
    file << "arrowOriginValidCount=" << acStatus.ArrowOriginValidCount << "\n";
    file << "arrowDestinationValidCount=" << acStatus.ArrowDestinationValidCount << "\n";
    file << "bowAimValidCount=" << acStatus.BowAimValidCount << "\n";
    file << "bowRotationValidCount=" << acStatus.BowRotationValidCount << "\n";
    file << "leftWeaponOffsetValidCount=" << acStatus.LeftWeaponOffsetValidCount << "\n";
    file << "rightWeaponOffsetValidCount=" << acStatus.RightWeaponOffsetValidCount << "\n";
    file << "primaryMagicOffsetValidCount=" << acStatus.PrimaryMagicOffsetValidCount << "\n";
    file << "primaryMagicAimValidCount=" << acStatus.PrimaryMagicAimValidCount << "\n";
    file << "secondaryMagicOffsetValidCount=" << acStatus.SecondaryMagicOffsetValidCount << "\n";
    file << "secondaryMagicAimValidCount=" << acStatus.SecondaryMagicAimValidCount << "\n";
    file << "stalePoseRemovedCount=" << acStatus.StalePoseRemovedCount << "\n";
    file << "remoteEquipmentMatchCount=" << acStatus.RemoteEquipmentMatchCount << "\n";
    file << "equipmentComponentUpsertCount=" << acStatus.EquipmentComponentUpsertCount << "\n";
    file << "staleEquipmentRemovedCount=" << acStatus.StaleEquipmentRemovedCount << "\n";
    file << "equipmentWeaponDrawQueuedCount=" << acStatus.EquipmentWeaponDrawQueuedCount << "\n";
    file << "equipmentMissingFormIdCount=" << acStatus.EquipmentMissingFormIdCount << "\n";
    file << "equipmentMissingActorCount=" << acStatus.EquipmentMissingActorCount << "\n";
    file << "last.playerId=" << acStatus.LastPlayerId << "\n";
    file << "last.formId=" << acStatus.LastFormId << "\n";
    file << "last.poseSequence=" << acStatus.LastPoseSequence << "\n";
    file << "last.spellOriginValid=" << (acStatus.LastSpellOriginValid ? "1" : "0") << "\n";
    file << "last.spellDestinationValid=" << (acStatus.LastSpellDestinationValid ? "1" : "0") << "\n";
    file << "last.arrowOriginValid=" << (acStatus.LastArrowOriginValid ? "1" : "0") << "\n";
    file << "last.arrowDestinationValid=" << (acStatus.LastArrowDestinationValid ? "1" : "0") << "\n";
    file << "last.bowAimValid=" << (acStatus.LastBowAimValid ? "1" : "0") << "\n";
    file << "last.bowRotationValid=" << (acStatus.LastBowRotationValid ? "1" : "0") << "\n";
    file << "last.leftWeaponOffsetValid=" << (acStatus.LastLeftWeaponOffsetValid ? "1" : "0") << "\n";
    file << "last.rightWeaponOffsetValid=" << (acStatus.LastRightWeaponOffsetValid ? "1" : "0") << "\n";
    file << "last.primaryMagicOffsetValid=" << (acStatus.LastPrimaryMagicOffsetValid ? "1" : "0") << "\n";
    file << "last.primaryMagicAimValid=" << (acStatus.LastPrimaryMagicAimValid ? "1" : "0") << "\n";
    file << "last.secondaryMagicOffsetValid=" << (acStatus.LastSecondaryMagicOffsetValid ? "1" : "0") << "\n";
    file << "last.secondaryMagicAimValid=" << (acStatus.LastSecondaryMagicAimValid ? "1" : "0") << "\n";
    file << "lastEquipment.playerId=" << acStatus.LastEquipmentPlayerId << "\n";
    file << "lastEquipment.formId=" << acStatus.LastEquipmentFormId << "\n";
    file << "lastEquipment.sequence=" << acStatus.LastEquipmentSequence << "\n";
    file << "lastEquipment.weaponDrawn=" << (acStatus.LastEquipmentWeaponDrawn ? "1" : "0") << "\n";
    file << "lastEquipment.weaponFullyDrawn=" << (acStatus.LastEquipmentWeaponFullyDrawn ? "1" : "0") << "\n";
    file << "lastEquipment.desiredWeaponDrawn=" << (acStatus.LastEquipmentDesiredWeaponDrawn ? "1" : "0") << "\n";
    file << "lastEquipment.weaponDrawQueued=" << (acStatus.LastEquipmentWeaponDrawQueued ? "1" : "0") << "\n";
    file << "last.localMovementAvailable=" << (acStatus.LastApply.LocalMovementAvailable ? "1" : "0") << "\n";
    file << "last.remoteMovementAvailable=" << (acStatus.LastApply.RemoteMovementAvailable ? "1" : "0") << "\n";
    file << "last.sameCellAvailable=" << (acStatus.LastApply.SameCellAvailable ? "1" : "0") << "\n";
    file << "last.sameCell=" << (acStatus.LastApply.SameCell ? "1" : "0") << "\n";
    file << "last.sameWorldSpaceAvailable=" << (acStatus.LastApply.SameWorldSpaceAvailable ? "1" : "0") << "\n";
    file << "last.sameWorldSpace=" << (acStatus.LastApply.SameWorldSpace ? "1" : "0") << "\n";
    file << "last.sameSpaceForApply=" << (acStatus.LastApply.SameSpaceForApply ? "1" : "0") << "\n";
    file << "last.actorAvailable=" << (acStatus.LastApply.ActorAvailable ? "1" : "0") << "\n";
    file << "last.rootAvailable=" << (acStatus.LastApply.RootAvailable ? "1" : "0") << "\n";
    file << "last.headNodeFound=" << (acStatus.LastApply.HeadNodeFound ? "1" : "0") << "\n";
    file << "last.leftHandNodeFound=" << (acStatus.LastApply.LeftHandNodeFound ? "1" : "0") << "\n";
    file << "last.rightHandNodeFound=" << (acStatus.LastApply.RightHandNodeFound ? "1" : "0") << "\n";
    file << "last.hmdCopied=" << (acStatus.LastApply.HmdCopied ? "1" : "0") << "\n";
    file << "last.leftHandCopied=" << (acStatus.LastApply.LeftHandCopied ? "1" : "0") << "\n";
    file << "last.rightHandCopied=" << (acStatus.LastApply.RightHandCopied ? "1" : "0") << "\n";
    file << "last.vrikDetected=" << (acStatus.LastApply.VrikDetected ? "1" : "0") << "\n";
    file << "last.vrikInterfaceAvailable=" << (acStatus.LastApply.VrikInterfaceAvailable ? "1" : "0") << "\n";
    file << "last.vrikLeftFingersValid=" << (acStatus.LastApply.VrikLeftFingersValid ? "1" : "0") << "\n";
    file << "last.vrikRightFingersValid=" << (acStatus.LastApply.VrikRightFingersValid ? "1" : "0") << "\n";
    file << "last.vrikCameraOffsetsValid=" << (acStatus.LastApply.VrikCameraOffsetsValid ? "1" : "0") << "\n";
    file << "last.movementApplied=" << (acStatus.LastApply.MovementApplied ? "1" : "0") << "\n";
    file << "last.hmdFallbackMovementApplied=" << (acStatus.LastApply.HmdFallbackMovementApplied ? "1" : "0") << "\n";
    file << "last.invalidTransformCount=" << acStatus.LastApply.InvalidTransformCount << "\n";
    file << "last.invalidVrikCount=" << acStatus.LastApply.InvalidVrikCount << "\n";
    file << "last.invalidMovement=" << (acStatus.LastApply.InvalidMovement ? "1" : "0") << "\n";
}

bool CopyPoseNodeToSceneNode(NiAVObject* apTarget, const VRPoseNodeData& acPose) noexcept
{
    if (!apTarget || !IsRemotePoseNodeSafe(acPose))
        return false;

    auto& world = apTarget->world;
    world.rotate[0][0] = acPose.AxisX.x;
    world.rotate[0][1] = acPose.AxisX.y;
    world.rotate[0][2] = acPose.AxisX.z;
    world.rotate[1][0] = acPose.AxisY.x;
    world.rotate[1][1] = acPose.AxisY.y;
    world.rotate[1][2] = acPose.AxisY.z;
    world.rotate[2][0] = acPose.AxisZ.x;
    world.rotate[2][1] = acPose.AxisZ.y;
    world.rotate[2][2] = acPose.AxisZ.z;
    world.translate.x = acPose.Position.x;
    world.translate.y = acPose.Position.y;
    world.translate.z = acPose.Position.z;
    world.scale = acPose.Scale;
    return true;
}

NiAVObject* FindChildNode(NiAVObject* apRoot, const char* apName) noexcept
{
    if (!apRoot || !apName || !apName[0])
        return nullptr;

    BSFixedString name(apName);
    return apRoot->GetObjectByName(name);
}

void ApplyRemoteAvatarPoseToActor(Actor* apActor, const VRPoseUpdate& acPose, RemoteAvatarApplyResult& aResult) noexcept
{
    if (!apActor)
        return;

    aResult.ActorAvailable = true;
    auto* pRoot = apActor->GetNiNode();
    if (!pRoot)
        return;

    aResult.RootAvailable = true;
    auto* pHead = FindChildNode(pRoot, "NPC Head [Head]");
    auto* pLeftHand = FindChildNode(pRoot, "NPC L Hand [LHnd]");
    auto* pRightHand = FindChildNode(pRoot, "NPC R Hand [RHnd]");
    aResult.HeadNodeFound = pHead != nullptr;
    aResult.LeftHandNodeFound = pLeftHand != nullptr;
    aResult.RightHandNodeFound = pRightHand != nullptr;

    if (acPose.Hmd.Valid && !IsRemotePoseNodeSafe(acPose.Hmd))
        ++aResult.InvalidTransformCount;
    if (acPose.LeftHand.Valid && !IsRemotePoseNodeSafe(acPose.LeftHand))
        ++aResult.InvalidTransformCount;
    if (acPose.RightHand.Valid && !IsRemotePoseNodeSafe(acPose.RightHand))
        ++aResult.InvalidTransformCount;

    aResult.HmdCopied = CopyPoseNodeToSceneNode(pHead, acPose.Hmd);
    aResult.LeftHandCopied = CopyPoseNodeToSceneNode(pLeftHand, acPose.LeftHand);
    aResult.RightHandCopied = CopyPoseNodeToSceneNode(pRightHand, acPose.RightHand);

    aResult.VrikDetected = acPose.Vrik.Detected;
    aResult.VrikInterfaceAvailable = acPose.Vrik.InterfaceAvailable;
    aResult.VrikLeftFingersValid = IsRemoteFingerCurlSafe(acPose.Vrik.LeftFingers);
    aResult.VrikRightFingersValid = IsRemoteFingerCurlSafe(acPose.Vrik.RightFingers);
    aResult.VrikCameraOffsetsValid = IsRemoteVrikCameraOffsetsSafe(acPose.Vrik);

    if (acPose.Vrik.LeftFingers.Valid && !aResult.VrikLeftFingersValid)
        ++aResult.InvalidVrikCount;
    if (acPose.Vrik.RightFingers.Valid && !aResult.VrikRightFingersValid)
        ++aResult.InvalidVrikCount;
    if (acPose.Vrik.CameraOffsetsValid && !aResult.VrikCameraOffsetsValid)
        ++aResult.InvalidVrikCount;
}

void ValidateRemoteAvatarPoseForStatus(const VRPoseUpdate& acPose, RemoteAvatarApplyResult& aResult) noexcept
{
    if (acPose.Hmd.Valid && !IsRemotePoseNodeSafe(acPose.Hmd))
        ++aResult.InvalidTransformCount;
    if (acPose.LeftHand.Valid && !IsRemotePoseNodeSafe(acPose.LeftHand))
        ++aResult.InvalidTransformCount;
    if (acPose.RightHand.Valid && !IsRemotePoseNodeSafe(acPose.RightHand))
        ++aResult.InvalidTransformCount;

    aResult.VrikDetected = acPose.Vrik.Detected;
    aResult.VrikInterfaceAvailable = acPose.Vrik.InterfaceAvailable;
    aResult.VrikLeftFingersValid = IsRemoteFingerCurlSafe(acPose.Vrik.LeftFingers);
    aResult.VrikRightFingersValid = IsRemoteFingerCurlSafe(acPose.Vrik.RightFingers);
    aResult.VrikCameraOffsetsValid = IsRemoteVrikCameraOffsetsSafe(acPose.Vrik);

    if (acPose.Vrik.LeftFingers.Valid && !aResult.VrikLeftFingersValid)
        ++aResult.InvalidVrikCount;
    if (acPose.Vrik.RightFingers.Valid && !aResult.VrikRightFingersValid)
        ++aResult.InvalidVrikCount;
    if (acPose.Vrik.CameraOffsetsValid && !aResult.VrikCameraOffsetsValid)
        ++aResult.InvalidVrikCount;
}

void AccumulateSpaceResult(RemoteAvatarStatusSnapshot& aStatus, const RemoteAvatarApplyResult& acResult) noexcept
{
    if (acResult.SameSpaceForApply)
        ++aStatus.SameSpaceCount;
    if (acResult.SameCell)
        ++aStatus.SameCellCount;
    if (acResult.SameWorldSpace)
        ++aStatus.SameWorldSpaceCount;
}

void AccumulateApplyResult(RemoteAvatarStatusSnapshot& aStatus, const RemoteAvatarApplyResult& acResult) noexcept
{
    if (!acResult.RootAvailable && acResult.ActorAvailable)
        ++aStatus.MissingRootCount;
    if (acResult.HeadNodeFound)
        ++aStatus.HeadNodeFoundCount;
    if (acResult.LeftHandNodeFound)
        ++aStatus.LeftHandNodeFoundCount;
    if (acResult.RightHandNodeFound)
        ++aStatus.RightHandNodeFoundCount;
    if (acResult.HmdCopied)
        ++aStatus.HmdCopiedCount;
    if (acResult.LeftHandCopied)
        ++aStatus.LeftHandCopiedCount;
    if (acResult.RightHandCopied)
        ++aStatus.RightHandCopiedCount;
    if (acResult.VrikDetected)
        ++aStatus.VrikDetectedCount;
    if (acResult.VrikInterfaceAvailable)
        ++aStatus.VrikInterfaceAvailableCount;
    if (acResult.VrikLeftFingersValid)
        ++aStatus.VrikLeftFingersValidCount;
    if (acResult.VrikRightFingersValid)
        ++aStatus.VrikRightFingersValidCount;
    if (acResult.VrikCameraOffsetsValid)
        ++aStatus.VrikCameraOffsetsValidCount;
    if (acResult.MovementApplied)
        ++aStatus.MovementAppliedCount;
    if (acResult.HmdFallbackMovementApplied)
        ++aStatus.HmdFallbackMovementCount;
    aStatus.InvalidTransformCount += acResult.InvalidTransformCount;
    aStatus.InvalidVrikCount += acResult.InvalidVrikCount;
    if (acResult.InvalidMovement)
        ++aStatus.InvalidMovementCount;
}

void AccumulatePoseContext(RemoteAvatarStatusSnapshot& aStatus, const VRPoseUpdate& acPose) noexcept
{
    if (acPose.SpellOrigin.Valid)
        ++aStatus.SpellOriginValidCount;
    if (acPose.SpellDestination.Valid)
        ++aStatus.SpellDestinationValidCount;
    if (acPose.ArrowOrigin.Valid)
        ++aStatus.ArrowOriginValidCount;
    if (acPose.ArrowDestination.Valid)
        ++aStatus.ArrowDestinationValidCount;
    if (acPose.BowAim.Valid)
        ++aStatus.BowAimValidCount;
    if (acPose.BowRotation.Valid)
        ++aStatus.BowRotationValidCount;
    if (acPose.LeftWeaponOffset.Valid)
        ++aStatus.LeftWeaponOffsetValidCount;
    if (acPose.RightWeaponOffset.Valid)
        ++aStatus.RightWeaponOffsetValidCount;
    if (acPose.PrimaryMagicOffset.Valid)
        ++aStatus.PrimaryMagicOffsetValidCount;
    if (acPose.PrimaryMagicAim.Valid)
        ++aStatus.PrimaryMagicAimValidCount;
    if (acPose.SecondaryMagicOffset.Valid)
        ++aStatus.SecondaryMagicOffsetValidCount;
    if (acPose.SecondaryMagicAim.Valid)
        ++aStatus.SecondaryMagicAimValidCount;

    aStatus.LastSpellOriginValid = acPose.SpellOrigin.Valid;
    aStatus.LastSpellDestinationValid = acPose.SpellDestination.Valid;
    aStatus.LastArrowOriginValid = acPose.ArrowOrigin.Valid;
    aStatus.LastArrowDestinationValid = acPose.ArrowDestination.Valid;
    aStatus.LastBowAimValid = acPose.BowAim.Valid;
    aStatus.LastBowRotationValid = acPose.BowRotation.Valid;
    aStatus.LastLeftWeaponOffsetValid = acPose.LeftWeaponOffset.Valid;
    aStatus.LastRightWeaponOffsetValid = acPose.RightWeaponOffset.Valid;
    aStatus.LastPrimaryMagicOffsetValid = acPose.PrimaryMagicOffset.Valid;
    aStatus.LastPrimaryMagicAimValid = acPose.PrimaryMagicAim.Valid;
    aStatus.LastSecondaryMagicOffsetValid = acPose.SecondaryMagicOffset.Valid;
    aStatus.LastSecondaryMagicAimValid = acPose.SecondaryMagicAim.Valid;
}
#endif
}

CharacterService::CharacterService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
{
    m_referenceAddedConnection = m_dispatcher.sink<ActorAddedEvent>().connect<&CharacterService::OnActorAdded>(this);
    m_referenceRemovedConnection = m_dispatcher.sink<ActorRemovedEvent>().connect<&CharacterService::OnActorRemoved>(this);

    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&CharacterService::OnUpdate>(this);
    m_actionConnection = m_dispatcher.sink<ActionEvent>().connect<&CharacterService::OnActionEvent>(this);

    m_connectedConnection = m_dispatcher.sink<ConnectedEvent>().connect<&CharacterService::OnConnected>(this);
    m_disconnectedConnection = m_dispatcher.sink<DisconnectedEvent>().connect<&CharacterService::OnDisconnected>(this);

    m_assignCharacterConnection = m_dispatcher.sink<AssignCharacterResponse>().connect<&CharacterService::OnAssignCharacter>(this);
    m_characterSpawnConnection = m_dispatcher.sink<CharacterSpawnRequest>().connect<&CharacterService::OnCharacterSpawn>(this);
    m_referenceMovementSnapshotConnection = m_dispatcher.sink<ServerReferencesMoveRequest>().connect<&CharacterService::OnReferencesMoveRequest>(this);
    m_factionsConnection = m_dispatcher.sink<NotifyFactionsChanges>().connect<&CharacterService::OnFactionsChanges>(this);
    m_ownershipTransferConnection = m_dispatcher.sink<NotifyOwnershipTransfer>().connect<&CharacterService::OnOwnershipTransfer>(this);
    m_removeCharacterConnection = m_dispatcher.sink<NotifyRemoveCharacter>().connect<&CharacterService::OnRemoveCharacter>(this);
    m_remoteSpawnDataReceivedConnection = m_dispatcher.sink<NotifySpawnData>().connect<&CharacterService::OnRemoteSpawnDataReceived>(this);

    m_mountConnection = m_dispatcher.sink<MountEvent>().connect<&CharacterService::OnMountEvent>(this);
    m_notifyMountConnection = m_dispatcher.sink<NotifyMount>().connect<&CharacterService::OnNotifyMount>(this);

    m_initPackageConnection = m_dispatcher.sink<InitPackageEvent>().connect<&CharacterService::OnInitPackageEvent>(this);
    m_newPackageConnection = m_dispatcher.sink<NotifyNewPackage>().connect<&CharacterService::OnNotifyNewPackage>(this);

    m_notifyRespawnConnection = m_dispatcher.sink<NotifyRespawn>().connect<&CharacterService::OnNotifyRespawn>(this);
    m_beastFormChangeConnection = m_dispatcher.sink<BeastFormChangeEvent>().connect<&CharacterService::OnBeastFormChange>(this);

    m_addExperienceEventConnection = m_dispatcher.sink<AddExperienceEvent>().connect<&CharacterService::OnAddExperienceEvent>(this);
    m_syncExperienceConnection = m_dispatcher.sink<NotifySyncExperience>().connect<&CharacterService::OnNotifySyncExperience>(this);

    m_dialogueEventConnection = m_dispatcher.sink<DialogueEvent>().connect<&CharacterService::OnDialogueEvent>(this);
    m_dialogueSyncConnection = m_dispatcher.sink<NotifyDialogue>().connect<&CharacterService::OnNotifyDialogue>(this);

    m_subtitleEventConnection = m_dispatcher.sink<SubtitleEvent>().connect<&CharacterService::OnSubtitleEvent>(this);
    m_subtitleSyncConnection = m_dispatcher.sink<NotifySubtitle>().connect<&CharacterService::OnNotifySubtitle>(this);

    m_actorTeleportConnection = m_dispatcher.sink<NotifyActorTeleport>().connect<&CharacterService::OnNotifyActorTeleport>(this);

    m_relinquishConnection = m_dispatcher.sink<NotifyRelinquishControl>().connect<&CharacterService::OnNotifyRelinquishControl>(this);

    m_partyJoinedConnection = aDispatcher.sink<PartyJoinedEvent>().connect<&CharacterService::OnPartyJoinedEvent>(this);
}

void CharacterService::DeleteRemoteEntityComponents(entt::entity aEntity) const noexcept
{
    m_world.remove<FaceGenComponent, InterpolationComponent, RemoteAnimationComponent, RemoteVRPoseComponent, RemoteVREquipmentComponent, RemoteComponent, CacheComponent, WaitingFor3D,
                   PlayerComponent>(aEntity);
}

bool CharacterService::TakeOwnership(const uint32_t acFormId, const uint32_t acServerId, const entt::entity acEntity) const noexcept
{
    Actor* pActor = Cast<Actor>(TESForm::GetById(acFormId));
    if (!pActor)
    {
        spdlog::error("Cannot find actor to take control over, form id: {:X}, server id: {:X}", acFormId, acServerId);
        return false;
    }

    ActorExtension* pExtension = pActor->GetExtension();
    if (pExtension->IsRemotePlayer())
    {
        spdlog::error("Cannot take control over remote player actor, form id: {:X}, server id: {:X}", acFormId, acServerId);
        return false;
    }

    if (pActor->IsPlayerSummon())
    {
        spdlog::error("Cannot take control over remote player summon, form id: {:X}, server id: {:X}", acFormId, acServerId);
        return false;
    }

    pExtension->SetRemote(false);

    // TODO(cosideci): this should be done differently.
    // Send an ownership claim request, and have the server broadcast the result.
    // Only then should components be added or removed.
    m_world.emplace_or_replace<LocalComponent>(acEntity, acServerId);
    m_world.emplace_or_replace<LocalAnimationComponent>(acEntity);
    DeleteRemoteEntityComponents(acEntity);

    RequestOwnershipClaim request;
    request.ServerId = acServerId;
    request.NewActorData = BuildActorData(pActor);

    m_transport.Send(request);

    return true;
}

void CharacterService::DeleteTempActor(const uint32_t aFormId) noexcept
{
    Actor* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    if (pActor && pActor->IsTemporary())
    {
        pActor->Delete();
        spdlog::info("\tDeleted actor {:X}", aFormId);
    }
}

void CharacterService::OnActorAdded(const ActorAddedEvent& acEvent) noexcept
{
    Actor* pActor = Cast<Actor>(TESForm::GetById(acEvent.FormId));

    if (acEvent.FormId == 0x14)
    {
        pActor->GetExtension()->SetPlayer(true);
    }

    entt::entity entity;

    const auto view = m_world.view<RemoteComponent>();
    const auto it = std::find_if(
        std::begin(view), std::end(view),
        [&acEvent, view](entt::entity entity)
        {
            auto& remoteComponent = view.get<RemoteComponent>(entity);
            return remoteComponent.CachedRefId == acEvent.FormId;
        });

    if (it != std::end(view))
    {
        Actor* pActor = Cast<Actor>(TESForm::GetById(acEvent.FormId));
        pActor->GetExtension()->SetRemote(true);

        entity = *it;
    }
    else
        entity = m_world.create();

    m_world.emplace_or_replace<FormIdComponent>(entity, acEvent.FormId);
    m_world.emplace_or_replace<EarlyAnimationBufferComponent>(entity);

    ProcessNewEntity(entity);
}

void CharacterService::OnActorRemoved(const ActorRemovedEvent& acEvent) noexcept
{
    auto view = m_world.view<FormIdComponent>();
    const auto entityIt = std::find_if(view.begin(), view.end(), [view, formId = acEvent.FormId](auto aEntity) { return view.get<FormIdComponent>(aEntity).Id == formId; });

    if (entityIt == view.end())
    {
        spdlog::error("Actor to remove not found in form ids map {:X}", acEvent.FormId);
        return;
    }

    const auto cId = *entityIt;

    auto& formIdComponent = view.get<FormIdComponent>(cId);
    CancelServerAssignment(*entityIt, formIdComponent.Id);

    m_world.remove<EarlyAnimationBufferComponent>(cId);

    if (m_world.all_of<FormIdComponent>(cId))
        m_world.remove<FormIdComponent>(cId);

    if (m_world.orphan(cId))
        m_world.destroy(cId);

    spdlog::info("Actor removed, form id: {:X}", acEvent.FormId);
}

void CharacterService::OnUpdate(const UpdateEvent& acUpdateEvent) noexcept
{
    RunSpawnUpdates();
    RunLocalUpdates();
    RunFactionsUpdates();
    RunRemoteUpdates(acUpdateEvent);
    RunExperienceUpdates();
    ApplyCachedWeaponDraws(acUpdateEvent);
}

void CharacterService::OnConnected(const ConnectedEvent& acConnectedEvent) const noexcept
{
    // Go through all the forms that were previously detected
    auto view = m_world.view<FormIdComponent>(entt::exclude<ObjectComponent>);
    Vector<entt::entity> entities(view.begin(), view.end());

    for (auto entity : entities)
    {
        auto& formIdComponent = m_world.get<FormIdComponent>(entity);
        // Delete all temporary actors on connect
        if (formIdComponent.Id > 0xFF000000)
        {
            Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
            if (pActor)
                pActor->Delete();

            continue;
        }

        ProcessNewEntity(entity);
    }
}

void CharacterService::OnDisconnected(const DisconnectedEvent& acDisconnectedEvent) const noexcept
{
    auto remoteView = m_world.view<FormIdComponent, RemoteComponent>();
    for (auto entity : remoteView)
    {
        auto& formIdComponent = remoteView.get<FormIdComponent>(entity);

        auto pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
        if (!pActor)
            continue;

        if (pActor->GetExtension()->IsRemotePlayer())
            pActor->Delete();
        else
            pActor->GetExtension()->SetRemote(false);
    }

    m_world.clear<WaitingForAssignmentComponent, LocalComponent, RemoteComponent, RemoteVRPoseComponent, RemoteVREquipmentComponent>();
}

void CharacterService::OnAssignCharacter(const AssignCharacterResponse& acMessage) noexcept
{
    spdlog::info("Received for cookie {:X}, server id {:X}", acMessage.Cookie, acMessage.ServerId);

    auto view = m_world.view<WaitingForAssignmentComponent>();
    const auto itor =
        std::find_if(std::begin(view), std::end(view), [view, cookie = acMessage.Cookie](auto entity) { return view.get<WaitingForAssignmentComponent>(entity).Cookie == cookie; });

    if (itor == std::end(view))
    {
        spdlog::warn("Never found requested cookie: {}", acMessage.Cookie);
        return;
    }

    const auto cEntity = *itor;

    m_world.remove<WaitingForAssignmentComponent>(cEntity);
#if (!IS_MASTER)
    m_world.remove<ReplayedActionsDebugComponent>(cEntity);
#endif

    const auto formIdComponent = m_world.try_get<FormIdComponent>(cEntity);
    if (!formIdComponent)
    {
        spdlog::error(__FUNCTION__ ": form id component doesn't exist, cookie: {:X}", acMessage.Cookie);
        return;
    }

    Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent->Id));
    if (!pActor)
    {
        spdlog::error(__FUNCTION__ ": actor not found, form id: {:X}", formIdComponent->Id);
        m_world.destroy(cEntity);
        return;
    }

    // TODO: how could this possibly trigger?
    // it's kinda interfering with my WaitingFor3D code
    if (acMessage.PlayerId != 0)
        m_world.emplace_or_replace<PlayerComponent>(cEntity, acMessage.PlayerId);

    if (acMessage.Owner)
    {
        spdlog::info("Received local actor, form id: {:X}", pActor->GetFormIdData());

        m_world.emplace_or_replace<LocalComponent>(cEntity, acMessage.ServerId);
        auto& localAnimationComponent = m_world.emplace_or_replace<LocalAnimationComponent>(cEntity);

        pActor->GetExtension()->SetRemote(false);

        if (auto* pEarlyAnimComponent = m_world.try_get<EarlyAnimationBufferComponent>(cEntity))
        {
            for (const auto& action : pEarlyAnimComponent->Actions)
            {
                localAnimationComponent.Append(action);
            }
        }
        m_world.remove<EarlyAnimationBufferComponent>(cEntity);
    }
    else
    {
        spdlog::info("Received remote actor, form id: {:X}, isweapondrawn: {}", pActor->GetFormIdData(), acMessage.IsWeaponDrawn);

        m_world.emplace_or_replace<RemoteComponent>(cEntity, acMessage.ServerId, formIdComponent->Id);

        pActor->GetExtension()->SetRemote(true);

        m_world.remove<EarlyAnimationBufferComponent>(cEntity);
        InterpolationSystem::Setup(m_world, cEntity);
        AnimationSystem::Setup(m_world, cEntity);
        AnimationSystem::AddActionsForReplay(m_world.get<RemoteAnimationComponent>(cEntity), acMessage.ActionsToReplay);

#if (!IS_MASTER)
        m_world.emplace_or_replace<ReplayedActionsDebugComponent>(cEntity, acMessage.ActionsToReplay);
#endif

        pActor->SetActorValues(acMessage.AllActorValues);
        pActor->SetActorInventory(acMessage.CurrentInventory);

        if (pActor->IsDead() != acMessage.IsDead)
            acMessage.IsDead ? pActor->Kill() : pActor->Respawn();

        m_weaponDrawUpdates[pActor->GetFormIdData()] = {acMessage.IsWeaponDrawn};

        MoveActor(pActor, acMessage.WorldSpaceId, acMessage.CellId, acMessage.Position);
    }
}

void CharacterService::OnCharacterSpawn(const CharacterSpawnRequest& acMessage) const noexcept
{
    auto remoteView = m_world.view<RemoteComponent>();
    const auto remoteItor =
        std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.ServerId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

    if (remoteItor != std::end(remoteView))
    {
        spdlog::warn("Character with remote id {:X} is already spawned.", acMessage.ServerId);
        return;
    }

    Actor* pActor = nullptr;

    std::optional<entt::entity> entity;

    // Custom forms
    if (acMessage.FormId == GameId{})
    {
        TESNPC* pNpc = nullptr;

        entity = m_world.create();

        if (acMessage.BaseId != GameId{})
        {
            const auto cNpcId = World::Get().GetModSystem().GetGameId(acMessage.BaseId);
            if (cNpcId == 0)
            {
                spdlog::error(
                    "Failed to retrieve NPC, it will not be spawned, possibly missing mod, base: {:X}:{:X}, form: {:X}:{:X}", acMessage.BaseId.BaseId, acMessage.BaseId.ModId,
                    acMessage.FormId.BaseId, acMessage.FormId.ModId);
                return;
            }

            pNpc = Cast<TESNPC>(TESForm::GetById(cNpcId));
            pNpc->Deserialize(acMessage.AppearanceBuffer, acMessage.ChangeFlags);
        }
        else
        {
            // Players and npcs with temporary ref ids and base ids (usually random events)
            pNpc = TESNPC::Create(acMessage.AppearanceBuffer, acMessage.ChangeFlags);
            FaceGenSystem::Setup(m_world, *entity, acMessage.FaceTints);
        }

        pActor = Actor::Create(pNpc);
    }
    else
    {
        const uint32_t cActorId = World::Get().GetModSystem().GetGameId(acMessage.FormId);

        auto waitingView = m_world.view<FormIdComponent, WaitingForAssignmentComponent>();
        const auto waitingItor =
            std::find_if(std::begin(waitingView), std::end(waitingView), [waitingView, cActorId](auto entity) { return waitingView.get<FormIdComponent>(entity).Id == cActorId; });

        if (waitingItor != std::end(waitingView))
        {
            spdlog::info("Character with form id {:X} already has a spawn request in progress.", cActorId);
            return;
        }

        auto* const pForm = TESForm::GetById(cActorId);
        pActor = Cast<Actor>(pForm);

        if (!pActor)
        {
            spdlog::error("Failed to retrieve Actor {:X}, it will not be spawned, possibly missing mod", cActorId);
            spdlog::error("\tForm : {:X}", pForm ? pForm->GetFormIdData() : 0);
            return;
        }

        const auto view = m_world.view<FormIdComponent>();
        const auto itor = std::find_if(std::begin(view), std::end(view), [cActorId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == cActorId; });

        if (itor != std::end(view))
            entity = *itor;
        else
            entity = m_world.create();
    }

    if (!pActor)
    {
        spdlog::error("Actor object {:X} could not be created.", acMessage.ServerId);
        return;
    }

    spdlog::info("CharacterSpawnRequest, server id: {:X}, form id: {:X}", acMessage.ServerId, pActor->GetFormIdData());

    if (pActor->IsDisabled())
    {
        spdlog::warn("Disabled actor is being re-enabled: {:X}", pActor->GetFormIdData());
        pActor->EnableImpl();
    }

    pActor->GetExtension()->SetRemote(true);

    auto rotation = pActor->GetRotationData();
    rotation.x = acMessage.Rotation.x;
    rotation.z = acMessage.Rotation.y;
    pActor->SetRotationData(rotation);
    pActor->MoveTo(PlayerCharacter::Get()->GetParentCellData(), acMessage.Position);
    pActor->SetActorValues(acMessage.InitialActorValues);

    pActor->GetExtension()->SetPlayer(acMessage.IsPlayer);
    if (acMessage.IsPlayer)
    {
        pActor->SetIgnoreFriendlyHit(true);
        pActor->SetPlayerRespawnMode();
        m_world.emplace_or_replace<PlayerComponent>(*entity, acMessage.PlayerId);
    }

    if (pActor->IsDead() != acMessage.IsDead)
        acMessage.IsDead ? pActor->Kill() : pActor->Respawn();

    spdlog::info("Spawn Request Is summon {}", acMessage.IsPlayerSummon);

    if (acMessage.IsPlayerSummon)
    {
        // Prevents remote summons agroing other players.
        pActor->SetCommandingActor(PlayerCharacter::Get()->GetHandle());
    }

    auto& remoteComponent = m_world.emplace_or_replace<RemoteComponent>(*entity, acMessage.ServerId, pActor->GetFormIdData());

    auto& interpolationComponent = InterpolationSystem::Setup(m_world, *entity);
    interpolationComponent.Position = acMessage.Position;

    AnimationSystem::Setup(m_world, *entity);

    m_world.emplace_or_replace<WaitingFor3D>(*entity, acMessage);

    auto& remoteAnimationComponent = m_world.get<RemoteAnimationComponent>(*entity);

    AnimationSystem::AddActionsForReplay(remoteAnimationComponent, acMessage.ActionsToReplay);

#if (!IS_MASTER)
    m_world.emplace_or_replace<ReplayedActionsDebugComponent>(*entity, acMessage.ActionsToReplay);
#endif
}

void CharacterService::OnRemoteSpawnDataReceived(const NotifySpawnData& acMessage) noexcept
{
    auto view = m_world.view<FormIdComponent>(entt::exclude<ObjectComponent>);

    const auto itor = std::find_if(
        std::begin(view), std::end(view),
        [view, id = acMessage.Id](auto entity)
        {
            if (auto serverId = Utils::GetServerId(entity))
            {
                if (*serverId == id)
                    return true;
            }
            return false;
        });

    if (itor == std::end(view))
        return;

    if (auto* pWaitingFor3D = m_world.try_get<WaitingFor3D>(*itor))
    {
        pWaitingFor3D->SpawnRequest.InitialActorValues = acMessage.NewActorData.InitialActorValues;
        pWaitingFor3D->SpawnRequest.InventoryContent = acMessage.NewActorData.InitialInventory;
        pWaitingFor3D->SpawnRequest.IsDead = acMessage.NewActorData.IsDead;
        pWaitingFor3D->SpawnRequest.IsWeaponDrawn = acMessage.NewActorData.IsWeaponDrawn;
    }

    auto& formIdComponent = view.get<FormIdComponent>(*itor);
    Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));

    if (!pActor)
        return;

    pActor->SetActorValues(acMessage.NewActorData.InitialActorValues);
    pActor->SetActorInventory(acMessage.NewActorData.InitialInventory);
    m_weaponDrawUpdates[pActor->GetFormIdData()] = {acMessage.NewActorData.IsWeaponDrawn};

    if (pActor->IsDead() != acMessage.NewActorData.IsDead)
        acMessage.NewActorData.IsDead ? pActor->Kill() : pActor->Respawn();

    spdlog::info("Applied remote spawn data, actor form id: {:X}", pActor->GetFormIdData());
}

void CharacterService::OnReferencesMoveRequest(const ServerReferencesMoveRequest& acMessage) const noexcept
{
    auto view = m_world.view<RemoteComponent, InterpolationComponent, RemoteAnimationComponent>();

    for (const auto& [serverId, update] : acMessage.Updates)
    {
        auto itor = std::find_if(std::begin(view), std::end(view), [serverId = serverId, view](entt::entity entity) { return view.get<RemoteComponent>(entity).Id == serverId; });

        if (itor == std::end(view))
            continue;

        auto& interpolationComponent = view.get<InterpolationComponent>(*itor);
        auto& animationComponent = view.get<RemoteAnimationComponent>(*itor);
        const auto& movement = update.UpdatedMovement;

        InterpolationComponent::TimePoint point;
        point.Tick = acMessage.Tick;
        point.Position = movement.Position;
        point.Rotation = {movement.Rotation.x, 0.f, movement.Rotation.y};
        point.Variables = movement.Variables;
        point.Direction = movement.Direction;

        InterpolationSystem::AddPoint(interpolationComponent, point);

        for (const auto& action : update.ActionEvents)
        {
            animationComponent.TimePoints.push_back(action);
        }
    }
}

void CharacterService::OnActionEvent(const ActionEvent& acActionEvent) const noexcept
{
    auto view = m_world.view<LocalAnimationComponent, FormIdComponent>();
    const auto itor =
        std::find_if(std::begin(view), std::end(view), [id = acActionEvent.ActorId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (itor != std::end(view))
    {
        auto& localComponent = view.get<LocalAnimationComponent>(*itor);

        localComponent.Append(acActionEvent);
    }
    else if (m_transport.IsOnline())
    {
        // A `LocalAnimationComponent` is not attached yet, but the actor already exists and is running animations

        auto view = m_world.view<FormIdComponent, EarlyAnimationBufferComponent>();
        const auto itor =
            std::find_if(std::begin(view), std::end(view), [id = acActionEvent.ActorId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == id; });

        if (itor != std::end(view))
        {
            view.get<EarlyAnimationBufferComponent>(*itor).Actions.push_back(acActionEvent);
        }
    }
}

void CharacterService::OnFactionsChanges(const NotifyFactionsChanges& acEvent) const noexcept
{
    auto view = m_world.view<RemoteComponent, FormIdComponent, CacheComponent>();

    for (const auto& [id, factions] : acEvent.Changes)
    {
        const auto itor = std::find_if(std::begin(view), std::end(view), [id = id, view](entt::entity entity) { return view.get<RemoteComponent>(entity).Id == id; });

        if (itor != std::end(view))
        {
            auto& formIdComponent = view.get<FormIdComponent>(*itor);

            auto* const pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
            if (!pActor)
                return;

            auto& cacheComponent = view.get<CacheComponent>(*itor);
            cacheComponent.FactionsContent = factions;

            pActor->SetFactions(cacheComponent.FactionsContent);
        }
    }
}

void CharacterService::OnOwnershipTransfer(const NotifyOwnershipTransfer& acMessage) const noexcept
{
    // TODO(cosideci): handle case if no one has it, therefore no RemoteComponent
    auto view = m_world.view<RemoteComponent, FormIdComponent>();

    const auto itor = std::find_if(std::begin(view), std::end(view), [&acMessage, &view](auto entity) { return view.get<RemoteComponent>(entity).Id == acMessage.ServerId; });

    if (itor != std::end(view))
    {
        auto& formIdComponent = view.get<FormIdComponent>(*itor);

        if (TakeOwnership(formIdComponent.Id, acMessage.ServerId, *itor))
        {
            spdlog::info("Ownership claimed {:X}", acMessage.ServerId);
            return;
        }
    }

    spdlog::warn("Actor for ownership transfer not found {:X}", acMessage.ServerId);

    RequestOwnershipTransfer request{};
    request.ServerId = acMessage.ServerId;

    m_transport.Send(request);
}

void CharacterService::OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) const noexcept
{
    auto view = m_world.view<RemoteComponent>();

    const auto itor = std::find_if(std::begin(view), std::end(view), [id = acMessage.ServerId, view](entt::entity entity) { return view.get<RemoteComponent>(entity).Id == id; });

    if (itor != std::end(view))
    {
        if (auto* pFormIdComponent = m_world.try_get<FormIdComponent>(*itor))
            CharacterService::DeleteTempActor(pFormIdComponent->Id);

        DeleteRemoteEntityComponents(*itor);
    }
}

void CharacterService::OnNotifyRespawn(const NotifyRespawn& acMessage) const noexcept
{
    auto view = m_world.view<FormIdComponent, RemoteComponent>();
    const auto entityIt = std::find_if(view.begin(), view.end(), [view, id = acMessage.ActorId](auto aEntity) { return view.get<RemoteComponent>(aEntity).Id == id; });

    if (entityIt == view.end())
    {
        spdlog::error("Actor to respawn not found in: {:X}", acMessage.ActorId);
        return;
    }

    const auto cId = *entityIt;

    auto& formIdComponent = view.get<FormIdComponent>(cId);
    CancelServerAssignment(*entityIt, formIdComponent.Id);

    m_world.remove<EarlyAnimationBufferComponent>(cId);

    if (m_world.all_of<FormIdComponent>(cId))
        m_world.remove<FormIdComponent>(cId);

    if (m_world.orphan(cId))
        m_world.destroy(cId);

    RequestRespawn request;
    request.ActorId = acMessage.ActorId;

    m_transport.Send(request);
}

void CharacterService::OnBeastFormChange(const BeastFormChangeEvent& acEvent) const noexcept
{
    auto view = m_world.view<FormIdComponent>();

    const auto it = std::find_if(view.begin(), view.end(), [view](auto entity) { return view.get<FormIdComponent>(entity).Id == 0x14; });

    std::optional<uint32_t> serverIdRes = Utils::GetServerId(*it);
    if (!serverIdRes.has_value())
    {
        spdlog::error("{}: failed to find server id", __FUNCTION__);
        return;
    }

    uint32_t serverId = serverIdRes.value();

    RequestRespawn request;
    request.ActorId = serverId;

    Actor* pActor = Utils::GetByServerId<Actor>(serverId);
    if (!pActor)
    {
        spdlog::warn(__FUNCTION__ ": could not find actor for server id {:X}", serverId);
        return;
    }

    TESNPC* pNpc = Cast<TESNPC>(pActor->GetBaseFormData());
    if (!pNpc)
    {
        spdlog::warn(__FUNCTION__ ": could not find actor baseform for server id {:X}", serverId);
        return;
    }

    pNpc->Serialize(&request.AppearanceBuffer);
    request.ChangeFlags = pNpc->GetChangeFlags();

    m_transport.Send(request);
}

void CharacterService::OnMountEvent(const MountEvent& acEvent) const noexcept
{
    auto view = m_world.view<FormIdComponent>();

    const auto riderIt = std::find_if(std::begin(view), std::end(view), [id = acEvent.RiderID, view](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (riderIt == std::end(view))
    {
        spdlog::warn("Rider not found, form id: {:X}", acEvent.RiderID);
        return;
    }

    const entt::entity cRiderEntity = *riderIt;

    std::optional<uint32_t> riderServerIdRes = Utils::GetServerId(cRiderEntity);
    if (!riderServerIdRes.has_value())
    {
        spdlog::error("{}: failed to find server id", __FUNCTION__);
        return;
    }

    const auto mountIt = std::find_if(std::begin(view), std::end(view), [id = acEvent.MountID, view](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (mountIt == std::end(view))
    {
        spdlog::warn("Mount not found, form id: {:X}", acEvent.MountID);
        return;
    }

    const entt::entity cMountEntity = *mountIt;

    std::optional<uint32_t> mountServerIdRes = Utils::GetServerId(cMountEntity);
    if (!mountServerIdRes.has_value())
    {
        spdlog::error("{}: failed to find server id", __FUNCTION__);
        return;
    }

    if (m_world.try_get<RemoteComponent>(cMountEntity))
        TakeOwnership(acEvent.MountID, *mountServerIdRes, cMountEntity);

    MountRequest request;
    request.MountId = mountServerIdRes.value();
    request.RiderId = riderServerIdRes.value();

    m_transport.Send(request);
}

void CharacterService::OnNotifyMount(const NotifyMount& acMessage) const noexcept
{
    auto remoteView = m_world.view<RemoteComponent, FormIdComponent>();

    const auto riderIt =
        std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.RiderId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

    if (riderIt == std::end(remoteView))
    {
        spdlog::warn("Rider with remote id {:X} not found.", acMessage.RiderId);
        return;
    }

    auto& riderFormIdComponent = remoteView.get<FormIdComponent>(*riderIt);
    TESForm* pRiderForm = TESForm::GetById(riderFormIdComponent.Id);
    Actor* pRider = Cast<Actor>(pRiderForm);

    Actor* pMount = nullptr;

    auto formView = m_world.view<FormIdComponent>();
    Vector<entt::entity> entities(formView.begin(), formView.end());

    // TODO(cosideci): remove this, cause of NotifyRelinquishControl?
    for (auto entity : entities)
    {
        std::optional<uint32_t> serverIdRes = Utils::GetServerId(entity);
        if (!serverIdRes.has_value())
        {
            spdlog::error("{}: failed to find server id", __FUNCTION__);
            continue;
        }

        uint32_t serverId = serverIdRes.value();

        if (serverId == acMessage.MountId)
        {
            auto& mountFormIdComponent = m_world.get<FormIdComponent>(entity);

            if (m_world.all_of<LocalComponent>(entity))
            {
                m_world.remove<LocalAnimationComponent, LocalComponent>(entity);
                m_world.emplace_or_replace<RemoteComponent>(entity, acMessage.MountId, mountFormIdComponent.Id);
            }

            TESForm* pMountForm = TESForm::GetById(mountFormIdComponent.Id);
            pMount = Cast<Actor>(pMountForm);
            pMount->GetExtension()->SetRemote(true);

            InterpolationSystem::Setup(m_world, entity);
            AnimationSystem::Setup(m_world, entity);

            break;
        }
    }

    pRider->InitiateMountPackage(pMount);
}

void CharacterService::OnInitPackageEvent(const InitPackageEvent& acEvent) const noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>();

    const auto actorIt = std::find_if(std::begin(view), std::end(view), [id = acEvent.ActorId, view](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (actorIt == std::end(view))
        return;

    const entt::entity cActorEntity = *actorIt;

    std::optional<uint32_t> actorServerIdRes = Utils::GetServerId(cActorEntity);
    if (!actorServerIdRes.has_value())
    {
        spdlog::error("{}: failed to find server id", __FUNCTION__);
        return;
    }

    NewPackageRequest request;
    request.ActorId = actorServerIdRes.value();
    if (!m_world.GetModSystem().GetServerModId(acEvent.PackageId, request.PackageId.ModId, request.PackageId.BaseId))
        return;

    m_transport.Send(request);
}

void CharacterService::OnNotifyNewPackage(const NotifyNewPackage& acMessage) const noexcept
{
    auto remoteView = m_world.view<RemoteComponent, FormIdComponent>();
    const auto remoteIt =
        std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.ActorId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

    if (remoteIt == std::end(remoteView))
    {
        spdlog::warn("Actor for package with remote id {:X} not found.", acMessage.ActorId);
        return;
    }

    auto formIdComponent = remoteView.get<FormIdComponent>(*remoteIt);

    const TESForm* pForm = TESForm::GetById(formIdComponent.Id);
    Actor* pActor = Cast<Actor>(pForm);

    const uint32_t cPackageFormId = World::Get().GetModSystem().GetGameId(acMessage.PackageId);
    const TESForm* pPackageForm = TESForm::GetById(cPackageFormId);
    if (!pPackageForm)
    {
        spdlog::warn("Actor package not found, base id: {:X}, mod id: {:X}", acMessage.PackageId.BaseId, acMessage.PackageId.ModId);
        return;
    }

    TESPackage* pPackage = Cast<TESPackage>(pPackageForm);

    pActor->SetPackage(pPackage);
}

void CharacterService::OnAddExperienceEvent(const AddExperienceEvent& acEvent) noexcept
{
    m_cachedExperience += acEvent.Experience;
}

void CharacterService::OnNotifySyncExperience(const NotifySyncExperience& acMessage) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();

    if (PlayerCharacter::LastUsedCombatSkill == -1)
        return;

    pPlayer->AddSkillExperience(PlayerCharacter::LastUsedCombatSkill, acMessage.Experience);
}

void CharacterService::OnDialogueEvent(const DialogueEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>(entt::exclude<ObjectComponent>);
    auto entityIt = std::find_if(view.begin(), view.end(), [view, formId = acEvent.ActorID](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (entityIt == view.end())
        return;

    auto serverIdRes = Utils::GetServerId(*entityIt);
    if (!serverIdRes)
    {
        spdlog::error("{}: server id not found for form id {:X}", __FUNCTION__, acEvent.ActorID);
        return;
    }

    DialogueRequest request{};
    request.ServerId = serverIdRes.value();
    request.SoundFilename = acEvent.VoiceFile;

    m_transport.Send(request);
}

void CharacterService::OnNotifyDialogue(const NotifyDialogue& acMessage) noexcept
{
    auto remoteView = m_world.view<RemoteComponent, FormIdComponent>();
    const auto remoteIt =
        std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.ServerId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

    if (remoteIt == std::end(remoteView))
    {
        spdlog::warn("Actor for dialogue with remote id {:X} not found.", acMessage.ServerId);
        return;
    }

    auto formIdComponent = remoteView.get<FormIdComponent>(*remoteIt);
    const TESForm* pForm = TESForm::GetById(formIdComponent.Id);
    Actor* pActor = Cast<Actor>(pForm);

    if (!pActor)
        return;

    pActor->StopCurrentDialogue(true);
    pActor->SpeakSound(acMessage.SoundFilename.c_str());
}

void CharacterService::OnSubtitleEvent(const SubtitleEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>(entt::exclude<ObjectComponent>);
    auto entityIt = std::find_if(view.begin(), view.end(), [view, formId = acEvent.SpeakerID](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (entityIt == view.end())
        return;

    auto serverIdRes = Utils::GetServerId(*entityIt);
    if (!serverIdRes)
    {
        spdlog::error("{}: server id not found for form id {:X}", __FUNCTION__, acEvent.SpeakerID);
        return;
    }

    SubtitleRequest request{};
    request.ServerId = serverIdRes.value();
    request.Text = acEvent.Text;
    request.TopicFormId = acEvent.TopicFormID;

    m_transport.Send(request);
}

void CharacterService::OnNotifySubtitle(const NotifySubtitle& acMessage) noexcept
{
    auto remoteView = m_world.view<RemoteComponent, FormIdComponent>();
    const auto remoteIt =
        std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.ServerId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

    if (remoteIt == std::end(remoteView))
    {
        spdlog::warn("Actor for dialogue with remote id {:X} not found.", acMessage.ServerId);
        return;
    }

    auto formIdComponent = remoteView.get<FormIdComponent>(*remoteIt);
    const TESForm* pForm = TESForm::GetById(formIdComponent.Id);
    Actor* pActor = Cast<Actor>(pForm);

    if (!pActor)
        return;

    // This is only for fallout 4
    TESTopicInfo* pInfo = nullptr;
    pInfo = Cast<TESTopicInfo>(TESForm::GetById(acMessage.TopicFormId));

    SubtitleManager::Get()->ShowSubtitle(pActor, acMessage.Text.c_str(), pInfo);
}

void CharacterService::OnNotifyRelinquishControl(const NotifyRelinquishControl& acMessage) noexcept
{
    auto formView = m_world.view<FormIdComponent>();
    Vector<entt::entity> entities(formView.begin(), formView.end());

    // TODO(cosideci): this entity iteration shouldn't technically be necessary, just look for the local component
    for (auto entity : entities)
    {
        std::optional<uint32_t> serverIdRes = Utils::GetServerId(entity);
        if (!serverIdRes.has_value())
        {
            spdlog::error(__FUNCTION__ ": failed to find server id for entity");
            continue;
        }

        uint32_t serverId = serverIdRes.value();

        if (serverId == acMessage.ServerId)
        {
            auto& formIdComponent = m_world.get<FormIdComponent>(entity);

            if (m_world.all_of<LocalComponent>(entity))
            {
                m_world.remove<LocalAnimationComponent, LocalComponent>(entity);
                m_world.emplace_or_replace<RemoteComponent>(entity, acMessage.ServerId, formIdComponent.Id);
            }

            Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
            if (!pActor)
            {
                // Probably left the room and/or temporary.
                spdlog::info(__FUNCTION__ ": no local Actor for serverId {:X} to relinquish", serverId);
                continue;
            }

            pActor->GetExtension()->SetRemote(true);

            InterpolationSystem::Setup(m_world, entity);
            AnimationSystem::Setup(m_world, entity);

            spdlog::info(__FUNCTION__ ": relinquished control of actor {:X} with server id {:X}", pActor->GetFormIdData(), acMessage.ServerId);

            return;
        }
    }

    spdlog::error("Did not find actor to relinquish control of, server id {:X}", acMessage.ServerId);
}

void CharacterService::OnNotifyActorTeleport(const NotifyActorTeleport& acMessage) noexcept
{
    auto& modSystem = m_world.GetModSystem();

    const uint32_t cActorId = World::Get().GetModSystem().GetGameId(acMessage.FormId);
    Actor* pActor = Cast<Actor>(TESForm::GetById(cActorId));
    if (!pActor)
    {
        spdlog::error(__FUNCTION__ ": failed to retrieve actor to teleport.");
        return;
    }

    MoveActor(pActor, acMessage.WorldSpaceId, acMessage.CellId, acMessage.Position);

    spdlog::info(
        "Successfully teleported actor, form id: {:X}, world space: {:X}, cell: {:X}, position: ({}, {}, {})", pActor->GetFormIdData(), acMessage.WorldSpaceId.BaseId,
        acMessage.CellId.BaseId, acMessage.Position.x, acMessage.Position.y, acMessage.Position.z);
}

void CharacterService::OnPartyJoinedEvent(const PartyJoinedEvent& acEvent) noexcept
{
    // Takes ownership of all actors
    if (acEvent.IsLeader)
    {
        auto view = m_world.view<FormIdComponent>(entt::exclude<ObjectComponent>);
        Vector<entt::entity> entities(view.begin(), view.end());

        for (auto entity : entities)
            ProcessNewEntity(entity);
    }
}

void CharacterService::MoveActor(const Actor* apActor, const GameId& acWorldSpaceId, const GameId& acCellId, const Vector3_NetQuantize& acPosition) const noexcept
{
    TESObjectCELL* pCell = nullptr;
    if (!acWorldSpaceId)
    {
        const uint32_t cCellId = m_world.GetModSystem().GetGameId(acCellId);
        pCell = Cast<TESObjectCELL>(TESForm::GetById(cCellId));
    }
    // In case of lazy-loading of exterior cells
    else
    {
        const uint32_t cWorldSpaceId = m_world.GetModSystem().GetGameId(acWorldSpaceId);
        TESWorldSpace* const pWorldSpace = Cast<TESWorldSpace>(TESForm::GetById(cWorldSpaceId));
        if (pWorldSpace)
        {
            GridCellCoords coordinates = GridCellCoords::CalculateGridCellCoords(acPosition);
            pCell = pWorldSpace->LoadCell(coordinates.X, coordinates.Y);
        }
    }

    if (!pCell)
    {
        spdlog::error(
            __FUNCTION__ ": failed to fetch cell to teleport, actor: {:X}, worldspace: {:X}, cell: {:X}, position: {}, {}, {}", apActor->GetFormIdData(), acWorldSpaceId.BaseId,
            acCellId.BaseId, acPosition.x, acPosition.y, acPosition.z);
        return;
    }

    apActor->MoveTo(pCell, acPosition);
}

void CharacterService::ProcessNewEntity(entt::entity aEntity) const noexcept
{
    if (!m_transport.IsOnline())
        return;

    auto& formIdComponent = m_world.get<FormIdComponent>(aEntity);

    Actor* const pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
    if (!pActor)
    {
        spdlog::warn(__FUNCTION__ ": actor for new entity not found, form id: {:X}", formIdComponent.Id);
        return;
    }

    if (auto* pRemoteComponent = m_world.try_get<RemoteComponent>(aEntity); pRemoteComponent)
    {
        // TODO(cosideci): don't just take all actors (i.e. from other parties),
        // maybe check it server side, add a variable to the request.
        if (m_world.GetPartyService().IsLeader() && !pActor->IsTemporary() && !pActor->IsMount())
        {
            spdlog::info("Sending ownership claim for actor {:X} with server id {:X}", pActor->GetFormIdData(), pRemoteComponent->Id);

            TakeOwnership(pActor->GetFormIdData(), pRemoteComponent->Id, aEntity);
        }
        else
            spdlog::info("New entity remotely managed, form id: {:X}, server id: {:X}", pActor->GetFormIdData(), pRemoteComponent->Id);

        return;
    }

    if (m_world.any_of<RemoteComponent, LocalComponent, WaitingForAssignmentComponent>(aEntity))
        return;

    CacheSystem::Setup(World::Get(), aEntity, pActor);

    RequestServerAssignment(aEntity);
}

void CharacterService::RequestServerAssignment(const entt::entity aEntity) const noexcept
{
    if (!m_transport.IsOnline())
        return;

    static uint32_t sCookieSeed = 0;

    const auto& formIdComponent = m_world.get<FormIdComponent>(aEntity);

    auto* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
    if (!pActor)
        return;

    TESNPC* pNpc = Cast<TESNPC>(pActor->GetBaseFormData());
    if (!pNpc)
        return;

    AssignCharacterRequest message{};

    message.Cookie = sCookieSeed;

    if (!m_world.GetModSystem().GetServerModId(formIdComponent.Id, message.ReferenceId))
    {
        spdlog::error("Server reference id not found for form id {:X}", formIdComponent.Id);
        return;
    }

    const auto* pParentCell = pActor->GetParentCellData();
    if (!pParentCell)
    {
        spdlog::error("Server assignment failed for form id {:X}: parent cell unavailable", pActor->GetFormIdData());
        return;
    }

    if (!m_world.GetModSystem().GetServerModId(pParentCell->GetFormIdData(), message.CellId))
    {
        spdlog::error("Server cell id not found for cell id {:X}", pParentCell->GetFormIdData());
        return;
    }

    if (const auto pWorldSpace = pActor->GetWorldSpace())
    {
        if (!m_world.GetModSystem().GetServerModId(pWorldSpace->GetFormIdData(), message.WorldSpaceId))
            return;
    }

    const auto& position = pActor->GetPositionData();
    const auto& rotation = pActor->GetRotationData();
    message.Position = position;
    message.Rotation.x = rotation.x;
    message.Rotation.y = rotation.z;

    // Serialize the base form
    const auto isPlayer = (formIdComponent.Id == 0x14);
    const auto isTemporary = pActor->IsTemporary();

    if (isPlayer)
    {
        pNpc->MarkChanged(0x2000800);
    }

    const auto changeFlags = pNpc->GetChangeFlags();

    if (isPlayer || changeFlags != 0)
    {
        message.ChangeFlags = changeFlags;
        pNpc->Serialize(&message.AppearanceBuffer);
    }

    if (isPlayer)
    {
        const auto* pPlayer = PlayerCharacter::Get();
        if (pPlayer && pPlayer->CanReadTintData())
        {
            auto& entries = message.FaceTints.Entries;

            const auto& tints = pPlayer->GetTints();

            entries.resize(tints.length);

            for (auto i = 0u; i < tints.length; ++i)
            {
                entries[i].Alpha = tints[i]->alpha;
                entries[i].Color = tints[i]->color;
                entries[i].Type = tints[i]->type;

                if (tints[i]->texture)
                    entries[i].Name = tints[i]->texture->name.AsAscii();
            }

            message.HasFaceTints = true;
        }
    }

    if (isPlayer)
    {
        const auto* pPlayer = PlayerCharacter::Get();
        if (pPlayer && pPlayer->CanReadObjectiveData())
        {
            auto& questLog = message.QuestContent.Entries;
            auto& modSystem = m_world.GetModSystem();

            for (const auto& objective : pPlayer->GetObjectives())
            {
                if (!objective.instance)
                    continue;

                auto* pQuest = objective.instance->quest;
                if (!pQuest)
                    continue;

                if (!QuestService::IsNonSyncableQuest(pQuest))
                {
                    GameId id{};

                    if (modSystem.GetServerModId(pQuest->GetFormIdData(), id))
                    {
                        auto& entry = questLog.emplace_back();
                        entry.Stage = pQuest->GetCurrentStageData();
                        entry.Id = id;
                    }
                }
            }

            // remove duplicates
            const auto ip = std::unique(questLog.begin(), questLog.end());
            questLog.resize(std::distance(questLog.begin(), ip));

            message.HasQuestContent = true;
        }
    }

    message.CurrentActorData = BuildActorData(pActor);

    message.FactionsContent = pActor->GetFactions();
    message.IsDragon = pActor->IsDragon();
    message.IsMount = pActor->IsMount();
    message.IsPlayerSummon = pActor->GetCommandingActor() && pActor->GetCommandingActor()->GetFormIdData() == 0x14;

    if (pNpc->IsTemporary())
        pNpc = pNpc->GetTemplateBase();

    if (isTemporary)
    {
        if (pNpc && !m_world.GetModSystem().GetServerModId(pNpc->GetFormIdData(), message.FormId))
        {
            spdlog::error("Server NPC form id not found for form id {:X}", pNpc->GetFormIdData());
            return;
        }
    }

    // Serialize actions
    auto* const pExtension = pActor->GetExtension();

    message.LatestAction = pExtension->LatestAnimation;
    pActor->SaveAnimationVariables(message.LatestAction.Variables);

    spdlog::info("Request id: {:X}, cookie: {:X}, entity: {:X}", formIdComponent.Id, sCookieSeed, to_integral(aEntity));

    if (m_transport.Send(message))
    {
        m_world.emplace<WaitingForAssignmentComponent>(aEntity, sCookieSeed);

        sCookieSeed++;
    }
}

void CharacterService::CancelServerAssignment(const entt::entity aEntity, const uint32_t aFormId) const noexcept
{
    if (m_world.all_of<RemoteComponent>(aEntity))
    {
        Actor* pActor = Cast<Actor>(TESForm::GetById(aFormId));

        if (pActor)
        {
            if (pActor->IsTemporary())
            {
                spdlog::info("Temporary Remote Deleted {:X}", aFormId);
                pActor->Delete();
            }
            else
            {
                pActor->GetExtension()->SetRemote(false);
            }
        }

        DeleteRemoteEntityComponents(aEntity);

        return;
    }

    // In the event we were waiting for assignment, drop it
    if (m_world.all_of<WaitingForAssignmentComponent>(aEntity))
    {
        auto& waitingComponent = m_world.get<WaitingForAssignmentComponent>(aEntity);

        CancelAssignmentRequest message;
        message.Cookie = waitingComponent.Cookie;

        m_transport.Send(message);

        m_world.remove<WaitingForAssignmentComponent>(aEntity);
    }

    if (m_world.all_of<LocalComponent>(aEntity))
    {
        auto& localComponent = m_world.get<LocalComponent>(aEntity);

        RequestOwnershipTransfer request{};
        request.ServerId = localComponent.Id;

        if (Actor* pActor = Cast<Actor>(TESForm::GetById(aFormId)))
        {
            if (!pActor->IsTemporary())
            {
                auto& modSystem = m_world.GetModSystem();

                if (TESWorldSpace* pWorldSpace = pActor->GetWorldSpace())
                {
                    if (!modSystem.GetServerModId(pWorldSpace->GetFormIdData(), request.WorldSpaceId))
                        spdlog::error("World space id not found, despite having a world space, {:X}", pWorldSpace->GetFormIdData());
                }

                if (TESObjectCELL* pCell = pActor->GetParentCellEx())
                {
                    if (!modSystem.GetServerModId(pCell->GetFormIdData(), request.CellId))
                        spdlog::error("Cell id not found, despite having a cell, {:X}", pCell->GetFormIdData());
                }

                request.Position = pActor->GetPositionData();
            }
        }

        spdlog::info(
            "Transferring ownership of local actor, server id: {:X}, worldspace: {:X}, cell: {:X}, position: "
            "({}, {}, {})",
            request.ServerId, request.WorldSpaceId.BaseId, request.CellId.BaseId, request.Position.x, request.Position.y, request.Position.z);

        m_transport.Send(request);

        m_world.remove<LocalAnimationComponent, LocalComponent>(aEntity);
    }
}

Actor* CharacterService::CreateCharacterForEntity(entt::entity aEntity) const noexcept
{
    auto* pWaitingFor3D = m_world.try_get<WaitingFor3D>(aEntity);
    auto* pInterpolationComponent = m_world.try_get<InterpolationComponent>(aEntity);

    if (!pWaitingFor3D || !pInterpolationComponent)
    {
        spdlog::error(__FUNCTION__ ": could not find WaitingFor3D or InterpolationComponent");
        return nullptr;
    }

    auto& acMessage = pWaitingFor3D->SpawnRequest;

    Actor* pActor = nullptr;

    // Custom forms
    if (acMessage.FormId == GameId{})
    {
        TESNPC* pNpc = nullptr;

        if (acMessage.BaseId != GameId{})
        {
            const uint32_t cNpcId = World::Get().GetModSystem().GetGameId(acMessage.BaseId);
            if (cNpcId == 0)
            {
                spdlog::error("Failed to retrieve NPC, it will not be spawned, possibly missing mod");
                return nullptr;
            }

            pNpc = Cast<TESNPC>(TESForm::GetById(cNpcId));
            pNpc->Deserialize(acMessage.AppearanceBuffer, acMessage.ChangeFlags);
        }
        else
        {
            pNpc = TESNPC::Create(acMessage.AppearanceBuffer, acMessage.ChangeFlags);
            FaceGenSystem::Setup(m_world, aEntity, acMessage.FaceTints);
        }

        pActor = Actor::Create(pNpc);
    }

    auto& remoteComponent = m_world.get<RemoteComponent>(aEntity);

    if (!pActor)
    {
        spdlog::error(__FUNCTION__ ": could not spawn actor for remote server id {:X}.", remoteComponent.Id);
        return nullptr;
    }

    pActor->GetExtension()->SetRemote(true);
    auto rotation = pActor->GetRotationData();
    rotation.x = acMessage.Rotation.x;
    rotation.z = acMessage.Rotation.y;
    pActor->SetRotationData(rotation);
    pActor->MoveTo(PlayerCharacter::Get()->GetParentCellData(), pInterpolationComponent->Position);
    pActor->SetActorValues(acMessage.InitialActorValues);

    pActor->GetExtension()->SetPlayer(acMessage.IsPlayer);
    if (acMessage.IsPlayer)
    {
        pActor->SetIgnoreFriendlyHit(true);
        pActor->SetPlayerRespawnMode();
        m_world.emplace_or_replace<PlayerComponent>(aEntity, acMessage.PlayerId);
    }

    if (pActor->IsDead() != acMessage.IsDead)
        acMessage.IsDead ? pActor->Kill() : pActor->Respawn();

    spdlog::info("Spawned character for entity, server id: {:X}", remoteComponent.Id);

    return pActor;
}

ActorData CharacterService::BuildActorData(Actor* apActor) const noexcept
{
    ActorData actorData{};
    actorData.InitialActorValues = apActor->GetEssentialActorValues();
    actorData.InitialInventory = apActor->GetActorInventory();
    actorData.IsDead = apActor->IsDead();
    actorData.IsWeaponDrawn = apActor->GetActorStateData().IsWeaponFullyDrawn();

    return actorData;
}

void CharacterService::RunLocalUpdates() const noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 100ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;

    lastSendTimePoint = now;

    ClientReferencesMoveRequest message;
    message.Tick = m_transport.GetClock().GetCurrentTick();

    auto animatedLocalView = m_world.view<LocalComponent, LocalAnimationComponent, FormIdComponent>();

    for (auto entity : animatedLocalView)
    {
        auto& localComponent = animatedLocalView.get<LocalComponent>(entity);
        auto& animationComponent = animatedLocalView.get<LocalAnimationComponent>(entity);
        auto& formIdComponent = animatedLocalView.get<FormIdComponent>(entity);

        AnimationSystem::Serialize(m_world, message, localComponent, animationComponent, formIdComponent);
    }

    m_transport.Send(message);
}

void CharacterService::RunRemoteUpdates(const UpdateEvent& acUpdateEvent) noexcept
{
    UpdateRemoteVRPoseComponents(acUpdateEvent);

    // Delay by 300ms to let the interpolation system accumulate interpolation points
    const auto tick = m_transport.GetClock().GetCurrentTick() - 300;

    // Interpolation has to keep running even if the actor is not in view, otherwise we will never know if we need to spawn it
    auto interpolatedEntities = m_world.view<RemoteComponent, InterpolationComponent>();

    for (auto entity : interpolatedEntities)
    {
        auto* pFormIdComponent = m_world.try_get<FormIdComponent>(entity);
        auto& interpolationComponent = interpolatedEntities.get<InterpolationComponent>(entity);

        Actor* pActor = nullptr;
        if (pFormIdComponent)
        {
            auto* pForm = TESForm::GetById(pFormIdComponent->Id);
            pActor = Cast<Actor>(pForm);
        }

        InterpolationSystem::Update(pActor, interpolationComponent, tick);
    }

    auto animatedView = m_world.view<RemoteComponent, RemoteAnimationComponent, FormIdComponent>();

    for (auto entity : animatedView)
    {
        auto& animationComponent = animatedView.get<RemoteAnimationComponent>(entity);
        auto& formIdComponent = animatedView.get<FormIdComponent>(entity);

        auto* pForm = TESForm::GetById(formIdComponent.Id);
        auto* pActor = Cast<Actor>(pForm);
        if (!pActor)
            continue;

        AnimationSystem::Update(m_world, pActor, animationComponent, tick);
    }

    auto facegenView = m_world.view<FormIdComponent, FaceGenComponent>();

    for (auto entity : facegenView)
    {
        auto& formIdComponent = facegenView.get<FormIdComponent>(entity);
        auto& faceGenComponent = facegenView.get<FaceGenComponent>(entity);

        const auto* pForm = TESForm::GetById(formIdComponent.Id);
        auto* pActor = Cast<Actor>(pForm);
        if (!pActor)
            continue;

        FaceGenSystem::Update(m_world, pActor, faceGenComponent);
    }

    auto waitingView = m_world.view<FormIdComponent, WaitingFor3D>();

    Vector<entt::entity> toRemove;
    for (auto entity : waitingView)
    {
        auto& formIdComponent = waitingView.get<FormIdComponent>(entity);
        auto& waitingFor3D = waitingView.get<WaitingFor3D>(entity);

        Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
        if (!pActor || !pActor->GetNiNode())
            continue;

        // By now, the actor has materialized in the world and is ready for further setup

        pActor->SetActorInventory(waitingFor3D.SpawnRequest.InventoryContent);
        pActor->SetFactions(waitingFor3D.SpawnRequest.FactionsContent);

        if (!waitingFor3D.SpawnRequest.ActionsToReplay.Actions.empty())
        {
            pActor->LoadAnimationVariables(waitingFor3D.SpawnRequest.ActionsToReplay.Actions[0].Variables);
        }

        m_weaponDrawUpdates[pActor->GetFormIdData()] = {waitingFor3D.SpawnRequest.IsWeaponDrawn};

        if (pActor->IsDead() != waitingFor3D.SpawnRequest.IsDead)
            waitingFor3D.SpawnRequest.IsDead ? pActor->Kill() : pActor->Respawn();

        if (pActor->IsVampireLord())
            pActor->FixVampireLordModel();

        toRemove.push_back(entity);

        spdlog::info("Applied 3D for actor, form id: {:X}", pActor->GetFormIdData());
    }

    for (auto entity : toRemove)
        m_world.remove<WaitingFor3D>(entity);
}

void CharacterService::UpdateRemoteVRPoseComponents(const UpdateEvent& acUpdateEvent) noexcept
{
#if TP_SKYRIM_VR
    static double s_remoteAvatarStatusWriteTimer = 0.0;
    s_remoteAvatarStatusWriteTimer += acUpdateEvent.Delta;
    RemoteAvatarStatusSnapshot avatarStatus{};

    const auto& poseService = m_world.ctx().at<VRPoseService>();
    const auto& remotePoses = poseService.GetRemotePoses();

    auto remotePlayers = m_world.view<RemoteComponent, PlayerComponent>();
    for (auto entity : remotePlayers)
    {
        ++avatarStatus.RemotePlayerCount;
        const auto& player = remotePlayers.get<PlayerComponent>(entity);
        const auto poseIt = remotePoses.find(player.Id);
        if (poseIt == remotePoses.end())
            continue;

        ++avatarStatus.RemotePoseMatchCount;
        m_world.emplace_or_replace<RemoteVRPoseComponent>(entity, player.Id, poseIt->second);
        ++avatarStatus.ComponentUpsertCount;
        avatarStatus.LastPlayerId = player.Id;
        avatarStatus.LastPoseSequence = poseIt->second.Sequence;
        AccumulatePoseContext(avatarStatus, poseIt->second);

#if TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS
        ++avatarStatus.ActorTargetAttemptCount;

#if TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE
        const auto& movementService = m_world.ctx().at<VRMovementService>();
        auto applyResult = CheckRemoteAvatarSpace(movementService, player.Id);
        if (!applyResult.LocalMovementAvailable)
            ++avatarStatus.ActorTargetSkippedNoLocalMovementCount;
        if (!applyResult.RemoteMovementAvailable)
            ++avatarStatus.ActorTargetSkippedNoRemoteMovementCount;
        if (applyResult.SameCellAvailable && !applyResult.SameCell)
            ++avatarStatus.ActorTargetSkippedDifferentCellCount;
        if (applyResult.SameWorldSpaceAvailable && !applyResult.SameWorldSpace)
            ++avatarStatus.ActorTargetSkippedDifferentWorldSpaceCount;

        avatarStatus.LastApply = applyResult;
        AccumulateSpaceResult(avatarStatus, applyResult);
        if (!applyResult.SameSpaceForApply)
            continue;
#else
        RemoteAvatarApplyResult applyResult{};
        ++avatarStatus.ActorTargetSkippedNoRemoteMovementCount;
        avatarStatus.LastApply = applyResult;
        continue;
#endif

        auto* pFormId = m_world.try_get<FormIdComponent>(entity);
        if (!pFormId)
        {
            ++avatarStatus.MissingFormIdCount;
            continue;
        }

        avatarStatus.LastFormId = pFormId->Id;

        auto* pActor = Cast<Actor>(TESForm::GetById(pFormId->Id));
        if (!pActor)
        {
            ++avatarStatus.MissingActorCount;
            continue;
        }

#if TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SKELETON_WRITES
        ApplyRemoteAvatarPoseToActor(pActor, poseIt->second, applyResult);
#else
        applyResult.ActorAvailable = true;
        applyResult.RootAvailable = pActor->GetNiNode() != nullptr;
        ValidateRemoteAvatarPoseForStatus(poseIt->second, applyResult);
#endif
        avatarStatus.LastApply = applyResult;
        AccumulateApplyResult(avatarStatus, applyResult);
#endif
    }

    std::vector<entt::entity> stalePoseEntities;
    auto poseView = m_world.view<RemoteVRPoseComponent>();
    for (auto entity : poseView)
    {
        auto& poseComponent = poseView.get<RemoteVRPoseComponent>(entity);
        if (remotePoses.find(poseComponent.PlayerId) != remotePoses.end())
            continue;

        stalePoseEntities.push_back(entity);
    }

    for (auto entity : stalePoseEntities)
    {
        m_world.remove<RemoteVRPoseComponent>(entity);
        ++avatarStatus.StalePoseRemovedCount;
    }

#if TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC && TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE
    const auto& inventoryService = m_world.ctx().at<VRInventoryService>();
    const auto& remoteEquipment = inventoryService.GetRemoteEquipment();

    for (auto entity : remotePlayers)
    {
        const auto& player = remotePlayers.get<PlayerComponent>(entity);
        const auto equipmentIt = remoteEquipment.find(player.Id);
        if (equipmentIt == remoteEquipment.end())
            continue;

        const auto* pPreviousEquipment = m_world.try_get<RemoteVREquipmentComponent>(entity);
        const bool desiredWeaponDrawn = DesiredRemoteWeaponDrawn(equipmentIt->second);
        const bool previousDesiredWeaponDrawn = pPreviousEquipment ? DesiredRemoteWeaponDrawn(pPreviousEquipment->Equipment) : false;
        const bool shouldQueueWeaponDraw =
            !pPreviousEquipment ||
            pPreviousEquipment->Equipment.Sequence != equipmentIt->second.Sequence ||
            previousDesiredWeaponDrawn != desiredWeaponDrawn;

        ++avatarStatus.RemoteEquipmentMatchCount;
        m_world.emplace_or_replace<RemoteVREquipmentComponent>(entity, player.Id, equipmentIt->second);
        ++avatarStatus.EquipmentComponentUpsertCount;
        avatarStatus.LastEquipmentPlayerId = player.Id;
        avatarStatus.LastEquipmentSequence = equipmentIt->second.Sequence;
        avatarStatus.LastEquipmentWeaponDrawn = equipmentIt->second.WeaponDrawn;
        avatarStatus.LastEquipmentWeaponFullyDrawn = equipmentIt->second.WeaponFullyDrawn;
        avatarStatus.LastEquipmentDesiredWeaponDrawn = desiredWeaponDrawn;

        const auto* pFormId = m_world.try_get<FormIdComponent>(entity);
        if (!pFormId || pFormId->Id == 0)
        {
            ++avatarStatus.EquipmentMissingFormIdCount;
            continue;
        }

        avatarStatus.LastEquipmentFormId = pFormId->Id;

        if (!shouldQueueWeaponDraw)
            continue;

        if (!Cast<Actor>(TESForm::GetById(pFormId->Id)))
            ++avatarStatus.EquipmentMissingActorCount;

        const auto drawIt = m_weaponDrawUpdates.find(pFormId->Id);
        if (drawIt == m_weaponDrawUpdates.end() || drawIt->second.m_drawWeapon != desiredWeaponDrawn)
        {
            m_weaponDrawUpdates[pFormId->Id] = {desiredWeaponDrawn};
            ++avatarStatus.EquipmentWeaponDrawQueuedCount;
            avatarStatus.LastEquipmentWeaponDrawQueued = true;
        }
    }

    std::vector<entt::entity> staleEquipmentEntities;
    auto equipmentView = m_world.view<RemoteVREquipmentComponent>();
    for (auto entity : equipmentView)
    {
        auto& equipmentComponent = equipmentView.get<RemoteVREquipmentComponent>(entity);
        if (remoteEquipment.find(equipmentComponent.PlayerId) != remoteEquipment.end())
            continue;

        staleEquipmentEntities.push_back(entity);
    }

    for (auto entity : staleEquipmentEntities)
    {
        m_world.remove<RemoteVREquipmentComponent>(entity);
        ++avatarStatus.StaleEquipmentRemovedCount;
    }
#endif

    if (s_remoteAvatarStatusWriteTimer >= 1.0)
    {
        s_remoteAvatarStatusWriteTimer = 0.0;
        WriteRemoteAvatarStatus(avatarStatus);
    }
#else
    TP_UNUSED(acUpdateEvent);
#endif
}

void CharacterService::RunFactionsUpdates() const noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 2000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;

    lastSendTimePoint = now;

    RequestFactionsChanges message;

    auto factionedActors = m_world.view<LocalComponent, CacheComponent, FormIdComponent>();
    for (auto entity : factionedActors)
    {
        auto& formIdComponent = factionedActors.get<FormIdComponent>(entity);
        auto& localComponent = factionedActors.get<LocalComponent>(entity);
        auto& cacheComponent = factionedActors.get<CacheComponent>(entity);

        const auto* pForm = TESForm::GetById(formIdComponent.Id);
        const auto* pActor = Cast<Actor>(pForm);
        if (!pActor)
            continue;

        // Check if cached factions and current factions are identical
        auto factions = pActor->GetFactions();

        if (cacheComponent.FactionsContent == factions)
            continue;

        cacheComponent.FactionsContent = factions;

        // If not send the current factions and replace the cached factions
        message.Changes[localComponent.Id] = factions;
    }

    if (!message.Changes.empty())
        m_transport.Send(message);
}

void CharacterService::RunSpawnUpdates() const noexcept
{
    auto invisibleView = m_world.view<RemoteComponent, InterpolationComponent, RemoteAnimationComponent, WaitingFor3D>(entt::exclude<FormIdComponent>);
    Vector<entt::entity> entities(invisibleView.begin(), invisibleView.end());

    for (const auto entity : entities)
    {
        auto& remoteComponent = m_world.get<RemoteComponent>(entity);
        auto& interpolationComponent = m_world.get<InterpolationComponent>(entity);

        if (const auto pWorldSpace = PlayerCharacter::Get()->GetWorldSpace())
        {
            float characterX = interpolationComponent.Position.x;
            float characterY = interpolationComponent.Position.y;
            const auto characterCoords = GridCellCoords::CalculateGridCellCoords(characterX, characterY);
            const TES* pTES = TES::Get();
            const auto playerCoords = GridCellCoords(pTES->GetCenterGridXData(), pTES->GetCenterGridYData());

            // TODO(cosideci): IsDragon probably shouldn't be straight up false here.
            if (GridCellCoords::IsCellInGridCell(characterCoords, playerCoords, false))
            {
                auto* pActor = Cast<Actor>(TESForm::GetById(remoteComponent.CachedRefId));
                if (!pActor)
                {
                    pActor = CreateCharacterForEntity(entity);
                    if (!pActor)
                        continue;

                    remoteComponent.CachedRefId = pActor->GetFormIdData();
                }

                pActor->MoveTo(PlayerCharacter::Get()->GetParentCellData(), interpolationComponent.Position);
            }
        }
    }
}

void CharacterService::RunExperienceUpdates() noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 1000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;

    lastSendTimePoint = now;

    if (m_cachedExperience == 0.f)
        return;

    if (!World::Get().GetPartyService().IsInParty())
        return;

    SyncExperienceRequest message;
    message.Experience = m_cachedExperience;

    m_cachedExperience = 0.f;

    m_transport.Send(message);

    spdlog::debug("Sending over experience {}", message.Experience);
}

void CharacterService::ApplyCachedWeaponDraws(const UpdateEvent& acUpdateEvent) noexcept
{
    std::vector<uint32_t> toRemove{};

    for (auto& [cId, _] : m_weaponDrawUpdates)
    {
        auto& data = m_weaponDrawUpdates[cId];

        data.m_timer += acUpdateEvent.Delta;

        // We do 2 passes because Skyrim's weapon drawing is the most finnicky thing in existence
        double maxTime = data.m_isFirstPass ? 0.5 : 2.0;
        if (data.m_timer <= maxTime)
            continue;

        Actor* pActor = Cast<Actor>(TESForm::GetById(cId));
        if (!pActor)
            continue;

        pActor->SetWeaponDrawnEx(data.m_drawWeapon);

        if (!data.m_isFirstPass)
            toRemove.push_back(cId);

        data.m_isFirstPass = false;
    }

    for (uint32_t id : toRemove)
        m_weaponDrawUpdates.erase(id);
}
