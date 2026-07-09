#!/usr/bin/env python3
"""Audit the mandatory VRIK IK data lane in the VR pose relay."""

from __future__ import annotations

import pathlib
import sys


REQUIRED_TOKENS = {
    "Code/encoding/Structs/VRPoseUpdate.h": (
        "struct VRFingerCurlData",
        "struct VRVrikData",
        "VRFingerCurlData LeftFingers",
        "VRFingerCurlData RightFingers",
        "glm::vec3 Position",
        "CameraOffsetsValid",
        "FinalSmoothingOffset",
        "VRVrikData Vrik",
    ),
    "Code/encoding/Structs/VRPoseUpdate.cpp": (
        "VRFingerCurlData::Serialize",
        "VRFingerCurlData::Deserialize",
        "VRVrikData::Serialize",
        "VRVrikData::Deserialize",
        "SerializeVector3(aWriter, Position)",
        "DeserializeVector3(aReader, Position)",
        "Vrik.Serialize(aWriter)",
        "Vrik.Deserialize(aReader)",
    ),
    "Code/client/Services/Generic/VRPoseService.cpp": (
        "IsVrikInstalled",
        "IsNewerSequence",
        "vrik.dll",
        "VRIK.dll",
        "SkyrimTogetherVR.vrik",
        "ReadKeyValueFile",
        "bridge.epoch",
        "kVrikBridgeReadInterval",
        "BuildFingerData",
        "cameraOffsetsValid",
        "FinalSmoothingOffset",
        "BuildLocalVrikData",
        "m_localVrik",
        "WriteVrikData",
        "DisconnectedEvent",
        "VRPoseService::OnDisconnected",
        "m_remotePoses.clear()",
        "m_remotePoseAges.clear()",
        "local.vrik",
        "local.spellDestination",
        "local.arrowDestination",
        "local.leftWeaponOffset",
        "local.rightWeaponOffset",
        "local.primaryMagicOffset",
        "local.primaryMagicAim",
        "local.secondaryMagicOffset",
        "local.secondaryMagicAim",
        '".spellDestination"',
        '".arrowDestination"',
        '".leftWeaponOffset"',
        '".rightWeaponOffset"',
        '".primaryMagicOffset"',
        '".secondaryMagicAim"',
        '".vrik"',
    ),
    "Code/client/Components/RemoteVRPoseComponent.h": (
        "struct RemoteVRPoseComponent",
        "uint32_t PlayerId",
        "VRPoseUpdate Pose",
    ),
    "Code/client/Components.h": (
        "RemoteVRPoseComponent.h",
    ),
    "Code/client/Services/Generic/CharacterService.cpp": (
        "Services/VRMovementService.h",
        "UpdateRemoteVRPoseComponents",
        "RemoteVRPoseComponent",
        "PlayerComponent",
        "poseService.GetRemotePoses()",
        "acMovementService.GetRemoteMovements()",
        "TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS",
        "TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SKELETON_WRITES",
        "TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE",
        "CheckRemoteAvatarSpace",
        "SameSpaceForApply",
        "actorTargetSkippedDifferentCellCount",
        "actorTargetSkippedDifferentWorldSpaceCount",
        "actorMovementAuthorityEnabled=0",
        "actorSkeletonWritesEnabled",
        "last.sameSpaceForApply",
        "IsRemotePoseNodeSafe",
        "std::isfinite",
        "InvalidTransformCount",
        "InvalidMovementCount",
        "CopyPoseNodeToSceneNode",
        "ApplyRemoteAvatarPoseToActor",
        "RemoteAvatarStatusSnapshot",
        "WriteRemoteAvatarStatus",
        "SkyrimTogetherVR.avatar",
        "headNodeFoundCount",
        "hmdCopiedCount",
        "leftHandCopiedCount",
        "rightHandCopiedCount",
        "vrikDetectedCount",
        "vrikInterfaceAvailableCount",
        "invalidVrikCount",
        "invalidTransformCount",
        "invalidMovementCount",
        "last.vrikInterfaceAvailable",
        "last.invalidVrikCount",
        "last.invalidTransformCount",
        "last.invalidMovement",
        "remoteEquipmentMatchCount",
        "spellOriginValidCount",
        "arrowOriginValidCount",
        "leftWeaponOffsetValidCount",
        "primaryMagicOffsetValidCount",
        "last.spellOriginValid",
        "lastEquipment.sequence",
        "GetObjectByName",
        "NPC Head [Head]",
        "NPC L Hand [LHnd]",
        "NPC R Hand [RHnd]",
    ),
    "Code/client/Services/CharacterService.h": (
        "UpdateRemoteVRPoseComponents",
        "RunRemoteUpdates(const UpdateEvent&",
    ),
    "Code/client/World.cpp": (
        "TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC",
        "SkyrimTogetherVR remote avatar validation mode",
        "ctx().emplace<VRPoseService>(m_dispatcher, m_transport);",
        "ctx().emplace<CharacterService>",
    ),
    "Code/client/xmake.lua": (
        "SkyrimTogetherVRClientAvatarSync",
        "SkyrimTogetherVRGameplayClient",
        "remote_avatar_sync = true",
        "remote_avatar_actor_targets = true",
        "connection_only = false",
        "TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC=",
        "TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS=",
    ),
    "Code/immersive_launcher/xmake.lua": (
        "SkyrimVRImmersiveLauncherAvatarSync",
        "SkyrimVRImmersiveLauncherGameplay",
        "SkyrimTogetherVRAvatarSync",
        "SkyrimTogetherVRGameplay",
        "TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY=1",
        "TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY=0",
        "TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC=1",
        "TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS=1",
        "SkyrimTogetherVRClientAvatarSync",
        "SkyrimTogetherVRGameplayClient",
        "/WHOLEARCHIVE:SkyrimTogetherVRClientAvatarSync",
        "/WHOLEARCHIVE:SkyrimTogetherVRGameplayClient",
    ),
    "Code/vrik_bridge/main.cpp": (
        "SKSEPlugin_Query",
        "SKSEPlugin_Load",
        "kVrikMessageGetInterface = 0xF2AFAEE6",
        '"VRIK"',
        "SkyrimTogetherVR.vrik",
        "std::atomic<vrikPluginApi::IVrikInterface001*>",
        "g_snapshotLock",
        "CopyLatestSnapshot",
        "bridge.epoch",
        "bridge.loaded=",
        "leftFingers",
        "rightFingers",
        "getCameraOffsettingAmount",
        "getFinalCameraOffsettingAmount",
        "getFinalSmoothingOffsettingAmount",
    ),
    "Code/vrik_bridge/xmake.lua": (
        'target("SkyrimTogetherVRVrikBridge")',
        'set_kind("shared")',
        'set_basename("SkyrimTogetherVRVrikBridge")',
    ),
    "Code/xmake.lua": (
        'includes("vrik_bridge")',
    ),
    "BuildSkyrimTogetherVR-Windows.ps1": (
        "SkyrimTogetherVRVrikBridge",
        "SkyrimTogetherVRAvatarSync",
        "Data\\SKSE\\Plugins",
    ),
    "Code/client/Services/VRPoseService.h": (
        "GetLocalVrikData",
        "VRVrikData m_localVrik",
        "OnDisconnected",
        "m_disconnectedConnection",
    ),
    "Code/server/Services/VRPoseRelayService.h": (
        "PlayerPoseRelayState",
        "LastSequence",
        "LastRelayTick",
        "ShouldRelayPose",
        "OnPlayerLeave",
        "m_playerPoseRelayState",
        "m_playerLeaveConnection",
    ),
    "Code/server/Services/VRPoseRelayService.cpp": (
        "kMinPoseRelayIntervalMs",
        "IsNewerSequence",
        "pose.Sequence == 0",
        "!HasAnyPoseNode(pose)",
        "state.HasSequence",
        "GameServer::Get()->GetTick()",
        "now - state.LastRelayTick < kMinPoseRelayIntervalMs",
        "state.LastSequence = pose.Sequence",
        "m_playerPoseRelayState.erase",
        "GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);",
    ),
    "Code/client/Games/PapyrusFunctions.cpp": (
        "AppendVrikSummary",
        "AppendAvatarSummary",
        "AppendAvatarTelemetry",
        'GetHandoffPath("SkyrimTogetherVR.avatar")',
        "actorTargetsEnabled",
        "last.sameSpaceForApply",
        "actorTargetSkippedDifferentCellCount",
        "hmdCopiedCount",
        "vrikDetectedCount",
        "vrikInterfaceAvailableCount",
        "invalidVrikCount",
        "invalidTransformCount",
        "invalidMovementCount",
        "last.vrikInterfaceAvailable",
        "last.invalidVrikCount",
        "last.invalidTransformCount",
        "last.invalidMovement",
        "remoteEquipmentMatchCount",
        "spellOriginValidCount",
        "last.spellOriginValid",
        "pose.GetLocalVrikData()",
        "acPose.Vrik",
    ),
    "Tools/SkyrimVR/vr_handoff.py": (
        ".vrik.detected",
        ".vrik.interfaceAvailable",
        '"avatar": "SkyrimTogetherVR.avatar"',
        "actorTargetsEnabled",
        "sameSpace=",
        "skipCell=",
        "remoteEquipmentMatchCount",
        "spellOriginValidCount",
        "last.spellOriginValid",
        '"avatar"',
        "vrik=detected",
        "local.vrik.detected=1",
        "local.vrik.interfaceAvailable=0",
        "remote.7.vrik.detected=1",
        "remote.7.vrik.interfaceAvailable=1",
        "targets=yes",
        "sameSpace=yes",
        "invalidTransforms=0",
        "invalidMovement=no",
        "skipCell=0",
        "equipment=1",
        "spell=yes/yes",
    ),
    "Tools/SkyrimVR/audit_runtime_handoff.py": (
        "local.vrik.interfaceAvailable",
        "remote.*.vrik.interfaceAvailable=1",
        "local VRIK API",
        "remote VRIK API",
        "require local VRIK bridge API lane data",
    ),
    "Tools/SkyrimVR/install_vr_prereqs.py": (
        "VRIK Player Avatar",
        "SKSE/Plugins/VRIK.dll",
        "Scripts/VRIK.pex",
        "vrik.esp",
        "--enable-plugins",
    ),
    "Code/tests/encoding.cpp": (
        "pose.Vrik.Detected = true",
        "pose.Vrik.InterfaceAvailable = true",
        "pose.Vrik.LeftFingers.Valid = true",
        "pose.Vrik.RightFingers.Valid = true",
        "pose.Vrik.CameraOffsetsValid = true",
    ),
    "Docs/SkyrimVR/vr-pose-replication.md": (
        "VRIK",
        "finger curl",
        "camera offset",
        "mandatory",
        "VRMovementService",
        "SkyrimTogetherVRVrikBridge",
        "SkyrimTogetherVR.vrik",
        "RemoteVRPoseComponent",
        "RemoteVREquipmentComponent",
        "remote avatar",
        "SkyrimTogetherVR.avatar",
        "same-space guard",
        "actorTargetSkippedDifferentCellCount",
        "non-increasing sequence",
        "20 Hz",
    ),
    "Docs/SkyrimVR/porting-status.md": (
        "VRIK",
        "mandatory",
        "audit_vrik_ik.py",
        "SkyrimTogetherVRVrikBridge",
        "Data\\SKSE\\Plugins",
        "RemoteVRPoseComponent",
    ),
}

FORBIDDEN_TOKENS = {
    "Docs/SkyrimVR/vr-pose-replication.md": (
        "future proxy-marker work",
    ),
}


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def read_text(root: pathlib.Path, relative_path: str) -> str:
    return (root / relative_path).read_text(encoding="utf-8", errors="replace")


def main() -> int:
    root = repo_root()
    failures: list[str] = []

    for relative_path, tokens in REQUIRED_TOKENS.items():
        text = read_text(root, relative_path)
        missing = [token for token in tokens if token not in text]
        if missing:
            failures.append(f"{relative_path}: missing VRIK IK tokens: {', '.join(missing)}")

    for relative_path, tokens in FORBIDDEN_TOKENS.items():
        text = read_text(root, relative_path)
        present = [token for token in tokens if token in text]
        if present:
            failures.append(f"{relative_path}: stale non-mandatory wording present: {', '.join(present)}")

    print(f"Audited VRIK IK files: {len(set(REQUIRED_TOKENS) | set(FORBIDDEN_TOKENS))}")
    print(f"VRIK IK audit failures: {len(failures)}")
    for failure in failures:
        print(f"- {failure}")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
