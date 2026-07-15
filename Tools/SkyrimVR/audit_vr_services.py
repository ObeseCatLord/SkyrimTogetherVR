#!/usr/bin/env python3
import argparse
import pathlib


REQUIRED_WORLD_TOKENS = (
    "#include <Services/VRInventoryService.h>",
    "TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE",
    "ctx().emplace<VRInventoryService>(*this, m_dispatcher, m_transport);",
    "#ifndef TP_SKYRIM_VR_ENABLE_POSE_SERVICE",
    "#if TP_SKYRIM_VR_ENABLE_POSE_SERVICE",
)

REQUIRED_MOVEMENT_WORLD_TOKENS = (
    "#include <Services/VRMovementService.h>",
    "TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE",
    "ctx().emplace<VRMovementService>(*this, m_dispatcher, m_transport);",
)

REQUIRED_ACTIVATION_WORLD_TOKENS = (
    "#include <Services/VRActivationService.h>",
    "TP_SKYRIM_VR_ENABLE_ACTIVATION_OBSERVATION_SERVICE",
    "ctx().emplace<VRActivationService>(*this, m_dispatcher, m_transport);",
)

REQUIRED_MAGIC_WORLD_TOKENS = (
    "#include <Services/VRMagicService.h>",
    "TP_SKYRIM_VR_ENABLE_MAGIC_OBSERVATION_SERVICE",
    "ctx().emplace<VRMagicService>(*this, m_dispatcher, m_transport);",
)

REQUIRED_COMBAT_WORLD_TOKENS = (
    "#include <Services/VRCombatService.h>",
    "TP_SKYRIM_VR_ENABLE_COMBAT_OBSERVATION_SERVICE",
    "ctx().emplace<VRCombatService>(*this, m_dispatcher, m_transport);",
)

REQUIRED_PROJECTILE_WORLD_TOKENS = (
    "#include <Services/VRProjectileService.h>",
    "TP_SKYRIM_VR_ENABLE_PROJECTILE_OBSERVATION_SERVICE",
    "ctx().emplace<VRProjectileService>(*this, m_dispatcher, m_transport);",
)

REQUIRED_GRAB_WORLD_TOKENS = (
    "#include <Services/VRGrabService.h>",
    "TP_SKYRIM_VR_ENABLE_GRAB_OBSERVATION_SERVICE",
    "ctx().emplace<VRGrabService>(*this, m_dispatcher, m_transport);",
)

REQUIRED_SAVELOAD_WORLD_TOKENS = (
    "#include <Services/VRSaveLoadService.h>",
    "TP_SKYRIM_VR_ENABLE_SAVELOAD_OBSERVATION_SERVICE",
    "ctx().emplace<VRSaveLoadService>(*this, m_dispatcher, m_transport);",
)

REQUIRED_XMAKE_TOKENS = (
    "local observation_services = options.observation_services or false",
    "local pose_service = options.pose_service or false",
    "local remote_player_proxy = options.remote_player_proxy or false",
    'add_defines("TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE=" .. vr_define_value(observation_services))',
    'add_defines("TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE=" .. vr_define_value(observation_services))',
    'add_defines("TP_SKYRIM_VR_ENABLE_POSE_SERVICE=" .. vr_define_value(pose_service))',
    'add_defines("TP_SKYRIM_VR_ENABLE_BODY_POSE_CAPTURE=" .. vr_define_value(pose_service))',
    'add_defines("TP_SKYRIM_VR_ENABLE_REMOTE_PLAYER_PROXY_SERVICE=" .. vr_define_value(remote_player_proxy))',
    "observation_services = true",
    "pose_service = true",
    "remote_player_proxy = true",
)

REQUIRED_MOVEMENT_XMAKE_TOKENS = (
    'add_defines("TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE=" .. vr_define_value(observation_services))',
)

REQUIRED_ACTIVATION_XMAKE_TOKENS = (
    'add_defines("TP_SKYRIM_VR_ENABLE_ACTIVATION_OBSERVATION_SERVICE=" .. vr_define_value(observation_services))',
)

REQUIRED_MAGIC_XMAKE_TOKENS = (
    'add_defines("TP_SKYRIM_VR_ENABLE_MAGIC_OBSERVATION_SERVICE=" .. vr_define_value(observation_services))',
)

REQUIRED_COMBAT_XMAKE_TOKENS = (
    'add_defines("TP_SKYRIM_VR_ENABLE_COMBAT_OBSERVATION_SERVICE=" .. vr_define_value(observation_services))',
)

REQUIRED_PROJECTILE_XMAKE_TOKENS = (
    'add_defines("TP_SKYRIM_VR_ENABLE_PROJECTILE_OBSERVATION_SERVICE=" .. vr_define_value(observation_services))',
)

REQUIRED_GRAB_XMAKE_TOKENS = (
    'add_defines("TP_SKYRIM_VR_ENABLE_GRAB_OBSERVATION_SERVICE=" .. vr_define_value(observation_services))',
)

REQUIRED_SAVELOAD_XMAKE_TOKENS = (
    'add_defines("TP_SKYRIM_VR_ENABLE_SAVELOAD_OBSERVATION_SERVICE=" .. vr_define_value(observation_services))',
)

REQUIRED_MOVEMENT_OBSERVER_TOKENS = (
    "SkyrimTogetherVR.movement",
    "IsNewerSequence",
    "RequestVRMovementUpdate",
    "NotifyVRMovementUpdate",
    "m_lastMovement.Sequence = ++m_sequence;",
    "localMovement",
    "remoteMovement",
    "Position",
    "Rotation",
    "Direction",
    "GetCellId",
    "GetWorldSpace",
)

FORBIDDEN_MOVEMENT_OBSERVER_TOKENS = (
    "ClientReferencesMoveRequest",
    "ServerReferencesMoveRequest",
    "MovementComponent",
    "LocalComponent",
    "RemoteComponent",
    "OwnerComponent",
    "MoveTo(",
    "SetPosition(",
)

REQUIRED_INVENTORY_OBSERVER_TOKENS = (
    "SkyrimTogetherVR.inventory",
    "policy=equipment_snapshot_only",
    "fullInventoryTraversal=0",
    "inventoryMutation=0",
    "remoteEquipmentMutation=0",
    "normalInventoryPackets=0",
    "IsNewerSequence",
    "RequestVREquipmentUpdate",
    "NotifyVREquipmentUpdate",
    "m_lastEquipment.Sequence = ++m_sequence;",
    "weaponDrawn",
    "weaponFullyDrawn",
    "leftWeapon",
    "rightWeapon",
    "leftSpell",
    "rightSpell",
    "powerOrShout",
    "localEquipment",
    "remoteEquipment",
    "GetEquippedWeapon",
    "GetEquippedAmmo",
)

FORBIDDEN_INVENTORY_OBSERVER_TOKENS = (
    "EquipManager",
    "RequestInventoryChanges",
    "RequestEquipmentChanges",
    "NotifyInventoryChanges",
    "NotifyEquipmentChanges",
    "ScopedInventoryOverride",
    "ScopedEquipOverride",
    "SetInventory",
    "AddOrRemoveItem",
    "GetActorInventory",
    "GetEquipment",
    "GetInventory(",
)

REQUIRED_ACTIVATION_OBSERVER_TOKENS = (
    "SkyrimTogetherVR.activation",
    "RequestVRActivationEvent",
    "NotifyVRActivationEvent",
    "localActivation",
    "remoteActivation",
    "TESActivateEvent",
    "acEvent.GetActionRefData() != pPlayer",
    "ObjectId",
    "CellId",
    "WorldSpaceId",
    "Position",
    "FormType",
    "OpenState",
    "GetOpenState",
    "GetActivateEventData().RegisterSink",
)

FORBIDDEN_ACTIVATION_OBSERVER_TOKENS = (
    "ActivateRequest",
    "NotifyActivate",
    "AssignObjectsRequest",
    "AssignObjectsResponse",
    "LockChangeRequest",
    "NotifyLockChange",
    "ObjectComponent",
    "FormIdComponent",
    "CreateObjectEntity",
    "Utils::GetByServerId",
    "pObject->Activate",
    "LockChange(",
    "SetInventory",
    "EquipManager",
    "HookActivate",
    "HookPickUpObject",
    "HookDropObject",
)

REQUIRED_MAGIC_OBSERVER_TOKENS = (
    "SkyrimTogetherVR.magic",
    "RequestVRMagicEffectEvent",
    "NotifyVRMagicEffectEvent",
    "localMagicEffect",
    "remoteMagicEffect",
    "TESMagicEffectApplyEvent",
    "EffectId",
    "CasterId",
    "TargetId",
    "CasterIsPlayer",
    "TargetIsPlayer",
    "GetMagicEffectApplyEventData().RegisterSink",
)

FORBIDDEN_MAGIC_OBSERVER_TOKENS = (
    "SpellCastRequest",
    "NotifySpellCast",
    "InterruptCastRequest",
    "NotifyInterruptCast",
    "AddTargetRequest",
    "NotifyAddTarget",
    "RemoveSpellRequest",
    "NotifyRemoveSpell",
    "MagicCaster",
    "MagicTarget",
    "CastSpellImmediate",
    "AddTarget(",
    "DispelAllSpells",
    "ScopedSpellCastOverride",
    "EquipManager",
    "HookSpellCast",
    "HookAddTarget",
)

REQUIRED_COMBAT_OBSERVER_TOKENS = (
    "SkyrimTogetherVR.combat",
    "RequestVRCombatHitEvent",
    "NotifyVRCombatHitEvent",
    "localCombatHit",
    "remoteCombatHit",
    "TESHitEvent",
    "HitterId",
    "HitteeId",
    "HitterIsPlayer",
    "HitteeIsPlayer",
    "GetHitEventData().RegisterSink",
)

FORBIDDEN_COMBAT_OBSERVER_TOKENS = (
    "ProjectileLaunchRequest",
    "NotifyProjectileLaunch",
    "ProjectileLaunchedEvent",
    "Projectile::Launch",
    "CombatComponent",
    "LocalComponent",
    "RemoteComponent",
    "ObjectComponent",
    "SetCombatTarget",
    "SetCombatTargetEx",
    "RunTargetUpdates",
    "OnProjectile",
    "#include <Events/HitEvent.h>",
    "HookLaunch",
)

REQUIRED_PROJECTILE_OBSERVER_TOKENS = (
    "SkyrimTogetherVR.projectile",
    "RequestVRProjectileEvent",
    "NotifyVRProjectileEvent",
    "localProjectileEvent",
    "remoteProjectileEvent",
    "TESPlayerBowShotEvent",
    "TESSpellCastEvent",
    "GetPlayerBowShotEventData().RegisterSink",
    "GetSpellCastEventData().RegisterSink",
    "ArrowOrigin",
    "ArrowDestination",
    "SpellOrigin",
    "SpellDestination",
)

FORBIDDEN_PROJECTILE_OBSERVER_TOKENS = (
    "ProjectileLaunchRequest",
    "NotifyProjectileLaunch",
    "ProjectileLaunchedEvent",
    "Projectile::Launch",
    "LaunchData",
    "HookLaunch",
    "#include <Projectiles/Projectile.h>",
    "CombatService",
    "CombatComponent",
    "LocalComponent",
    "RemoteComponent",
    "ObjectComponent",
    "FormIdComponent",
    "SetCombatTarget",
    "SetCombatTargetEx",
)

REQUIRED_SAVELOAD_OBSERVER_TOKENS = (
    "SkyrimTogetherVR.saveload",
    "TESLoadGameEvent",
    "GetLoadGameEventData().RegisterSink",
    "GetModSystem().GetServerModId",
    "loadGameObserved",
    "loadGameCount",
    "readyAfterLastLoad",
    "waitingForReadyAfterLoad",
    "connectionState",
    "player.serverModId",
    "playerCell.serverModId",
    "playerWorldSpace.serverModId",
    "IsVrPlayerReadyForSaveLoad",
    "SkyrimTogetherVR::TryGetReadablePlayerForVR",
)

FORBIDDEN_SAVELOAD_OBSERVER_TOKENS = (
    "BGSSaveLoadManager",
    ".Save(",
    ".Load(",
    "QueueDisconnect",
    "QueueConnect",
    "RequestDisconnect",
    "RequestConnect",
    "GetTransport().Close",
    "GetTransport().Connect",
    "CharacterService",
    "ObjectService",
    "CombatService",
    "MagicService",
    "InventoryService",
    "ProjectileLaunchRequest",
)

REQUIRED_MOVEMENT_RELAY_TOKENS = {
    "Code/encoding/Opcodes.h": (
        "kRequestVRMovementUpdate",
        "kNotifyVRMovementUpdate",
    ),
    "Code/encoding/Messages/ClientMessageFactory.h": (
        "#include <Messages/RequestVRMovementUpdate.h>",
        "RequestVRMovementUpdate",
    ),
    "Code/encoding/Messages/ServerMessageFactory.h": (
        "#include <Messages/NotifyVRMovementUpdate.h>",
        "NotifyVRMovementUpdate",
    ),
    "Code/encoding/Structs/VRMovementUpdate.h": (
        "struct VRMovementUpdate",
        "CellId",
        "WorldSpaceId",
        "Position",
        "Rotation",
        "Direction",
    ),
    "Code/encoding/Messages/RequestVRMovementUpdate.h": (
        "struct RequestVRMovementUpdate",
        "kRequestVRMovementUpdate",
        "VRMovementUpdate Movement",
    ),
    "Code/encoding/Messages/NotifyVRMovementUpdate.h": (
        "struct NotifyVRMovementUpdate",
        "kNotifyVRMovementUpdate",
        "uint32_t PlayerId",
        "VRMovementUpdate Movement",
    ),
    "Code/server/Services/VRMovementRelayService.cpp": (
        "PacketEvent<RequestVRMovementUpdate>",
        "NotifyVRMovementUpdate",
        "kMinMovementRelayIntervalMs",
        "IsNewerSequence",
        "OnPlayerLeave",
        "ShouldRelayMovement",
        "movement.Sequence == 0",
        "state.HasSequence",
        "GameServer::Get()->GetTick()",
        "now - state.LastRelayTick < kMinMovementRelayIntervalMs",
        "state.LastSequence = movement.Sequence",
        "m_playerMovementRelayState.erase",
        "notify.PlayerId = playerId;",
        "GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);",
    ),
    "Code/server/Services/VRMovementRelayService.h": (
        "PlayerMovementRelayState",
        "LastSequence",
        "LastRelayTick",
        "HasSequence",
        "OnPlayerLeave",
        "ShouldRelayMovement",
        "m_playerMovementRelayState",
        "m_playerLeaveConnection",
    ),
    "Code/server/World.cpp": (
        "#include <Services/VRMovementRelayService.h>",
        "ctx().emplace<VRMovementRelayService>(*this, m_dispatcher);",
    ),
    "Code/tests/encoding.cpp": (
        "BuildMovementUpdate",
        "RequestVRMovementUpdate",
        "NotifyVRMovementUpdate",
        'GIVEN("VRMovementUpdate")',
    ),
}

REQUIRED_POSE_RELAY_TOKENS = {
    "Code/encoding/Opcodes.h": (
        "kRequestVRPoseUpdate",
        "kNotifyVRPoseUpdate",
    ),
    "Code/encoding/Messages/ClientMessageFactory.h": (
        "#include <Messages/RequestVRPoseUpdate.h>",
        "RequestVRPoseUpdate",
    ),
    "Code/encoding/Messages/ServerMessageFactory.h": (
        "#include <Messages/NotifyVRPoseUpdate.h>",
        "NotifyVRPoseUpdate",
    ),
    "Code/encoding/Structs/VRPoseUpdate.h": (
        "struct VRPoseUpdate",
        "VRPoseNodeData",
        "VRVrikData",
        "VRBodyPoseData",
        "IsVRBodyPoseDataSafe",
        "IsVRPoseUpdateSafe",
        "HasAnyVRPosePayload",
    ),
    "Code/encoding/Messages/RequestVRPoseUpdate.h": (
        "struct RequestVRPoseUpdate",
        "kRequestVRPoseUpdate",
        "VRPoseUpdate Pose",
    ),
    "Code/encoding/Messages/NotifyVRPoseUpdate.h": (
        "struct NotifyVRPoseUpdate",
        "kNotifyVRPoseUpdate",
        "uint32_t PlayerId",
        "VRPoseUpdate Pose",
    ),
    "Code/server/Services/VRPoseRelayService.cpp": (
        "PacketEvent<RequestVRPoseUpdate>",
        "NotifyVRPoseUpdate",
        "kMinPoseRelayIntervalMs",
        "IsNewerSequence",
        "OnPlayerLeave",
        "ShouldRelayPose",
        "pose.Sequence == 0",
        "HasAnyVRPosePayload(pose)",
        "IsVRPoseUpdateSafe(pose)",
        "state.HasSequence",
        "GameServer::Get()->GetTick()",
        "now - state.LastRelayTick < kMinPoseRelayIntervalMs",
        "m_playerPoseRelayState[aPlayerId]",
        "m_playerPoseRelayState.erase",
    ),
    "Code/server/Services/VRPoseRelayService.h": (
        "PlayerPoseRelayState",
        "LastSequence",
        "LastRelayTick",
        "HasSequence",
        "OnPlayerLeave",
        "ShouldRelayPose",
        "m_playerPoseRelayState",
        "m_playerLeaveConnection",
    ),
    "Code/server/World.cpp": (
        "#include <Services/VRPoseRelayService.h>",
        "ctx().emplace<VRPoseRelayService>(*this, m_dispatcher);",
    ),
    "Code/tests/encoding.cpp": (
        "BuildPoseUpdate",
        "RequestVRPoseUpdate",
        "NotifyVRPoseUpdate",
        'GIVEN("VRPoseUpdate")',
        'GIVEN("VRPoseUpdate validation")',
        "IsVRBodyPoseDataSafe",
        "IsVRPoseUpdateSafe",
    ),
}

REQUIRED_EQUIPMENT_RELAY_TOKENS = {
    "Code/encoding/Opcodes.h": (
        "kRequestVREquipmentUpdate",
        "kNotifyVREquipmentUpdate",
    ),
    "Code/encoding/Messages/ClientMessageFactory.h": (
        "#include <Messages/RequestVREquipmentUpdate.h>",
        "RequestVREquipmentUpdate",
    ),
    "Code/encoding/Messages/ServerMessageFactory.h": (
        "#include <Messages/NotifyVREquipmentUpdate.h>",
        "NotifyVREquipmentUpdate",
    ),
    "Code/encoding/Structs/VREquipmentUpdate.h": (
        "struct VREquipmentUpdate",
        "WeaponDrawn",
        "WeaponFullyDrawn",
        "LeftWeapon",
        "RightWeapon",
        "PowerOrShout",
    ),
    "Code/encoding/Messages/RequestVREquipmentUpdate.h": (
        "struct RequestVREquipmentUpdate",
        "kRequestVREquipmentUpdate",
        "VREquipmentUpdate Equipment",
    ),
    "Code/encoding/Messages/NotifyVREquipmentUpdate.h": (
        "struct NotifyVREquipmentUpdate",
        "kNotifyVREquipmentUpdate",
        "uint32_t PlayerId",
        "VREquipmentUpdate Equipment",
    ),
    "Code/server/Services/VREquipmentRelayService.cpp": (
        "PacketEvent<RequestVREquipmentUpdate>",
        "NotifyVREquipmentUpdate",
        "kMinEquipmentRelayIntervalMs",
        "IsNewerSequence",
        "OnPlayerLeave",
        "ShouldRelayEquipment",
        "equipment.Sequence == 0",
        "state.HasSequence",
        "GameServer::Get()->GetTick()",
        "now - state.LastRelayTick < kMinEquipmentRelayIntervalMs",
        "state.LastSequence = equipment.Sequence",
        "m_playerEquipmentRelayState.erase",
        "notify.PlayerId = playerId;",
        "GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);",
    ),
    "Code/server/Services/VREquipmentRelayService.h": (
        "PlayerEquipmentRelayState",
        "LastSequence",
        "LastRelayTick",
        "HasSequence",
        "OnPlayerLeave",
        "ShouldRelayEquipment",
        "m_playerEquipmentRelayState",
        "m_playerLeaveConnection",
    ),
    "Code/server/World.cpp": (
        "#include <Services/VREquipmentRelayService.h>",
        "ctx().emplace<VREquipmentRelayService>(*this, m_dispatcher);",
    ),
    "Code/tests/encoding.cpp": (
        "BuildEquipmentUpdate",
        "RequestVREquipmentUpdate",
        "NotifyVREquipmentUpdate",
        'GIVEN("VREquipmentUpdate")',
    ),
}

REQUIRED_REMOTE_AVATAR_COMPONENT_TOKENS = {
    "Code/client/Services/VRAvatarService.h": (
        "Canonical VR avatar client path backed exclusively by GameplayBridge",
        "GameplayBridge::AdapterHandle",
        "RemoteAvatar",
    ),
    "Code/client/Services/Generic/VRAvatarService.cpp": (
        "AssignCharacterRequest",
        "CharacterSpawnRequest",
        "ServerReferencesMoveRequest",
        "NotifyRemoveCharacter",
        "GameplayBridge::CommandKind::CreateRemoteAvatar",
        "GameplayBridge::CommandKind::UpdateRemoteRootTransform",
        "GameplayBridge::CommandKind::DestroyRemoteAvatar",
        "return TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS != 0 &&",
        "schema=commonlib_bridge_v1",
        "actorSkeletonWritesEnabled=0",
        "remoteMovementAcceptedCount",
        "sameSpaceCount",
        "invalidTransformCount",
    ),
    "Code/client/VRGameplayBridge.cpp": (
        "TrySubmitCommand",
        "TryConsumeEvent",
        "GetDiagnostics",
        "GetActiveCapabilities",
    ),
    "Code/vr_gameplay_bridge/AvatarManager.cpp": (
        "RE::ActorHandle",
        "CreateRemoteAvatar",
        "UpdateRemoteRootTransform",
        "DestroyRemoteAvatar",
        "PlaceObjectAtMe",
        "SetPosition",
        "SetAngle",
        "SetScale",
    ),
    "Code/client/World.cpp": (
        "TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC",
        "ctx().emplace<VRPoseService>(m_dispatcher, m_transport);",
        "ctx().emplace<PartyService>(*this, m_dispatcher, m_transport);",
        "ctx().emplace<VRAvatarService>(*this, m_dispatcher, m_transport);",
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
}

FORBIDDEN_REMOTE_AVATAR_COMPONENT_TOKENS = {
    "Code/client/Services/Generic/VRAvatarService.cpp": (
        "RE::Actor",
        "RE::ActorHandle",
        "PlaceObjectAtMe",
        "SetPosition",
        "SetAngle",
        "SetScale",
        "GetObjectByName",
    ),
    "Code/vr_gameplay_bridge/AvatarManager.cpp": (
        "GetObjectByName",
        "NPC Head [Head]",
        "NPC L Hand [LHnd]",
        "NPC R Hand [RHnd]",
    ),
}

REQUIRED_REMOTE_PLAYER_PROXY_TOKENS = {
    "Code/client/Services/VRRemotePlayerService.h": (
        "struct VRRemotePlayerInfo",
        "struct VRRemotePlayerService",
        "GetRemotePlayerCount",
        "GetRemotePlayers",
        "NotifyPlayerJoined",
        "NotifyPlayerList",
        "NotifyPlayerCellChanged",
        "NotifyPlayerLeft",
    ),
    "Code/client/Services/Generic/VRRemotePlayerService.cpp": (
        "SkyrimTogetherVR.remoteplayers",
        "VRPoseService",
        "VRMovementService",
        "VRInventoryService",
        "VRActivationService",
        "VRMagicService",
        "VRCombatService",
        "VRProjectileService",
        "VRGrabService",
        "VRHiggsService",
        "OnPlayerJoined",
        "OnPlayerList",
        "OnPlayerCellChanged",
        "OnPlayerLeft",
        "WriteRemotePlayerStatusFile",
        "trackedPlayerCount",
        "remoteActivationCount",
        "remoteMagicEffectCount",
        "remoteCombatHitCount",
        "remoteProjectileEventCount",
        "remoteGrabCount",
        "poseMatchedCount",
        "movementMatchedCount",
        "equipmentMatchedCount",
        "activationMatchedCount",
        "magicMatchedCount",
        "combatMatchedCount",
        "projectileMatchedCount",
        "grabMatchedCount",
        "vrikMatchedCount",
        "sameCellCount",
        "sameWorldSpaceCount",
        "sameSpaceCount",
        "avatarValidationReadyCount",
        "higgsAvatarValidationReadyCount",
        "poseAvailable",
        "vrikDetected",
        "vrikInterfaceAvailable",
        "vrikAvailable",
        "movementAvailable",
        "equipmentAvailable",
        "activationAvailable",
        "magicAvailable",
        "combatAvailable",
        "projectileAvailable",
        "grabAvailable",
        "higgsAvailable",
        "avatarValidationReady",
        "avatarValidationBlocker",
        "higgsAvatarValidationReady",
        "higgsAvatarValidationBlocker",
    ),
    "Code/client/World.cpp": (
        "Services/VRRemotePlayerService.h",
        "TP_SKYRIM_VR_ENABLE_REMOTE_PLAYER_PROXY_SERVICE",
        "remote-player proxy service is enabled in readout-only mode",
        "ctx().emplace<VRRemotePlayerService>",
    ),
    "Code/client/xmake.lua": (
        'add_defines("TP_SKYRIM_VR_ENABLE_REMOTE_PLAYER_PROXY_SERVICE=" .. vr_define_value(remote_player_proxy))',
    ),
    "Code/client/Games/PapyrusFunctions.cpp": (
        "SkyrimTogetherVR.remoteplayers",
        "SkyrimTogetherVR.discovery",
        "AppendRemotePlayersSummary",
        "AppendRemotePlayersTelemetry",
        "AppendDiscoverySummary",
        "AppendDiscoveryTelemetry",
        "poseMatchedCount",
        "actorCount",
        "actorLimit",
        "currentGrid",
        "cachedWorldSpaceFormId",
        "actor.0.formId",
        "activationMatchedCount",
        "magicMatchedCount",
        "combatMatchedCount",
        "projectileMatchedCount",
        "grabMatchedCount",
        '" action="',
        '"\\naction activation="',
        "sameCellCount",
        "avatarValidationReadyCount",
        "higgsAvatarValidationReadyCount",
    ),
    "Tools/SkyrimVR/vr_handoff.py": (
        "SkyrimTogetherVR.remoteplayers",
        '"remoteplayers"',
        '"proxy"',
        "remotePlayer.7.username=Remote Tester",
        "activation=yes",
        "magic=yes",
        "combat=yes",
        "projectile=yes",
        "grab=yes",
        "sameCell=yes",
        "avatarReady=yes",
        "higgsAvatarReady=yes",
    ),
}

FORBIDDEN_REMOTE_PLAYER_PROXY_TOKENS = {
    "Code/client/Services/Generic/VRRemotePlayerService.cpp": (
        "MoveTo",
        "ForcePosition",
        "SetRotationData",
        "TESObjectREFR",
        "EquipManager",
        "AddOrRemoveItem",
        "m_world.emplace_or_replace<RemoteVRPoseComponent>",
        "m_world.emplace_or_replace<RemoteVREquipmentComponent>",
        "ApplyRemoteAvatar",
    ),
}

REQUIRED_PLAYER_CELL_HANDOFF_TOKENS = {
    "Code/client/Services/Generic/PlayerService.cpp": (
        "SkyrimTogetherVR.playercell",
        "PlayerService network-only mode: sending cell/grid changes; level reads are disabled",
        "constexpr uint16_t kVrFallbackLevel = 1;",
        "TP_UNUSED(acDeltaTime);",
        'file << "currentLevel=" << m_cachedVrLevel',
        "WriteVrPlayerCellStatusFile",
        "RunVrPlayerCellStatusWrite",
        "gridCellRequestCount",
        "exteriorCellRequestCount",
        "interiorCellRequestCount",
        "levelRequestCount",
        "offlineSkippedRequestCount",
        "worldSpaceTranslationFailureCount",
        "lastGrid.valid",
        "lastGrid.worldSpace",
        "lastGrid.playerCell",
        "lastGrid.center",
        "lastGrid.cellCount",
        "lastGrid.connectionGeneration",
        "lastCell.valid",
        "lastCell.exterior",
        "lastCell.connectionGeneration",
        "lastCell.currentCoords",
        "const bool sent = m_transport.Send(request);",
        "const bool sent = m_transport.Send(message);",
        "m_lastVrGridConnectionGeneration = m_transport.GetConnectionGeneration();",
        "m_lastVrCellConnectionGeneration = m_transport.GetConnectionGeneration();",
        "PlayerLevelRequest",
        "ShiftGridCellRequest",
        "EnterExteriorCellRequest",
        "EnterInteriorCellRequest",
    ),
    "Code/client/Services/PlayerService.h": (
        "RunVrPlayerCellStatusWrite",
        "WriteVrPlayerCellStatusFile",
        "m_vrGridCellRequestCount",
        "m_vrExteriorCellRequestCount",
        "m_vrInteriorCellRequestCount",
        "m_vrLevelRequestCount",
        "m_vrPlayerCellStatusPath",
        "m_lastVrGridConnectionGeneration",
        "m_lastVrCellConnectionGeneration",
    ),
    "Code/client/Services/TransportService.h": (
        "m_localPlayerId = 0",
        "m_sessionId = 0",
        "m_connectionGeneration = 0",
        "GetSessionId",
        "GetConnectionGeneration",
    ),
    "Code/client/Services/Generic/TransportService.cpp": (
        "m_sessionId =",
        "m_localPlayerId = 0;",
        "m_localPlayerId = acMessage.PlayerId;",
        "m_connectionGeneration = acMessage.ConnectionGeneration;",
        'request.Username = "Skyrim VR Player";',
        "request.WorldSpaceId = {};",
        "request.CellId = {};",
        "request.Level = 1;",
        "request.PlayerTime = TimeModel{};",
        "if (!pMod || !pMod->IsLoaded())",
        "authentication_not_queued",
    ),
    "Code/client/Services/Generic/VRConnectionService.cpp": (
        'file << "sessionId=" << m_transport.GetSessionId()',
        'file << "connectionGeneration=" << m_transport.GetConnectionGeneration()',
    ),
    "Tools/SkyrimVR/devbench_new_game.py": (
        'launch_env.pop("STVR_AUTOCONNECT", None)',
        'launch_env.pop("STVR_PASSWORD", None)',
        'launch_env["STVR_VM_UPDATE_MODE"] = args.vm_update_mode',
        'choices=("off", "observe", "active")',
        'default="observe"',
        '"--connect requires --vm-update-mode active',
        '"--load-save"',
        '"game", {"action": "load", "name": args.load_save}',
        '"RaceSex Menu" not in value.get("openMenus", [])',
        '"SkyrimTogetherVR.lifecycle"',
        '"Skyrim Together stable gameplay lifecycle"',
        'player_cell.get("lifecycleEpoch") != lifecycle.get("epoch")',
        "--connect verification requires --launch-game",
        'value.get("sessionId") == session_id',
        'value.get("connectionGeneration") == status.get("connectionGeneration")',
        'value.get("lastGrid.connectionGeneration") == status.get("connectionGeneration")',
        'value.get("lastCell.connectionGeneration") == status.get("connectionGeneration")',
        'status_int(value, "worldSpaceTranslationFailureCount") == 0',
        'status_int(value, "lastGrid.cellCount") > 0',
        'status_int(value, "lastGrid.worldSpace.serverBaseId") > 0',
        'status_int(value, "lastGrid.playerCell.serverBaseId") > 0',
        'status_int(value, "lastCell.cell.serverBaseId") > 0',
        'status_int(value, "lastCell.worldSpace.serverBaseId") > 0',
    ),
    "Code/client/Games/PapyrusFunctions.cpp": (
        "SkyrimTogetherVR.playercell",
        "AppendPlayerCellSummary",
        "AppendPlayerCellTelemetry",
        "sessionId",
        "connectionGeneration",
        "lastGrid.connectionGeneration",
        "lastCell.connectionGeneration",
        "gridCellRequestCount",
        "exteriorCellRequestCount",
        "interiorCellRequestCount",
        "levelRequestCount",
        "lastGrid.worldSpace.serverModId",
        "lastCell.currentCoords",
    ),
    "Tools/SkyrimVR/vr_handoff.py": (
        "SkyrimTogetherVR.playercell",
        '"playercell"',
        "gridCellRequestCount",
        "exteriorCellRequestCount",
        "interiorCellRequestCount",
        "levelRequestCount",
    ),
    "Tools/SkyrimVR/audit_runtime_handoff.py": (
        "player_cell_schema_detail",
        "player-cell schema",
        "lastGrid.worldSpace",
        "lastCell.currentCoords",
    ),
    "Tools/SkyrimVR/collect_runtime_evidence.py": (
        "player_cell_status",
        "VR player cell/grid/level status",
    ),
    "Tools/SkyrimVR/audit_runtime_evidence_zip.py": (
        "player_cell_status",
    ),
}

REQUIRED_SAVELOAD_HANDOFF_TOKENS = {
    "Code/client/Games/PapyrusFunctions.cpp": (
        "SkyrimTogetherVR.saveload",
        "AppendSaveLoadSummary",
        "AppendSaveLoadTelemetry",
        "player.serverModId",
        "playerCell.serverModId",
        "playerWorldSpace.serverModId",
    ),
    "Tools/SkyrimVR/vr_handoff.py": (
        "SkyrimTogetherVR.saveload",
        "playerCell.serverBaseId",
        "playerWorldSpace.serverBaseId",
    ),
    "Docs/SkyrimVR/connection-only-mode.md": (
        "save/load readiness counters plus raw and server-mapped load cell/worldspace ids",
        "save/load raw/server player/cell/worldspace ids",
    ),
    "Docs/SkyrimVR/vr-connection-handoff.md": (
        "raw plus server-mapped load cell/worldspace ids",
        "save/load raw/server player/cell/worldspace ids",
    ),
    "Docs/SkyrimVR/vr-handoff-bridge.md": (
        "The save/load section reports",
        "`SkyrimTogetherVR.saveload`",
    ),
}

REQUIRED_ACTIVATION_RELAY_TOKENS = {
    "Code/encoding/Opcodes.h": (
        "kRequestVRActivationEvent",
        "kNotifyVRActivationEvent",
    ),
    "Code/encoding/Messages/ClientMessageFactory.h": (
        "#include <Messages/RequestVRActivationEvent.h>",
        "RequestVRActivationEvent",
    ),
    "Code/encoding/Messages/ServerMessageFactory.h": (
        "#include <Messages/NotifyVRActivationEvent.h>",
        "NotifyVRActivationEvent",
    ),
    "Code/encoding/Structs/VRActivationEvent.h": (
        "struct VRActivationEvent",
        "ObjectId",
        "CellId",
        "WorldSpaceId",
        "Position",
        "FormType",
        "OpenState",
    ),
    "Code/encoding/Messages/RequestVRActivationEvent.h": (
        "struct RequestVRActivationEvent",
        "kRequestVRActivationEvent",
        "VRActivationEvent Activation",
    ),
    "Code/encoding/Messages/NotifyVRActivationEvent.h": (
        "struct NotifyVRActivationEvent",
        "kNotifyVRActivationEvent",
        "uint32_t PlayerId",
        "VRActivationEvent Activation",
    ),
    "Code/server/Services/VRActivationRelayService.cpp": (
        "PacketEvent<RequestVRActivationEvent>",
        "NotifyVRActivationEvent",
        "kMinActivationRelayIntervalMs",
        "IsNewerSequence",
        "HasActivationObject",
        "OnPlayerLeave",
        "ShouldRelayActivation",
        "activation.Sequence == 0 || !HasActivationObject(activation)",
        "state.HasSequence",
        "GameServer::Get()->GetTick()",
        "now - state.LastRelayTick < kMinActivationRelayIntervalMs",
        "state.LastSequence = activation.Sequence",
        "m_playerActivationRelayState.erase",
        "notify.PlayerId = playerId;",
        "GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);",
    ),
    "Code/server/Services/VRActivationRelayService.h": (
        "PlayerActivationRelayState",
        "LastSequence",
        "LastRelayTick",
        "HasSequence",
        "OnPlayerLeave",
        "ShouldRelayActivation",
        "m_playerActivationRelayState",
        "m_playerLeaveConnection",
    ),
    "Code/server/World.cpp": (
        "#include <Services/VRActivationRelayService.h>",
        "ctx().emplace<VRActivationRelayService>(*this, m_dispatcher);",
    ),
    "Code/tests/encoding.cpp": (
        "BuildActivationEvent",
        "RequestVRActivationEvent",
        "NotifyVRActivationEvent",
        'GIVEN("VRActivationEvent")',
    ),
}

REQUIRED_MAGIC_RELAY_TOKENS = {
    "Code/encoding/Opcodes.h": (
        "kRequestVRMagicEffectEvent",
        "kNotifyVRMagicEffectEvent",
    ),
    "Code/encoding/Messages/ClientMessageFactory.h": (
        "#include <Messages/RequestVRMagicEffectEvent.h>",
        "RequestVRMagicEffectEvent",
    ),
    "Code/encoding/Messages/ServerMessageFactory.h": (
        "#include <Messages/NotifyVRMagicEffectEvent.h>",
        "NotifyVRMagicEffectEvent",
    ),
    "Code/encoding/Structs/VRMagicEffectEvent.h": (
        "struct VRMagicEffectEvent",
        "EffectId",
        "CasterId",
        "TargetId",
        "CasterIsPlayer",
        "TargetIsPlayer",
    ),
    "Code/encoding/Messages/RequestVRMagicEffectEvent.h": (
        "struct RequestVRMagicEffectEvent",
        "kRequestVRMagicEffectEvent",
        "VRMagicEffectEvent MagicEffect",
    ),
    "Code/encoding/Messages/NotifyVRMagicEffectEvent.h": (
        "struct NotifyVRMagicEffectEvent",
        "kNotifyVRMagicEffectEvent",
        "uint32_t PlayerId",
        "VRMagicEffectEvent MagicEffect",
    ),
    "Code/server/Services/VRMagicRelayService.cpp": (
        "PacketEvent<RequestVRMagicEffectEvent>",
        "NotifyVRMagicEffectEvent",
        "kMinMagicRelayIntervalMs",
        "IsNewerSequence",
        "HasMagicEffectContext",
        "OnPlayerLeave",
        "ShouldRelayMagicEffect",
        "magicEffect.Sequence == 0 || !HasMagicEffectContext(magicEffect)",
        "acMagicEffect.CasterIsPlayer || acMagicEffect.TargetIsPlayer",
        "state.HasSequence",
        "GameServer::Get()->GetTick()",
        "now - state.LastRelayTick < kMinMagicRelayIntervalMs",
        "state.LastSequence = magicEffect.Sequence",
        "m_playerMagicRelayState.erase",
        "notify.PlayerId = playerId;",
        "GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);",
    ),
    "Code/server/Services/VRMagicRelayService.h": (
        "PlayerMagicRelayState",
        "LastSequence",
        "LastRelayTick",
        "HasSequence",
        "OnPlayerLeave",
        "ShouldRelayMagicEffect",
        "m_playerMagicRelayState",
        "m_playerLeaveConnection",
    ),
    "Code/server/World.cpp": (
        "#include <Services/VRMagicRelayService.h>",
        "ctx().emplace<VRMagicRelayService>(*this, m_dispatcher);",
    ),
    "Code/tests/encoding.cpp": (
        "BuildMagicEffectEvent",
        "RequestVRMagicEffectEvent",
        "NotifyVRMagicEffectEvent",
        'GIVEN("VRMagicEffectEvent")',
    ),
}

REQUIRED_COMBAT_RELAY_TOKENS = {
    "Code/encoding/Opcodes.h": (
        "kRequestVRCombatHitEvent",
        "kNotifyVRCombatHitEvent",
    ),
    "Code/encoding/Messages/ClientMessageFactory.h": (
        "#include <Messages/RequestVRCombatHitEvent.h>",
        "RequestVRCombatHitEvent",
    ),
    "Code/encoding/Messages/ServerMessageFactory.h": (
        "#include <Messages/NotifyVRCombatHitEvent.h>",
        "NotifyVRCombatHitEvent",
    ),
    "Code/encoding/Structs/VRCombatHitEvent.h": (
        "struct VRCombatHitEvent",
        "HitterId",
        "HitteeId",
        "SourceId",
        "ProjectileId",
        "RawHitFlags",
        "PlanckHit",
        "HitterIsPlayer",
        "HitteeIsPlayer",
    ),
    "Code/encoding/Messages/RequestVRCombatHitEvent.h": (
        "struct RequestVRCombatHitEvent",
        "kRequestVRCombatHitEvent",
        "VRCombatHitEvent Hit",
    ),
    "Code/encoding/Messages/NotifyVRCombatHitEvent.h": (
        "struct NotifyVRCombatHitEvent",
        "kNotifyVRCombatHitEvent",
        "uint32_t PlayerId",
        "VRCombatHitEvent Hit",
    ),
    "Code/server/Services/VRCombatRelayService.cpp": (
        "PacketEvent<RequestVRCombatHitEvent>",
        "NotifyVRCombatHitEvent",
        "kMinCombatRelayIntervalMs",
        "IsNewerSequence",
        "HasCombatHitContext",
        "OnPlayerLeave",
        "ShouldRelayCombatHit",
        "hit.Sequence == 0 || !HasCombatHitContext(hit)",
        "acHit.HitterIsPlayer || acHit.HitteeIsPlayer",
        "state.HasSequence",
        "GameServer::Get()->GetTick()",
        "now - state.LastRelayTick < kMinCombatRelayIntervalMs",
        "state.LastSequence = hit.Sequence",
        "m_playerCombatRelayState.erase",
        "notify.PlayerId = playerId;",
        "GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);",
    ),
    "Code/server/Services/VRCombatRelayService.h": (
        "PlayerCombatRelayState",
        "LastSequence",
        "LastRelayTick",
        "HasSequence",
        "OnPlayerLeave",
        "ShouldRelayCombatHit",
        "m_playerCombatRelayState",
        "m_playerLeaveConnection",
    ),
    "Code/server/World.cpp": (
        "#include <Services/VRCombatRelayService.h>",
        "ctx().emplace<VRCombatRelayService>(*this, m_dispatcher);",
    ),
    "Code/tests/encoding.cpp": (
        "BuildCombatHitEvent",
        "RequestVRCombatHitEvent",
        "NotifyVRCombatHitEvent",
        'GIVEN("VRCombatHitEvent")',
    ),
}

REQUIRED_PROJECTILE_RELAY_TOKENS = {
    "Code/encoding/Opcodes.h": (
        "kRequestVRProjectileEvent",
        "kNotifyVRProjectileEvent",
    ),
    "Code/encoding/Messages/ClientMessageFactory.h": (
        "#include <Messages/RequestVRProjectileEvent.h>",
        "RequestVRProjectileEvent",
    ),
    "Code/encoding/Messages/ServerMessageFactory.h": (
        "#include <Messages/NotifyVRProjectileEvent.h>",
        "NotifyVRProjectileEvent",
    ),
    "Code/encoding/Structs/VRProjectileEvent.h": (
        "struct VRProjectileEvent",
        "Kind",
        "WeaponId",
        "AmmoId",
        "SpellId",
        "OriginValid",
        "DestinationValid",
    ),
    "Code/encoding/Messages/RequestVRProjectileEvent.h": (
        "struct RequestVRProjectileEvent",
        "kRequestVRProjectileEvent",
        "VRProjectileEvent Projectile",
    ),
    "Code/encoding/Messages/NotifyVRProjectileEvent.h": (
        "struct NotifyVRProjectileEvent",
        "kNotifyVRProjectileEvent",
        "uint32_t PlayerId",
        "VRProjectileEvent Projectile",
    ),
    "Code/server/Services/VRProjectileRelayService.cpp": (
        "PacketEvent<RequestVRProjectileEvent>",
        "NotifyVRProjectileEvent",
        "kMinProjectileRelayIntervalMs",
        "IsNewerSequence",
        "HasProjectileIntentContext",
        "OnPlayerLeave",
        "ShouldRelayProjectile",
        "projectile.Sequence == 0 || !HasProjectileIntentContext(projectile)",
        "acProjectile.SourceId",
        "acProjectile.OriginValid || acProjectile.DestinationValid",
        "state.HasSequence",
        "GameServer::Get()->GetTick()",
        "now - state.LastRelayTick < kMinProjectileRelayIntervalMs",
        "state.LastSequence = projectile.Sequence",
        "m_playerProjectileRelayState.erase",
        "notify.PlayerId = playerId;",
        "GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);",
    ),
    "Code/server/Services/VRProjectileRelayService.h": (
        "PlayerProjectileRelayState",
        "LastSequence",
        "LastRelayTick",
        "HasSequence",
        "OnPlayerLeave",
        "ShouldRelayProjectile",
        "m_playerProjectileRelayState",
        "m_playerLeaveConnection",
    ),
    "Code/server/World.cpp": (
        "#include <Services/VRProjectileRelayService.h>",
        "ctx().emplace<VRProjectileRelayService>(*this, m_dispatcher);",
    ),
    "Code/tests/encoding.cpp": (
        "BuildProjectileEvent",
        "RequestVRProjectileEvent",
        "NotifyVRProjectileEvent",
        'GIVEN("VRProjectileEvent")',
    ),
}

REQUIRED_GRAB_RELAY_TOKENS = {
    "Code/encoding/Opcodes.h": (
        "kRequestVRGrabEvent",
        "kNotifyVRGrabEvent",
    ),
    "Code/encoding/Messages/ClientMessageFactory.h": (
        "#include <Messages/RequestVRGrabEvent.h>",
        "RequestVRGrabEvent",
    ),
    "Code/encoding/Messages/ServerMessageFactory.h": (
        "#include <Messages/NotifyVRGrabEvent.h>",
        "NotifyVRGrabEvent",
    ),
    "Code/encoding/Structs/VRGrabEvent.h": (
        "struct VRGrabEvent",
        "ObjectId",
        "CellId",
        "WorldSpaceId",
        "Grabbed",
    ),
    "Code/encoding/Messages/RequestVRGrabEvent.h": (
        "struct RequestVRGrabEvent",
        "kRequestVRGrabEvent",
        "VRGrabEvent Grab",
    ),
    "Code/encoding/Messages/NotifyVRGrabEvent.h": (
        "struct NotifyVRGrabEvent",
        "kNotifyVRGrabEvent",
        "uint32_t PlayerId",
        "VRGrabEvent Grab",
    ),
    "Code/server/Services/VRGrabRelayService.cpp": (
        "PacketEvent<RequestVRGrabEvent>",
        "NotifyVRGrabEvent",
        "kMinGrabRelayIntervalMs",
        "IsNewerSequence",
        "HasGrabObject",
        "OnPlayerLeave",
        "ShouldRelayGrab",
        "grab.Sequence == 0 || !HasGrabObject(grab)",
        "state.HasSequence",
        "GameServer::Get()->GetTick()",
        "now - state.LastRelayTick < kMinGrabRelayIntervalMs",
        "state.LastSequence = grab.Sequence",
        "m_playerGrabRelayState.erase",
        "notify.PlayerId = playerId;",
        "GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);",
    ),
    "Code/server/Services/VRGrabRelayService.h": (
        "PlayerGrabRelayState",
        "LastSequence",
        "LastRelayTick",
        "HasSequence",
        "OnPlayerLeave",
        "ShouldRelayGrab",
        "m_playerGrabRelayState",
        "m_playerLeaveConnection",
    ),
    "Code/server/World.cpp": (
        "#include <Services/VRGrabRelayService.h>",
        "ctx().emplace<VRGrabRelayService>(*this, m_dispatcher);",
    ),
    "Code/tests/encoding.cpp": (
        "BuildGrabEvent",
        "RequestVRGrabEvent",
        "NotifyVRGrabEvent",
        'GIVEN("VRGrabEvent")',
    ),
}

REQUIRED_LAYOUT_TOKENS = {
    "Code/client/Games/Skyrim/NetImmerse/NiAVObject.h": (
        "virtual void Unk_VRFunc();",
        "static_assert(offsetof(NiAVObject, world) == 0x7C);",
        "static_assert(offsetof(NiAVObject, userData) == 0x110);",
        "static_assert(sizeof(NiAVObject) == 0x138);",
    ),
    "Code/client/Games/Skyrim/RuntimeLayout.h": (
        "namespace Skyrim::RuntimeLayout",
        "template <class T> [[nodiscard]] const T& Ref",
        "template <class T> [[nodiscard]] T Value",
        "template <class T> void Store",
        "template <class T> [[nodiscard]] const T* Ptr",
        "template <class T> [[nodiscard]] T* Ptr",
        "struct TESObjectREFRCommonLibNgOffsets",
        "ObjectReference = 0x40",
        "Rotation = 0x48",
        "Position = 0x54",
        "ParentCell = 0x60",
        "LoadedData = 0x68",
        "ExtraDataList = 0x70",
        "struct TESObjectREFRLocalShimOffsets",
        "struct TESFormCommonLibNgOffsets",
        "SourceFiles = 0x8",
        "FormID = 0x14",
        "InGameFormFlags = 0x18",
        "FormType = 0x1A",
        "struct TESFormLocalShimOffsets",
        "struct TESFullNameCommonLibNgOffsets",
        "struct TESFullNameLocalShimOffsets",
        "struct BGSKeywordFormCommonLibNgOffsets",
        "struct BGSKeywordFormLocalShimOffsets",
        "struct TESGlobalCommonLibNgOffsets",
        "Value = 0x34",
        "Size = 0x38",
        "struct TESGlobalLocalShimOffsets",
        "struct CalendarCommonLibNgOffsets",
        "GameYear = 0x8",
        "GameMonth = 0x10",
        "GameDay = 0x18",
        "GameHour = 0x20",
        "GameDaysPassed = 0x28",
        "TimeScale = 0x30",
        "RawDaysPassed = 0x3C",
        "struct TimeDataLocalShimOffsets",
        "struct TESActorBaseDataCommonLibNgOffsets",
        "Flags = 0x8",
        "Level = 0x10",
        "BaseTemplateForm = 0x30",
        "Factions = 0x40",
        "Size = 0x58",
        "struct TESActorBaseDataLocalShimOffsets",
        "struct TESActorBaseCommonLibNgOffsets",
        "ActorData = 0x30",
        "FullName = 0xD8",
        "KeywordForm = 0x110",
        "Size = 0x150",
        "struct TESActorBaseLocalShimOffsets",
        "struct TESRaceFormCommonLibNgOffsets",
        "Race = 0x8",
        "Size = 0x10",
        "struct TESRaceFormLocalShimOffsets",
        "struct TESNPCCommonLibNgOffsets",
        "RaceForm = 0x150",
        "NpcClass = 0x1C0",
        "CombatStyle = 0x1D8",
        "OriginalRace = 0x1E8",
        "FaceNpc = 0x1F0",
        "DefaultOutfit = 0x218",
        "HeadParts = 0x238",
        "NumHeadParts = 0x240",
        "BodyTintColor = 0x246",
        "Relationships = 0x250",
        "FaceData = 0x258",
        "TintLayers = 0x260",
        "Size = 0x268",
        "struct TESNPCLocalShimOffsets",
        "struct BGSOutfitCommonLibNgOffsets",
        "OutfitItems = 0x20",
        "Size = 0x38",
        "struct BGSOutfitLocalShimOffsets",
        "struct BGSBipedObjectFormCommonLibNgOffsets",
        "SlotMask = 0x8",
        "ArmorType = 0xC",
        "Size = 0x10",
        "struct BGSBipedObjectFormLocalShimOffsets",
        "struct TESObjectARMOCommonLibNgOffsets",
        "BipedObjectForm = 0x1B0",
        "SlotMask = 0x1B8",
        "Size = 0x228",
        "struct TESObjectARMOLocalShimOffsets",
        "struct EffectSettingCommonLibNgOffsets",
        "Archetype = 0xC0",
        "struct EffectSettingLocalShimOffsets",
        "struct MagicItemCommonLibNgOffsets",
        "Effects = 0x58",
        "AVEffectSetting = 0x78",
        "struct MagicItemLocalShimOffsets",
        "struct SpellItemCommonLibNgOffsets",
        "Data = 0xC0",
        "CastingType = 0xD0",
        "Delivery = 0xD4",
        "CastDuration = 0xD8",
        "Range = 0xDC",
        "CastingPerk = 0xE0",
        "struct SpellItemLocalShimOffsets",
        "struct MagicCasterCommonLibNgOffsets",
        "DesiredTarget = 0x20",
        "CurrentSpell = 0x28",
        "CastingTimer = 0x34",
        "ProjectileTimer = 0x44",
        "struct MagicCasterLocalShimOffsets",
        "struct ActorMagicCasterCommonLibNgOffsets",
        "CasterActor = 0xB8",
        "MagicNode = 0xC0",
        "CastingSource = 0xF4",
        "struct ActorMagicCasterLocalShimOffsets",
        "struct ActiveEffectCommonLibNgOffsets",
        "Spell = 0x40",
        "Target = 0x50",
        "ElapsedSeconds = 0x70",
        "Duration = 0x74",
        "Magnitude = 0x78",
        "CastingSource = 0x88",
        "struct ActiveEffectLocalShimOffsets",
        "struct ValueModifierEffectCommonLibNgOffsets",
        "ActorValue = 0x90",
        "Value = 0x94",
        "struct ValueModifierEffectLocalShimOffsets",
        "struct MagicTargetAddTargetDataCommonLibNgOffsets",
        "MagicItem = 0x8",
        "Effect = 0x10",
        "Unk40 = 0x40",
        "AreaTarget = 0x48",
        "DualCast = 0x49",
        "struct MagicTargetAddTargetDataLocalShimOffsets",
        "struct TESQuestCommonLibNgOffsets",
        "Priority = 0xDE",
        "CurrentStage = 0x228",
        "struct TESQuestLocalShimOffsets",
        "struct TESWorldSpaceCommonLibNgOffsets",
        "struct TESWorldSpaceLocalShimOffsets",
        "struct TESObjectCELLCommonLibNgOffsets",
        "CellFlags = 0x40",
        "References = 0x80",
        "WorldSpace = 0x120",
        "LoadedCellData = 0x128",
        "struct TESObjectCELLLocalShimOffsets",
        "References = 0x88",
        "WorldSpace = 0x128",
        "LoadedCellData = 0x130",
        "struct LoadedCellDataCommonLibNgOffsets",
        "EncounterZone = 0x160",
        "Size = 0x180",
        "struct LoadedCellDataLocalShimOffsets",
        "struct EquipDataLocalShimOffsets",
        "ExtraDataList = 0x0",
        "Count = 0x8",
        "Slot = 0x10",
        "SlotToReplace = 0x18",
        "QueueEquip = 0x20",
        "ForceEquip = 0x21",
        "PlaySound = 0x22",
        "ApplyNow = 0x23",
        "struct MagicEquipDataLocalShimOffsets",
        "EquipSlot = 0x0",
        "struct ShoutEquipDataLocalShimOffsets",
        "struct ExtraTextDisplayDataCommonLibNgOffsets",
        "DisplayName = 0x10",
        "DisplayNameText = 0x18",
        "OwnerQuest = 0x20",
        "OwnerInstance = 0x28",
        "TemperFactor = 0x2C",
        "CustomNameLength = 0x30",
        "struct ExtraTextDisplayDataLocalShimOffsets",
        "struct TESCommonLibNgOffsets",
        "GridCells = 0x78",
        "CenterGridX = 0xB0",
        "CenterGridY = 0xB4",
        "CurrentGridX = 0xB8",
        "CurrentGridY = 0xBC",
        "InteriorCell = 0xC0",
        "InteriorBuffer = 0xC8",
        "ExteriorBuffer = 0xD0",
        "ActiveImageSpaceModifiers = 0x108",
        "struct TESLocalShimOffsets",
        "struct AIProcessCommonLibNgOffsets",
        "MiddleProcess = 0x8",
        "struct AIProcessLocalShimOffsets",
        "struct MiddleProcessCommonLibNgOffsets",
        "Direction = 0xB8",
        "ActiveEffects = 0x1A0",
        "CommandingActor = 0x218",
        "LeftEquippedObject = 0x220",
        "RightEquippedObject = 0x260",
        "BothHandsEquippedObject = 0x268",
        "struct MiddleProcessLocalShimOffsets",
        "struct PlayerCharacterInfoRuntimeDataCommonLibNgOffsets",
        "Skills = 0xE0",
        "Size = 0x150",
        "struct PlayerCharacterGameStateDataCommonLibNgOffsets",
        "Difficulty = 0x0",
        "Size = 0xC",
        "struct PlayerSkillsCommonLibNgOffsets",
        "GlobalXp = 0x0",
        "GlobalLevelThreshold = 0x4",
        "Skills = 0x8",
        "LegendaryLevels = 0xE0",
        "struct PlayerSkillsLocalShimOffsets",
        "struct PlayerCharacterVRCommonLibNgOffsets",
        "VRNodeData = 0x3F0",
        "InfoRuntimeData = 0xFD0",
        "SkillsPointer = 0x10B0",
        "GameStateData = 0x11F4",
        "struct PlayerCharacterLocalShimOffsets",
        "Objectives = 0x588",
        "SkillsPointer = 0x9B8",
        "LocationForm = 0xAD0",
        "Difficulty = 0xB00",
        "BaseTints = 0xB18",
        "OverlayTints = 0xB30",
        "struct ActorCommonLibNgOffsets",
        "MagicTarget = 0x98",
        "ActorValueOwner = 0xB0",
        "ActorState = 0xB8",
        "BoolBits = 0xE0",
        "CurrentProcess = 0xF0",
        "CombatController = 0x158",
        "MagicCasters = 0x1A0",
        "SelectedSpells = 0x1C0",
        "SelectedPower = 0x1E0",
        "Race = 0x1F0",
        "BoolFlags = 0x1FC",
        "HealthModifiers = 0x228",
        "MagickaModifiers = 0x234",
        "StaminaModifiers = 0x240",
        "VoiceModifiers = 0x24C",
        "struct ActorLocalShimOffsets",
        "MagicTarget = 0xA0",
        "ActorValueOwner = 0xB8",
        "ActorState = 0xC0",
        "BoolBits = 0xE8",
        "CurrentProcess = 0xF8",
        "CombatController = 0x160",
        "MagicCasters = 0x1A8",
        "SelectedSpells = 0x1C8",
        "SelectedPower = 0x1E8",
        "Race = 0x1F8",
        "BoolFlags = 0x204",
        "HealthModifiers = 0x230",
        "StaminaModifiers = 0x23C",
        "MagickaModifiers = 0x248",
        "VoiceModifiers = 0x254",
        "struct CombatControllerCommonLibNgOffsets",
        "CombatGroup = 0x0",
        "Inventory = 0x10",
        "AttackerHandle = 0x28",
        "TargetHandle = 0x2C",
        "StartedCombat = 0x35",
        "TargetSelectors = 0x98",
        "ActiveTargetSelector = 0xB0",
        "CachedAttacker = 0xC8",
        "Size = 0xD8",
        "struct CombatControllerLocalShimOffsets",
        "TargetSelectors = 0xA0",
        "ActiveTargetSelector = 0xB8",
        "CachedAttacker = 0xD0",
        "Size = 0xE0",
        "struct CombatTargetCommonLibNgOffsets",
        "AttackerCount = 0xA4",
        "Flags = 0xA6",
        "struct CombatTargetLocalShimOffsets",
        "struct CombatGroupCommonLibNgOffsets",
        "Targets = 0x8",
        "Size = 0x168",
        "struct CombatGroupLocalShimOffsets",
        "struct CombatInventoryCommonLibNgOffsets",
        "MaximumRange = 0x1B8",
        "Size = 0x1C8",
        "struct CombatInventoryLocalShimOffsets",
        "struct CombatTargetSelectorLocalShimOffsets",
        "Controller = 0x10",
        "Priority = 0x1C",
        "Flags = 0x20",
        "StandardSize = 0x30",
        "struct BSScriptIVirtualMachineCommonLibNgSlots",
        "GetScriptObjectType1 = 0x9",
        "BindNativeMethod = 0x18",
        "SendEvent = 0x24",
        "GetObjectHandlePolicy = 0x2D",
        "struct BSScriptIVirtualMachineLocalShimSlots",
        "struct BSTEventSourceCommonLibNgOffsets",
        "struct BSTEventSourceLocalShimOffsets",
        "struct ScriptEventSourceHolderCommonLibNgOffsets",
        "Activate = 0x58",
        "GrabRelease = 0x580",
        "Hit = 0x5D8",
        "LoadGame = 0x688",
        "MagicEffectApply = 0x738",
        "SpellCast = 0xE18",
        "PlayerBowShot = 0xE70",
        "SeFastTravelEnd = 0x1238",
        "SeSize = 0x1290",
        "VrSize = 0x1238",
        "struct EventDispatcherManagerLocalShimOffsets",
        "struct ProjectileCommonLibNgOffsets",
        "Power = 0x188",
        "struct ProjectileLocalShimOffsets",
        "Power = 0x190",
        "struct IMenuCommonLibNgOffsets",
        "MenuFlags = 0x1C",
        "InputContext = 0x20",
        "FxDelegate = 0x28",
        "VrUnk30 = 0x30",
        "VrUnk34 = 0x34",
        "VrUnk38 = 0x38",
        "SeSize = 0x30",
        "VrSize = 0x40",
        "struct IMenuLocalShimOffsets",
        "struct UICommonLibNgOffsets",
        "MenuStack = 0x110",
        "MenuMap = 0x128",
        "NumPausesGame = 0x160",
        "Size = 0x1C8",
        "struct UILocalShimOffsets",
        "struct MenuControlsCommonLibNgOffsets",
        "IsProcessing = 0x80",
        "BeastForm = 0x81",
        "RemapMode = 0x82",
        "Toggle = 0x83",
        "Unk84 = 0x84",
        "Size = 0x88",
        "struct MenuControlsLocalShimOffsets",
        "struct ExtraContainerChangesCommonLibNgOffsets",
        "EntryObject = 0x0",
        "EntryExtraLists = 0x8",
        "EntryCountDelta = 0x10",
        "DataEntries = 0x0",
        "DataParent = 0x8",
        "DataTotalWeight = 0x10",
        "DataArmorWeight = 0x14",
        "DataUnk18 = 0x18",
        "struct ExtraContainerChangesLocalShimOffsets",
        "struct TESContainerCommonLibNgOffsets",
        "EntryCount = 0x0",
        "EntryForm = 0x8",
        "EntryData = 0x10",
        "Entries = 0x8",
        "Count = 0x10",
        "struct TESContainerLocalShimOffsets",
        "struct InventoryEntryCommonLibNgOffsets",
        "Object = 0x0",
        "ExtraLists = 0x8",
        "struct InventoryEntryLocalShimOffsets",
        "struct ExtraDataListCommonLibNgOffsets",
        "BitfieldSize = 0x18",
        "Bitfield = 0x10",
        "Lock = 0x18",
        "struct ExtraDataListLocalShimOffsets",
        "struct BSGraphicsRendererDataCommonLibNgOffsets",
        "Forwarder = 0x38",
        "Context = 0x40",
        "RenderWindows = 0x48",
        "RenderTargets = 0xA48",
        "DepthStencilTargets = 0x1FA8",
        "CubeMapRenderTargets = 0x26C8",
        "Texture3DRenderTargets = 0x2708",
        "ClearColor = 0x2768",
        "ClearStencil = 0x2778",
        "ClassName = 0x27A8",
        "HInstance = 0x27B0",
        "struct BSGraphicsRendererDataLocalShimOffsets",
        "struct BSGraphicsRendererCommonLibNgOffsets",
        "Data = 0x10",
        "struct BSGraphicsRendererLocalShimOffsets",
    ),
    "Code/client/Games/Skyrim/TESObjectREFR.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibRefrOffsets = Skyrim::RuntimeLayout::TESObjectREFRCommonLibNgOffsets",
        "LocalRefrOffsets = Skyrim::RuntimeLayout::TESObjectREFRLocalShimOffsets",
        "GetBaseFormData",
        "GetRotationData",
        "SetRotationData",
        "GetPositionData",
        "GetParentCellData",
        "GetExtraDataListData",
    ),
    "Code/client/Games/Skyrim/Forms/TESForm.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibFormOffsets = Skyrim::RuntimeLayout::TESFormCommonLibNgOffsets",
        "LocalFormOffsets = Skyrim::RuntimeLayout::TESFormLocalShimOffsets",
        "GetFormFlagsData",
        "SetFormFlagsData",
        "GetFormIdData",
        "GetInGameFormFlagsData",
        "SetInGameFormFlagsData",
        "GetFormTypeData",
        "GetIgnoreFriendlyHit() const noexcept { return (GetFormFlagsData() & IGNORE_FRIENDLY_HITS) != 0; }",
        "bool IsDisabled() const noexcept { return (GetFormFlagsData() & DISABLED) != 0; }",
        "bool IsTemporary() const noexcept { return GetFormIdData() >= 0xFF000000; }",
        "static_assert(offsetof(TESForm, flags) == TESForm::LocalFormOffsets::Flags);",
        "static_assert(offsetof(TESForm, formID) == TESForm::LocalFormOffsets::FormID);",
        "static_assert(offsetof(TESForm, unk10) == TESForm::LocalFormOffsets::InGameFormFlags);",
        "static_assert(offsetof(TESForm, formType) == TESForm::LocalFormOffsets::FormType);",
        "static_assert(sizeof(TESForm) == TESForm::LocalFormOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/Forms/TESGlobal.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibGlobalOffsets = Skyrim::RuntimeLayout::TESGlobalCommonLibNgOffsets",
        "LocalGlobalOffsets = Skyrim::RuntimeLayout::TESGlobalLocalShimOffsets",
        "GetValueData",
        "SetValueData",
        "static_assert(TESGlobal::CommonLibGlobalOffsets::Value == 0x34);",
        "static_assert(TESGlobal::CommonLibGlobalOffsets::Size == 0x38);",
        "static_assert(offsetof(TESGlobal, f) == TESGlobal::LocalGlobalOffsets::Value);",
        "static_assert(sizeof(TESGlobal) == TESGlobal::LocalGlobalOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/TimeManager.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibCalendarOffsets = Skyrim::RuntimeLayout::CalendarCommonLibNgOffsets",
        "LocalTimeDataOffsets = Skyrim::RuntimeLayout::TimeDataLocalShimOffsets",
        "GetGameYearData",
        "GetGameMonthData",
        "GetGameDayData",
        "GetGameHourData",
        "GetGameDaysPassedData",
        "GetTimeScaleData",
        "static_assert(TimeData::CommonLibCalendarOffsets::GameYear == 0x8);",
        "static_assert(TimeData::CommonLibCalendarOffsets::GameMonth == 0x10);",
        "static_assert(TimeData::CommonLibCalendarOffsets::GameDay == 0x18);",
        "static_assert(TimeData::CommonLibCalendarOffsets::GameHour == 0x20);",
        "static_assert(TimeData::CommonLibCalendarOffsets::GameDaysPassed == 0x28);",
        "static_assert(TimeData::CommonLibCalendarOffsets::TimeScale == 0x30);",
        "static_assert(TimeData::CommonLibCalendarOffsets::RawDaysPassed == 0x3C);",
        "static_assert(TimeData::CommonLibCalendarOffsets::Size == 0x40);",
        "static_assert(offsetof(TimeData, GameYear) == TimeData::LocalTimeDataOffsets::GameYear);",
        "static_assert(offsetof(TimeData, GameMonth) == TimeData::LocalTimeDataOffsets::GameMonth);",
        "static_assert(offsetof(TimeData, GameDay) == TimeData::LocalTimeDataOffsets::GameDay);",
        "static_assert(offsetof(TimeData, GameHour) == TimeData::LocalTimeDataOffsets::GameHour);",
        "static_assert(offsetof(TimeData, GameDaysPassed) == TimeData::LocalTimeDataOffsets::GameDaysPassed);",
        "static_assert(offsetof(TimeData, TimeScale) == TimeData::LocalTimeDataOffsets::TimeScale);",
        "static_assert(offsetof(TimeData, rawDaysPassed) == TimeData::LocalTimeDataOffsets::RawDaysPassed);",
    ),
    "Code/client/Games/Skyrim/Components/TESActorBaseData.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibActorBaseDataOffsets = Skyrim::RuntimeLayout::TESActorBaseDataCommonLibNgOffsets",
        "LocalActorBaseDataOffsets = Skyrim::RuntimeLayout::TESActorBaseDataLocalShimOffsets",
        "GetFlagsData",
        "SetFlagsData",
        "GetFactionsData",
        "bool IsEssential() const noexcept { return (GetFlagsData() & BaseFlags::IS_ESSENTIAL) != 0; }",
        "static_assert(TESActorBaseData::CommonLibActorBaseDataOffsets::Flags == 0x8);",
        "static_assert(TESActorBaseData::CommonLibActorBaseDataOffsets::Level == 0x10);",
        "static_assert(TESActorBaseData::CommonLibActorBaseDataOffsets::BaseTemplateForm == 0x30);",
        "static_assert(TESActorBaseData::CommonLibActorBaseDataOffsets::Factions == 0x40);",
        "static_assert(TESActorBaseData::CommonLibActorBaseDataOffsets::Size == 0x58);",
        "static_assert(offsetof(TESActorBaseData, flags) == TESActorBaseData::LocalActorBaseDataOffsets::Flags);",
        "static_assert(offsetof(TESActorBaseData, level) == TESActorBaseData::LocalActorBaseDataOffsets::Level);",
        "static_assert(offsetof(TESActorBaseData, owner) == TESActorBaseData::LocalActorBaseDataOffsets::Owner);",
        "static_assert(offsetof(TESActorBaseData, factions) == TESActorBaseData::LocalActorBaseDataOffsets::Factions);",
        "static_assert(sizeof(TESActorBaseData) == TESActorBaseData::LocalActorBaseDataOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/Components/TESFullName.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibFullNameOffsets = Skyrim::RuntimeLayout::TESFullNameCommonLibNgOffsets",
        "LocalFullNameOffsets = Skyrim::RuntimeLayout::TESFullNameLocalShimOffsets",
        "GetFullNameData",
        "GetFullNameStringData",
        "static_assert(TESFullName::CommonLibFullNameOffsets::Value == 0x8);",
        "static_assert(TESFullName::CommonLibFullNameOffsets::Size == 0x10);",
        "static_assert(offsetof(TESFullName, value) == TESFullName::LocalFullNameOffsets::Value);",
        "static_assert(sizeof(TESFullName) == TESFullName::LocalFullNameOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/Components/BGSKeywordForm.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibKeywordFormOffsets = Skyrim::RuntimeLayout::BGSKeywordFormCommonLibNgOffsets",
        "LocalKeywordFormOffsets = Skyrim::RuntimeLayout::BGSKeywordFormLocalShimOffsets",
        "GetKeywordsData",
        "GetKeywordCountData",
        "ContainsKeywordData",
        "static_assert(BGSKeywordForm::CommonLibKeywordFormOffsets::Keywords == 0x8);",
        "static_assert(BGSKeywordForm::CommonLibKeywordFormOffsets::Count == 0x10);",
        "static_assert(BGSKeywordForm::CommonLibKeywordFormOffsets::Size == 0x18);",
        "static_assert(offsetof(BGSKeywordForm, keywords) == BGSKeywordForm::LocalKeywordFormOffsets::Keywords);",
        "static_assert(offsetof(BGSKeywordForm, count) == BGSKeywordForm::LocalKeywordFormOffsets::Count);",
        "static_assert(sizeof(BGSKeywordForm) == BGSKeywordForm::LocalKeywordFormOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/Forms/TESActorBase.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibActorBaseOffsets = Skyrim::RuntimeLayout::TESActorBaseCommonLibNgOffsets",
        "LocalActorBaseOffsets = Skyrim::RuntimeLayout::TESActorBaseLocalShimOffsets",
        "GetActorBaseData",
        "GetFullNameData",
        "GetKeywordFormData",
        "static_assert(TESActorBase::CommonLibActorBaseOffsets::ActorData == 0x30);",
        "static_assert(TESActorBase::CommonLibActorBaseOffsets::FullName == 0xD8);",
        "static_assert(TESActorBase::CommonLibActorBaseOffsets::KeywordForm == 0x110);",
        "static_assert(TESActorBase::CommonLibActorBaseOffsets::Size == 0x150);",
        "static_assert(offsetof(TESActorBase, actorData) == TESActorBase::LocalActorBaseOffsets::ActorData);",
        "static_assert(offsetof(TESActorBase, fullName) == TESActorBase::LocalActorBaseOffsets::FullName);",
        "static_assert(offsetof(TESActorBase, keywordForm) == TESActorBase::LocalActorBaseOffsets::KeywordForm);",
        "static_assert(sizeof(TESActorBase) == TESActorBase::LocalActorBaseOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/Components/TESRaceForm.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibRaceFormOffsets = Skyrim::RuntimeLayout::TESRaceFormCommonLibNgOffsets",
        "LocalRaceFormOffsets = Skyrim::RuntimeLayout::TESRaceFormLocalShimOffsets",
        "GetRaceData",
        "SetRaceData",
        "static_assert(TESRaceForm::CommonLibRaceFormOffsets::Race == 0x8);",
        "static_assert(TESRaceForm::CommonLibRaceFormOffsets::Size == 0x10);",
        "static_assert(offsetof(TESRaceForm, race) == TESRaceForm::LocalRaceFormOffsets::Race);",
        "static_assert(sizeof(TESRaceForm) == TESRaceForm::LocalRaceFormOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/Forms/TESNPC.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibNpcOffsets = Skyrim::RuntimeLayout::TESNPCCommonLibNgOffsets",
        "LocalNpcOffsets = Skyrim::RuntimeLayout::TESNPCLocalShimOffsets",
        "GetRaceFormData",
        "GetNpcClassData",
        "SetNpcClassData",
        "GetCombatStyleData",
        "SetCombatStyleData",
        "SetOriginalRaceData",
        "GetTemplateNpcData",
        "GetDefaultOutfitData",
        "SetDefaultOutfitData",
        "GetHeadPartsData",
        "GetHeadPartsCountData",
        "static_assert(TESNPC::CommonLibNpcOffsets::RaceForm == 0x150);",
        "static_assert(TESNPC::CommonLibNpcOffsets::NpcClass == 0x1C0);",
        "static_assert(TESNPC::CommonLibNpcOffsets::CombatStyle == 0x1D8);",
        "static_assert(TESNPC::CommonLibNpcOffsets::OriginalRace == 0x1E8);",
        "static_assert(TESNPC::CommonLibNpcOffsets::FaceNpc == 0x1F0);",
        "static_assert(TESNPC::CommonLibNpcOffsets::DefaultOutfit == 0x218);",
        "static_assert(TESNPC::CommonLibNpcOffsets::HeadParts == 0x238);",
        "static_assert(TESNPC::CommonLibNpcOffsets::NumHeadParts == 0x240);",
        "static_assert(TESNPC::CommonLibNpcOffsets::BodyTintColor == 0x246);",
        "static_assert(TESNPC::CommonLibNpcOffsets::Relationships == 0x250);",
        "static_assert(TESNPC::CommonLibNpcOffsets::FaceData == 0x258);",
        "static_assert(TESNPC::CommonLibNpcOffsets::TintLayers == 0x260);",
        "static_assert(TESNPC::CommonLibNpcOffsets::Size == 0x268);",
        "static_assert(offsetof(TESNPC, raceForm) == TESNPC::LocalNpcOffsets::RaceForm);",
        "static_assert(offsetof(TESNPC, npcClass) == TESNPC::LocalNpcOffsets::NpcClass);",
        "static_assert(offsetof(TESNPC, combatStyle) == TESNPC::LocalNpcOffsets::CombatStyle);",
        "static_assert(offsetof(TESNPC, overlayRace) == TESNPC::LocalNpcOffsets::OriginalRace);",
        "static_assert(offsetof(TESNPC, npcTemplate) == TESNPC::LocalNpcOffsets::FaceNpc);",
        "static_assert(offsetof(TESNPC, outfits) == TESNPC::LocalNpcOffsets::DefaultOutfit);",
        "static_assert(offsetof(TESNPC, headparts) == TESNPC::LocalNpcOffsets::HeadParts);",
        "static_assert(offsetof(TESNPC, headpartsCount) == TESNPC::LocalNpcOffsets::NumHeadParts);",
        "static_assert(offsetof(TESNPC, color) == TESNPC::LocalNpcOffsets::BodyTintColor);",
        "static_assert(offsetof(TESNPC, relationships) == TESNPC::LocalNpcOffsets::Relationships);",
        "static_assert(offsetof(TESNPC, faceMorphs) == TESNPC::LocalNpcOffsets::FaceData);",
        "static_assert(sizeof(TESNPC) == TESNPC::LocalNpcOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/Forms/BGSOutfit.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibOutfitOffsets = Skyrim::RuntimeLayout::BGSOutfitCommonLibNgOffsets",
        "LocalOutfitOffsets = Skyrim::RuntimeLayout::BGSOutfitLocalShimOffsets",
        "GetOutfitItemsData",
        "static_assert(BGSOutfit::CommonLibOutfitOffsets::OutfitItems == 0x20);",
        "static_assert(BGSOutfit::CommonLibOutfitOffsets::Size == 0x38);",
        "static_assert(offsetof(BGSOutfit, outfitItems) == BGSOutfit::LocalOutfitOffsets::OutfitItems);",
        "static_assert(sizeof(BGSOutfit) == BGSOutfit::LocalOutfitOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/Components/BGSBipedObjectForm.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibBipedObjectOffsets = Skyrim::RuntimeLayout::BGSBipedObjectFormCommonLibNgOffsets",
        "LocalBipedObjectOffsets = Skyrim::RuntimeLayout::BGSBipedObjectFormLocalShimOffsets",
        "GetSlotMaskData",
        "HasPartData",
        "static_assert(BGSBipedObjectForm::CommonLibBipedObjectOffsets::SlotMask == 0x8);",
        "static_assert(BGSBipedObjectForm::CommonLibBipedObjectOffsets::ArmorType == 0xC);",
        "static_assert(BGSBipedObjectForm::CommonLibBipedObjectOffsets::Size == 0x10);",
        "static_assert(offsetof(BGSBipedObjectForm, bitfield) == BGSBipedObjectForm::LocalBipedObjectOffsets::SlotMask);",
        "static_assert(offsetof(BGSBipedObjectForm, unk8) == BGSBipedObjectForm::LocalBipedObjectOffsets::ArmorType);",
        "static_assert(sizeof(BGSBipedObjectForm) == BGSBipedObjectForm::LocalBipedObjectOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/Forms/TESObjectARMO.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibArmorOffsets = Skyrim::RuntimeLayout::TESObjectARMOCommonLibNgOffsets",
        "LocalArmorOffsets = Skyrim::RuntimeLayout::TESObjectARMOLocalShimOffsets",
        "GetSlotMaskData",
        "BGSBipedObjectForm::Part::Body",
        "static_assert(TESObjectARMO::CommonLibArmorOffsets::BipedObjectForm == 0x1B0);",
        "static_assert(TESObjectARMO::CommonLibArmorOffsets::SlotMask == 0x1B8);",
        "static_assert(TESObjectARMO::CommonLibArmorOffsets::Size == 0x228);",
        "static_assert(offsetof(TESObjectARMO, slotType) == TESObjectARMO::LocalArmorOffsets::SlotMask);",
    ),
    "Code/client/Games/Skyrim/Forms/EffectSetting.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibEffectSettingOffsets = Skyrim::RuntimeLayout::EffectSettingCommonLibNgOffsets",
        "LocalEffectSettingOffsets = Skyrim::RuntimeLayout::EffectSettingLocalShimOffsets",
        "GetFullNameData",
        "GetKeywordFormData",
        "GetArchetypeData",
        "static_assert(EffectSetting::CommonLibEffectSettingOffsets::FullName == 0x20);",
        "static_assert(EffectSetting::CommonLibEffectSettingOffsets::KeywordForm == 0x40);",
        "static_assert(EffectSetting::CommonLibEffectSettingOffsets::Archetype == 0xC0);",
        "static_assert(offsetof(EffectSetting, fullName) == EffectSetting::LocalEffectSettingOffsets::FullName);",
        "static_assert(offsetof(EffectSetting, keywordForm) == EffectSetting::LocalEffectSettingOffsets::KeywordForm);",
        "static_assert(offsetof(EffectSetting, eArchetype) == EffectSetting::LocalEffectSettingOffsets::Archetype);",
    ),
    "Code/client/Games/Skyrim/Forms/MagicItem.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibMagicItemOffsets = Skyrim::RuntimeLayout::MagicItemCommonLibNgOffsets",
        "LocalMagicItemOffsets = Skyrim::RuntimeLayout::MagicItemLocalShimOffsets",
        "GetFullNameData",
        "GetKeywordFormData",
        "GetEffectsData",
        "GetHostileCountData",
        "GetAVEffectSettingData",
        "static_assert(MagicItem::CommonLibMagicItemOffsets::FullName == 0x30);",
        "static_assert(MagicItem::CommonLibMagicItemOffsets::KeywordForm == 0x40);",
        "static_assert(MagicItem::CommonLibMagicItemOffsets::Effects == 0x58);",
        "static_assert(MagicItem::CommonLibMagicItemOffsets::HostileCount == 0x70);",
        "static_assert(MagicItem::CommonLibMagicItemOffsets::AVEffectSetting == 0x78);",
        "static_assert(MagicItem::CommonLibMagicItemOffsets::Size == 0x90);",
        "static_assert(offsetof(MagicItem, fullName) == MagicItem::LocalMagicItemOffsets::FullName);",
        "static_assert(offsetof(MagicItem, keyword) == MagicItem::LocalMagicItemOffsets::KeywordForm);",
        "static_assert(offsetof(MagicItem, listOfEffects) == MagicItem::LocalMagicItemOffsets::Effects);",
        "static_assert(offsetof(MagicItem, iHostileCount) == MagicItem::LocalMagicItemOffsets::HostileCount);",
        "static_assert(offsetof(MagicItem, pAVEffectSetting) == MagicItem::LocalMagicItemOffsets::AVEffectSetting);",
        "static_assert(sizeof(MagicItem) == MagicItem::LocalMagicItemOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/Forms/SpellItem.h": (
        "CommonLibSpellItemOffsets = Skyrim::RuntimeLayout::SpellItemCommonLibNgOffsets",
        "LocalSpellItemOffsets = Skyrim::RuntimeLayout::SpellItemLocalShimOffsets",
        "GetCastingTypeData",
        "GetDeliveryData",
        "static_assert(SpellItem::CommonLibSpellItemOffsets::Data == 0xC0);",
        "static_assert(SpellItem::CommonLibSpellItemOffsets::CastingType == 0xD0);",
        "static_assert(SpellItem::CommonLibSpellItemOffsets::Delivery == 0xD4);",
        "static_assert(SpellItem::CommonLibSpellItemOffsets::Size == 0xE8);",
        "static_assert(offsetof(SpellItem, eCastingType) == SpellItem::LocalSpellItemOffsets::CastingType);",
        "static_assert(offsetof(SpellItem, eDelivery) == SpellItem::LocalSpellItemOffsets::Delivery);",
        "static_assert(sizeof(SpellItem) == SpellItem::LocalSpellItemOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/Forms/EnchantmentItem.h": (
        "CommonLibEnchantmentItemOffsets = Skyrim::RuntimeLayout::EnchantmentItemCommonLibNgOffsets",
        "LocalEnchantmentItemOffsets = Skyrim::RuntimeLayout::EnchantmentItemLocalShimOffsets",
        "GetCostOverrideData",
        "GetFlagsData",
        "GetCastingTypeData",
        "GetChargeOverrideData",
        "GetDeliveryData",
        "GetSpellTypeData",
        "GetChargeTimeData",
        "GetBaseEnchantmentData",
        "GetWornRestrictionsData",
        "static_assert(EnchantmentItem::CommonLibEnchantmentItemOffsets::Data == 0x90);",
        "static_assert(EnchantmentItem::CommonLibEnchantmentItemOffsets::CastingType == 0x98);",
        "static_assert(EnchantmentItem::CommonLibEnchantmentItemOffsets::Delivery == 0xA0);",
        "static_assert(EnchantmentItem::CommonLibEnchantmentItemOffsets::BaseEnchantment == 0xB0);",
        "static_assert(EnchantmentItem::CommonLibEnchantmentItemOffsets::WornRestrictions == 0xB8);",
        "static_assert(EnchantmentItem::CommonLibEnchantmentItemOffsets::Size == 0xC0);",
        "static_assert(offsetof(EnchantmentItem, iCostOverride) == EnchantmentItem::LocalEnchantmentItemOffsets::CostOverride);",
        "static_assert(offsetof(EnchantmentItem, eCastingType) == EnchantmentItem::LocalEnchantmentItemOffsets::CastingType);",
        "static_assert(offsetof(EnchantmentItem, eDelivery) == EnchantmentItem::LocalEnchantmentItemOffsets::Delivery);",
        "static_assert(offsetof(EnchantmentItem, pBaseEnchantment) == EnchantmentItem::LocalEnchantmentItemOffsets::BaseEnchantment);",
        "static_assert(offsetof(EnchantmentItem, pWornRestrictions) == EnchantmentItem::LocalEnchantmentItemOffsets::WornRestrictions);",
        "static_assert(sizeof(EnchantmentItem) == EnchantmentItem::LocalEnchantmentItemOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/Magic/MagicCaster.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibMagicCasterOffsets = Skyrim::RuntimeLayout::MagicCasterCommonLibNgOffsets",
        "LocalMagicCasterOffsets = Skyrim::RuntimeLayout::MagicCasterLocalShimOffsets",
        "GetDesiredTargetData",
        "GetCurrentSpellData",
        "GetStateData",
        "static_assert(MagicCaster::CommonLibMagicCasterOffsets::DesiredTarget == 0x20);",
        "static_assert(MagicCaster::CommonLibMagicCasterOffsets::CurrentSpell == 0x28);",
        "static_assert(MagicCaster::CommonLibMagicCasterOffsets::Size == 0x48);",
        "static_assert(offsetof(MagicCaster, hDesiredTarget) == MagicCaster::LocalMagicCasterOffsets::DesiredTarget);",
        "static_assert(offsetof(MagicCaster, pCurrentSpell) == MagicCaster::LocalMagicCasterOffsets::CurrentSpell);",
        "static_assert(sizeof(MagicCaster) == MagicCaster::LocalMagicCasterOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/Magic/ActorMagicCaster.h": (
        "CommonLibActorMagicCasterOffsets = Skyrim::RuntimeLayout::ActorMagicCasterCommonLibNgOffsets",
        "LocalActorMagicCasterOffsets = Skyrim::RuntimeLayout::ActorMagicCasterLocalShimOffsets",
        "GetCasterActorData",
        "GetMagicNodeData",
        "GetCastingSourceData",
        "static_assert(ActorMagicCaster::CommonLibActorMagicCasterOffsets::CasterActor == 0xB8);",
        "static_assert(ActorMagicCaster::CommonLibActorMagicCasterOffsets::MagicNode == 0xC0);",
        "static_assert(ActorMagicCaster::CommonLibActorMagicCasterOffsets::CastingSource == 0xF4);",
        "static_assert(ActorMagicCaster::CommonLibActorMagicCasterOffsets::Size == 0x100);",
        "static_assert(offsetof(ActorMagicCaster, pCasterActor) == ActorMagicCaster::LocalActorMagicCasterOffsets::CasterActor);",
        "static_assert(offsetof(ActorMagicCaster, pMagicNode) == ActorMagicCaster::LocalActorMagicCasterOffsets::MagicNode);",
        "static_assert(offsetof(ActorMagicCaster, eCastingSource) == ActorMagicCaster::LocalActorMagicCasterOffsets::CastingSource);",
    ),
    "Code/client/Games/Skyrim/Magic/ActorMagicCaster.cpp": (
        "apThis->GetCasterActorData()",
        "apThis->GetCurrentSpellData()",
        "apThis->GetDesiredTargetData()",
        "apThis->GetCastingSourceData()",
        "pCasterActor->GetFormIdData()",
    ),
    "Code/client/Games/Skyrim/Effects/ActiveEffect.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibActiveEffectOffsets = Skyrim::RuntimeLayout::ActiveEffectCommonLibNgOffsets",
        "LocalActiveEffectOffsets = Skyrim::RuntimeLayout::ActiveEffectLocalShimOffsets",
        "GetCasterHandleData",
        "GetSpellData",
        "GetEffectData",
        "GetTargetData",
        "GetElapsedSecondsData",
        "SetElapsedSecondsData",
        "GetDurationData",
        "GetMagnitudeData",
        "SetMagnitudeData",
        "GetFlagsData",
        "GetCastingSourceData",
        "static_assert(ActiveEffect::CommonLibActiveEffectOffsets::Spell == 0x40);",
        "static_assert(ActiveEffect::CommonLibActiveEffectOffsets::ElapsedSeconds == 0x70);",
        "static_assert(ActiveEffect::CommonLibActiveEffectOffsets::Duration == 0x74);",
        "static_assert(ActiveEffect::CommonLibActiveEffectOffsets::Magnitude == 0x78);",
        "static_assert(ActiveEffect::CommonLibActiveEffectOffsets::Flags == 0x7C);",
        "static_assert(ActiveEffect::CommonLibActiveEffectOffsets::CastingSource == 0x88);",
        "static_assert(offsetof(ActiveEffect, pSpell) == ActiveEffect::LocalActiveEffectOffsets::Spell);",
        "static_assert(offsetof(ActiveEffect, fElapsedSeconds) == ActiveEffect::LocalActiveEffectOffsets::ElapsedSeconds);",
        "static_assert(offsetof(ActiveEffect, fMagnitude) == ActiveEffect::LocalActiveEffectOffsets::Magnitude);",
        "static_assert(sizeof(ActiveEffect) == ActiveEffect::LocalActiveEffectOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/Effects/ValueModifierEffect.h": (
        "CommonLibValueModifierEffectOffsets = Skyrim::RuntimeLayout::ValueModifierEffectCommonLibNgOffsets",
        "LocalValueModifierEffectOffsets = Skyrim::RuntimeLayout::ValueModifierEffectLocalShimOffsets",
        "GetActorValueIndexData",
        "GetValueData",
        "SetValueData",
        "static_assert(ValueModifierEffect::CommonLibValueModifierEffectOffsets::ActorValue == 0x90);",
        "static_assert(ValueModifierEffect::CommonLibValueModifierEffectOffsets::Value == 0x94);",
        "static_assert(ValueModifierEffect::CommonLibValueModifierEffectOffsets::Size == 0x98);",
        "static_assert(offsetof(ValueModifierEffect, actorValueIndex) == ValueModifierEffect::LocalValueModifierEffectOffsets::ActorValue);",
        "static_assert(offsetof(ValueModifierEffect, value) == ValueModifierEffect::LocalValueModifierEffectOffsets::Value);",
        "static_assert(sizeof(ValueModifierEffect) == ValueModifierEffect::LocalValueModifierEffectOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/Effects/InvisibilityEffect.cpp": (
        "apThis->GetTargetData()",
        "pTarget->GetTargetAsActor()",
    ),
    "Code/client/Games/Skyrim/Magic/MagicTarget.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibAddTargetDataOffsets = Skyrim::RuntimeLayout::MagicTargetAddTargetDataCommonLibNgOffsets",
        "LocalAddTargetDataOffsets = Skyrim::RuntimeLayout::MagicTargetAddTargetDataLocalShimOffsets",
        "GetCasterData",
        "SetCasterData",
        "GetSpellData",
        "SetSpellData",
        "GetEffectItemData",
        "SetEffectItemData",
        "GetMagnitudeData",
        "SetMagnitudeData",
        "GetUnk40Data",
        "SetUnk40Data",
        "GetCastingSourceData",
        "SetCastingSourceData",
        "IsDualCastData",
        "SetDualCastData",
        "static_assert(AddTargetData::CommonLibAddTargetDataOffsets::MagicItem == 0x8);",
        "static_assert(AddTargetData::CommonLibAddTargetDataOffsets::Effect == 0x10);",
        "static_assert(AddTargetData::CommonLibAddTargetDataOffsets::Magnitude == 0x3C);",
        "static_assert(AddTargetData::CommonLibAddTargetDataOffsets::CastingSource == 0x44);",
        "static_assert(AddTargetData::CommonLibAddTargetDataOffsets::DualCast == 0x49);",
        "static_assert(sizeof(AddTargetData) == AddTargetData::LocalAddTargetDataOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/Magic/MagicTarget.cpp": (
        "arData.GetCasterData()",
        "arData.GetSpellData()",
        "arData.GetEffectItemData()",
        "arData.GetMagnitudeData()",
        "arData.IsDualCastData()",
    ),
    "Code/client/Games/Skyrim/Forms/TESQuest.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibQuestOffsets = Skyrim::RuntimeLayout::TESQuestCommonLibNgOffsets",
        "LocalQuestOffsets = Skyrim::RuntimeLayout::TESQuestLocalShimOffsets",
        "GetFullNameData",
        "GetQuestFlagsData",
        "SetQuestFlagsData",
        "GetPriorityData",
        "GetQuestTypeData",
        "GetCurrentStageData",
        "static_assert(TESQuest::CommonLibQuestOffsets::FullName == 0x28);",
        "static_assert(TESQuest::CommonLibQuestOffsets::Flags == 0xDC);",
        "static_assert(TESQuest::CommonLibQuestOffsets::Priority == 0xDE);",
        "static_assert(TESQuest::CommonLibQuestOffsets::Type == 0xDF);",
        "static_assert(TESQuest::CommonLibQuestOffsets::CurrentStage == 0x228);",
        "static_assert(TESQuest::CommonLibQuestOffsets::Size == 0x268);",
        "static_assert(sizeof(TESQuest) == TESQuest::LocalQuestOffsets::Size);",
        "static_assert(offsetof(TESQuest, fullName) == TESQuest::LocalQuestOffsets::FullName);",
        "static_assert(offsetof(TESQuest, flags) == TESQuest::LocalQuestOffsets::Flags);",
        "static_assert(offsetof(TESQuest, priority) == TESQuest::LocalQuestOffsets::Priority);",
        "static_assert(offsetof(TESQuest, type) == TESQuest::LocalQuestOffsets::Type);",
        "static_assert(offsetof(TESQuest, currentStage) == TESQuest::LocalQuestOffsets::CurrentStage);",
    ),
    "Code/client/Games/Skyrim/Forms/TESWorldSpace.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibWorldSpaceOffsets = Skyrim::RuntimeLayout::TESWorldSpaceCommonLibNgOffsets",
        "LocalWorldSpaceOffsets = Skyrim::RuntimeLayout::TESWorldSpaceLocalShimOffsets",
        "GetFullNameData",
        "static_assert(TESWorldSpace::CommonLibWorldSpaceOffsets::FullName == 0x20);",
        "static_assert(offsetof(TESWorldSpace, fullName) == TESWorldSpace::LocalWorldSpaceOffsets::FullName);",
    ),
    "Code/client/Games/Forms.cpp": (
        "loadBuffer.formId = GetFormIdData();",
        "SetFormFlagsData(GetFormFlagsData() | 0x200000);",
        "auto formFlags = GetFormFlagsData();",
        "auto inGameFormFlags = GetInGameFormFlagsData();",
        "SetInGameFormFlagsData(0xFFFF);",
        "GetFormIdData(), changeFlags",
        "SetNpcClassData",
        "SetCombatStyleData",
        "GetRaceFormData().SetRaceData",
        "SetDefaultOutfitData",
        "SetOriginalRaceData",
        "GetHeadPartsData",
        "GetHeadPartsCountData",
    ),
    "Code/client/Games/Skyrim/Forms/MagicItem.cpp": (
        "GetKeywordFormData().ContainsKeywordData",
        "GetEffectsData()",
        "GetFormIdData() == bosFormID",
        "pEffect->GetEffectSettingData()",
        "GetArchetypeData() == EffectArchetypes::ArchetypeID::kBoundWeapon",
        "pEffectSetting->GetFormIdData() == aEffectId",
    ),
    "Code/client/Games/Skyrim/SaveLoad.h": (
        "GetFlagsData",
        "SetFlagsData",
        "GetSaveNameData",
    ),
    "Code/client/Games/Skyrim/SaveLoad.cpp": (
        "apData->SetFlagsData(apData->GetFlagsData() | 4);",
        "apData->GetSaveNameData()",
    ),
    "Code/client/Games/Skyrim/Magic/EffectItem.cpp": (
        "GetArchetypeData() == EffectArchetypes::ArchetypeID::kValueModifier",
        "GetEffectSettingData()",
        "pEffectSettingData->GetKeywordFormData()",
        "keywordForm.GetKeywordCountData()",
        "keywordForm.ContainsKeywordData",
        "pEffectSettingData->GetFullNameData().GetFullNameStringData()",
    ),
    "Code/client/Services/Generic/QuestService.cpp": (
        "pQuest->GetFullNameData().GetFullNameStringData()",
        "pQuest->GetQuestTypeData()",
        "pQuest->GetCurrentStageData()",
        "pQuest->GetPriorityData()",
    ),
    "Code/client/Services/Generic/DiscordService.cpp": (
        "pWorldspace->GetFullNameData()",
        "worldspaceName.GetFullNameStringData()",
    ),
    "Code/client/Services/Generic/TransportService.cpp": (
        "pNpc->GetFullNameData().GetFullNameStringData()",
    ),
    "Code/client/Services/Debug/Views/QuestDebugView.cpp": (
        "pQuest->GetFullNameData().GetFullNameStringData()",
    ),
    "Code/client/Services/Debug/Views/FormDebugView.cpp": (
        "pEffect->GetSpellData()",
        "pEffect->GetElapsedSecondsData()",
        "pEffect->GetDurationData()",
        "pEffect->GetMagnitudeData()",
        "pEffect->GetFlagsData()",
        "pEffect->SetElapsedSecondsData",
    ),
    "Code/client/Games/Skyrim/Forms/TESNPC.cpp": (
        "apThis->GetFullNameData().GetFullNameStringData()",
        "apSelectedNpc->GetFullNameData().GetFullNameStringData()",
    ),
    "Code/client/Games/Skyrim/Actor.cpp": (
        "pNpc->GetFullNameData().GetFullNameStringData()",
        "#if TP_SKYRIM_VR\n    POINTER_SKYRIMSE(TGetLevel, s_getLevel, 36344);",
        "#else\n    POINTER_SKYRIMSE(TGetLevel, s_getLevel, 37334);",
    ),
    "Code/client/Games/Skyrim/Forms/TESObjectCELL.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibCellOffsets = Skyrim::RuntimeLayout::TESObjectCELLCommonLibNgOffsets",
        "LocalCellOffsets = Skyrim::RuntimeLayout::TESObjectCELLLocalShimOffsets",
        "GetCellFlagsData",
        "IsInteriorCellData",
        "GetWorldSpaceData",
        "GetLoadedCellData",
        "CommonLibLoadedCellDataOffsets = Skyrim::RuntimeLayout::LoadedCellDataCommonLibNgOffsets",
        "LocalLoadedCellDataOffsets = Skyrim::RuntimeLayout::LoadedCellDataLocalShimOffsets",
        "GetEncounterZoneData",
        "static_assert(LoadedCellData::CommonLibLoadedCellDataOffsets::EncounterZone == 0x160);",
        "static_assert(LoadedCellData::CommonLibLoadedCellDataOffsets::Size == 0x180);",
        "static_assert(offsetof(LoadedCellData, encounterZone) == LoadedCellData::LocalLoadedCellDataOffsets::EncounterZone);",
        "static_assert(sizeof(LoadedCellData) == LoadedCellData::LocalLoadedCellDataOffsets::Size);",
        "GetReferenceData()",
        "GetCapacityData",
        "GetAvailableData",
        "GetReferenceArrayData",
        "GetReferenceAtData",
        "GetReferenceData",
        "return nullptr;",
    ),
    "Code/client/Games/TES.h": (
        "#include <Games/Primitives.h>",
        "#include <NetImmerse/NiPointer.h>",
        "#include <RuntimeLayout.h>",
        "CommonLibTESOffsets = Skyrim::RuntimeLayout::TESCommonLibNgOffsets",
        "LocalTESOffsets = Skyrim::RuntimeLayout::TESLocalShimOffsets",
        "GetGridCellsData",
        "GetCenterGridXData",
        "GetCenterGridYData",
        "GetCurrentGridXData",
        "GetCurrentGridYData",
        "GetInteriorCellData",
        "GetInteriorBufferData",
        "GetExteriorBufferData",
        "GetActiveImageSpaceModifiersData",
        "static_assert(offsetof(TES, cells) == TES::LocalTESOffsets::GridCells);",
        "static_assert(offsetof(TES, centerGridX) == TES::LocalTESOffsets::CenterGridX);",
        "static_assert(offsetof(TES, centerGridY) == TES::LocalTESOffsets::CenterGridY);",
        "static_assert(offsetof(TES, currentGridX) == TES::LocalTESOffsets::CurrentGridX);",
        "static_assert(offsetof(TES, currentGridY) == TES::LocalTESOffsets::CurrentGridY);",
        "static_assert(offsetof(TES, interiorCell) == TES::LocalTESOffsets::InteriorCell);",
        "static_assert(offsetof(TES, activeImageSpaceModifiers) == TES::LocalTESOffsets::ActiveImageSpaceModifiers);",
    ),
    "Code/client/Games/TES.cpp": (
        "#if TP_SKYRIM_VR\n    POINTER_SKYRIMSE(TES*, tes, 516923);",
        "#else\n    POINTER_SKYRIMSE(TES*, tes, 400441);",
        "#if TP_SKYRIM_VR\n    POINTER_SKYRIMSE(ProcessLists*, processLists, 514167);",
        "#else\n    POINTER_SKYRIMSE(ProcessLists*, processLists, 400315);",
    ),
    "Code/client/Games/ModManager.cpp": (
        "#if TP_SKYRIM_VR\n    POINTER_SKYRIMSE(ModManager*, modManager, 514141);",
        "#else\n    POINTER_SKYRIMSE(ModManager*, modManager, 400269);",
    ),
    "Code/client/Games/TimeManager.cpp": (
        "#if TP_SKYRIM_VR\n    POINTER_SKYRIMSE(TimeData*, s_instance, 514287);",
        "#else\n    POINTER_SKYRIMSE(TimeData*, s_instance, 400447);",
    ),
    "Code/client/Games/INISettingCollection.cpp": (
        "#if TP_SKYRIM_VR\n    POINTER_SKYRIMSE(INISettingCollection*, settingCollection, 524557);",
        "#else\n    POINTER_SKYRIMSE(INISettingCollection*, settingCollection, 411155);",
    ),
    "Code/client/Services/Generic/DiscoveryService.cpp": (
        "SkyrimTogetherVR.discovery",
        "actorLimit=",
        "currentGrid=",
        "centerGrid=",
        "cachedWorldSpaceFormId=",
        "cachedInteriorCellFormId=",
        "playerCellFormId=",
        "playerWorldSpaceFormId=",
        "locationFormId=",
        ".formId=",
        "GetCurrentGridXData",
        "GetCurrentGridYData",
        "GetCenterGridXData",
        "GetCenterGridYData",
        "kVrSkipActorHandleDiscovery",
        "!TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC",
    ),
    "Code/client/Services/Generic/CharacterService.cpp": (
        "GetCenterGridXData",
        "GetCenterGridYData",
        "pQuest->GetCurrentStageData()",
    ),
    "Code/client/Services/Debug/Views/PlayerDebugView.cpp": (
        "GetCurrentGridXData",
        "GetCurrentGridYData",
        "GetCenterGridXData",
        "GetCenterGridYData",
        "GetCurrentSpellData",
    ),
    "Code/client/Services/Debug/DebugService.cpp": (
        "GetActiveImageSpaceModifiersData",
    ),
    "Code/client/Games/Skyrim/PlayerCharacter.cpp": (
        "GetCenterGridXData",
        "GetCenterGridYData",
        "pSkillData->GetSkillData(aSkill).xp",
        "#if TP_SKYRIM_VR\n    POINTER_SKYRIMSE(PlayerCharacter*, s_character, 517014);",
        "#else\n    POINTER_SKYRIMSE(PlayerCharacter*, s_character, 401069);",
    ),
    "Code/client/Services/Debug/Views/SkillView.cpp": (
        "pSkills->GetGlobalXpData()",
        "pSkills->GetGlobalLevelThresholdData()",
        "pSkills->GetSkillData(skill)",
        "pSkills->GetLegendaryLevelData(skill)",
    ),
    "Code/client/Games/Skyrim/Actor.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibActorOffsets = Skyrim::RuntimeLayout::ActorCommonLibNgOffsets",
        "LocalActorOffsets = Skyrim::RuntimeLayout::ActorLocalShimOffsets",
        "GetMagicTargetData",
        "GetActorValueOwnerData",
        "GetActorStateData",
        "GetBoolBitsData",
        "GetCurrentProcessData",
        "GetCachedMagicCasterData",
        "SetCachedMagicCasterData",
        "GetSelectedSpellData",
        "GetSelectedPowerOrShoutData",
        "SetSelectedPowerOrShoutData",
        "GetRaceData",
        "GetBoolFlagsData",
        "SetBoolFlagsData",
        "GetHealthModifiersData",
        "GetMagickaModifiersData",
        "GetStaminaModifiersData",
        "GetVoiceModifiersData",
        "GetTemporaryHealthModifierData",
        "static_assert(offsetof(Actor, casters) == Actor::LocalActorOffsets::MagicCasters);",
        "static_assert(offsetof(Actor, healthModifiers) == Actor::LocalActorOffsets::HealthModifiers);",
        "static_assert(offsetof(Actor, magickaModifiers) == Actor::LocalActorOffsets::MagickaModifiers);",
        "static_assert(sizeof(Actor::ActorValueModifiers) == 0xC);",
    ),
    "Code/client/Services/Generic/OverlayService.cpp": (
        "apActor->GetTemporaryHealthModifierData()",
    ),
    "Code/client/Games/Skyrim/Combat/CombatController.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibCombatControllerOffsets = Skyrim::RuntimeLayout::CombatControllerCommonLibNgOffsets",
        "LocalCombatControllerOffsets = Skyrim::RuntimeLayout::CombatControllerLocalShimOffsets",
        "GetCombatGroupData",
        "GetInventoryData",
        "GetAttackerHandleData",
        "GetTargetHandleData",
        "GetStartedCombatData",
        "GetTargetSelectorsData",
        "GetActiveTargetSelectorData",
        "SetActiveTargetSelectorData",
        "GetCachedAttackerData",
        "static_assert(CombatController::CommonLibCombatControllerOffsets::TargetSelectors == 0x98);",
        "static_assert(CombatController::CommonLibCombatControllerOffsets::ActiveTargetSelector == 0xB0);",
        "static_assert(CombatController::CommonLibCombatControllerOffsets::CachedAttacker == 0xC8);",
        "static_assert(CombatController::CommonLibCombatControllerOffsets::Size == 0xD8);",
        "static_assert(offsetof(CombatController, pCombatGroup) == CombatController::LocalCombatControllerOffsets::CombatGroup);",
        "static_assert(offsetof(CombatController, pInventory) == CombatController::LocalCombatControllerOffsets::Inventory);",
        "static_assert(offsetof(CombatController, attackerHandle) == CombatController::LocalCombatControllerOffsets::AttackerHandle);",
        "static_assert(offsetof(CombatController, targetHandle) == CombatController::LocalCombatControllerOffsets::TargetHandle);",
        "static_assert(offsetof(CombatController, startedCombat) == CombatController::LocalCombatControllerOffsets::StartedCombat);",
        "static_assert(offsetof(CombatController, targetSelectors) == CombatController::LocalCombatControllerOffsets::TargetSelectors);",
        "static_assert(offsetof(CombatController, pActiveTargetSelector) == CombatController::LocalCombatControllerOffsets::ActiveTargetSelector);",
        "static_assert(offsetof(CombatController, pCachedAttacker) == CombatController::LocalCombatControllerOffsets::CachedAttacker);",
        "static_assert(sizeof(CombatController) == CombatController::LocalCombatControllerOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/Combat/CombatController.cpp": (
        "auto& targetSelectorData = GetTargetSelectorsData();",
        "HasFlagsData(1)",
        "HasFlagsData(2)",
        "SetActiveTargetSelectorData(nullptr);",
        "SetActiveTargetSelectorData(pTargetSelector);",
        "TESObjectREFR::GetByHandle(GetAttackerHandleData())",
        "GetTargetHandleData()",
    ),
    "Code/client/Games/Skyrim/Combat/CombatGroup.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibCombatTargetOffsets = Skyrim::RuntimeLayout::CombatTargetCommonLibNgOffsets",
        "LocalCombatTargetOffsets = Skyrim::RuntimeLayout::CombatTargetLocalShimOffsets",
        "GetTargetHandleData",
        "GetAttackerCountData",
        "GetFlagsData",
        "CommonLibCombatGroupOffsets = Skyrim::RuntimeLayout::CombatGroupCommonLibNgOffsets",
        "LocalCombatGroupOffsets = Skyrim::RuntimeLayout::CombatGroupLocalShimOffsets",
        "GetTargetsData",
        "static_assert(offsetof(CombatTarget, targetHandle) == CombatTarget::LocalCombatTargetOffsets::TargetHandle);",
        "static_assert(offsetof(CombatTarget, attackerCount) == CombatTarget::LocalCombatTargetOffsets::AttackerCount);",
        "static_assert(offsetof(CombatTarget, flags) == CombatTarget::LocalCombatTargetOffsets::Flags);",
        "static_assert(sizeof(CombatTarget) == CombatTarget::LocalCombatTargetOffsets::Size);",
        "static_assert(offsetof(CombatGroup, targets) == CombatGroup::LocalCombatGroupOffsets::Targets);",
        "static_assert(sizeof(CombatGroup) == CombatGroup::LocalCombatGroupOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/Combat/CombatInventory.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibCombatInventoryOffsets = Skyrim::RuntimeLayout::CombatInventoryCommonLibNgOffsets",
        "LocalCombatInventoryOffsets = Skyrim::RuntimeLayout::CombatInventoryLocalShimOffsets",
        "GetMaximumRangeData",
        "static_assert(offsetof(CombatInventory, maximumRange) == CombatInventory::LocalCombatInventoryOffsets::MaximumRange);",
        "static_assert(sizeof(CombatInventory) == CombatInventory::LocalCombatInventoryOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/Combat/CombatTargetSelector.h": (
        "#include <RuntimeLayout.h>",
        "LocalCombatTargetSelectorOffsets = Skyrim::RuntimeLayout::CombatTargetSelectorLocalShimOffsets",
        "GetCombatControllerData",
        "GetTargetHandleData",
        "GetPriorityData",
        "GetFlagsData",
        "HasFlagsData",
        "Skyrim::RuntimeLayout::Value<CombatController*>(this, LocalCombatTargetSelectorOffsets::Controller)",
        "Skyrim::RuntimeLayout::Value<BSPointerHandle<Actor>>(this, LocalCombatTargetSelectorOffsets::TargetHandle)",
        "Skyrim::RuntimeLayout::Value<uint32_t>(this, LocalCombatTargetSelectorOffsets::Priority)",
        "Skyrim::RuntimeLayout::Value<uint32_t>(this, LocalCombatTargetSelectorOffsets::Flags)",
        "static_assert(offsetof(CombatTargetSelector, pCombatController) == CombatTargetSelector::LocalCombatTargetSelectorOffsets::Controller);",
        "static_assert(offsetof(CombatTargetSelector, hTarget) == CombatTargetSelector::LocalCombatTargetSelectorOffsets::TargetHandle);",
        "static_assert(offsetof(CombatTargetSelector, ePriority) == CombatTargetSelector::LocalCombatTargetSelectorOffsets::Priority);",
        "static_assert(offsetof(CombatTargetSelector, flags) == CombatTargetSelector::LocalCombatTargetSelectorOffsets::Flags);",
        "static_assert(sizeof(CombatTargetSelector) == CombatTargetSelector::LocalCombatTargetSelectorOffsets::Size);",
        "static_assert(sizeof(CombatTargetSelectorStandard) == CombatTargetSelector::LocalCombatTargetSelectorOffsets::StandardSize);",
    ),
    "Code/client/Games/Skyrim/Misc/BSScript.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibVirtualMachineSlots = Skyrim::RuntimeLayout::BSScriptIVirtualMachineCommonLibNgSlots",
        "LocalVirtualMachineSlots = Skyrim::RuntimeLayout::BSScriptIVirtualMachineLocalShimSlots",
        "virtual bool BindNativeMethod(IFunction* apFunction);",
        "uint32_t refCount;",
        "uint32_t pad0C;",
        "static_assert(IVirtualMachine::CommonLibVirtualMachineSlots::GetScriptObjectType1 == 0x9);",
        "static_assert(IVirtualMachine::CommonLibVirtualMachineSlots::BindNativeMethod == 0x18);",
        "static_assert(IVirtualMachine::CommonLibVirtualMachineSlots::SendEvent == 0x24);",
        "static_assert(IVirtualMachine::CommonLibVirtualMachineSlots::GetObjectHandlePolicy == 0x2D);",
        "static_assert(sizeof(IVirtualMachine) == IVirtualMachine::LocalVirtualMachineSlots::Size);",
    ),
    "Code/client/Games/Misc/BSScript.cpp": (
        "BindSkyrimTogetherNative",
        "BindNativeMethod(apFunction)",
        'spdlog::warn("SkyrimTogetherVR Papyrus native bind hook received a null VM");',
        '"SkyrimTogetherVR Papyrus native bind failed: {}::{}"',
        "auto* pVm = apThis ? *apThis : nullptr;",
        "new BSScript::ConnectToSkyrimTogetherFunc",
    ),
    "Code/client/Games/Skyrim/Events/EventDispatcher.h": (
        "#include <RuntimeLayout.h>",
        "static_assert(sizeof(EventDispatcher<UnknownEvent>) == Skyrim::RuntimeLayout::BSTEventSourceLocalShimOffsets::Size);",
        "NiPointer<TESObjectREFR> objectActivated;",
        "NiPointer<TESObjectREFR> actionRef;",
        "GetObjectActivatedData",
        "static_assert(sizeof(TESActivateEvent) == 0x10);",
        "NiPointer<TESObjectREFR> target;",
        "NiPointer<TESObjectREFR> cause;",
        "GetTargetData",
        "GetCauseData",
        "static_assert(sizeof(TESHitEvent) == 0x20);",
        "NiPointer<TESObjectREFR> caster;",
        "uint32_t magicEffect;",
        "GetMagicEffectData",
        "static_assert(sizeof(TESMagicEffectApplyEvent) == 0x18);",
        "GetObjectData",
        "GetSpellData",
        "static_assert(sizeof(TESSpellCastEvent) == 0x10);",
        "GetWeaponData",
        "GetAmmoData",
        "GetShotPowerData",
        "IsSunGazingData",
        "static_assert(sizeof(TESPlayerBowShotEvent) == 0x10);",
        "CommonLibEventSourceOffsets = Skyrim::RuntimeLayout::ScriptEventSourceHolderCommonLibNgOffsets",
        "LocalEventSourceOffsets = Skyrim::RuntimeLayout::EventDispatcherManagerLocalShimOffsets",
        "GetActivateEventData",
        "GetHitEventData",
        "GetLoadGameEventData",
        "GetMagicEffectApplyEventData",
        "GetSpellCastEventData",
        "GetPlayerBowShotEventData",
        "static_assert(offsetof(EventDispatcherManager, activateEvent) == EventDispatcherManager::LocalEventSourceOffsets::Activate);",
        "static_assert(offsetof(EventDispatcherManager, hitEvent) == EventDispatcherManager::LocalEventSourceOffsets::Hit);",
        "static_assert(offsetof(EventDispatcherManager, loadGameEvent) == EventDispatcherManager::LocalEventSourceOffsets::LoadGame);",
        "static_assert(offsetof(EventDispatcherManager, magicEffectApplyEvent) == EventDispatcherManager::LocalEventSourceOffsets::MagicEffectApply);",
        "static_assert(offsetof(EventDispatcherManager, spellCastEvent) == EventDispatcherManager::LocalEventSourceOffsets::SpellCast);",
        "static_assert(offsetof(EventDispatcherManager, playerBowShotEvent) == EventDispatcherManager::LocalEventSourceOffsets::PlayerBowShot);",
        "static_assert(sizeof(EventDispatcherManager) == EventDispatcherManager::LocalEventSourceOffsets::VrSize);",
        "static_assert(sizeof(EventDispatcherManager) == EventDispatcherManager::LocalEventSourceOffsets::SeSize);",
    ),
    "Code/client/Services/Debug/Views/CombatView.cpp": (
        "GetCombatControllerData",
        "GetCachedAttackerData",
        "GetInventoryData",
        "GetMaximumRangeData",
        "GetCombatGroupData",
        "GetTargetsData",
        "GetAttackerCountData",
        "GetActiveTargetSelectorData",
        "GetTargetHandleData",
    ),
    "Code/client/Games/Skyrim/Misc/ActorState.h": (
        "GetFlags1Data",
        "GetFlags2Data",
        "SetFlagsData",
        "IsWeaponDrawn() const noexcept { return (GetFlags2Data() >> 5 & 7) >= 3; }",
        "IsBleedingOut() const noexcept { return (GetFlags1Data() & 0x1E00000)",
    ),
    "Code/client/Games/Skyrim/PlayerCharacter.h": (
        "VrCommonLibOffsets = Skyrim::RuntimeLayout::PlayerCharacterVRCommonLibNgOffsets",
        "VrInfoRuntimeOffsets = Skyrim::RuntimeLayout::PlayerCharacterInfoRuntimeDataCommonLibNgOffsets",
        "VrGameStateOffsets = Skyrim::RuntimeLayout::PlayerCharacterGameStateDataCommonLibNgOffsets",
        "LocalPlayerOffsets = Skyrim::RuntimeLayout::PlayerCharacterLocalShimOffsets",
        "CommonLibPlayerSkillsOffsets = Skyrim::RuntimeLayout::PlayerSkillsCommonLibNgOffsets",
        "LocalPlayerSkillsOffsets = Skyrim::RuntimeLayout::PlayerSkillsLocalShimOffsets",
        "GetGlobalXpData",
        "GetGlobalLevelThresholdData",
        "GetSkillData",
        "GetLegendaryLevelData",
        "PlayerSkillsRuntimeData",
        "GetSkillsData",
        "GetDifficultyData",
        "GetObjectives",
        "CanReadTintData",
        "CanReadObjectiveData",
        "static_assert(Skills::CommonLibPlayerSkillsOffsets::Skills == 0x8);",
        "static_assert(Skills::CommonLibPlayerSkillsOffsets::LegendaryLevels == 0xE0);",
        "static_assert(sizeof(Skills) == Skills::LocalPlayerSkillsOffsets::Size);",
        "static_assert(PlayerCharacter::VrCommonLibOffsets::SkillsPointer == 0x10B0);",
        "static_assert(PlayerCharacter::VrCommonLibOffsets::GameStateData == 0x11F4);",
        "static_assert(PlayerCharacter::VrInfoRuntimeOffsets::Skills == 0xE0);",
        "static_assert(PlayerCharacter::VrGameStateOffsets::Difficulty == 0x0);",
        "InfoRuntimeData + PlayerCharacter::VrInfoRuntimeOffsets::Skills",
        "GameStateData + PlayerCharacter::VrGameStateOffsets::Difficulty",
    ),
    "Code/encoding/Messages/AssignCharacterRequest.h": (
        "HasQuestContent",
        "HasFaceTints",
    ),
    "Code/encoding/Messages/AssignCharacterRequest.cpp": (
        "Serialization::WriteBool(aWriter, HasQuestContent);",
        "Serialization::WriteBool(aWriter, HasFaceTints);",
        "HasQuestContent = Serialization::ReadBool(aReader);",
        "HasFaceTints = Serialization::ReadBool(aReader);",
    ),
    "Code/client/Services/Generic/CharacterService.cpp": (
        "GetBaseFormData",
        "GetParentCellData",
        "GetParentCellEx",
        "GetPositionData",
        "GetRotationData",
        "SetRotationData",
        "GetActorStateData",
        "CanReadTintData",
        "CanReadObjectiveData",
        "message.HasFaceTints = true;",
        "message.HasQuestContent = true;",
    ),
    "Code/client/Services/Generic/InventoryService.cpp": (
        "GetActorStateData",
    ),
    "Code/client/Services/Generic/ActorValueService.cpp": (
        "GetActorStateData",
    ),
    "Code/client/Services/Generic/MagicService.cpp": (
        "GetSelectedSpellData",
        "GetBaseFormData",
        "GetCasterActorData",
        "pActor->GetMagicCaster(CS::LEFT_HAND)",
        "pSpell->GetCastingTypeData()",
        "pSpellItem->GetCastingTypeData()",
        "data.SetCasterData(pCaster)",
        "data.SetSpellData(pSpell)",
        "data.SetEffectItemData(pEffect)",
        "data.SetMagnitudeData(acMessage.Magnitude)",
        "data.SetUnk40Data(1.0f)",
        "data.SetCastingSourceData(MagicSystem::CastingSource::CASTING_SOURCE_COUNT)",
        "data.SetDualCastData(acMessage.IsDualCasting)",
    ),
    "Code/client/Services/Generic/ObjectService.cpp": (
        "GetBaseFormData",
        "GetParentCellEx",
        "GetPositionData",
        "GetLoadedCellData",
        "GetEncounterZoneData",
        "pBaseForm->GetFormTypeData() == FormType::Container",
        "pBaseForm->GetFormTypeData() == FormType::Door",
    ),
    "Code/client/Games/Skyrim/Forms/TESObjectCELL.cpp": (
        "GetBaseFormData",
        "GetReferenceData",
        "GetReferenceArrayData",
        "GetCapacityData",
        "GetReferenceAtData",
        "pBaseForm->GetFormTypeData() == formType",
    ),
    "Code/client/Games/Skyrim/TESObjectREFR.cpp": (
        "apThis->GetBaseFormData()",
        "GetWorldSpaceData",
        "IsInteriorCellData",
        "pBaseContainer->GetEntriesData()",
        "pBaseContainer->GetEntryCountData()",
        "pGameEntry->GetFormData()",
        "pGameEntry->GetCountData()",
        "pExtraDataList->GetDataData()",
        "pExtraDataList->GetBitfieldData()",
        "pExtraEnchantment->GetEnchantmentData()",
        "pEnchantment->GetEffectsData()",
        "pEnchantment->GetFormIdData()",
        "pEffectItem->GetEffectItemData()",
        "pEffectItem->GetEffectSettingData()->GetFormIdData()",
        "pObject->GetFormTypeData() == FormType::Weapon",
        "pBaseForm->GetFormTypeData() != FormType::Book",
        "pBaseForm->GetFormTypeData() == FormType::Door",
        "apThis->GetFormTypeData() == Actor::Type",
    ),
    "Code/client/Games/Skyrim/Actor.cpp": (
        "IsInteriorCellData",
        "GetExtraDataListData",
        "GetCurrentProcessData",
        "GetCachedMagicCasterData(i)",
        "SetCachedMagicCasterData(i",
        "GetMiddleProcessData",
        "GetLeftEquippedObjectData",
        "GetRightEquippedObjectData",
        "GetCurrentAmmoFromVtable",
        "GetBothHandsEquippedObjectData",
        "pLeftEquippedObject->GetObjectData()",
        "pRightEquippedObject->GetObjectData()",
        "pBothHandsObject->GetObjectData()",
        "pObject && pObject->GetFormTypeData() == FormType::Ammo",
        "GetBoolBitsData",
        "GetSelectedSpellData",
        "GetSelectedPowerOrShoutData",
        "SetSelectedPowerOrShoutData",
        "GetCommandingActorData",
        "SetCommandingActorData",
        "GetBoolFlagsData",
        "SetBoolFlagsData",
        "static_cast<TESLevItem*>(pItem)",
        "pValueModEffect->GetActorValueIndexData()",
    ),
    "Code/client/Systems/AnimationSystem.cpp": (
        "GetActorStateData",
        "GetParentCellData",
        "GetPositionData",
        "GetRotationData",
        "GetCurrentProcessData",
        "GetMiddleProcessData",
        "GetDirectionData",
        "SetFlagsData",
    ),
    "Code/client/ModCompat/BehaviorVar.cpp": (
        "apActor->GetFormIdData()",
    ),
    "Code/client/Systems/InterpolationSystem.cpp": (
        "GetCurrentProcessData",
        "GetMiddleProcessData",
        "SetDirectionData",
    ),
    "Code/client/Games/Animation.cpp": (
        "GetActorStateData",
        "GetFlags1Data",
        "GetFlags2Data",
    ),
    "Code/client/Systems/FaceGenSystem.cpp": (
        "GetBaseFormData",
    ),
    "Code/client/Games/ModManager.cpp": (
        "GetBaseFormData",
    ),
    "Code/client/Games/Forms.cpp": (
        "GetBaseFormData",
    ),
    "Code/client/Services/Debug/Views/FormDebugView.cpp": (
        "GetMiddleProcessData",
        "GetActiveEffectsData",
    ),
    "Code/client/Services/Generic/VRActivationService.cpp": (
        "GetWorldSpaceData",
    ),
    "Code/client/Games/Skyrim/PlayerCharacter.cpp": (
        "apObject->GetBaseFormData()",
    ),
    "Code/client/Services/Debug/DebugService.cpp": (
        "GetBaseFormData",
        "GetMenuMapData",
    ),
    "Code/client/Services/Debug/Views/ComponentView.cpp": (
        "GetPositionData",
    ),
    "Code/client/Services/Debug/Views/EntitiesView.cpp": (
        "GetBaseFormData",
        "GetPositionData",
        "GetRotationData",
    ),
    "Code/client/Services/Debug/Views/FormDebugView.cpp": (
        "GetParentCellData",
        "GetBaseFormData",
        "GetCurrentProcessData",
    ),
    "Code/client/Services/Debug/Views/QuestDebugView.cpp": (
        "GetBaseFormData",
    ),
    "Code/server/Services/CharacterService.cpp": (
        "if (message.HasFaceTints)",
        "if (message.HasQuestContent)",
    ),
    "Code/client/Games/Skyrim/ExtraData/ExtraContainerChanges.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibContainerChangesOffsets = Skyrim::RuntimeLayout::ExtraContainerChangesCommonLibNgOffsets",
        "LocalContainerChangesOffsets = Skyrim::RuntimeLayout::ExtraContainerChangesLocalShimOffsets",
        "CommonLibSSE-NG names this InventoryEntryData",
        "CommonLibSSE-NG names this pointed-to object InventoryChanges",
        "GetEntryList",
        "FindEntryByFormId",
        "VisitInventory",
        "auto* pEntries = GetEntryList();",
        "const auto* pEntries = GetEntryList();",
        "static_assert(sizeof(ExtraContainerChanges) == ExtraContainerChanges::LocalContainerChangesOffsets::Size);",
        "static_assert(offsetof(ExtraContainerChanges, data) == ExtraContainerChanges::LocalContainerChangesOffsets::Data);",
        "static_assert(sizeof(ExtraContainerChanges::Entry) == ExtraContainerChanges::LocalContainerChangesOffsets::EntrySize);",
        "static_assert(offsetof(ExtraContainerChanges::Entry, form) == ExtraContainerChanges::LocalContainerChangesOffsets::EntryObject);",
        "static_assert(offsetof(ExtraContainerChanges::Entry, dataList) == ExtraContainerChanges::LocalContainerChangesOffsets::EntryExtraLists);",
        "static_assert(offsetof(ExtraContainerChanges::Entry, count) == ExtraContainerChanges::LocalContainerChangesOffsets::EntryCountDelta);",
        "static_assert(sizeof(ExtraContainerChanges::Data) == ExtraContainerChanges::LocalContainerChangesOffsets::DataSize);",
        "static_assert(offsetof(ExtraContainerChanges::Data, entries) == ExtraContainerChanges::LocalContainerChangesOffsets::DataEntries);",
        "static_assert(offsetof(ExtraContainerChanges::Data, parent) == ExtraContainerChanges::LocalContainerChangesOffsets::DataParent);",
        "static_assert(offsetof(ExtraContainerChanges::Data, totalWeight) == ExtraContainerChanges::LocalContainerChangesOffsets::DataTotalWeight);",
        "static_assert(offsetof(ExtraContainerChanges::Data, armorWeight) == ExtraContainerChanges::LocalContainerChangesOffsets::DataArmorWeight);",
        "static_assert(offsetof(ExtraContainerChanges::Data, unk18) == ExtraContainerChanges::LocalContainerChangesOffsets::DataUnk18);",
    ),
    "Code/client/Games/Skyrim/ExtraData/ExtraContainerChanges.cpp": (
        "Skyrim::RuntimeLayout::Value<TESForm*>(this, ExtraContainerChanges::CommonLibContainerChangesOffsets::EntryObject)",
        "Skyrim::RuntimeLayout::Value<GameList<ExtraDataList>*>",
        "ExtraContainerChanges::CommonLibContainerChangesOffsets::EntryExtraLists",
        "ExtraContainerChanges::CommonLibContainerChangesOffsets::EntryCountDelta",
        "ExtraContainerChanges::CommonLibContainerChangesOffsets::DataEntries",
    ),
    "Code/client/Games/Skyrim/Components/TESContainer.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibContainerOffsets = Skyrim::RuntimeLayout::TESContainerCommonLibNgOffsets",
        "LocalContainerOffsets = Skyrim::RuntimeLayout::TESContainerLocalShimOffsets",
        "CommonLibSSE-NG names this ContainerObject",
        "GetCountData",
        "GetFormData",
        "GetEntriesData",
        "GetEntryCountData",
        "static_assert(sizeof(TESContainer::Entry) == TESContainer::Entry::LocalEntryOffsets::EntrySize);",
        "static_assert(offsetof(TESContainer::Entry, count) == TESContainer::Entry::LocalEntryOffsets::EntryCount);",
        "static_assert(offsetof(TESContainer::Entry, form) == TESContainer::Entry::LocalEntryOffsets::EntryForm);",
        "static_assert(offsetof(TESContainer::Entry, data) == TESContainer::Entry::LocalEntryOffsets::EntryData);",
        "static_assert(sizeof(TESContainer) == TESContainer::LocalContainerOffsets::Size);",
        "static_assert(offsetof(TESContainer, entries) == TESContainer::LocalContainerOffsets::Entries);",
        "static_assert(offsetof(TESContainer, count) == TESContainer::LocalContainerOffsets::Count);",
    ),
    "Code/client/Games/Skyrim/Misc/InventoryEntry.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibInventoryEntryOffsets = Skyrim::RuntimeLayout::InventoryEntryCommonLibNgOffsets",
        "LocalInventoryEntryOffsets = Skyrim::RuntimeLayout::InventoryEntryLocalShimOffsets",
        "CommonLibSSE-NG names this InventoryEntryData",
        "GetObjectData",
        "GetExtraListsData",
        "GetCountData",
        "static_assert(sizeof(InventoryEntry) == InventoryEntry::LocalInventoryEntryOffsets::Size);",
        "static_assert(offsetof(InventoryEntry, pObject) == InventoryEntry::LocalInventoryEntryOffsets::Object);",
        "static_assert(offsetof(InventoryEntry, pExtraLists) == InventoryEntry::LocalInventoryEntryOffsets::ExtraLists);",
        "static_assert(offsetof(InventoryEntry, count) == InventoryEntry::LocalInventoryEntryOffsets::Count);",
    ),
    "Code/client/Games/Skyrim/ExtraData/ExtraDataList.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibExtraDataListOffsets = Skyrim::RuntimeLayout::ExtraDataListCommonLibNgOffsets",
        "LocalExtraDataListOffsets = Skyrim::RuntimeLayout::ExtraDataListLocalShimOffsets",
        "GetDataData",
        "GetBitfieldData",
        "GetLockData",
        "static_assert(sizeof(ExtraDataList::Bitfield) == ExtraDataList::LocalExtraDataListOffsets::BitfieldSize);",
        "static_assert(offsetof(ExtraDataList, data) == ExtraDataList::LocalExtraDataListOffsets::Data);",
        "static_assert(offsetof(ExtraDataList, bitfield) == ExtraDataList::LocalExtraDataListOffsets::Bitfield);",
        "static_assert(offsetof(ExtraDataList, lock) == ExtraDataList::LocalExtraDataListOffsets::Lock);",
    ),
    "Code/client/Games/Skyrim/ExtraData/ExtraDataList.cpp": (
        "GetDataData()",
        "GetBitfieldData()",
        "GetLockData()",
        "ExtraDataList::LocalExtraDataListOffsets::BitfieldSize",
    ),
    "Code/client/Games/Skyrim/ExtraData/ExtraTextDisplayData.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibTextDisplayOffsets = Skyrim::RuntimeLayout::ExtraTextDisplayDataCommonLibNgOffsets",
        "LocalTextDisplayOffsets = Skyrim::RuntimeLayout::ExtraTextDisplayDataLocalShimOffsets",
        "enum class DisplayDataType : int32_t",
        "GetDisplayNameData",
        "GetDisplayNameStringData",
        "SetDisplayNameData",
        "GetDisplayNameTextData",
        "GetOwnerQuestData",
        "GetOwnerInstanceData",
        "SetOwnerInstanceData",
        "GetTemperFactorData",
        "SetTemperFactorData",
        "GetCustomNameLengthData",
        "SetCustomNameLengthData",
        "static_assert(ExtraTextDisplayData::CommonLibTextDisplayOffsets::DisplayName == 0x10);",
        "static_assert(ExtraTextDisplayData::CommonLibTextDisplayOffsets::CustomNameLength == 0x30);",
        "static_assert(offsetof(ExtraTextDisplayData, DisplayName) == ExtraTextDisplayData::LocalTextDisplayOffsets::DisplayName);",
        "static_assert(offsetof(ExtraTextDisplayData, usCustomNameLength) == ExtraTextDisplayData::LocalTextDisplayOffsets::CustomNameLength);",
        "static_assert(sizeof(ExtraTextDisplayData) == ExtraTextDisplayData::LocalTextDisplayOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/AI/AIProcess.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibAIProcessOffsets = Skyrim::RuntimeLayout::AIProcessCommonLibNgOffsets",
        "LocalAIProcessOffsets = Skyrim::RuntimeLayout::AIProcessLocalShimOffsets",
        "GetMiddleProcessData",
        "static_assert(offsetof(AIProcess, middleProcess) == AIProcess::LocalAIProcessOffsets::MiddleProcess);",
    ),
    "Code/client/Games/Skyrim/Misc/MiddleProcess.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibMiddleProcessOffsets = Skyrim::RuntimeLayout::MiddleProcessCommonLibNgOffsets",
        "LocalMiddleProcessOffsets = Skyrim::RuntimeLayout::MiddleProcessLocalShimOffsets",
        "GetDirectionData",
        "SetDirectionData",
        "GetActiveEffectsData",
        "GetCommandingActorData",
        "SetCommandingActorData",
        "GetLeftEquippedObjectData",
        "GetRightEquippedObjectData",
        "GetBothHandsEquippedObjectData",
        "static_assert(offsetof(MiddleProcess, direction) == MiddleProcess::LocalMiddleProcessOffsets::Direction);",
        "static_assert(offsetof(MiddleProcess, ActiveEffects) == MiddleProcess::LocalMiddleProcessOffsets::ActiveEffects);",
        "static_assert(offsetof(MiddleProcess, commandingActor) == MiddleProcess::LocalMiddleProcessOffsets::CommandingActor);",
        "static_assert(offsetof(MiddleProcess, leftEquippedObject) == MiddleProcess::LocalMiddleProcessOffsets::LeftEquippedObject);",
        "static_assert(offsetof(MiddleProcess, rightEquippedObject) == MiddleProcess::LocalMiddleProcessOffsets::RightEquippedObject);",
        "static_assert(offsetof(MiddleProcess, bothHandsEquippedObject) == MiddleProcess::LocalMiddleProcessOffsets::BothHandsEquippedObject);",
    ),
    "Code/client/Games/Skyrim/Projectiles/Projectile.h": (
        "CommonLibProjectileOffsets = Skyrim::RuntimeLayout::ProjectileCommonLibNgOffsets",
        "LocalProjectileOffsets = Skyrim::RuntimeLayout::ProjectileLocalShimOffsets",
        "CommonLibLaunchDataOffsets = Skyrim::RuntimeLayout::ProjectileLaunchDataCommonLibNgOffsets",
        "LocalLaunchDataOffsets = Skyrim::RuntimeLayout::ProjectileLaunchDataLocalShimOffsets",
        "GetOriginData",
        "SetOriginData",
        "GetProjectileBaseData",
        "SetProjectileBaseData",
        "GetShooterData",
        "SetShooterData",
        "GetWeaponSourceData",
        "SetWeaponSourceData",
        "GetAmmoSourceData",
        "SetAmmoSourceData",
        "GetSpellData",
        "SetSpellData",
        "GetCastingSourceData",
        "SetCastingSourceData",
        "IsChainShatterData",
        "SetChainShatterData",
        "GetPowerData",
        "SetPowerData",
        "static_assert(sizeof(Projectile::LaunchData) == 0xA8);",
        "static_assert(offsetof(Projectile::LaunchData, origin) == 0x8);",
        "static_assert(offsetof(Projectile::LaunchData, projectileBase) == 0x20);",
        "static_assert(offsetof(Projectile::LaunchData, shooter) == 0x28);",
        "static_assert(offsetof(Projectile::LaunchData, angleZ) == 0x48);",
        "static_assert(offsetof(Projectile::LaunchData, angleX) == 0x4C);",
        "static_assert(offsetof(Projectile::LaunchData, unk50) == 0x50);",
        "static_assert(offsetof(Projectile::LaunchData, desiredTarget) == 0x58);",
        "static_assert(offsetof(Projectile::LaunchData, unk60) == 0x60);",
        "static_assert(offsetof(Projectile::LaunchData, unk64) == 0x64);",
        "static_assert(offsetof(Projectile::LaunchData, parentCell) == 0x68);",
        "static_assert(offsetof(Projectile::LaunchData, spell) == 0x70);",
        "static_assert(offsetof(Projectile::LaunchData, castingSource) == 0x78);",
        "static_assert(offsetof(Projectile::LaunchData, pad7C) == 0x7C);",
        "static_assert(offsetof(Projectile::LaunchData, enchantItem) == 0x80);",
        "static_assert(offsetof(Projectile::LaunchData, poison) == 0x88);",
        "static_assert(offsetof(Projectile::LaunchData, area) == 0x90);",
        "static_assert(offsetof(Projectile::LaunchData, power) == 0x94);",
        "static_assert(offsetof(Projectile::LaunchData, scale) == 0x98);",
        "static_assert(offsetof(Projectile::LaunchData, alwaysHit) == 0x9C);",
        "static_assert(offsetof(Projectile::LaunchData, chainShatter) == 0x9F);",
        "static_assert(offsetof(Projectile::LaunchData, useOrigin) == 0xA0);",
        "static_assert(offsetof(Projectile::LaunchData, deferInitialization) == 0xA1);",
        "static_assert(offsetof(Projectile::LaunchData, forceConeOfFire) == 0xA2);",
        "static_assert(Projectile::LaunchData::CommonLibLaunchDataOffsets::ProjectileBase == 0x20);",
        "static_assert(Projectile::LaunchData::CommonLibLaunchDataOffsets::Spell == 0x70);",
        "static_assert(Projectile::LaunchData::CommonLibLaunchDataOffsets::Power == 0x94);",
        "static_assert(Projectile::LaunchData::CommonLibLaunchDataOffsets::Size == 0xA8);",
        "static_assert(Projectile::CommonLibProjectileOffsets::Power == 0x188);",
    ),
    "Code/client/Games/Skyrim/Projectiles/Projectile.cpp": (
        "SetPowerData",
        "GetPowerData",
        "apLaunchData.GetPowerData()",
        "arData.GetSpellData()",
        "arData.GetShooterData()",
        "arData.GetOriginData()",
        "arData.GetProjectileBaseData()",
        "arData.GetWeaponSourceData()",
        "arData.GetAmmoSourceData()",
        "arData.GetParentCellData()",
        "arData.GetCastingSourceData()",
        "arData.IsChainShatterData()",
        "arData.DefersInitializationData()",
        "arData.ForcesConeOfFireData()",
        "pSpell->GetCastingTypeData()",
    ),
    "Code/client/Services/Generic/CombatService.cpp": (
        "launchData.SetParentCellData",
        "launchData.GetParentCellData",
        "launchData.SetShooterData",
        "launchData.SetOriginData",
        "launchData.SetProjectileBaseData",
        "launchData.SetWeaponSourceData",
        "launchData.SetAmmoSourceData",
        "launchData.SetAngleZData",
        "launchData.SetAngleXData",
        "launchData.SetSpellData",
        "launchData.SetCastingSourceData",
        "launchData.SetAreaData",
        "launchData.SetPowerData",
        "launchData.SetScaleData",
        "launchData.SetAlwaysHitData",
        "launchData.SetNoDamageOutsideCombatData",
        "launchData.SetAutoAimData",
        "launchData.SetForceConeOfFireData",
        "launchData.SetUseOriginData",
        "launchData.SetChainShatterData",
    ),
    "Code/client/Games/Skyrim/Interface/IMenu.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibIMenuOffsets = Skyrim::RuntimeLayout::IMenuCommonLibNgOffsets",
        "LocalIMenuOffsets = Skyrim::RuntimeLayout::IMenuLocalShimOffsets",
        "GetMenuFlagsData",
        "SetMenuFlagsData",
        "Skyrim::RuntimeLayout::Value<uint32_t>(this, CommonLibIMenuOffsets::MenuFlags)",
        "Skyrim::RuntimeLayout::Store<uint32_t>(this, CommonLibIMenuOffsets::MenuFlags, aFlags)",
        "PausesGame() const { return GetMenuFlagsData() & kPausesGame; }",
        "static_assert(offsetof(IMenu, IMenu::uiMenuFlags) == IMenu::LocalIMenuOffsets::MenuFlags);",
        "static_assert(offsetof(IMenu, IMenu::eInputContext) == IMenu::LocalIMenuOffsets::InputContext);",
        "static_assert(offsetof(IMenu, IMenu::fxDelegate) == IMenu::LocalIMenuOffsets::FxDelegate);",
        "static_assert(offsetof(IMenu, IMenu::unk30) == IMenu::LocalIMenuOffsets::VrUnk30);",
        "static_assert(offsetof(IMenu, IMenu::unk34) == IMenu::LocalIMenuOffsets::VrUnk34);",
        "static_assert(offsetof(IMenu, IMenu::unk38) == IMenu::LocalIMenuOffsets::VrUnk38);",
        "static_assert(sizeof(IMenu) == IMenu::LocalIMenuOffsets::VrSize);",
        "static_assert(sizeof(IMenu) == IMenu::LocalIMenuOffsets::SeSize);",
    ),
    "Code/client/Games/Skyrim/Interface/IMenu.cpp": (
        "SetMenuFlagsData(GetMenuFlagsData() | auiFlag);",
        "SetMenuFlagsData(GetMenuFlagsData() & ~auiFlag);",
    ),
    "Code/client/Games/Skyrim/Interface/UI.cpp": (
        "GetMenuFlagsData",
        "GetMenuStackData",
        "GetMenuMapData",
        "ProbeVrMenuOpen",
        "CommonLibUIOffsets::MenuMap == 0x128",
        "IMenu::CommonLibIMenuOffsets::MenuFlags == 0x1C",
        "IMenu::CommonLibIMenuOffsets::InputContext == 0x20",
        "IMenu::kOnStack == 0x40",
        "sizeof(MenuTable) == 0x30",
        "offsetof(MenuTable, m_size) == 0xC",
        "offsetof(MenuTable, m_entries) == 0x28",
        "offsetof(MenuTableEntry, next) == 0x18",
        "MenuOpenState::Unavailable",
    ),
    "Code/client/Games/Skyrim/Interface/UI.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibUIOffsets = Skyrim::RuntimeLayout::UICommonLibNgOffsets",
        "LocalUIOffsets = Skyrim::RuntimeLayout::UILocalShimOffsets",
        "GetMenuStackData",
        "Skyrim::RuntimeLayout::Ref<GameArray<IMenu*>>(this, CommonLibUIOffsets::MenuStack)",
        "GetMenuMapData",
        "MenuOpenState GetMenuOpen",
        "Skyrim::RuntimeLayout::Ref<creation::BSTHashMap<BSFixedString, UIMenuEntry>>",
        "static_assert(sizeof(UI) == UI::LocalUIOffsets::Size);",
        "static_assert(offsetof(UI, UI::menuStack) == UI::LocalUIOffsets::MenuStack);",
        "static_assert(offsetof(UI, UI::menuMap) == UI::LocalUIOffsets::MenuMap);",
        "static_assert(offsetof(UI, UI::numPausesGame) == UI::LocalUIOffsets::NumPausesGame);",
    ),
    "Code/client/Games/Skyrim/VR/VRMenuOpenProbe.h": (
        "enum class MenuOpenState",
        "Unavailable",
        "kMaximumMenuCapacity = 4096",
        "acMenuTable.m_freeCount > capacity",
        "acMenuTable.m_freeOffset > capacity",
        "aIsReadable(acMenuTable.m_entries",
        "entry.key.data != apName",
        "aIsReadable(pMenu, aReadableMenuSize)",
        "aReadFlags(pMenu) & aOnStackMask",
    ),
    "Code/client/Games/Skyrim/Interface/MenuControls.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibMenuControlsOffsets = Skyrim::RuntimeLayout::MenuControlsCommonLibNgOffsets",
        "LocalMenuControlsOffsets = Skyrim::RuntimeLayout::MenuControlsLocalShimOffsets",
        "SetToggleData",
        "Skyrim::RuntimeLayout::Store<uint8_t>(this, CommonLibMenuControlsOffsets::Toggle",
        "static_assert(offsetof(MenuControls, isProcessing) == MenuControls::LocalMenuControlsOffsets::IsProcessing);",
        "static_assert(offsetof(MenuControls, beastForm) == MenuControls::LocalMenuControlsOffsets::BeastForm);",
        "static_assert(offsetof(MenuControls, remapMode) == MenuControls::LocalMenuControlsOffsets::RemapMode);",
        "static_assert(offsetof(MenuControls, unk83) == MenuControls::LocalMenuControlsOffsets::Toggle);",
        "static_assert(offsetof(MenuControls, unk84) == MenuControls::LocalMenuControlsOffsets::Unk84);",
        "static_assert(sizeof(MenuControls) == MenuControls::LocalMenuControlsOffsets::Size);",
    ),
    "Code/client/Games/Skyrim/Interface/MenuControls.cpp": (
        "SetToggleData(b);",
    ),
    "Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.h": (
        "#include <RuntimeLayout.h>",
        "CommonLibRendererDataOffsets = Skyrim::RuntimeLayout::BSGraphicsRendererDataCommonLibNgOffsets",
        "LocalRendererDataOffsets = Skyrim::RuntimeLayout::BSGraphicsRendererDataLocalShimOffsets",
        "GetForwarderData",
        "GetContextData",
        "GetRenderWindowData",
        "GetRenderTargetsData",
        "GetDepthStencilTargetsData",
        "GetCubeMapRenderTargetsData",
        "CommonLibRendererOffsets = Skyrim::RuntimeLayout::BSGraphicsRendererCommonLibNgOffsets",
        "LocalRendererOffsets = Skyrim::RuntimeLayout::BSGraphicsRendererLocalShimOffsets",
        "GetRendererData",
        "static_assert(offsetof(RendererData, pForwarder) == RendererData::LocalRendererDataOffsets::Forwarder);",
        "static_assert(offsetof(RendererData, pContext) == RendererData::LocalRendererDataOffsets::Context);",
        "static_assert(offsetof(RendererData, RenderWindowA) == RendererData::LocalRendererDataOffsets::RenderWindows);",
        "static_assert(offsetof(RendererData, pRenderTargetsA) == RendererData::LocalRendererDataOffsets::RenderTargets);",
        "static_assert(offsetof(RendererData, pDepthStencilTargetsA) == RendererData::LocalRendererDataOffsets::DepthStencilTargets);",
        "static_assert(offsetof(RendererData, pCubeMapRenderTargetsA) == RendererData::LocalRendererDataOffsets::CubeMapRenderTargets);",
        "static_assert(offsetof(Renderer, Data) == Renderer::LocalRendererOffsets::Data);",
    ),
    "Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.cpp": (
        "auto& renderer = self->GetRendererData();",
        "g_RenderWindow = renderer.GetRenderWindowData();",
        "renderer.GetRenderWindowData()->pSwapChain",
        "renderer.GetForwarderData()",
        "renderer.GetContextData()",
    ),
}

FORBIDDEN_LAYOUT_TOKENS = {
    "Code/client/Games/Skyrim/Events/EventDispatcher.h": (
        "TESObjectREFR* object;",
        "TESObjectREFR* hit;",
        "TESObjectREFR* hitter;",
        "uint32_t uiMagicEffectFormID;",
        "static_assert(sizeof(EventDispatcherManager) == 4752);",
        "static_assert(offsetof(EventDispatcherManager, activateEvent) == 88);",
        "static_assert(offsetof(EventDispatcherManager, fastTravelEndEvent) == 4664);",
    ),
    "Code/client/Games/Skyrim/Forms/TESForm.h": (
        "static_assert(sizeof(TESForm) == 0x20);",
        "return (flags & IGNORE_FRIENDLY_HITS) != 0;",
        "bool IsDisabled() const noexcept { return (flags & DISABLED) != 0; }",
        "bool IsTemporary() const noexcept { return formID >= 0xFF000000; }",
        "return formType == FormType::Ingredient || formType == FormType::Alchemy;",
    ),
    "Code/client/Games/Skyrim/Forms/TESGlobal.h": (
        "static_assert(offsetof(TESGlobal, f) == 0x34);",
    ),
    "Code/client/Games/Skyrim/Components/TESActorBaseData.h": (
        "bool IsEssential() const noexcept { return flags & BaseFlags::IS_ESSENTIAL; }",
        "flags |= BaseFlags::IS_ESSENTIAL;",
        "flags &= ~BaseFlags::IS_ESSENTIAL;",
        "static_assert(offsetof(TESActorBaseData, owner) == 0x30);",
        "static_assert(offsetof(TESActorBaseData, factions) == 0x40);",
    ),
    "Code/client/Games/Skyrim/Forms/MagicItem.cpp": (
        "keyword.count",
        "keyword.Contains",
        "listOfEffects",
        "pSpell->formID",
        "if (formID == bosFormID)",
        "switch (formID)",
        "pEffectSetting->formID == aEffectId",
        "pEffectSetting->keywordForm",
        "pEffectSetting->eArchetype",
    ),
    "Code/client/Games/Skyrim/Actor.cpp": (
        "pPowerOrShout->formType",
        "apThis->formType",
        "pValueModEffect->actorValueIndex",
    ),
    "Code/client/Games/Skyrim/Effects/InvisibilityEffect.cpp": (
        "apThis->pTarget",
    ),
    "Code/client/Games/Skyrim/EquipManager.cpp": (
        "apItem->formType",
        "apData->count",
        "apData->pSlot",
        "apData->pEquipSlot",
        "apData->bQueueEquip",
        "apData->bForceEquip",
        "apData->bPlaySound",
        "apData->bApplyNow",
        "apData->pExtraDataList",
        "apData->pSlotToReplace",
    ),
    "Code/client/Games/Skyrim/Magic/EffectItem.cpp": (
        "pEffectSetting->eArchetype",
        "pEffectSetting->keywordForm",
        "pEffectSetting->fullName.value",
    ),
    "Code/client/Services/Generic/QuestService.cpp": (
        "pQuest->fullName.value",
        "pQuest->type",
        "pQuest->currentStage",
        "pQuest->priority",
    ),
    "Code/client/Services/Generic/CharacterService.cpp": (
        "pQuest->currentStage",
    ),
    "Code/client/Services/Generic/DiscordService.cpp": (
        "pWorldspace->fullName.value",
    ),
    "Code/client/Services/Generic/TransportService.cpp": (
        "pNpc->fullName.value",
    ),
    "Code/client/Services/Debug/Views/QuestDebugView.cpp": (
        "pQuest->fullName.value",
    ),
    "Code/client/Services/Debug/Views/FormDebugView.cpp": (
        "pEffect->pSpell",
        "pEffect->fElapsedSeconds",
        "pEffect->fDuration",
        "pEffect->fMagnitude",
        "pEffect->uiFlags",
    ),
    "Code/client/Games/Skyrim/Forms/TESNPC.cpp": (
        "fullName.value",
    ),
    "Code/client/Games/Skyrim/Actor.cpp": (
        "pNpc->fullName.value",
    ),
    "Code/client/Games/Skyrim/Forms/TESNPC.h": (
        "static_assert(offsetof(TESNPC, npcClass) == 0x1C0);",
        "static_assert(offsetof(TESNPC, color) == 0x246);",
        "static_assert(offsetof(TESNPC, relationships) == 0x250);",
    ),
    "Code/client/Games/Skyrim/Forms/TESObjectARMO.h": (
        "return (slotType & 0x4) != 0;",
        "slotType & 0x4",
    ),
    "Code/client/Games/Forms.cpp": (
        "loadBuffer.formId = formID;",
        "reinterpret_cast<uint8_t*>(&flags)",
        "reinterpret_cast<uint8_t*>(&unk10)",
        "unk10 = 0xFFFF;",
        "pUnk->unk330, formID, changeFlags",
        "flags |= 0x200000;",
        "pPlayerBaseForm->npcClass",
        "pPlayerBaseForm->combatStyle",
        "pPlayerBaseForm->raceForm.race",
        "pPlayerBaseForm->outfits[0]",
        "pNpc->overlayRace = nullptr",
        "headparts[i]",
        "headpartsCount",
    ),
    "Code/client/Services/Generic/VRActivationService.cpp": (
        "acEvent.object",
        "activateEvent.RegisterSink",
    ),
    "Code/client/Services/Generic/ObjectService.cpp": (
        "acEvent->object",
        "activateEvent.RegisterSink",
    ),
    "Code/client/Services/Generic/VRCombatService.cpp": (
        "acEvent.hitter",
        "acEvent.hit",
        "hitEvent.RegisterSink",
    ),
    "Code/client/Services/Generic/VRMagicService.cpp": (
        "acEvent.hCaster",
        "acEvent.hTarget",
        "acEvent.uiMagicEffectFormID",
        "magicEffectApplyEvent.RegisterSink",
    ),
    "Code/client/Services/Generic/VRProjectileService.cpp": (
        "acEvent.object.object",
        "acEvent.weapon",
        "acEvent.ammo",
        "acEvent.shotPower",
        "acEvent.isSunGazing",
        "acEvent.spell",
        "playerBowShotEvent.RegisterSink",
        "spellCastEvent.RegisterSink",
    ),
    "Code/client/Services/Generic/VRSaveLoadService.cpp": (
        "loadGameEvent.RegisterSink",
    ),
    "Code/client/Services/Generic/DiscoveryService.cpp": (
        "loadGameEvent.RegisterSink",
        "pTES->centerGridX",
        "pTES->centerGridY",
        "pTES->currentGridX",
        "pTES->currentGridY",
    ),
    "Code/client/Services/Generic/CharacterService.cpp": (
        "pTES->centerGridX",
        "pTES->centerGridY",
    ),
    "Code/client/Services/Debug/Views/PlayerDebugView.cpp": (
        "pTES->centerGridX",
        "pTES->centerGridY",
        "pTES->currentGridX",
        "pTES->currentGridY",
    ),
    "Code/client/Services/Debug/DebugService.cpp": (
        "TES::Get()->activeImageSpaceModifiers",
    ),
    "Code/client/Games/Skyrim/PlayerCharacter.cpp": (
        "pTes->centerGridX",
        "pTes->centerGridY",
    ),
    "Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.h": (
        "static_assert(offsetof(RendererData, pForwarder) == 0x38);",
        "static_assert(offsetof(RendererData, RenderWindowA) == 0x48);",
        "static_assert(offsetof(RendererData, pRenderTargetsA) == 0xA48);",
        "static_assert(offsetof(RendererData, pDepthStencilTargetsA) == 0x1FA8);",
        "static_assert(offsetof(RendererData, pCubeMapRenderTargetsA) == 0x26C8);",
    ),
    "Code/client/Games/Skyrim/Combat/CombatController.h": (
        "static_assert(offsetof(CombatController, targetHandle) == 0x2C);",
        "static_assert(offsetof(CombatController, startedCombat) == 0x35);",
        "static_assert(offsetof(CombatController, targetSelectors) == 0xA0);",
        "static_assert(offsetof(CombatController, pActiveTargetSelector) == 0xB8);",
        "static_assert(sizeof(CombatController) == 0xE0);",
    ),
    "Code/client/Games/Skyrim/Combat/CombatGroup.h": (
        "static_assert(sizeof(CombatTarget) == 0xA8);",
        "static_assert(sizeof(CombatGroup) == 0x168);",
    ),
    "Code/client/Games/Skyrim/Combat/CombatInventory.h": (
        "static_assert(sizeof(CombatInventory) == 0x1C8);",
    ),
    "Code/client/Games/Skyrim/Combat/CombatTargetSelector.h": (
        "static_assert(offsetof(CombatTargetSelector, ePriority) == 0x1C);",
        "static_assert(sizeof(CombatTargetSelector) == 0x28);",
        "static_assert(sizeof(CombatTargetSelectorStandard) == 0x30);",
    ),
    "Code/client/Games/Skyrim/ExtraData/ExtraContainerChanges.h": (
        "uint8_t pad[8];",
        "static_assert(sizeof(ExtraContainerChanges) == 0x18);",
        "static_assert(offsetof(ExtraContainerChanges, data) == 0x10);",
        "static_assert(sizeof(ExtraContainerChanges::Entry) == 0x18);",
        "static_assert(offsetof(ExtraContainerChanges::Entry, form) == 0x0);",
        "static_assert(offsetof(ExtraContainerChanges::Entry, dataList) == 0x8);",
        "static_assert(offsetof(ExtraContainerChanges::Entry, count) == 0x10);",
        "static_assert(sizeof(ExtraContainerChanges::Data) == 0x20);",
        "static_assert(offsetof(ExtraContainerChanges::Data, entries) == 0x0);",
        "static_assert(offsetof(ExtraContainerChanges::Data, parent) == 0x8);",
        "static_assert(offsetof(ExtraContainerChanges::Data, totalWeight) == 0x10);",
        "static_assert(offsetof(ExtraContainerChanges::Data, armorWeight) == 0x14);",
        "static_assert(offsetof(ExtraContainerChanges::Data, unk18) == 0x18);",
    ),
    "Code/client/Games/Skyrim/Components/TESContainer.h": (
        "static_assert(sizeof(TESContainer::Entry) == 0x18);",
        "static_assert(offsetof(TESContainer::Entry, count) == 0x0);",
        "static_assert(offsetof(TESContainer::Entry, form) == 0x8);",
        "static_assert(offsetof(TESContainer::Entry, data) == 0x10);",
        "static_assert(sizeof(TESContainer) == 0x18);",
        "static_assert(offsetof(TESContainer, entries) == 0x8);",
        "static_assert(offsetof(TESContainer, count) == 0x10);",
    ),
    "Code/client/Games/Skyrim/Misc/InventoryEntry.h": (
        "static_assert(sizeof(InventoryEntry) == 0x18);",
        "static_assert(offsetof(InventoryEntry, pObject) == 0x0);",
        "static_assert(offsetof(InventoryEntry, pExtraLists) == 0x8);",
        "static_assert(offsetof(InventoryEntry, count) == 0x10);",
    ),
    "Code/client/Games/Skyrim/ExtraData/ExtraDataList.h": (
        "static_assert(sizeof(ExtraDataList::Bitfield) == 0x18);",
        "static_assert(offsetof(ExtraDataList, data) == 0x8);",
        "static_assert(offsetof(ExtraDataList, bitfield) == 0x10);",
        "static_assert(offsetof(ExtraDataList, lock) == 0x18);",
    ),
    "Code/client/Games/Skyrim/Projectiles/Projectile.h": (
        "float fYAngle;",
        "uint8_t unk54[0x14];",
        "bool bUnkBool1;",
        "uint8_t unk7D[0xB];",
    ),
    "Code/client/Games/Skyrim/Interface/MenuControls.h": (
        "static_assert(sizeof(MenuControls) == 0x90);",
        "static_assert(offsetof(MenuControls, isProcessing) == 0x80);",
        "static_assert(offsetof(MenuControls, beastForm) == 0x81);",
        "static_assert(offsetof(MenuControls, remapMode) == 0x82);",
        "static_assert(offsetof(MenuControls, unk83) == 0x83);",
        "static_assert(offsetof(MenuControls, unk84) == 0x84);",
        "static_assert(sizeof(MenuControls) == 0x88);",
        "bool canBeOpened;",
    ),
    "Code/client/Games/Skyrim/Interface/IMenu.h": (
        "static_assert(offsetof(IMenu, IMenu::uiMenuFlags) == 0x1C);",
        "static_assert(offsetof(IMenu, IMenu::eInputContext) == 0x20);",
        "static_assert(offsetof(IMenu, IMenu::fxDelegate) == 0x28);",
        "static_assert(offsetof(IMenu, IMenu::unk30) == 0x30);",
        "static_assert(offsetof(IMenu, IMenu::unk34) == 0x34);",
        "static_assert(offsetof(IMenu, IMenu::unk38) == 0x38);",
        "static_assert(sizeof(IMenu) == 0x40);",
        "static_assert(sizeof(IMenu) == 0x30);",
    ),
    "Code/client/Games/Skyrim/Interface/UI.h": (
        "static_assert(offsetof(UI, UI::menuStack) == 0x110);",
        "static_assert(offsetof(UI, UI::menuMap) == 0x128);",
        "static_assert(offsetof(UI, UI::numPausesGame) == 0x160);",
    ),
    "Code/client/Games/Skyrim/TESObjectREFR.h": (
        "CommonLibSseNgObjectReferenceOffset",
        "CommonLibSseNgRotationOffset",
        "CommonLibSseNgPositionOffset",
        "CommonLibSseNgParentCellOffset",
        "CommonLibSseNgLoadedDataOffset",
        "CommonLibSseNgExtraDataListOffset",
    ),
    "Code/client/Games/Skyrim/Forms/TESObjectCELL.h": (
        "CommonLibSseNgCellFlagsOffset",
        "CommonLibSseNgReferencesOffset",
        "CommonLibSseNgWorldSpaceOffset",
        "CommonLibSseNgLoadedCellDataOffset",
    ),
    "Code/client/Games/Skyrim/Actor.h": (
        "CommonLibSseNgMagicTargetOffset",
        "CommonLibSseNgActorValueOwnerOffset",
        "CommonLibSseNgActorStateOffset",
        "CommonLibSseNgBoolBitsOffset",
        "CommonLibSseNgCurrentProcessOffset",
        "CommonLibSseNgCombatControllerOffset",
        "CommonLibSseNgSelectedSpellsOffset",
        "CommonLibSseNgSelectedPowerOffset",
        "CommonLibSseNgRaceOffset",
        "CommonLibSseNgBoolFlagsOffset",
    ),
    "Code/client/Games/Skyrim/PlayerCharacter.h": (
        "CommonLibSseNgVrInfoRuntimeDataOffset",
        "CommonLibSseNgVrSkillsOffset",
        "CommonLibSseNgVrGameStateDataOffset",
        "VRNodeDataOffset",
    ),
    "Code/client/Games/Skyrim/PlayerCharacter.cpp": (
        "CommonLibSseNgVrSkillsOffset",
        "CommonLibSseNgVrGameStateDataOffset",
    ),
    "Code/client/Games/Skyrim/Projectiles/Projectile.h": (
        "CommonLibSseNgPowerOffset",
    ),
    "Code/client/Games/Skyrim/AI/AIProcess.h": (
        "static_assert(offsetof(AIProcess, middleProcess) == 0x8);",
    ),
    "Code/client/Games/Skyrim/Misc/MiddleProcess.h": (
        "static_assert(offsetof(MiddleProcess, direction) == 0xB8);",
        "static_assert(offsetof(MiddleProcess, ActiveEffects) == 0x1A0);",
        "static_assert(offsetof(MiddleProcess, commandingActor) == 0x218);",
        "static_assert(offsetof(MiddleProcess, leftEquippedObject) == 0x220);",
        "static_assert(offsetof(MiddleProcess, rightEquippedObject) == 0x260);",
    ),
    "Code/client/Games/Skyrim/Misc/BSScript.h": (
        "virtual void BindNativeMethod(IFunction* apFunction);",
        "static_assert(sizeof(IVirtualMachine) == 0x10);",
    ),
    "Code/client/Games/Misc/BSScript.cpp": (
        "(*apThis)->BindNativeMethod(new BSScript::",
    ),
}

REQUIRED_VR_COMMONLIB_ACCESSOR_TOKENS = {
    "Code/client/Games/Skyrim/Actor.cpp": (
        "using TGetLocation = TESForm*(const TESObjectREFR*);",
        "static VersionDbPtr<TGetLocation> getActorLocation(19385);",
        "return pGetActorLocation ? pGetActorLocation(this) : nullptr;",
    ),
    "Code/client/Services/Generic/VRMovementService.cpp": (
        "GetBaseFormData",
        "GetParentCellData",
        "GetPositionData",
        "GetRotationData",
        "pWorldSpace->GetFormIdData()",
    ),
    "Code/client/Services/Generic/VRInventoryService.cpp": (
        "GetBaseFormData",
        "GetParentCellData",
        "GetActorStateData",
        "GetSelectedSpellData",
        "GetSelectedPowerOrShoutData",
        "apForm->GetFormIdData()",
    ),
    "Code/client/Services/Generic/VRActivationService.cpp": (
        "GetBaseFormData",
        "GetParentCellEx",
        "GetWorldSpace",
        "GetPositionData",
        "pObject->GetFormIdData()",
        "pCell->GetFormIdData()",
        "pWorldSpace->GetFormIdData()",
        "pBaseForm->GetFormTypeData()",
    ),
    "Code/client/Services/Generic/VRMagicService.cpp": (
        "GetBaseFormData",
        "GetPositionData",
        "pCaster->GetFormIdData()",
        "pTarget->GetFormIdData()",
        "pBaseForm->GetFormTypeData()",
    ),
    "Code/client/Services/Generic/VRCombatService.cpp": (
        "GetBaseFormData",
        "GetPositionData",
        "pHitter->GetFormIdData()",
        "pHittee->GetFormIdData()",
        "pBaseForm->GetFormTypeData()",
    ),
    "Code/client/Services/Generic/VRProjectileService.cpp": (
        "pPlayer->GetFormIdData()",
    ),
    "Code/client/Services/Generic/VRConnectionService.cpp": (
        "VRLifecycleService",
        "IsReady()",
    ),
    "Code/client/Services/Generic/VRLifecycleService.cpp": (
        "TryGetReadablePlayerForVR",
        "GetBaseFormData",
        "GetParentCellData",
        "GetFormIdData",
        "MenuOpenState::Unavailable",
        "MenuOpenState::Open",
        'return "ui_unavailable";',
    ),
    "Code/tests/vr_menu_open_probe.cpp": (
        "VR menu probe distinguishes closed and open menus",
        "VR menu probe fails closed for invalid state",
        "VR menu probe treats a registered null menu as closed",
    ),
    "Code/client/Services/Generic/VRSaveLoadService.cpp": (
        "GetBaseFormData",
        "GetParentCellData",
        "GetParentCellEx",
        "apForm->GetFormIdData()",
    ),
    "Code/client/Services/Generic/TransportService.cpp": (
        "GetBaseFormData",
        "GetParentCellData",
        "GetGameYearData",
        "GetGameMonthData",
        "GetGameDayData",
        "GetGameHourData",
        "GetTimeScaleData",
        "GetValueData",
    ),
    "Code/client/Services/Generic/DiscoveryService.cpp": (
        "GetCurrentLocation",
        "GetParentCellEx",
    ),
    "Code/client/Services/Generic/DiscordService.cpp": (
        "GetCurrentLocation",
    ),
    "Code/client/Services/Generic/PlayerService.cpp": (
        "GetActorStateData",
        "GetSelectedSpellData",
        "GetSelectedPowerOrShoutData",
        "GetCurrentProcessData",
        "GetRaceData",
        "GetPositionData",
        "SetValueData",
    ),
    "Code/client/Services/Generic/PartyService.cpp": (
        "SetValueData",
    ),
    "Code/client/Services/Generic/CalendarService.cpp": (
        "GetGameYearData",
        "GetGameMonthData",
        "GetGameDayData",
        "GetGameHourData",
        "GetGameDaysPassedData",
        "GetTimeScaleData",
        "GetValueData",
        "SetValueData",
    ),
    "Code/client/Services/Debug/Views/CalendarDebugView.cpp": (
        "GetGameYearData",
        "GetGameMonthData",
        "GetGameDayData",
        "GetGameHourData",
        "GetGameDaysPassedData",
        "GetTimeScaleData",
        "GetValueData",
    ),
    "Code/client/Games/Skyrim/Actor.cpp": (
        "GetMagicTargetData",
        "GetActorValueOwnerData",
        "GetActorBaseData",
        "GetFactionsData",
        "GetDefaultOutfitData",
        "GetOutfitItemsData",
        "GetFormTypeData() == FormType::Armor",
        "GetFormTypeData() == FormType::LeveledItem",
        "pPowerOrShout->GetFormTypeData()",
        "apThis->GetFormTypeData() != Actor::Type",
    ),
    "Code/client/Games/Skyrim/TESObjectREFR.cpp": (
        "GetFormTypeData() == FormType::Armor",
    ),
    "Code/client/Games/Skyrim/EquipManager.cpp": (
        "apItem->GetFormTypeData() == FormType::Ammo",
        "LocalEquipDataOffsets = Skyrim::RuntimeLayout::EquipDataLocalShimOffsets",
        "LocalMagicEquipDataOffsets = Skyrim::RuntimeLayout::MagicEquipDataLocalShimOffsets",
        "LocalShoutEquipDataOffsets = Skyrim::RuntimeLayout::ShoutEquipDataLocalShimOffsets",
        "GetExtraDataListData",
        "GetCountData",
        "GetSlotData",
        "GetSlotToReplaceData",
        "IsQueueEquipData",
        "IsForceEquipData",
        "PlaysSoundData",
        "AppliesNowData",
        "GetEquipSlotData",
        "GetUnk1Data",
        "GetUnk2Data",
        "evt.Count = apData->GetCountData();",
        "auto* pSlot = apData->GetSlotData();",
        "auto* pEquipSlot = apData->GetEquipSlotData();",
        "static_assert(EquipData::LocalEquipDataOffsets::Count == 0x8);",
        "static_assert(EquipData::LocalEquipDataOffsets::Slot == 0x10);",
        "static_assert(EquipData::LocalEquipDataOffsets::QueueEquip == 0x20);",
        "static_assert(EquipData::LocalEquipDataOffsets::Size == 0x28);",
        "static_assert(MagicEquipData::LocalMagicEquipDataOffsets::EquipSlot == 0x0);",
        "static_assert(MagicEquipData::LocalMagicEquipDataOffsets::QueueEquip == 0x8);",
        "static_assert(MagicEquipData::LocalMagicEquipDataOffsets::Size == 0x10);",
        "static_assert(ShoutEquipData::LocalShoutEquipDataOffsets::Unk1 == 0x0);",
        "static_assert(ShoutEquipData::LocalShoutEquipDataOffsets::Unk2 == 0x8);",
        "static_assert(ShoutEquipData::LocalShoutEquipDataOffsets::Size == 0x10);",
    ),
    "Code/client/Services/Generic/InventoryService.cpp": (
        "GetFormTypeData() == FormType::Armor",
    ),
    "Code/client/Services/Generic/MagicService.cpp": (
        "GetMagicTargetData",
        "GetSelectedSpellData",
        "GetBaseFormData",
        "data.SetSpellData(pSpell)",
        "data.SetEffectItemData",
        "data.SetMagnitudeData",
        "data.SetUnk40Data",
        "data.SetCastingSourceData",
    ),
    "Code/client/Games/Skyrim/Sky/Sky.cpp": (
        "apWeather->GetFormIdData()",
    ),
    "Code/client/Services/Debug/Views/AnimDebugView.cpp": (
        "pActor->GetFormIdData()",
    ),
    "Code/client/Services/Debug/Views/CellView.cpp": (
        "pWorldSpace->GetFormIdData()",
        "pCell->GetFormIdData()",
    ),
    "Code/client/Services/Debug/Views/CombatView.cpp": (
        "pTarget->GetFormIdData()",
    ),
    "Code/client/Services/Debug/Views/ContainerDebugView.cpp": (
        "actorId = pActor->GetFormIdData()",
    ),
    "Code/client/Services/Debug/Views/EntitiesView.cpp": (
        "owner ? owner->GetFormIdData() : 0x0",
        "pPlayer->GetParentCellData()->GetFormIdData()",
    ),
    "Code/client/Services/Debug/Views/FormDebugView.cpp": (
        "pParentCell->GetFormIdData()",
        "pRefr->GetFormIdData()",
        "pEffect->GetSpellData()",
        "pEffect->GetElapsedSecondsData()",
        "pEffect->GetDurationData()",
        "pEffect->GetMagnitudeData()",
        "pEffect->GetFlagsData()",
        "pEffect->SetElapsedSecondsData",
    ),
    "Code/client/Services/Debug/Views/PlayerDebugView.cpp": (
        "pLeftWeapon->GetFormIdData()",
        "pLeftSpell->GetFormIdData()",
        "pSelectedPower->GetFormIdData()",
        "playerParentCell->GetFormIdData()",
    ),
    "Code/client/Services/Debug/Views/QuestDebugView.cpp": (
        "pQuest->GetFormIdData()",
        "pActor->GetFormIdData()",
        "pPlayer->GetParentCellData()->GetFormIdData()",
    ),
    "Code/client/Services/Debug/Views/WeatherView.cpp": (
        "pCurrentWeather->GetFormIdData()",
    ),
    "Code/client/Services/Debug/Views/ActorValuesView.cpp": (
        "GetActorValueOwnerData",
    ),
}

FORBIDDEN_VR_RAW_MEMBER_TOKENS = {
    "Code/client/Services/Generic/VRMovementService.cpp": (
        "pPlayer->baseForm",
        "pPlayer->parentCell",
        "pPlayer->position",
        "pPlayer->rotation",
        "pWorldSpace->formID",
    ),
    "Code/client/Services/Generic/VRInventoryService.cpp": (
        "pPlayer->baseForm",
        "pPlayer->parentCell",
        "pPlayer->actorState",
        "pPlayer->magicItems",
        "pPlayer->equippedShout",
        "apForm->formID",
        "pPlayer->formID",
    ),
    "Code/client/Services/Generic/VRActivationService.cpp": (
        "pObject->baseForm",
        "pObject->position",
        "pObject->formID",
        "pCell->formID",
        "pWorldSpace->formID",
    ),
    "Code/client/Services/Generic/VRMagicService.cpp": (
        "apReference->baseForm",
        "apReference->position",
        "pCaster->formID",
        "pTarget->formID",
    ),
    "Code/client/Services/Generic/VRCombatService.cpp": (
        "apReference->baseForm",
        "apReference->position",
        "pHitter->formID",
        "pHittee->formID",
    ),
    "Code/client/Services/Generic/VRProjectileService.cpp": (
        "pPlayer->formID",
    ),
    "Code/client/Services/Generic/VRConnectionService.cpp": (
        "pPlayer->baseForm",
        "pPlayer->parentCell",
    ),
    "Code/client/Services/Generic/VRSaveLoadService.cpp": (
        "pPlayer->baseForm",
        "pPlayer->parentCell",
        "pPlayer->GetParentCell()",
        "pPlayer->formID",
        "pCell->formID",
        "pWorldSpace->formID",
    ),
    "Code/client/Services/Generic/TransportService.cpp": (
        "pPlayer->baseForm",
        "pPlayer->parentCell",
        "pGameTime->TimeScale",
        "pGameTime->GameHour",
        "pGameTime->GameYear",
        "pGameTime->GameMonth",
        "pGameTime->GameDay",
        "pGameTime->TimeScale->f",
        "pGameTime->GameHour->f",
        "pGameTime->GameYear->f",
        "pGameTime->GameMonth->f",
        "pGameTime->GameDay->f",
    ),
    "Code/client/Services/Generic/DiscoveryService.cpp": (
        "pPlayer->locationForm",
        "pPlayer->parentCell",
        "pPlayer->GetParentCell()",
    ),
    "Code/client/Services/Generic/DiscordService.cpp": (
        "pPlayer->locationForm",
    ),
    "Code/client/Services/Generic/PlayerService.cpp": (
        "pPlayer->actorState",
        "pPlayer->magicItems",
        "pPlayer->equippedShout",
        "pPlayer->currentProcess",
        "pPlayer->race",
        "pPlayer->position",
        "pKillMove->f",
        "pWorldEncountersEnabled->f",
    ),
    "Code/client/Services/Generic/PartyService.cpp": (
        "pWorldEncountersEnabled->f",
    ),
    "Code/client/Services/Generic/CalendarService.cpp": (
        "pGameTime->GameDay",
        "pGameTime->GameMonth",
        "pGameTime->GameYear",
        "pGameTime->GameHour",
        "pGameTime->GameDaysPassed",
        "pGameTime->TimeScale",
        "pGameTime->GameDay->f",
        "pGameTime->GameMonth->f",
        "pGameTime->GameYear->f",
        "pGameTime->GameHour->f",
        "pGameTime->GameDaysPassed->f",
        "pGameTime->TimeScale->f",
    ),
    "Code/client/Services/Debug/Views/CalendarDebugView.cpp": (
        "pGameTime->GameDay",
        "pGameTime->GameMonth",
        "pGameTime->GameYear",
        "pGameTime->GameHour",
        "pGameTime->GameDaysPassed",
        "pGameTime->TimeScale",
        "pGameTime->GameDay->f",
        "pGameTime->GameMonth->f",
        "pGameTime->GameYear->f",
        "pGameTime->GameHour->f",
        "pGameTime->GameDaysPassed->f",
        "pGameTime->TimeScale->f",
    ),
    "Code/client/Games/Skyrim/Actor.cpp": (
        "pPlayer->parentCell",
        "pPlayer->position",
        "pPlayer->rotation",
        "pCell->cellFlags",
        "Cast<TESNPC>(baseForm)",
        "apThis->baseForm",
        "apObject->baseForm",
        "magicTarget.",
        "actorValueOwner.",
        "return race &&",
        "currentProcess",
        "magicItems",
        "equippedShout",
        "extraData",
        "flags1",
        "flags2",
        "casters[",
        "->middleProcess",
        "middleProcess->",
        "leftEquippedObject",
        "rightEquippedObject",
        "ammoEquippedObject",
        "commandingActor",
        "pLeftEquippedObject->pObject",
        "pRightEquippedObject->pObject",
        "pAmmoEquippedObject->pObject",
        "pBase->actorData",
        "pNpc->actorData",
        ".actorData.factions",
        "actorData.SetEssential",
        "pBase->outfits[0]",
        "pDefaultOutfit->outfitItems",
        "pItem->formType == FormType::Armor",
        "pItem->formType == FormType::LeveledItem",
        "pValueModEffect->actorValueIndex",
    ),
    "Code/client/Games/Skyrim/TESObjectREFR.cpp": (
        "aForm.formType == FormType::Armor",
        "pExtraEnchantment->pEnchantment->formID",
        "pExtraEnchantment->pEnchantment->listOfEffects",
        "pEffectItem->pEffectSetting->formID",
        "pEffectItem->pEffectSetting",
        "pEffectItem->fRawCost",
        "pEffectItem->data",
        "pObject->formType == FormType::Weapon",
        "pBaseForm->formType",
        "apThis->formType",
    ),
    "Code/client/Games/Skyrim/Forms/MagicItem.cpp": (
        "pEffect->pEffectSetting",
    ),
    "Code/client/Games/Skyrim/Magic/MagicTarget.cpp": (
        "pEffectItem->pEffectSetting",
        "apThis->fMagnitude",
        "arData.pCaster",
        "arData.pSpell",
        "arData.pEffectItem",
        "arData.fMagnitude",
        "arData.bDualCast",
    ),
    "Code/client/Games/Skyrim/Effects/InvisibilityEffect.cpp": (
        "apThis->pTarget",
    ),
    "Code/client/Games/Skyrim/Forms/EnchantmentItem.cpp": (
        "effectItem.data",
        "effectItem.fRawCost",
        "effectItem.pEffectSetting",
    ),
    "Code/client/Games/Skyrim/SaveLoad.cpp": (
        "apData->flags",
        "apData->saveName",
    ),
    "Code/client/Games/Skyrim/Forms/TESObjectCELL.cpp": (
        "pBaseForm->formType",
    ),
    "Code/client/Services/Generic/InventoryService.cpp": (
        "pItem->formType == FormType::Armor",
    ),
    "Code/client/Systems/AnimationSystem.cpp": (
        "apActor->actorState",
        "pActor->parentCell",
        "pActor->position",
        "pActor->rotation",
        "pActor->currentProcess",
        ".flags1",
        ".flags2",
        "->middleProcess",
        "middleProcess->",
        "->direction",
    ),
    "Code/client/Systems/InterpolationSystem.cpp": (
        "apActor->currentProcess",
        "->middleProcess",
        "middleProcess->",
        "->direction",
    ),
    "Code/client/Games/Animation.cpp": (
        "pActor->actorState",
        ".flags1",
        ".flags2",
    ),
    "Code/client/Systems/FaceGenSystem.cpp": (
        "apActor->baseForm",
    ),
    "Code/client/Games/ModManager.cpp": (
        "apCharacter->baseForm",
    ),
    "Code/client/Games/Forms.cpp": (
        "PlayerCharacter::Get()->baseForm",
    ),
    "Code/client/Services/Debug/Views/FormDebugView.cpp": (
        "->middleProcess",
        "middleProcess->",
        "->ActiveEffects",
    ),
    "Code/client/Games/Skyrim/Projectiles/Projectile.cpp": (
        "->fPower",
        "pSpell->eCastingType",
        "arData.origin",
        "arData.projectileBase",
        "arData.shooter",
        "arData.weaponSource",
        "arData.ammoSource",
        "arData.angleZ",
        "arData.angleX",
        "arData.parentCell",
        "arData.spell",
        "arData.castingSource",
        "arData.area",
        "arData.power",
        "arData.scale",
        "arData.alwaysHit",
        "arData.noDamageOutsideCombat",
        "arData.autoAim",
        "arData.chainShatter",
        "arData.deferInitialization",
        "arData.forceConeOfFire",
    ),
    "Code/client/Services/Generic/CombatService.cpp": (
        "launchData.origin",
        "launchData.projectileBase",
        "launchData.shooter",
        "launchData.weaponSource",
        "launchData.ammoSource",
        "launchData.angleZ",
        "launchData.angleX",
        "launchData.parentCell",
        "launchData.spell",
        "launchData.castingSource",
        "launchData.area",
        "launchData.power",
        "launchData.scale",
        "launchData.alwaysHit",
        "launchData.noDamageOutsideCombat",
        "launchData.autoAim",
        "launchData.chainShatter",
        "launchData.useOrigin",
        "launchData.deferInitialization",
        "launchData.forceConeOfFire",
    ),
    "Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.cpp": (
        "self->Data",
        "renderer.RenderWindowA",
        "renderer.pForwarder",
        "renderer.pContext",
    ),
    "Code/client/Games/Skyrim/Combat/CombatController.cpp": (
        "attackerHandle",
        "targetHandle",
        "targetSelectors",
        "pActiveTargetSelector",
        "pTargetSelector->flags",
    ),
    "Code/client/Services/Debug/Views/CombatView.cpp": (
        "apThis->pCombatController",
        "apCombatTarget->attackerCount",
        "pActor->pCombatController",
        "pCombatController->pCombatGroup",
        "pCombatController->pInventory",
        "pCombatController->targetHandle",
        "pCombatController->pActiveTargetSelector",
        "pCombatController->pCachedAttacker",
        "pCombatInventory->maximumRange",
        "->targets",
        "target.targetHandle",
    ),
    "Code/client/Games/Skyrim/ExtraData/ExtraDataList.cpp": (
        "pExtraDataList->data",
        "pExtraDataList->bitfield",
        "bitfield->data",
        "auto pEntry = data;",
        "BSExtraData* pNext = data;",
        "BSScopedLock<BSRecursiveLock> _(lock);",
        "lock.Lock()",
        "lock.Unlock()",
    ),
    "Code/client/Games/Skyrim/Interface/UI.cpp": (
        "->uiMenuFlags",
        "menuStack",
        "menuMap",
    ),
    "Code/client/Games/Skyrim/Interface/IMenu.cpp": (
        "uiMenuFlags",
    ),
    "Code/client/Games/Skyrim/Interface/MenuControls.cpp": (
        "unk83",
    ),
    "Code/client/Games/Skyrim/TESObjectREFR.cpp": (
        "GetContainerChanges()->entries",
        "pExtraContChangesEntries",
        "pBaseContainer->count",
        "pBaseContainer->entries",
        "pGameEntry->form",
        "pGameEntry->count",
        "pExtraDataList->data",
        "pExtraDataList->bitfield",
        "apThis->baseForm",
        "pParentCell->worldspace",
        "pParentCell->cellFlags",
        "apCell->worldspace",
    ),
    "Code/client/Games/Skyrim/Forms/TESObjectCELL.cpp": (
        "pRef->baseForm",
        "refData.",
        "pReferenceData->refArray",
        "pReferenceData->capacity",
    ),
    "Code/client/Games/Skyrim/PlayerCharacter.cpp": (
        "apObject->baseForm",
        "pSkillData->skills",
    ),
    "Code/client/Services/Generic/CharacterService.cpp": (
        "PlayerCharacter::Get()->parentCell",
        "PlayerCharacter::Get()->position",
        "PlayerCharacter::Get()->objectives",
        "PlayerCharacter::Get()->GetTints()",
        "PlayerCharacter::Get()->GetObjectives()",
        "pActor->baseForm",
        "pActor->parentCell",
        "pActor->position",
        "pActor->rotation",
        "pActor->actorState",
        "apActor->actorState",
    ),
    "Code/client/Services/Generic/InventoryService.cpp": (
        "pActor->actorState",
    ),
    "Code/client/Services/Generic/ActorValueService.cpp": (
        "pActor->actorState",
    ),
    "Code/client/Services/Generic/MagicService.cpp": (
        "pActor->magicItems",
        "pActor->magicTarget",
        "pRemotePlayer->magicTarget",
        "pTarget->baseForm",
        "pCaster->baseForm",
        "pCaster->pCasterActor",
        "acEvent.pCaster->pCasterActor",
        "pActor->casters",
        "data.pCaster",
        "data.pSpell",
        "data.pEffectItem",
        "data.fMagnitude",
        "data.fUnkFloat1",
        "data.eCastingSource",
        "data.bDualCast",
        "pSpell->eCastingType",
        "pSpellItem->eCastingType",
    ),
    "Code/client/Services/Generic/OverlayService.cpp": (
        "apActor->healthModifiers",
        "temporaryModifier",
    ),
    "Code/client/Services/Debug/Views/ActorValuesView.cpp": (
        "pActor->actorValueOwner",
    ),
    "Code/client/Services/Generic/ObjectService.cpp": (
        "pPlayer->parentCell",
        "pObject->baseForm",
        "pBaseForm->formType",
        "pObject->parentCell",
        "pObject->position",
        "acEvent.pObject->parentCell",
        "pCell->loadedCellData",
        "pLoadedCellData->encounterZone",
    ),
    "Code/client/Services/Generic/VRActivationService.cpp": (
        "pCell->worldspace",
        "pBaseForm->formType",
    ),
    "Code/client/Services/Generic/VRMagicService.cpp": (
        "pBaseForm->formType",
    ),
    "Code/client/Services/Generic/VRCombatService.cpp": (
        "pBaseForm->formType",
    ),
    "Code/client/Services/Debug/DebugService.cpp": (
        "PlayerCharacter::Get()->baseForm",
        "pPlayer->baseForm",
        "pPlayer->currentProcess",
        "pPlayer->position",
        "->menuMap",
        ".menuMap",
    ),
    "Code/client/Services/Debug/Views/ComponentView.cpp": (
        "apRefr->position",
    ),
    "Code/client/Services/Debug/Views/SkillView.cpp": (
        "pPlayer->pSkills",
        "pSkills->xp",
        "pSkills->levelThreshold",
        "pSkills->skills",
        "pSkills->legendaryLevels",
    ),
    "Code/client/Games/Skyrim/Sky/Sky.cpp": (
        "apWeather->formID",
    ),
    "Code/client/Services/Debug/Views/AnimDebugView.cpp": (
        "pActor->formID",
    ),
    "Code/client/Services/Debug/Views/CombatView.cpp": (
        "pTarget->formID",
    ),
    "Code/client/Services/Debug/Views/ContainerDebugView.cpp": (
        "pActor->formID",
    ),
    "Code/client/Services/Debug/Views/QuestDebugView.cpp": (
        "pPlayer->objectives",
        "pPlayer->parentCell",
        "pPlayer->position",
        "pActor->baseForm",
        "pQuest->formID",
        "pActor->formID",
        "pPlayer->GetParentCellData()->formID",
    ),
    "Code/client/Services/Debug/Views/EntitiesView.cpp": (
        "pPlayer->parentCell",
        "pPlayer->position",
        "pActor->baseForm",
        "pRefr->baseForm",
        "pActor->position",
        "pActor->rotation",
        "owner->formID",
        "pPlayer->GetParentCellData()->formID",
    ),
    "Code/client/Services/Debug/Views/FormDebugView.cpp": (
        "pRefr->parentCell",
        "pRefr->baseForm",
        "pRefr->currentProcess",
        "pParentCell->formID",
        "pRefr->formID",
        "pEffect->pSpell",
        "pEffect->fElapsedSeconds",
        "pEffect->fDuration",
        "pEffect->fMagnitude",
        "pEffect->uiFlags",
    ),
    "Code/client/Services/Debug/Views/CellView.cpp": (
        "pPlayer->parentCell",
        "pWorldSpace->formID",
        "pCell->formID",
    ),
    "Code/client/Services/Debug/Views/PlayerDebugView.cpp": (
        "pPlayer->parentCell",
        "pPlayer->magicItems",
        "pPlayer->equippedShout",
        "->pCurrentSpell",
        "pLeftWeapon->formID",
        "pRightWeapon->formID",
        "pLeftSpell->formID",
        "pRightSpell->formID",
        "pSelectedPower->formID",
        "pWorldSpace->formID",
        "pCell->formID",
        "playerParentCell->formID",
    ),
    "Code/client/Services/Debug/Views/WeatherView.cpp": (
        "pCurrentWeather->formID",
    ),
}


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def missing_tokens(text: str, tokens: tuple[str, ...]) -> list[str]:
    return [token for token in tokens if token not in text]


def present_tokens(text: str, tokens: tuple[str, ...]) -> list[str]:
    return [token for token in tokens if token in text]


def fmt_tokens(tokens: list[str]) -> str:
    if not tokens:
        return "None.\n"
    return "".join(f"- `{token}`\n" for token in tokens)


def audit_required_token_map(root: pathlib.Path, required_tokens: dict[str, tuple[str, ...]]) -> dict[str, list[str]]:
    missing: dict[str, list[str]] = {}

    for rel_path, tokens in required_tokens.items():
        path = root / rel_path
        text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
        hits = missing_tokens(text, tokens)
        if hits:
            missing[rel_path] = hits

    return missing


def audit_layout_tokens(root: pathlib.Path) -> tuple[dict[str, list[str]], dict[str, list[str]]]:
    missing = audit_required_token_map(root, REQUIRED_LAYOUT_TOKENS)
    forbidden: dict[str, list[str]] = {}

    for rel_path, tokens in FORBIDDEN_LAYOUT_TOKENS.items():
        path = root / rel_path
        text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
        hits = present_tokens(text, tokens)
        if hits:
            forbidden[rel_path] = hits

    return missing, forbidden


def audit_vr_menu_predicate(root: pathlib.Path) -> list[str]:
    path = root / "Code/client/Games/Skyrim/Interface/UI.cpp"
    text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
    start = text.find("SkyrimTogetherVR::MenuOpenState UI::GetMenuOpen")
    end = text.find("void UI::CloseAllMenus", start)
    if start < 0 or end < 0:
        return ["missing tri-state UI::GetMenuOpen definition"]

    body = text[start:end]
    vr_branch = body.find("#if TP_SKYRIM_VR")
    desktop_branch = body.find("#else", vr_branch)
    probe = body.find("ProbeVrMenuOpen")
    relocation = body.find("POINTER_SKYRIMSE(TMenuSystem_IsOpen, s_isMenuOpen, 82074)")
    if not (0 <= vr_branch < probe < desktop_branch < relocation):
        return ["ID 82074 must remain desktop-only after the local VR menu predicate"]
    return []


def audit_forbidden_token_map(root: pathlib.Path, forbidden_tokens: dict[str, tuple[str, ...]]) -> dict[str, list[str]]:
    forbidden: dict[str, list[str]] = {}

    for rel_path, tokens in forbidden_tokens.items():
        path = root / rel_path
        text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
        hits = present_tokens(text, tokens)
        if hits:
            forbidden[rel_path] = hits

    return forbidden


def audit_vr_papyrus_native_bypass(root: pathlib.Path) -> list[str]:
    path = root / "Code/client/Games/Misc/BSScript.cpp"
    text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
    required_tokens = (
        "#if defined(TP_SKYRIM_VR) && TP_SKYRIM_VR",
        "Skyrim VR uses the standalone SKSEVR tick bridge.",
        "install no BSScript",
        "#if !defined(TP_SKYRIM_VR) || !TP_SKYRIM_VR",
    )
    missing = missing_tokens(text, required_tokens)

    install = text.find("void InstallSkyrimTogetherPapyrusNativeHooks()")
    vr_guard = text.find("#if defined(TP_SKYRIM_VR) && TP_SKYRIM_VR", install)
    vr_return = text.find("return;", vr_guard) if vr_guard >= 0 else -1
    non_vr_guard = text.find("#if !defined(TP_SKYRIM_VR) || !TP_SKYRIM_VR", install)
    initializer = text.find("static TiltedPhoques::Initializer s_vmHooks")
    if install < 0 or vr_guard < 0 or vr_return < 0 or non_vr_guard < 0 or not (install < vr_guard < vr_return < non_vr_guard):
        missing.append("VR must return before installing any BSScript native detour")
    if initializer < 0 or text.find("#if !defined(TP_SKYRIM_VR) || !TP_SKYRIM_VR", initializer) < 0:
        missing.append("VR must skip the BSScript static hook initializer")

    return missing


def extract_cpp_function(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        return ""

    opening_brace = text.find("{", start + len(signature))
    if opening_brace < 0:
        return ""

    depth = 0
    for index in range(opening_brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start : index + 1]

    return ""


def audit_vr_connection_only_game_call_safety(root: pathlib.Path) -> list[str]:
    errors: list[str] = []
    transport_path = root / "Code/client/Services/Generic/TransportService.cpp"
    player_path = root / "Code/client/Services/Generic/PlayerService.cpp"
    transport_text = transport_path.read_text(encoding="utf-8", errors="replace") if transport_path.exists() else ""
    player_text = player_path.read_text(encoding="utf-8", errors="replace") if player_path.exists() else ""

    if "TP_SKYRIM_VR && TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY && TP_SKYRIM_VR_ENABLE_PLAYER_CELL_SERVICE" in player_text:
        errors.append("Connection-only PlayerService safety must not depend on the player-cell diagnostics flag")

    connected = extract_cpp_function(transport_text, "void TransportService::OnConnected()")
    vr_auth_start = connected.find("#if TP_SKYRIM_VR\n    // The stable lifecycle gate")
    vr_auth_end = connected.find("#else", vr_auth_start)
    if vr_auth_start < 0 or vr_auth_end < 0:
        errors.append("TransportService::OnConnected must contain the dedicated VR authentication branch")
    else:
        vr_auth = connected[vr_auth_start:vr_auth_end]
        for token in ("PlayerCharacter::Get", "GetLevel(", "TimeData::Get"):
            if token in vr_auth:
                errors.append(f"VR authentication branch must not call `{token}`")

    for signature in (
        "void PlayerService::RunVrLevelUpdates(const double acDeltaTime) noexcept",
        "void PlayerService::WriteVrPlayerCellStatusFile() noexcept",
    ):
        function = extract_cpp_function(player_text, signature)
        if not function:
            errors.append(f"Could not inspect `{signature}`")
        elif "GetLevel(" in function:
            errors.append(f"Connection-only function `{signature}` must not call `GetLevel()`")

    update = extract_cpp_function(player_text, "void PlayerService::OnUpdate(const UpdateEvent& acEvent) noexcept")
    vr_cell_only = update.find("if constexpr (kVrCellOnlyPlayerService)")
    early_return = update.find("return;", vr_cell_only)
    desktop_level_update = update.find("RunLevelUpdates();")
    if vr_cell_only < 0 or early_return < 0 or desktop_level_update < 0 or not (vr_cell_only < early_return < desktop_level_update):
        errors.append("PlayerService::OnUpdate must return from connection-only VR handling before desktop level polling")

    return errors


def fmt_token_map(tokens_by_file: dict[str, list[str]]) -> str:
    if not tokens_by_file:
        return "None.\n"

    lines = []
    for rel_path, tokens in sorted(tokens_by_file.items()):
        lines.append(f"- `{rel_path}`\n")
        for token in tokens:
            lines.append(f"  - `{token}`\n")
    return "".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=pathlib.Path, default=pathlib.Path.cwd())
    parser.add_argument("--report", type=pathlib.Path, default=pathlib.Path("Docs/SkyrimVR/vr-services-audit.md"))
    args = parser.parse_args()

    root = args.repo.resolve()
    report = args.report if args.report.is_absolute() else root / args.report

    world_path = root / "Code/client/World.cpp"
    xmake_path = root / "Code/client/xmake.lua"
    inventory_path = root / "Code/client/Services/Generic/VRInventoryService.cpp"
    inventory_header_path = root / "Code/client/Services/VRInventoryService.h"
    movement_path = root / "Code/client/Services/Generic/VRMovementService.cpp"
    movement_header_path = root / "Code/client/Services/VRMovementService.h"
    activation_path = root / "Code/client/Services/Generic/VRActivationService.cpp"
    activation_header_path = root / "Code/client/Services/VRActivationService.h"
    magic_path = root / "Code/client/Services/Generic/VRMagicService.cpp"
    magic_header_path = root / "Code/client/Services/VRMagicService.h"
    combat_path = root / "Code/client/Services/Generic/VRCombatService.cpp"
    combat_header_path = root / "Code/client/Services/VRCombatService.h"
    projectile_path = root / "Code/client/Services/Generic/VRProjectileService.cpp"
    projectile_header_path = root / "Code/client/Services/VRProjectileService.h"
    grab_path = root / "Code/client/Services/Generic/VRGrabService.cpp"
    grab_header_path = root / "Code/client/Services/VRGrabService.h"
    saveload_path = root / "Code/client/Services/Generic/VRSaveLoadService.cpp"
    saveload_header_path = root / "Code/client/Services/VRSaveLoadService.h"

    world_text = world_path.read_text(encoding="utf-8", errors="replace") if world_path.exists() else ""
    xmake_text = xmake_path.read_text(encoding="utf-8", errors="replace") if xmake_path.exists() else ""
    inventory_text = inventory_path.read_text(encoding="utf-8", errors="replace") if inventory_path.exists() else ""
    movement_text = movement_path.read_text(encoding="utf-8", errors="replace") if movement_path.exists() else ""
    activation_text = activation_path.read_text(encoding="utf-8", errors="replace") if activation_path.exists() else ""
    magic_text = magic_path.read_text(encoding="utf-8", errors="replace") if magic_path.exists() else ""
    combat_text = combat_path.read_text(encoding="utf-8", errors="replace") if combat_path.exists() else ""
    projectile_text = projectile_path.read_text(encoding="utf-8", errors="replace") if projectile_path.exists() else ""
    saveload_text = saveload_path.read_text(encoding="utf-8", errors="replace") if saveload_path.exists() else ""

    missing_world = missing_tokens(world_text, REQUIRED_WORLD_TOKENS)
    missing_movement_world = missing_tokens(world_text, REQUIRED_MOVEMENT_WORLD_TOKENS)
    missing_activation_world = missing_tokens(world_text, REQUIRED_ACTIVATION_WORLD_TOKENS)
    missing_magic_world = missing_tokens(world_text, REQUIRED_MAGIC_WORLD_TOKENS)
    missing_combat_world = missing_tokens(world_text, REQUIRED_COMBAT_WORLD_TOKENS)
    missing_projectile_world = missing_tokens(world_text, REQUIRED_PROJECTILE_WORLD_TOKENS)
    missing_grab_world = missing_tokens(world_text, REQUIRED_GRAB_WORLD_TOKENS)
    missing_saveload_world = missing_tokens(world_text, REQUIRED_SAVELOAD_WORLD_TOKENS)
    missing_xmake = missing_tokens(xmake_text, REQUIRED_XMAKE_TOKENS)
    missing_movement_xmake = missing_tokens(xmake_text, REQUIRED_MOVEMENT_XMAKE_TOKENS)
    missing_activation_xmake = missing_tokens(xmake_text, REQUIRED_ACTIVATION_XMAKE_TOKENS)
    missing_magic_xmake = missing_tokens(xmake_text, REQUIRED_MAGIC_XMAKE_TOKENS)
    missing_combat_xmake = missing_tokens(xmake_text, REQUIRED_COMBAT_XMAKE_TOKENS)
    missing_projectile_xmake = missing_tokens(xmake_text, REQUIRED_PROJECTILE_XMAKE_TOKENS)
    missing_grab_xmake = missing_tokens(xmake_text, REQUIRED_GRAB_XMAKE_TOKENS)
    missing_saveload_xmake = missing_tokens(xmake_text, REQUIRED_SAVELOAD_XMAKE_TOKENS)
    missing_movement = missing_tokens(movement_text, REQUIRED_MOVEMENT_OBSERVER_TOKENS)
    forbidden_movement = present_tokens(movement_text, FORBIDDEN_MOVEMENT_OBSERVER_TOKENS)
    missing_inventory = missing_tokens(inventory_text, REQUIRED_INVENTORY_OBSERVER_TOKENS)
    forbidden_inventory = present_tokens(inventory_text, FORBIDDEN_INVENTORY_OBSERVER_TOKENS)
    missing_activation = missing_tokens(activation_text, REQUIRED_ACTIVATION_OBSERVER_TOKENS)
    forbidden_activation = present_tokens(activation_text, FORBIDDEN_ACTIVATION_OBSERVER_TOKENS)
    missing_magic = missing_tokens(magic_text, REQUIRED_MAGIC_OBSERVER_TOKENS)
    forbidden_magic = present_tokens(magic_text, FORBIDDEN_MAGIC_OBSERVER_TOKENS)
    missing_combat = missing_tokens(combat_text, REQUIRED_COMBAT_OBSERVER_TOKENS)
    forbidden_combat = present_tokens(combat_text, FORBIDDEN_COMBAT_OBSERVER_TOKENS)
    missing_projectile = missing_tokens(projectile_text, REQUIRED_PROJECTILE_OBSERVER_TOKENS)
    forbidden_projectile = present_tokens(projectile_text, FORBIDDEN_PROJECTILE_OBSERVER_TOKENS)
    missing_saveload = missing_tokens(saveload_text, REQUIRED_SAVELOAD_OBSERVER_TOKENS)
    forbidden_saveload = present_tokens(saveload_text, FORBIDDEN_SAVELOAD_OBSERVER_TOKENS)
    missing_layout, forbidden_layout = audit_layout_tokens(root)
    vr_menu_predicate_errors = audit_vr_menu_predicate(root)
    if vr_menu_predicate_errors:
        missing_layout.setdefault("Code/client/Games/Skyrim/Interface/UI.cpp", []).extend(vr_menu_predicate_errors)
    missing_vr_papyrus_bypass = audit_vr_papyrus_native_bypass(root)
    vr_connection_only_safety_errors = audit_vr_connection_only_game_call_safety(root)
    missing_vr_commonlib_accessors = audit_required_token_map(root, REQUIRED_VR_COMMONLIB_ACCESSOR_TOKENS)
    forbidden_vr_raw_members = audit_forbidden_token_map(root, FORBIDDEN_VR_RAW_MEMBER_TOKENS)
    missing_movement_relay = audit_required_token_map(root, REQUIRED_MOVEMENT_RELAY_TOKENS)
    missing_pose_relay = audit_required_token_map(root, REQUIRED_POSE_RELAY_TOKENS)
    missing_equipment_relay = audit_required_token_map(root, REQUIRED_EQUIPMENT_RELAY_TOKENS)
    missing_remote_avatar_component = audit_required_token_map(root, REQUIRED_REMOTE_AVATAR_COMPONENT_TOKENS)
    forbidden_remote_avatar_component = audit_forbidden_token_map(root, FORBIDDEN_REMOTE_AVATAR_COMPONENT_TOKENS)
    missing_remote_player_proxy = audit_required_token_map(root, REQUIRED_REMOTE_PLAYER_PROXY_TOKENS)
    forbidden_remote_player_proxy = audit_forbidden_token_map(root, FORBIDDEN_REMOTE_PLAYER_PROXY_TOKENS)
    missing_player_cell_handoff = audit_required_token_map(root, REQUIRED_PLAYER_CELL_HANDOFF_TOKENS)
    missing_saveload_handoff = audit_required_token_map(root, REQUIRED_SAVELOAD_HANDOFF_TOKENS)
    missing_activation_relay = audit_required_token_map(root, REQUIRED_ACTIVATION_RELAY_TOKENS)
    missing_magic_relay = audit_required_token_map(root, REQUIRED_MAGIC_RELAY_TOKENS)
    missing_combat_relay = audit_required_token_map(root, REQUIRED_COMBAT_RELAY_TOKENS)
    missing_projectile_relay = audit_required_token_map(root, REQUIRED_PROJECTILE_RELAY_TOKENS)
    missing_grab_relay = audit_required_token_map(root, REQUIRED_GRAB_RELAY_TOKENS)
    missing_layout_count = sum(len(tokens) for tokens in missing_layout.values())
    forbidden_layout_count = sum(len(tokens) for tokens in forbidden_layout.values())
    missing_vr_commonlib_accessor_count = sum(len(tokens) for tokens in missing_vr_commonlib_accessors.values())
    forbidden_vr_raw_member_count = sum(len(tokens) for tokens in forbidden_vr_raw_members.values())
    missing_movement_relay_count = sum(len(tokens) for tokens in missing_movement_relay.values())
    missing_pose_relay_count = sum(len(tokens) for tokens in missing_pose_relay.values())
    missing_equipment_relay_count = sum(len(tokens) for tokens in missing_equipment_relay.values())
    missing_remote_avatar_component_count = sum(len(tokens) for tokens in missing_remote_avatar_component.values())
    forbidden_remote_avatar_component_count = sum(len(tokens) for tokens in forbidden_remote_avatar_component.values())
    missing_remote_player_proxy_count = sum(len(tokens) for tokens in missing_remote_player_proxy.values())
    forbidden_remote_player_proxy_count = sum(len(tokens) for tokens in forbidden_remote_player_proxy.values())
    missing_player_cell_handoff_count = sum(len(tokens) for tokens in missing_player_cell_handoff.values())
    missing_saveload_handoff_count = sum(len(tokens) for tokens in missing_saveload_handoff.values())
    missing_activation_relay_count = sum(len(tokens) for tokens in missing_activation_relay.values())
    missing_magic_relay_count = sum(len(tokens) for tokens in missing_magic_relay.values())
    missing_combat_relay_count = sum(len(tokens) for tokens in missing_combat_relay.values())
    missing_projectile_relay_count = sum(len(tokens) for tokens in missing_projectile_relay.values())
    missing_grab_relay_count = sum(len(tokens) for tokens in missing_grab_relay.values())

    report.parent.mkdir(parents=True, exist_ok=True)
    with report.open("w", encoding="utf-8") as handle:
        handle.write("# SkyrimTogetherVR Service Audit\n\n")
        handle.write("Generated by `Tools/SkyrimVR/audit_vr_services.py`.\n\n")
        handle.write("## Summary\n\n")
        handle.write(f"- `World.cpp` inventory observer wiring tokens missing: {len(missing_world)}\n")
        handle.write(f"- `World.cpp` movement observer wiring tokens missing: {len(missing_movement_world)}\n")
        handle.write(f"- `World.cpp` activation observer wiring tokens missing: {len(missing_activation_world)}\n")
        handle.write(f"- `World.cpp` magic observer wiring tokens missing: {len(missing_magic_world)}\n")
        handle.write(f"- `World.cpp` combat observer wiring tokens missing: {len(missing_combat_world)}\n")
        handle.write(f"- `World.cpp` projectile observer wiring tokens missing: {len(missing_projectile_world)}\n")
        handle.write(f"- `World.cpp` grab observer wiring tokens missing: {len(missing_grab_world)}\n")
        handle.write(f"- `World.cpp` save/load observer wiring tokens missing: {len(missing_saveload_world)}\n")
        handle.write(f"- `xmake.lua` inventory observer flag tokens missing: {len(missing_xmake)}\n")
        handle.write(f"- `xmake.lua` movement observer flag tokens missing: {len(missing_movement_xmake)}\n")
        handle.write(f"- `xmake.lua` activation observer flag tokens missing: {len(missing_activation_xmake)}\n")
        handle.write(f"- `xmake.lua` magic observer flag tokens missing: {len(missing_magic_xmake)}\n")
        handle.write(f"- `xmake.lua` combat observer flag tokens missing: {len(missing_combat_xmake)}\n")
        handle.write(f"- `xmake.lua` projectile observer flag tokens missing: {len(missing_projectile_xmake)}\n")
        handle.write(f"- `xmake.lua` grab observer flag tokens missing: {len(missing_grab_xmake)}\n")
        handle.write(f"- `xmake.lua` save/load observer flag tokens missing: {len(missing_saveload_xmake)}\n")
        handle.write(f"- `VRMovementService.cpp` observer tokens missing: {len(missing_movement)}\n")
        handle.write(f"- `VRMovementService.cpp` forbidden actor-sync tokens present: {len(forbidden_movement)}\n")
        handle.write(f"- VR movement relay tokens missing: {missing_movement_relay_count}\n")
        handle.write(f"- VR pose relay tokens missing: {missing_pose_relay_count}\n")
        handle.write(f"- `VRInventoryService.cpp` observer tokens missing: {len(missing_inventory)}\n")
        handle.write(f"- `VRInventoryService.cpp` forbidden full-sync tokens present: {len(forbidden_inventory)}\n")
        handle.write(f"- VR equipment relay tokens missing: {missing_equipment_relay_count}\n")
        handle.write(f"- Remote VR avatar component tokens missing: {missing_remote_avatar_component_count}\n")
        handle.write(f"- Remote VR avatar component forbidden stale-cache tokens present: {forbidden_remote_avatar_component_count}\n")
        handle.write(f"- Remote player proxy service tokens missing: {missing_remote_player_proxy_count}\n")
        handle.write(f"- Remote player proxy service forbidden mutation tokens present: {forbidden_remote_player_proxy_count}\n")
        handle.write(f"- VR player cell handoff tokens missing: {missing_player_cell_handoff_count}\n")
        handle.write(f"- VR save/load handoff tokens missing: {missing_saveload_handoff_count}\n")
        handle.write(f"- `VRActivationService.cpp` observer tokens missing: {len(missing_activation)}\n")
        handle.write(f"- `VRActivationService.cpp` forbidden object-sync tokens present: {len(forbidden_activation)}\n")
        handle.write(f"- VR activation relay tokens missing: {missing_activation_relay_count}\n")
        handle.write(f"- `VRMagicService.cpp` observer tokens missing: {len(missing_magic)}\n")
        handle.write(f"- `VRMagicService.cpp` forbidden full magic-sync tokens present: {len(forbidden_magic)}\n")
        handle.write(f"- VR magic relay tokens missing: {missing_magic_relay_count}\n")
        handle.write(f"- `VRCombatService.cpp` observer tokens missing: {len(missing_combat)}\n")
        handle.write(f"- `VRCombatService.cpp` forbidden full combat/projectile-sync tokens present: {len(forbidden_combat)}\n")
        handle.write(f"- VR combat relay tokens missing: {missing_combat_relay_count}\n")
        handle.write(f"- `VRProjectileService.cpp` observer tokens missing: {len(missing_projectile)}\n")
        handle.write(f"- `VRProjectileService.cpp` forbidden full projectile-sync tokens present: {len(forbidden_projectile)}\n")
        handle.write(f"- VR projectile relay tokens missing: {missing_projectile_relay_count}\n")
        handle.write(f"- VR grab relay tokens missing: {missing_grab_relay_count}\n")
        handle.write(f"- `VRSaveLoadService.cpp` observer tokens missing: {len(missing_saveload)}\n")
        handle.write(f"- `VRSaveLoadService.cpp` forbidden save/load mutation tokens present: {len(forbidden_saveload)}\n")
        handle.write(f"- CommonLib-informed layout/accessor tokens missing: {missing_layout_count}\n")
        handle.write(f"- Forbidden stale layout tokens present: {forbidden_layout_count}\n")
        handle.write(f"- VR Papyrus native bypass requirements missing: {len(missing_vr_papyrus_bypass)}\n")
        handle.write(f"- VR connection-only unsafe game-call findings: {len(vr_connection_only_safety_errors)}\n")
        handle.write(f"- VR CommonLib accessor use tokens missing: {missing_vr_commonlib_accessor_count}\n")
        handle.write(f"- Forbidden raw VR game-object member reads present: {forbidden_vr_raw_member_count}\n")
        handle.write(f"- `VRMovementService.h` exists: {'yes' if movement_header_path.exists() else 'no'}\n")
        handle.write(f"- `VRMovementService.cpp` exists: {'yes' if movement_path.exists() else 'no'}\n")
        handle.write(f"- `VRInventoryService.h` exists: {'yes' if inventory_header_path.exists() else 'no'}\n")
        handle.write(f"- `VRInventoryService.cpp` exists: {'yes' if inventory_path.exists() else 'no'}\n")
        handle.write(f"- `VRActivationService.h` exists: {'yes' if activation_header_path.exists() else 'no'}\n")
        handle.write(f"- `VRActivationService.cpp` exists: {'yes' if activation_path.exists() else 'no'}\n")
        handle.write(f"- `VRMagicService.h` exists: {'yes' if magic_header_path.exists() else 'no'}\n")
        handle.write(f"- `VRMagicService.cpp` exists: {'yes' if magic_path.exists() else 'no'}\n")
        handle.write(f"- `VRCombatService.h` exists: {'yes' if combat_header_path.exists() else 'no'}\n")
        handle.write(f"- `VRCombatService.cpp` exists: {'yes' if combat_path.exists() else 'no'}\n")
        handle.write(f"- `VRProjectileService.h` exists: {'yes' if projectile_header_path.exists() else 'no'}\n")
        handle.write(f"- `VRProjectileService.cpp` exists: {'yes' if projectile_path.exists() else 'no'}\n")
        handle.write(f"- `VRGrabService.h` exists: {'yes' if grab_header_path.exists() else 'no'}\n")
        handle.write(f"- `VRGrabService.cpp` exists: {'yes' if grab_path.exists() else 'no'}\n")
        handle.write(f"- `VRSaveLoadService.h` exists: {'yes' if saveload_header_path.exists() else 'no'}\n")
        handle.write(f"- `VRSaveLoadService.cpp` exists: {'yes' if saveload_path.exists() else 'no'}\n\n")

        handle.write("## Missing World Wiring Tokens\n\n")
        handle.write(fmt_tokens(missing_world))
        handle.write("\n## Missing Movement World Wiring Tokens\n\n")
        handle.write(fmt_tokens(missing_movement_world))
        handle.write("\n## Missing Activation World Wiring Tokens\n\n")
        handle.write(fmt_tokens(missing_activation_world))
        handle.write("\n## Missing Magic World Wiring Tokens\n\n")
        handle.write(fmt_tokens(missing_magic_world))
        handle.write("\n## Missing Combat World Wiring Tokens\n\n")
        handle.write(fmt_tokens(missing_combat_world))
        handle.write("\n## Missing Projectile World Wiring Tokens\n\n")
        handle.write(fmt_tokens(missing_projectile_world))
        handle.write("\n## Missing Grab World Wiring Tokens\n\n")
        handle.write(fmt_tokens(missing_grab_world))
        handle.write("\n## Missing Save/Load World Wiring Tokens\n\n")
        handle.write(fmt_tokens(missing_saveload_world))
        handle.write("\n## Missing Xmake Flag Tokens\n\n")
        handle.write(fmt_tokens(missing_xmake))
        handle.write("\n## Missing Movement Xmake Flag Tokens\n\n")
        handle.write(fmt_tokens(missing_movement_xmake))
        handle.write("\n## Missing Activation Xmake Flag Tokens\n\n")
        handle.write(fmt_tokens(missing_activation_xmake))
        handle.write("\n## Missing Magic Xmake Flag Tokens\n\n")
        handle.write(fmt_tokens(missing_magic_xmake))
        handle.write("\n## Missing Combat Xmake Flag Tokens\n\n")
        handle.write(fmt_tokens(missing_combat_xmake))
        handle.write("\n## Missing Projectile Xmake Flag Tokens\n\n")
        handle.write(fmt_tokens(missing_projectile_xmake))
        handle.write("\n## Missing Grab Xmake Flag Tokens\n\n")
        handle.write(fmt_tokens(missing_grab_xmake))
        handle.write("\n## Missing Save/Load Xmake Flag Tokens\n\n")
        handle.write(fmt_tokens(missing_saveload_xmake))
        handle.write("\n## Missing Movement Observer Tokens\n\n")
        handle.write(fmt_tokens(missing_movement))
        handle.write("\n## Forbidden Actor-Sync Tokens In Movement Observer\n\n")
        handle.write(fmt_tokens(forbidden_movement))
        handle.write("\n## Missing VR Movement Relay Tokens\n\n")
        handle.write(fmt_token_map(missing_movement_relay))
        handle.write("\n## Missing VR Pose Relay Tokens\n\n")
        handle.write(fmt_token_map(missing_pose_relay))
        handle.write("\n## Missing Inventory Observer Tokens\n\n")
        handle.write(fmt_tokens(missing_inventory))
        handle.write("\n## Forbidden Full-Sync Tokens In Inventory Observer\n\n")
        handle.write(fmt_tokens(forbidden_inventory))
        handle.write("\n## Missing VR Equipment Relay Tokens\n\n")
        handle.write(fmt_token_map(missing_equipment_relay))
        handle.write("\n## Missing Remote VR Avatar Component Tokens\n\n")
        handle.write(fmt_token_map(missing_remote_avatar_component))
        handle.write("\n## Forbidden Remote VR Avatar Component Stale-Cache Tokens\n\n")
        handle.write(fmt_token_map(forbidden_remote_avatar_component))
        handle.write("\n## Missing Remote Player Proxy Service Tokens\n\n")
        handle.write(fmt_token_map(missing_remote_player_proxy))
        handle.write("\n## Forbidden Remote Player Proxy Service Mutation Tokens\n\n")
        handle.write(fmt_token_map(forbidden_remote_player_proxy))
        handle.write("\n## Missing VR Player Cell Handoff Tokens\n\n")
        handle.write(fmt_token_map(missing_player_cell_handoff))
        handle.write("\n## Missing VR Save/Load Handoff Tokens\n\n")
        handle.write(fmt_token_map(missing_saveload_handoff))
        handle.write("\n## Missing Activation Observer Tokens\n\n")
        handle.write(fmt_tokens(missing_activation))
        handle.write("\n## Forbidden Object-Sync Tokens In Activation Observer\n\n")
        handle.write(fmt_tokens(forbidden_activation))
        handle.write("\n## Missing VR Activation Relay Tokens\n\n")
        handle.write(fmt_token_map(missing_activation_relay))
        handle.write("\n## Missing Magic Observer Tokens\n\n")
        handle.write(fmt_tokens(missing_magic))
        handle.write("\n## Forbidden Full Magic-Sync Tokens In Magic Observer\n\n")
        handle.write(fmt_tokens(forbidden_magic))
        handle.write("\n## Missing VR Magic Relay Tokens\n\n")
        handle.write(fmt_token_map(missing_magic_relay))
        handle.write("\n## Missing Combat Observer Tokens\n\n")
        handle.write(fmt_tokens(missing_combat))
        handle.write("\n## Forbidden Full Combat/Projectile-Sync Tokens In Combat Observer\n\n")
        handle.write(fmt_tokens(forbidden_combat))
        handle.write("\n## Missing VR Combat Relay Tokens\n\n")
        handle.write(fmt_token_map(missing_combat_relay))
        handle.write("\n## Missing Projectile Observer Tokens\n\n")
        handle.write(fmt_tokens(missing_projectile))
        handle.write("\n## Forbidden Full Projectile-Sync Tokens In Projectile Observer\n\n")
        handle.write(fmt_tokens(forbidden_projectile))
        handle.write("\n## Missing VR Projectile Relay Tokens\n\n")
        handle.write(fmt_token_map(missing_projectile_relay))
        handle.write("\n## Missing VR Grab Relay Tokens\n\n")
        handle.write(fmt_token_map(missing_grab_relay))
        handle.write("\n## Missing Save/Load Observer Tokens\n\n")
        handle.write(fmt_tokens(missing_saveload))
        handle.write("\n## Forbidden Save/Load Mutation Tokens In Save/Load Observer\n\n")
        handle.write(fmt_tokens(forbidden_saveload))
        handle.write("\n## Missing CommonLib-Informed Layout/Accessor Tokens\n\n")
        handle.write(fmt_token_map(missing_layout))
        handle.write("\n## Forbidden Stale Layout Tokens\n\n")
        handle.write(fmt_token_map(forbidden_layout))
        handle.write("\n## Missing VR Papyrus Native Bypass Requirements\n\n")
        handle.write(fmt_tokens(missing_vr_papyrus_bypass))
        handle.write("\n## VR Connection-Only Unsafe Game Calls\n\n")
        handle.write(fmt_tokens(vr_connection_only_safety_errors))
        handle.write("\n## Missing VR CommonLib Accessor Use Tokens\n\n")
        handle.write(fmt_token_map(missing_vr_commonlib_accessors))
        handle.write("\n## Forbidden Raw VR Game-Object Member Reads\n\n")
        handle.write(fmt_token_map(forbidden_vr_raw_members))

    print(f"Missing World.cpp inventory observer wiring tokens: {len(missing_world)}")
    print(f"Missing World.cpp movement observer wiring tokens: {len(missing_movement_world)}")
    print(f"Missing World.cpp activation observer wiring tokens: {len(missing_activation_world)}")
    print(f"Missing World.cpp magic observer wiring tokens: {len(missing_magic_world)}")
    print(f"Missing World.cpp combat observer wiring tokens: {len(missing_combat_world)}")
    print(f"Missing World.cpp projectile observer wiring tokens: {len(missing_projectile_world)}")
    print(f"Missing World.cpp grab observer wiring tokens: {len(missing_grab_world)}")
    print(f"Missing World.cpp save/load observer wiring tokens: {len(missing_saveload_world)}")
    print(f"Missing xmake inventory observer flag tokens: {len(missing_xmake)}")
    print(f"Missing xmake movement observer flag tokens: {len(missing_movement_xmake)}")
    print(f"Missing xmake activation observer flag tokens: {len(missing_activation_xmake)}")
    print(f"Missing xmake magic observer flag tokens: {len(missing_magic_xmake)}")
    print(f"Missing xmake combat observer flag tokens: {len(missing_combat_xmake)}")
    print(f"Missing xmake projectile observer flag tokens: {len(missing_projectile_xmake)}")
    print(f"Missing xmake grab observer flag tokens: {len(missing_grab_xmake)}")
    print(f"Missing xmake save/load observer flag tokens: {len(missing_saveload_xmake)}")
    print(f"Missing VRMovementService observer tokens: {len(missing_movement)}")
    print(f"Forbidden VRMovementService actor-sync tokens: {len(forbidden_movement)}")
    print(f"Missing VR movement relay tokens: {missing_movement_relay_count}")
    print(f"Missing VR pose relay tokens: {missing_pose_relay_count}")
    print(f"Missing VRInventoryService observer tokens: {len(missing_inventory)}")
    print(f"Forbidden VRInventoryService full-sync tokens: {len(forbidden_inventory)}")
    print(f"Missing VR equipment relay tokens: {missing_equipment_relay_count}")
    print(f"Missing remote VR avatar component tokens: {missing_remote_avatar_component_count}")
    print(f"Forbidden remote VR avatar component stale-cache tokens: {forbidden_remote_avatar_component_count}")
    print(f"Missing remote player proxy service tokens: {missing_remote_player_proxy_count}")
    print(f"Forbidden remote player proxy service mutation tokens: {forbidden_remote_player_proxy_count}")
    print(f"Missing VR player cell handoff tokens: {missing_player_cell_handoff_count}")
    print(f"Missing VR save/load handoff tokens: {missing_saveload_handoff_count}")
    print(f"Missing VRActivationService observer tokens: {len(missing_activation)}")
    print(f"Forbidden VRActivationService object-sync tokens: {len(forbidden_activation)}")
    print(f"Missing VR activation relay tokens: {missing_activation_relay_count}")
    print(f"Missing VRMagicService observer tokens: {len(missing_magic)}")
    print(f"Forbidden VRMagicService full-sync tokens: {len(forbidden_magic)}")
    print(f"Missing VR magic relay tokens: {missing_magic_relay_count}")
    print(f"Missing VRCombatService observer tokens: {len(missing_combat)}")
    print(f"Forbidden VRCombatService full-sync tokens: {len(forbidden_combat)}")
    print(f"Missing VR combat relay tokens: {missing_combat_relay_count}")
    print(f"Missing VRProjectileService observer tokens: {len(missing_projectile)}")
    print(f"Forbidden VRProjectileService full-sync tokens: {len(forbidden_projectile)}")
    print(f"Missing VR projectile relay tokens: {missing_projectile_relay_count}")
    print(f"Missing VR grab relay tokens: {missing_grab_relay_count}")
    print(f"Missing VRSaveLoadService observer tokens: {len(missing_saveload)}")
    print(f"Forbidden VRSaveLoadService save/load mutation tokens: {len(forbidden_saveload)}")
    print(f"Missing CommonLib-informed layout/accessor tokens: {missing_layout_count}")
    print(f"Forbidden stale layout tokens: {forbidden_layout_count}")
    print(f"Missing VR Papyrus native bypass requirements: {len(missing_vr_papyrus_bypass)}")
    print(f"VR connection-only unsafe game-call findings: {len(vr_connection_only_safety_errors)}")
    print(f"Missing VR CommonLib accessor use tokens: {missing_vr_commonlib_accessor_count}")
    print(f"Forbidden raw VR game-object member reads: {forbidden_vr_raw_member_count}")
    print(f"VRMovementService.h exists: {movement_header_path.exists()}")
    print(f"VRMovementService.cpp exists: {movement_path.exists()}")
    print(f"VRInventoryService.h exists: {inventory_header_path.exists()}")
    print(f"VRInventoryService.cpp exists: {inventory_path.exists()}")
    print(f"VRActivationService.h exists: {activation_header_path.exists()}")
    print(f"VRActivationService.cpp exists: {activation_path.exists()}")
    print(f"VRMagicService.h exists: {magic_header_path.exists()}")
    print(f"VRMagicService.cpp exists: {magic_path.exists()}")
    print(f"VRCombatService.h exists: {combat_header_path.exists()}")
    print(f"VRCombatService.cpp exists: {combat_path.exists()}")
    print(f"VRProjectileService.h exists: {projectile_header_path.exists()}")
    print(f"VRProjectileService.cpp exists: {projectile_path.exists()}")
    print(f"VRGrabService.h exists: {grab_header_path.exists()}")
    print(f"VRGrabService.cpp exists: {grab_path.exists()}")
    print(f"VRSaveLoadService.h exists: {saveload_header_path.exists()}")
    print(f"VRSaveLoadService.cpp exists: {saveload_path.exists()}")

    if (
        missing_world
        or missing_movement_world
        or missing_activation_world
        or missing_magic_world
        or missing_combat_world
        or missing_projectile_world
        or missing_grab_world
        or missing_saveload_world
        or missing_xmake
        or missing_movement_xmake
        or missing_activation_xmake
        or missing_magic_xmake
        or missing_combat_xmake
        or missing_projectile_xmake
        or missing_grab_xmake
        or missing_saveload_xmake
        or missing_movement
        or forbidden_movement
        or missing_inventory
        or forbidden_inventory
        or missing_activation
        or forbidden_activation
        or missing_magic
        or forbidden_magic
        or missing_combat
        or forbidden_combat
        or missing_projectile
        or forbidden_projectile
        or missing_saveload
        or forbidden_saveload
        or missing_movement_relay
        or missing_pose_relay
        or missing_equipment_relay
        or missing_remote_avatar_component
        or forbidden_remote_avatar_component
        or missing_remote_player_proxy
        or forbidden_remote_player_proxy
        or missing_player_cell_handoff
        or missing_saveload_handoff
        or missing_activation_relay
        or missing_magic_relay
        or missing_combat_relay
        or missing_projectile_relay
        or missing_grab_relay
        or missing_layout
        or forbidden_layout
        or missing_vr_papyrus_bypass
        or vr_connection_only_safety_errors
        or missing_vr_commonlib_accessors
        or forbidden_vr_raw_members
        or not movement_header_path.exists()
        or not movement_path.exists()
        or not inventory_header_path.exists()
        or not inventory_path.exists()
        or not activation_header_path.exists()
        or not activation_path.exists()
        or not magic_header_path.exists()
        or not magic_path.exists()
        or not combat_header_path.exists()
        or not combat_path.exists()
        or not projectile_header_path.exists()
        or not projectile_path.exists()
        or not grab_header_path.exists()
        or not grab_path.exists()
        or not saveload_header_path.exists()
        or not saveload_path.exists()
    ):
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
