#!/usr/bin/env python3
"""Desktop-side control/readout helper for the SkyrimTogetherVR file handoff."""

from __future__ import annotations

import argparse
import html
import json
import os
import pathlib
import re
import sys
import tempfile
import threading
import time
import webbrowser

import vr_paths


DEFAULT_ENDPOINT = "127.0.0.1:10578"
COMMAND_FILE = "SkyrimTogetherVR.command"
CONFIG_FILE = "SkyrimTogetherVR.connection"
READOUT_FILES = {
    "status": "SkyrimTogetherVR.status",
    "pose": "SkyrimTogetherVR.pose",
    "avatar": "SkyrimTogetherVR.avatar",
    "remoteplayers": "SkyrimTogetherVR.remoteplayers",
    "movement": "SkyrimTogetherVR.movement",
    "inventory": "SkyrimTogetherVR.inventory",
    "discovery": "SkyrimTogetherVR.discovery",
    "playercell": "SkyrimTogetherVR.playercell",
    "activation": "SkyrimTogetherVR.activation",
    "magic": "SkyrimTogetherVR.magic",
    "combat": "SkyrimTogetherVR.combat",
    "projectile": "SkyrimTogetherVR.projectile",
    "grab": "SkyrimTogetherVR.grab",
    "compat": "SkyrimTogetherVR.compatibility",
    "higgs": "SkyrimTogetherVR.higgs",
    "higgsnet": "SkyrimTogetherVR.higgsnet",
    "planck": "SkyrimTogetherVR.planck",
    "saveload": "SkyrimTogetherVR.saveload",
}

SUMMARY_FIELDS = {
    "status": ("state", "online", "playerId", "error"),
    "pose": ("localPoseAvailable", "remotePoseCount"),
    "avatar": ("ready", "actorTargetsEnabled", "actorSkeletonWritesEnabled", "actorMovementAuthorityEnabled", "remotePlayerCount", "remotePoseMatchCount", "remoteEquipmentMatchCount", "equipmentWeaponDrawQueuedCount", "equipmentMissingFormIdCount", "equipmentMissingActorCount", "sameSpaceCount", "actorTargetSkippedDifferentCellCount", "actorTargetSkippedDifferentWorldSpaceCount", "spellOriginValidCount", "arrowOriginValidCount", "bowAimValidCount", "leftWeaponOffsetValidCount", "rightWeaponOffsetValidCount", "primaryMagicOffsetValidCount", "secondaryMagicOffsetValidCount", "actorTargetAttemptCount", "hmdCopiedCount", "leftHandCopiedCount", "rightHandCopiedCount", "vrikDetectedCount", "vrikInterfaceAvailableCount", "invalidVrikCount", "movementAppliedCount", "invalidTransformCount", "invalidMovementCount"),
    "remoteplayers": ("ready", "online", "trackedPlayerCount", "joinedPlayerCount", "poseMatchedCount", "movementMatchedCount", "equipmentMatchedCount", "activationMatchedCount", "magicMatchedCount", "combatMatchedCount", "projectileMatchedCount", "grabMatchedCount", "higgsMatchedCount", "vrikMatchedCount", "sameCellCount", "sameWorldSpaceCount", "sameSpaceCount", "avatarValidationReadyCount", "higgsAvatarValidationReadyCount"),
    "movement": ("ready", "localMovementAvailable", "remoteMovementCount"),
    "inventory": ("ready", "policy", "fullInventoryTraversal", "inventoryMutation", "remoteEquipmentMutation", "normalInventoryPackets", "localEquipmentAvailable", "remoteEquipmentCount"),
    "discovery": ("ready", "actorCount", "actorLimit", "currentGrid", "centerGrid", "cachedWorldSpaceFormId", "cachedInteriorCellFormId", "playerCellFormId", "playerWorldSpaceFormId", "locationFormId"),
    "playercell": ("ready", "online", "localPlayerId", "currentLevel", "gridCellRequestCount", "exteriorCellRequestCount", "interiorCellRequestCount", "levelRequestCount", "offlineSkippedRequestCount", "worldSpaceTranslationFailureCount"),
    "activation": ("localActivationAvailable", "remoteActivationCount"),
    "magic": ("localMagicEffectAvailable", "remoteMagicEffectCount"),
    "combat": ("localCombatHitAvailable", "remoteCombatHitCount"),
    "projectile": ("localProjectileEventAvailable", "remoteProjectileEventCount"),
    "grab": ("localGrabAvailable", "remoteGrabCount"),
    "compat": ("ready", "higgs.installed", "higgs.loaded", "planck.installed", "planck.loaded", "vrPhysicsCompatibilityModInstalled", "hookMode", "gameplayMode", "remoteAvatarPolicy", "remotePlayerProxyPolicy", "discoveryPolicy", "playerCellPolicy", "movementPolicy", "equipmentPolicy", "activationPolicy", "inventoryPolicy", "magicPolicy", "combatPolicy", "projectilePolicy", "grabPolicy", "higgsRelayPolicy", "saveLoadPolicy", "unvalidatedGameplayHooksSuppressed", "higgsPolicy", "planckPolicy"),
    "higgs": ("bridge.loaded", "bridge.sequence", "higgs.detected", "higgs.interfaceAvailable", "higgs.callbacksRegistered", "higgs.snapshotAvailable", "higgs.snapshotSequence", "higgs.twoHanding", "recentEventCount"),
    "higgsnet": ("ready", "localHiggsAvailable", "remoteHiggsCount"),
    "planck": ("bridge.loaded", "bridge.sequence", "planck.detected", "planck.interfaceRequestAttempted", "planck.interfaceAvailable", "planck.buildNumber", "planck.currentHitEventAvailable", "planck.currentHitEventObservationOnly", "planck.lastHitDataAvailable", "planck.lastHitDataProbeEnabled", "planck.lastHitDataReason", "planck.lastHitDataBoundary", "planck.policy"),
    "saveload": ("ready", "loadGameObserved", "loadGameCount", "readyAfterLastLoad", "playerCell.serverBaseId", "playerWorldSpace.serverBaseId"),
}

REMOTE_ID_PATTERN = re.compile(r"^([^.]+)\.(\d+)\.")


def default_game_path() -> pathlib.Path:
    return vr_paths.default_skyrim_vr_path()


def resolve_handoff_dir(args: argparse.Namespace) -> pathlib.Path:
    if args.handoff_dir:
        return args.handoff_dir.expanduser().resolve()

    game_path = args.game_path.expanduser().resolve()
    return game_path / "Data" / "SkyrimTogetherReborn"


def read_key_value_file(path: pathlib.Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.exists():
        return values

    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def write_atomic(path: pathlib.Path, contents: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent, delete=False) as handle:
        handle.write(contents)
        temp_name = handle.name

    os.replace(temp_name, path)


def command_path(handoff_dir: pathlib.Path) -> pathlib.Path:
    return handoff_dir / COMMAND_FILE


def config_path(handoff_dir: pathlib.Path) -> pathlib.Path:
    return handoff_dir / CONFIG_FILE


def read_connection_config(handoff_dir: pathlib.Path) -> dict[str, str]:
    values = read_key_value_file(config_path(handoff_dir))
    endpoint = values.get("endpoint", "").strip() or DEFAULT_ENDPOINT
    password = values.get("password", "")
    return {"endpoint": endpoint, "password": password}


def write_connection_config(handoff_dir: pathlib.Path, endpoint: str, password: str) -> pathlib.Path:
    endpoint = endpoint.strip()
    if not endpoint:
        raise ValueError("endpoint is required")

    contents = f"endpoint={endpoint}\npassword={password}\n"
    path = config_path(handoff_dir)
    write_atomic(path, contents)
    return path


def write_connect_command(handoff_dir: pathlib.Path, endpoint: str, password: str) -> pathlib.Path:
    endpoint = endpoint.strip()
    write_connection_config(handoff_dir, endpoint, password)
    contents = f"action=connect\nendpoint={endpoint}\npassword={password}\n"
    path = command_path(handoff_dir)
    write_atomic(path, contents)
    return path


def write_disconnect_command(handoff_dir: pathlib.Path) -> pathlib.Path:
    path = command_path(handoff_dir)
    write_atomic(path, "action=disconnect\n")
    return path


def read_readouts(handoff_dir: pathlib.Path) -> dict[str, dict[str, str]]:
    return {
        name: read_key_value_file(handoff_dir / file_name)
        for name, file_name in READOUT_FILES.items()
    }


def format_readout(name: str, values: dict[str, str], *, all_fields: bool = False) -> str:
    if not values:
        return f"[{name}] missing"

    if all_fields:
        fields = sorted(values)
    else:
        fields = [field for field in SUMMARY_FIELDS.get(name, ()) if field in values]
        if not fields:
            fields = sorted(values)[:8]

    pairs = " ".join(f"{field}={values[field]}" for field in fields)
    return f"[{name}] {pairs}"


def collect_remote_ids(values: dict[str, str], prefix: str) -> list[str]:
    ids = set()
    for key in values:
        match = REMOTE_ID_PATTERN.match(key)
        if match and match.group(1) == prefix:
            ids.add(match.group(2))

    return sorted(ids, key=lambda value: int(value) if value.isdigit() else value)


def game_id_summary(values: dict[str, str], prefix: str) -> str:
    mod_id = values.get(f"{prefix}.serverModId")
    base_id = values.get(f"{prefix}.serverBaseId")
    if mod_id is None and base_id is None:
        return ""
    return f"{mod_id or '?'}:{base_id or '?'}"


def bool_summary(value: str | None) -> str:
    if value == "1":
        return "yes"
    if value == "0":
        return "no"
    return "?"


def ensure_remote_player(players: dict[str, dict[str, str]], player_id: str) -> dict[str, str]:
    return players.setdefault(player_id, {"playerId": player_id})


def build_remote_players_payload(readouts: dict[str, dict[str, str]]) -> list[dict[str, str]]:
    players: dict[str, dict[str, str]] = {}

    proxy_values = readouts.get("remoteplayers", {})
    for player_id in collect_remote_ids(proxy_values, "remotePlayer"):
        prefix = f"remotePlayer.{player_id}"
        player = ensure_remote_player(players, player_id)
        username = proxy_values.get(f"{prefix}.username", "")
        level = proxy_values.get(f"{prefix}.level", "0")
        same_cell = bool_summary(proxy_values.get(f"{prefix}.sameCell"))
        same_world = bool_summary(proxy_values.get(f"{prefix}.sameWorldSpace"))
        same_space = bool_summary(proxy_values.get(f"{prefix}.sameSpace"))
        pose = bool_summary(proxy_values.get(f"{prefix}.poseAvailable"))
        hmd = bool_summary(proxy_values.get(f"{prefix}.hmdAvailable"))
        left_hand = bool_summary(proxy_values.get(f"{prefix}.leftHandAvailable"))
        right_hand = bool_summary(proxy_values.get(f"{prefix}.rightHandAvailable"))
        vrik_detected = bool_summary(proxy_values.get(f"{prefix}.vrikDetected"))
        vrik_api = bool_summary(proxy_values.get(f"{prefix}.vrikInterfaceAvailable"))
        vrik = bool_summary(proxy_values.get(f"{prefix}.vrikAvailable"))
        movement = bool_summary(proxy_values.get(f"{prefix}.movementAvailable"))
        equipment = bool_summary(proxy_values.get(f"{prefix}.equipmentAvailable"))
        activation = bool_summary(proxy_values.get(f"{prefix}.activationAvailable"))
        magic = bool_summary(proxy_values.get(f"{prefix}.magicAvailable"))
        combat = bool_summary(proxy_values.get(f"{prefix}.combatAvailable"))
        projectile = bool_summary(proxy_values.get(f"{prefix}.projectileAvailable"))
        grab = bool_summary(proxy_values.get(f"{prefix}.grabAvailable"))
        higgs = bool_summary(proxy_values.get(f"{prefix}.higgsAvailable"))
        avatar_ready = bool_summary(proxy_values.get(f"{prefix}.avatarValidationReady"))
        avatar_blocker = proxy_values.get(f"{prefix}.avatarValidationBlocker", "?")
        higgs_avatar_ready = bool_summary(proxy_values.get(f"{prefix}.higgsAvatarValidationReady"))
        higgs_avatar_blocker = proxy_values.get(f"{prefix}.higgsAvatarValidationBlocker", "?")
        cell = game_id_summary(proxy_values, f"{prefix}.cell")
        world = game_id_summary(proxy_values, f"{prefix}.worldSpace")
        player["proxy"] = (
            f"name={username or '?'} level={level} cell={cell or '?'} world={world or '?'} "
            f"sameCell={same_cell} sameWorld={same_world} sameSpace={same_space} "
            f"pose={pose} hmd={hmd} leftHand={left_hand} rightHand={right_hand} "
            f"vrikDetected={vrik_detected} vrikApi={vrik_api} vrik={vrik} "
            f"movement={movement} equipment={equipment} activation={activation} magic={magic} "
            f"combat={combat} projectile={projectile} grab={grab} higgs={higgs} "
            f"avatarReady={avatar_ready} blocker={avatar_blocker} "
            f"higgsAvatarReady={higgs_avatar_ready} higgsBlocker={higgs_avatar_blocker}"
        )

    pose_values = readouts.get("pose", {})
    for player_id in collect_remote_ids(pose_values, "remote"):
        prefix = f"remote.{player_id}"
        player = ensure_remote_player(players, player_id)
        hmd = pose_values.get(f"{prefix}.hmd.position", "")
        vrik = pose_values.get(f"{prefix}.vrik.detected")
        vrik_summary = " vrik=detected" if vrik == "1" else " vrik=missing" if vrik == "0" else ""
        player["pose"] = f"hmd={hmd}{vrik_summary}" if hmd else f"available{vrik_summary}"

    movement_values = readouts.get("movement", {})
    for player_id in collect_remote_ids(movement_values, "remoteMovement"):
        prefix = f"remoteMovement.{player_id}"
        player = ensure_remote_player(players, player_id)
        position = movement_values.get(f"{prefix}.position", "")
        direction = movement_values.get(f"{prefix}.direction", "")
        player["movement"] = f"pos={position} dir={direction}" if position or direction else "available"

    avatar_values = readouts.get("avatar", {})
    avatar_player_id = avatar_values.get("last.playerId")
    if avatar_player_id and avatar_player_id != "0":
        player = ensure_remote_player(players, avatar_player_id)
        actor_targets = bool_summary(avatar_values.get("actorTargetsEnabled"))
        skeleton_writes = bool_summary(avatar_values.get("actorSkeletonWritesEnabled"))
        movement_authority = bool_summary(avatar_values.get("actorMovementAuthorityEnabled"))
        head = bool_summary(avatar_values.get("last.headNodeFound"))
        left = bool_summary(avatar_values.get("last.leftHandNodeFound"))
        right = bool_summary(avatar_values.get("last.rightHandNodeFound"))
        hmd_copied = bool_summary(avatar_values.get("last.hmdCopied"))
        movement = bool_summary(avatar_values.get("last.movementApplied"))
        fallback = bool_summary(avatar_values.get("last.hmdFallbackMovementApplied"))
        same_space = bool_summary(avatar_values.get("last.sameSpaceForApply"))
        same_cell = bool_summary(avatar_values.get("last.sameCell"))
        same_world = bool_summary(avatar_values.get("last.sameWorldSpace"))
        local_movement = bool_summary(avatar_values.get("last.localMovementAvailable"))
        remote_movement = bool_summary(avatar_values.get("last.remoteMovementAvailable"))
        skip_cell = avatar_values.get("actorTargetSkippedDifferentCellCount", "0")
        skip_world = avatar_values.get("actorTargetSkippedDifferentWorldSpaceCount", "0")
        equipment_matches = avatar_values.get("remoteEquipmentMatchCount", "0")
        equipment_draw_queue_count = avatar_values.get("equipmentWeaponDrawQueuedCount", "0")
        equipment_missing_form_count = avatar_values.get("equipmentMissingFormIdCount", "0")
        equipment_missing_actor_count = avatar_values.get("equipmentMissingActorCount", "0")
        equipment_sequence = avatar_values.get("lastEquipment.sequence", "0")
        equipment_form = avatar_values.get("lastEquipment.formId", "0")
        equipment_drawn = bool_summary(avatar_values.get("lastEquipment.weaponDrawn"))
        equipment_desired_drawn = bool_summary(avatar_values.get("lastEquipment.desiredWeaponDrawn"))
        equipment_draw_queued = bool_summary(avatar_values.get("lastEquipment.weaponDrawQueued"))
        spell_origin = bool_summary(avatar_values.get("last.spellOriginValid"))
        spell_destination = bool_summary(avatar_values.get("last.spellDestinationValid"))
        arrow_origin = bool_summary(avatar_values.get("last.arrowOriginValid"))
        arrow_destination = bool_summary(avatar_values.get("last.arrowDestinationValid"))
        bow_aim = bool_summary(avatar_values.get("last.bowAimValid"))
        bow_rotation = bool_summary(avatar_values.get("last.bowRotationValid"))
        left_weapon = bool_summary(avatar_values.get("last.leftWeaponOffsetValid"))
        right_weapon = bool_summary(avatar_values.get("last.rightWeaponOffsetValid"))
        primary_magic = bool_summary(avatar_values.get("last.primaryMagicOffsetValid"))
        secondary_magic = bool_summary(avatar_values.get("last.secondaryMagicOffsetValid"))
        primary_aim = bool_summary(avatar_values.get("last.primaryMagicAimValid"))
        secondary_aim = bool_summary(avatar_values.get("last.secondaryMagicAimValid"))
        invalid_transforms = avatar_values.get("last.invalidTransformCount", "0")
        invalid_movement = bool_summary(avatar_values.get("last.invalidMovement"))
        player["avatar"] = (
            f"targets={actor_targets} skeletonWrites={skeleton_writes} moveAuthority={movement_authority} "
            f"head={head} left={left} right={right} "
            f"hmdCopied={hmd_copied} movement={movement} fallback={fallback} "
            f"invalidTransforms={invalid_transforms} invalidMovement={invalid_movement} "
            f"sameSpace={same_space} sameCell={same_cell} sameWorld={same_world} "
            f"localMove={local_movement} remoteMove={remote_movement} "
            f"skipCell={skip_cell} skipWorld={skip_world} "
            f"equipment={equipment_matches} drawQ={equipment_draw_queue_count}/{equipment_draw_queued} "
            f"equipSeq={equipment_sequence} equipForm={equipment_form} "
            f"drawn={equipment_drawn}->{equipment_desired_drawn} "
            f"equipMissing={equipment_missing_form_count}/{equipment_missing_actor_count} "
            f"spell={spell_origin}/{spell_destination} arrow={arrow_origin}/{arrow_destination} "
            f"bow={bow_aim}/{bow_rotation} weapon={left_weapon}/{right_weapon} "
            f"magic={primary_magic}/{secondary_magic} aim={primary_aim}/{secondary_aim}"
        )

    equipment_values = readouts.get("inventory", {})
    for remote_index in collect_remote_ids(equipment_values, "remoteEquipment"):
        prefix = f"remoteEquipment.{remote_index}"
        player_id = equipment_values.get(f"{prefix}.playerId", remote_index)
        player = ensure_remote_player(players, player_id)
        right_weapon = game_id_summary(equipment_values, f"{prefix}.rightWeapon")
        left_weapon = game_id_summary(equipment_values, f"{prefix}.leftWeapon")
        drawn = bool_summary(equipment_values.get(f"{prefix}.weaponDrawn"))
        player["equipment"] = f"drawn={drawn} right={right_weapon or '?'} left={left_weapon or '?'}"

    activation_values = readouts.get("activation", {})
    for player_id in collect_remote_ids(activation_values, "remoteActivation"):
        prefix = f"remoteActivation.{player_id}"
        player = ensure_remote_player(players, player_id)
        obj = game_id_summary(activation_values, f"{prefix}.object")
        open_state = activation_values.get(f"{prefix}.openState", "?")
        player["activation"] = f"object={obj or '?'} openState={open_state}"

    magic_values = readouts.get("magic", {})
    for player_id in collect_remote_ids(magic_values, "remoteMagicEffect"):
        prefix = f"remoteMagicEffect.{player_id}"
        player = ensure_remote_player(players, player_id)
        effect = game_id_summary(magic_values, f"{prefix}.effect")
        caster = bool_summary(magic_values.get(f"{prefix}.casterIsPlayer"))
        target = bool_summary(magic_values.get(f"{prefix}.targetIsPlayer"))
        player["magic"] = f"effect={effect or '?'} casterPlayer={caster} targetPlayer={target}"

    combat_values = readouts.get("combat", {})
    for player_id in collect_remote_ids(combat_values, "remoteCombatHit"):
        prefix = f"remoteCombatHit.{player_id}"
        player = ensure_remote_player(players, player_id)
        hitter = game_id_summary(combat_values, f"{prefix}.hitter")
        hittee = game_id_summary(combat_values, f"{prefix}.hittee")
        source = game_id_summary(combat_values, f"{prefix}.source")
        projectile = game_id_summary(combat_values, f"{prefix}.projectile")
        planck = bool_summary(combat_values.get(f"{prefix}.planckHit"))
        player["combat"] = (
            f"hitter={hitter or '?'} hittee={hittee or '?'} source={source or '?'} "
            f"projectile={projectile or '?'} planck={planck}"
        )

    projectile_values = readouts.get("projectile", {})
    for player_id in collect_remote_ids(projectile_values, "remoteProjectileEvent"):
        prefix = f"remoteProjectileEvent.{player_id}"
        player = ensure_remote_player(players, player_id)
        kind = projectile_values.get(f"{prefix}.kind", "?")
        spell = game_id_summary(projectile_values, f"{prefix}.spell")
        ammo = game_id_summary(projectile_values, f"{prefix}.ammo")
        player["projectile"] = f"kind={kind} spell={spell or '?'} ammo={ammo or '?'}"

    grab_values = readouts.get("grab", {})
    for player_id in collect_remote_ids(grab_values, "remoteGrab"):
        prefix = f"remoteGrab.{player_id}"
        player = ensure_remote_player(players, player_id)
        obj = game_id_summary(grab_values, f"{prefix}.object")
        grabbed = bool_summary(grab_values.get(f"{prefix}.grabbed"))
        player["grab"] = f"grabbed={grabbed} object={obj or '?'}"

    higgsnet_values = readouts.get("higgsnet", {})
    for player_id in collect_remote_ids(higgsnet_values, "remoteHiggs"):
        prefix = f"remoteHiggs.{player_id}"
        player = ensure_remote_player(players, player_id)
        api = bool_summary(higgsnet_values.get(f"{prefix}.interfaceAvailable"))
        callbacks = bool_summary(higgsnet_values.get(f"{prefix}.callbacksRegistered"))
        snapshot = bool_summary(higgsnet_values.get(f"{prefix}.snapshotAvailable"))
        two_handing = bool_summary(higgsnet_values.get(f"{prefix}.twoHanding"))
        event_kind = higgsnet_values.get(f"{prefix}.lastEvent.kind", "?")
        left_object = game_id_summary(higgsnet_values, f"{prefix}.left.grabbedObject")
        right_object = game_id_summary(higgsnet_values, f"{prefix}.right.grabbedObject")
        player["higgs"] = (
            f"api={api} callbacks={callbacks} snapshot={snapshot} twoHanding={two_handing} "
            f"event={event_kind} left={left_object or '?'} right={right_object or '?'}"
        )

    return [players[player_id] for player_id in sorted(players, key=lambda value: int(value) if value.isdigit() else value)]


def build_higgs_summary(readouts: dict[str, dict[str, str]]) -> dict[str, str]:
    values = readouts.get("higgs", {})
    net_values = readouts.get("higgsnet", {})
    if not values and not net_values:
        return {}

    return {
        "higgsBridge": bool_summary(values.get("bridge.loaded")),
        "higgsBridgeSeq": values.get("bridge.sequence", "0"),
        "higgsDetected": bool_summary(values.get("higgs.detected")),
        "higgsInterface": bool_summary(values.get("higgs.interfaceAvailable")),
        "higgsCallbacks": bool_summary(values.get("higgs.callbacksRegistered")),
        "higgsSnapshot": bool_summary(values.get("higgs.snapshotAvailable")),
        "higgsSnapshotSeq": values.get("higgs.snapshotSequence", "0"),
        "higgsTwoHanding": bool_summary(values.get("higgs.twoHanding")),
        "higgsEventCount": values.get("recentEventCount", "0"),
        "higgsRelayReady": bool_summary(net_values.get("ready")),
        "higgsRelayLocal": bool_summary(net_values.get("localHiggsAvailable")),
        "higgsRelayRemote": net_values.get("remoteHiggsCount", "0"),
    }


def build_planck_summary(readouts: dict[str, dict[str, str]]) -> dict[str, str]:
    values = readouts.get("planck", {})
    if not values:
        return {}

    return {
        "planckBridge": bool_summary(values.get("bridge.loaded")),
        "planckBridgeSeq": values.get("bridge.sequence", "0"),
        "planckDetected": bool_summary(values.get("planck.detected")),
        "planckRequest": bool_summary(values.get("planck.interfaceRequestAttempted")),
        "planckInterface": bool_summary(values.get("planck.interfaceAvailable")),
        "planckBuild": values.get("planck.buildNumber", "0"),
        "planckCurrentHit": bool_summary(values.get("planck.currentHitEventAvailable")),
        "planckCurrentHitObservationOnly": bool_summary(values.get("planck.currentHitEventObservationOnly")),
        "planckLastHitData": bool_summary(values.get("planck.lastHitDataAvailable")),
        "planckLastHitProbe": bool_summary(values.get("planck.lastHitDataProbeEnabled")),
        "planckLastHitReason": values.get("planck.lastHitDataReason", "?"),
        "planckLastHitBoundary": values.get("planck.lastHitDataBoundary", "?"),
        "planckPolicy": values.get("planck.policy", "?"),
    }


def build_compatibility_summary(readouts: dict[str, dict[str, str]]) -> dict[str, str]:
    values = readouts.get("compat", {})
    if not values:
        return {}

    return {
        "higgsInstalled": bool_summary(values.get("higgs.installed")),
        "higgsLoaded": bool_summary(values.get("higgs.loaded")),
        "planckInstalled": bool_summary(values.get("planck.installed")),
        "planckLoaded": bool_summary(values.get("planck.loaded")),
        "physicsCompat": bool_summary(values.get("vrPhysicsCompatibilityModInstalled")),
        "hookMode": values.get("hookMode", "?"),
        "gameplayMode": values.get("gameplayMode", "?"),
        "remoteAvatar": values.get("remoteAvatarPolicy", "?"),
        "proxy": values.get("remotePlayerProxyPolicy", "?"),
        "discovery": values.get("discoveryPolicy", "?"),
        "playerCell": values.get("playerCellPolicy", "?"),
        "movement": values.get("movementPolicy", "?"),
        "equipment": values.get("equipmentPolicy", "?"),
        "activation": values.get("activationPolicy", "?"),
        "inventory": values.get("inventoryPolicy", "?"),
        "magic": values.get("magicPolicy", "?"),
        "combat": values.get("combatPolicy", "?"),
        "projectile": values.get("projectilePolicy", "?"),
        "grab": values.get("grabPolicy", "?"),
        "higgsRelay": values.get("higgsRelayPolicy", "?"),
        "saveLoad": values.get("saveLoadPolicy", "?"),
        "unvalidatedSuppressed": bool_summary(values.get("unvalidatedGameplayHooksSuppressed")),
        "higgsPolicy": values.get("higgsPolicy", "?"),
        "planckPolicy": values.get("planckPolicy", "?"),
    }


def format_remote_player(player: dict[str, str]) -> str:
    fields = [
        f"playerId={player.get('playerId', '?')}",
        f"proxy={player.get('proxy', '-')}",
        f"pose={player.get('pose', '-')}",
        f"avatar={player.get('avatar', '-')}",
        f"movement={player.get('movement', '-')}",
        f"equipment={player.get('equipment', '-')}",
        f"activation={player.get('activation', '-')}",
        f"magic={player.get('magic', '-')}",
        f"combat={player.get('combat', '-')}",
        f"projectile={player.get('projectile', '-')}",
        f"grab={player.get('grab', '-')}",
        f"higgs={player.get('higgs', '-')}",
    ]
    return "[player] " + " ".join(fields)


def build_readout_payload(handoff_dir: pathlib.Path, *, all_fields: bool = False) -> dict[str, object]:
    readouts = read_readouts(handoff_dir)
    connection_config = read_connection_config(handoff_dir)
    return {
        "handoffDir": str(handoff_dir),
        "commandFile": str(command_path(handoff_dir)),
        "configFile": str(config_path(handoff_dir)),
        "connectionConfig": {
            "endpoint": connection_config["endpoint"],
            "passwordSet": "1" if connection_config["password"] else "0",
        },
        "files": {
            name: str(handoff_dir / file_name)
            for name, file_name in READOUT_FILES.items()
        },
        "summaries": {
            name: format_readout(name, values, all_fields=all_fields)
            for name, values in readouts.items()
        },
        "readouts": readouts,
        "remotePlayers": build_remote_players_payload(readouts),
        "compatibility": build_compatibility_summary(readouts),
        "higgs": build_higgs_summary(readouts),
        "planck": build_planck_summary(readouts),
    }


def render_panel_html(default_endpoint: str, default_password: str) -> str:
    endpoint = html.escape(default_endpoint, quote=True)
    password = html.escape(default_password, quote=True)
    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>SkyrimTogetherVR Companion</title>
  <style>
    :root {{
      color-scheme: dark;
      font-family: Segoe UI, Roboto, Arial, sans-serif;
      background: #16181d;
      color: #eef1f5;
    }}
    body {{
      margin: 0;
      padding: 24px;
      background: #16181d;
    }}
    main {{
      max-width: 1120px;
      margin: 0 auto;
    }}
    h1 {{
      margin: 0 0 18px;
      font-size: 28px;
      font-weight: 650;
    }}
    .controls {{
      display: grid;
      grid-template-columns: minmax(220px, 1fr) minmax(120px, 0.45fr) repeat(3, auto);
      gap: 10px;
      align-items: end;
      margin-bottom: 18px;
    }}
    label {{
      display: grid;
      gap: 6px;
      font-size: 12px;
      color: #b8c2cf;
      text-transform: uppercase;
      letter-spacing: 0.02em;
    }}
    input {{
      min-height: 38px;
      border: 1px solid #39414f;
      border-radius: 6px;
      background: #20242c;
      color: #eef1f5;
      padding: 0 10px;
      font: inherit;
    }}
    button {{
      min-height: 40px;
      border: 0;
      border-radius: 6px;
      padding: 0 14px;
      color: #0d1117;
      background: #7dd3fc;
      font: inherit;
      font-weight: 650;
      cursor: pointer;
    }}
    button.secondary {{
      background: #cbd5e1;
    }}
    button.danger {{
      background: #fca5a5;
    }}
    #notice {{
      min-height: 20px;
      margin: 0 0 12px;
      color: #b8c2cf;
      font-size: 13px;
    }}
    #summaries {{
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
      gap: 10px;
    }}
    .readout {{
      border: 1px solid #313844;
      border-radius: 8px;
      background: #20242c;
      padding: 12px;
      min-height: 72px;
    }}
    .readout h2 {{
      margin: 0 0 8px;
      font-size: 14px;
      color: #7dd3fc;
      text-transform: uppercase;
      letter-spacing: 0.04em;
    }}
	    .readout pre {{
	      margin: 0;
	      white-space: pre-wrap;
	      word-break: break-word;
	      font: 13px/1.45 Consolas, ui-monospace, monospace;
	      color: #e5e7eb;
	    }}
	    .players-panel {{
	      margin-top: 18px;
	      border: 1px solid #313844;
	      border-radius: 8px;
	      background: #20242c;
	      padding: 12px;
	      overflow-x: auto;
	    }}
	    .players-panel h2 {{
	      margin: 0 0 10px;
	      font-size: 14px;
	      color: #7dd3fc;
	      text-transform: uppercase;
	      letter-spacing: 0.04em;
	    }}
	    table {{
	      width: 100%;
	      border-collapse: collapse;
	      font-size: 13px;
	    }}
	    th,
	    td {{
	      border-top: 1px solid #313844;
	      padding: 7px 6px;
	      text-align: left;
	      vertical-align: top;
	      white-space: nowrap;
	    }}
	    th {{
	      color: #b8c2cf;
	      font-weight: 650;
	    }}
	    td {{
	      color: #e5e7eb;
	    }}
    @media (max-width: 760px) {{
      body {{
        padding: 14px;
      }}
      .controls {{
        grid-template-columns: 1fr;
      }}
    }}
  </style>
</head>
<body>
<main>
  <h1>SkyrimTogetherVR Companion</h1>
  <section class="controls">
    <label>Endpoint
      <input id="endpoint" value="{endpoint}" autocomplete="off">
    </label>
    <label>Password
      <input id="password" value="{password}" type="password" autocomplete="off">
    </label>
    <button id="connect">Connect</button>
    <button id="disconnect" class="danger">Disconnect</button>
    <button id="refresh" class="secondary">Refresh</button>
	  </section>
	  <p id="notice">Loading handoff files...</p>
  <section id="summaries"></section>
  <section class="players-panel">
    <h2>VR Compatibility</h2>
    <div id="compatibility"></div>
  </section>
  <section class="players-panel">
    <h2>HIGGS</h2>
    <div id="higgs"></div>
  </section>
  <section class="players-panel">
    <h2>PLANCK</h2>
    <div id="planck"></div>
  </section>
	  <section class="players-panel">
	    <h2>Remote Players</h2>
	    <div id="players"></div>
	  </section>
	</main>
	<script>
const notice = document.getElementById("notice");
const summaries = document.getElementById("summaries");
const players = document.getElementById("players");
const compatibility = document.getElementById("compatibility");
const higgs = document.getElementById("higgs");
const planck = document.getElementById("planck");

function setNotice(text) {{
  notice.textContent = text;
}}

function render(payload) {{
  setNotice(`handoffDir=${{payload.handoffDir}}`);
  summaries.replaceChildren();
  const order = ["status", "compat", "pose", "avatar", "remoteplayers", "movement", "inventory", "discovery", "playercell", "activation", "magic", "combat", "projectile", "grab", "higgs", "higgsnet", "planck", "saveload"];
  for (const name of order) {{
    const card = document.createElement("article");
    card.className = "readout";
    const title = document.createElement("h2");
    title.textContent = name;
    const body = document.createElement("pre");
    body.textContent = payload.summaries[name] || `[${{name}}] missing`;
	    card.append(title, body);
	    summaries.append(card);
	  }}
	  renderCompatibility(payload.compatibility || {{}});
	  renderHiggs(payload.higgs || {{}});
	  renderPlanck(payload.planck || {{}});
	  renderPlayers(payload.remotePlayers || []);
	}}

	function renderCompatibility(summary) {{
	  compatibility.replaceChildren();
	  const columns = ["higgsInstalled", "higgsLoaded", "planckInstalled", "planckLoaded", "physicsCompat", "hookMode", "gameplayMode", "remoteAvatar", "proxy", "movement", "equipment", "activation", "inventory", "magic", "combat", "projectile", "grab", "higgsRelay", "saveLoad", "unvalidatedSuppressed", "higgsPolicy", "planckPolicy"];
	  const table = document.createElement("table");
	  const tbody = document.createElement("tbody");
	  for (const column of columns) {{
	    const row = document.createElement("tr");
	    const key = document.createElement("th");
	    const value = document.createElement("td");
	    key.textContent = column;
	    value.textContent = summary[column] || "-";
	    row.append(key, value);
	    tbody.append(row);
	  }}
	  table.append(tbody);
	  compatibility.append(table);
	}}

	function renderHiggs(summary) {{
	  higgs.replaceChildren();
	  const columns = ["higgsBridge", "higgsBridgeSeq", "higgsDetected", "higgsInterface", "higgsCallbacks", "higgsSnapshot", "higgsSnapshotSeq", "higgsTwoHanding", "higgsEventCount", "higgsRelayReady", "higgsRelayLocal", "higgsRelayRemote"];
	  const table = document.createElement("table");
	  const tbody = document.createElement("tbody");
	  for (const column of columns) {{
	    const row = document.createElement("tr");
	    const key = document.createElement("th");
	    const value = document.createElement("td");
	    key.textContent = column;
	    value.textContent = summary[column] || "-";
	    row.append(key, value);
	    tbody.append(row);
	  }}
	  table.append(tbody);
	  higgs.append(table);
	}}

	function renderPlanck(summary) {{
	  planck.replaceChildren();
	  const columns = ["planckBridge", "planckBridgeSeq", "planckDetected", "planckRequest", "planckInterface", "planckBuild", "planckCurrentHit", "planckCurrentHitObservationOnly", "planckLastHitData", "planckLastHitProbe", "planckLastHitReason", "planckLastHitBoundary", "planckPolicy"];
	  const table = document.createElement("table");
	  const tbody = document.createElement("tbody");
	  for (const column of columns) {{
	    const row = document.createElement("tr");
	    const key = document.createElement("th");
	    const value = document.createElement("td");
	    key.textContent = column;
	    value.textContent = summary[column] || "-";
	    row.append(key, value);
	    tbody.append(row);
	  }}
	  table.append(tbody);
	  planck.append(table);
	}}

	function renderPlayers(remotePlayers) {{
	  players.replaceChildren();
	  if (!remotePlayers.length) {{
	    const empty = document.createElement("p");
	    empty.textContent = "No remote player readouts";
	    players.append(empty);
	    return;
	  }}

	  const columns = ["playerId", "proxy", "pose", "avatar", "movement", "equipment", "activation", "magic", "combat", "projectile", "grab", "higgs"];
	  const table = document.createElement("table");
	  const thead = document.createElement("thead");
	  const header = document.createElement("tr");
	  for (const column of columns) {{
	    const th = document.createElement("th");
	    th.textContent = column;
	    header.append(th);
	  }}
	  thead.append(header);

	  const tbody = document.createElement("tbody");
	  for (const player of remotePlayers) {{
	    const row = document.createElement("tr");
	    for (const column of columns) {{
	      const cell = document.createElement("td");
	      cell.textContent = player[column] || "-";
	      row.append(cell);
	    }}
	    tbody.append(row);
	  }}

	  table.append(thead, tbody);
	  players.append(table);
	}}

async function refresh() {{
  const response = await fetch("/api/readouts", {{ cache: "no-store" }});
  if (!response.ok) throw new Error(await response.text());
  render(await response.json());
}}

async function postCommand(path, body) {{
  const response = await fetch(path, {{
    method: "POST",
    headers: {{ "Content-Type": "application/x-www-form-urlencoded" }},
    body
  }});
  if (!response.ok) throw new Error(await response.text());
  const payload = await response.json();
  setNotice(payload.message);
  await refresh();
}}

document.getElementById("connect").addEventListener("click", async () => {{
  try {{
    const body = new URLSearchParams({{
      endpoint: document.getElementById("endpoint").value,
      password: document.getElementById("password").value
    }});
    await postCommand("/api/connect", body);
  }} catch (error) {{
    setNotice(error.message);
  }}
}});

document.getElementById("disconnect").addEventListener("click", async () => {{
  try {{
    await postCommand("/api/disconnect", new URLSearchParams());
  }} catch (error) {{
    setNotice(error.message);
  }}
}});

document.getElementById("refresh").addEventListener("click", () => refresh().catch(error => setNotice(error.message)));
refresh().catch(error => setNotice(error.message));
setInterval(() => refresh().catch(error => setNotice(error.message)), 1000);
</script>
</body>
</html>
"""


def print_readouts(handoff_dir: pathlib.Path, *, all_fields: bool = False) -> None:
    readouts = read_readouts(handoff_dir)
    print(f"handoffDir={handoff_dir}")
    for name, values in readouts.items():
        print(format_readout(name, values, all_fields=all_fields))
    compatibility = build_compatibility_summary(readouts)
    if compatibility:
        print(
            "[compatibility-summary] "
            + " ".join(f"{field}={value}" for field, value in compatibility.items())
        )
    higgs = build_higgs_summary(readouts)
    if higgs:
        print(
            "[higgs-summary] "
            + " ".join(f"{field}={value}" for field, value in higgs.items())
        )
    planck = build_planck_summary(readouts)
    if planck:
        print(
            "[planck-summary] "
            + " ".join(f"{field}={value}" for field, value in planck.items())
        )


def print_remote_players(handoff_dir: pathlib.Path) -> None:
    players = build_remote_players_payload(read_readouts(handoff_dir))
    if not players:
        print("[players] none")
        return

    for player in players:
        print(format_remote_player(player))


def command_connect(args: argparse.Namespace) -> int:
    handoff_dir = resolve_handoff_dir(args)
    path = write_connect_command(handoff_dir, args.endpoint, args.password)
    print(f"Wrote connect command: {path}")
    return 0


def command_config(args: argparse.Namespace) -> int:
    handoff_dir = resolve_handoff_dir(args)
    current = read_connection_config(handoff_dir)

    if args.endpoint is not None or args.password is not None:
        endpoint = args.endpoint if args.endpoint is not None else current["endpoint"]
        password = args.password if args.password is not None else current["password"]
        path = write_connection_config(handoff_dir, endpoint, password)
        print(f"Wrote connection config: {path}")

    config = read_connection_config(handoff_dir)
    print(f"endpoint={config['endpoint']}")
    print(f"passwordSet={'1' if config['password'] else '0'}")
    return 0


def command_disconnect(args: argparse.Namespace) -> int:
    handoff_dir = resolve_handoff_dir(args)
    path = write_disconnect_command(handoff_dir)
    print(f"Wrote disconnect command: {path}")
    return 0


def command_status(args: argparse.Namespace) -> int:
    handoff_dir = resolve_handoff_dir(args)
    while True:
        print_readouts(handoff_dir, all_fields=args.all)
        print_remote_players(handoff_dir)
        if not args.watch:
            return 0
        sys.stdout.flush()
        time.sleep(args.interval)
        print()


def command_players(args: argparse.Namespace) -> int:
    handoff_dir = resolve_handoff_dir(args)
    while True:
        print_remote_players(handoff_dir)
        if not args.watch:
            return 0
        sys.stdout.flush()
        time.sleep(args.interval)
        print()


def command_files(args: argparse.Namespace) -> int:
    handoff_dir = resolve_handoff_dir(args)
    print(f"handoffDir={handoff_dir}")
    print(f"command={command_path(handoff_dir)}")
    print(f"connection={config_path(handoff_dir)}")
    for name, file_name in READOUT_FILES.items():
        print(f"{name}={handoff_dir / file_name}")
    return 0


def panel_url(host: str, port: int) -> str:
    display_host = "127.0.0.1" if host in ("", "0.0.0.0", "::") else host
    if ":" in display_host and not display_host.startswith("["):
        display_host = f"[{display_host}]"
    return f"http://{display_host}:{port}/"


def command_serve(args: argparse.Namespace) -> int:
    import http.server
    import urllib.parse

    handoff_dir = resolve_handoff_dir(args)
    config = read_connection_config(handoff_dir)
    default_endpoint = args.endpoint if args.endpoint is not None else config["endpoint"]
    default_password = args.password if args.password is not None else config["password"]
    panel_html = render_panel_html(default_endpoint, default_password)

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, format: str, *args: object) -> None:
            return

        def send_bytes(self, status: int, content_type: str, data: bytes) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def send_text(self, status: int, text: str) -> None:
            self.send_bytes(status, "text/plain; charset=utf-8", text.encode("utf-8"))

        def send_json(self, status: int, payload: dict[str, object]) -> None:
            self.send_bytes(status, "application/json; charset=utf-8", json.dumps(payload).encode("utf-8"))

        def do_GET(self) -> None:
            parsed = urllib.parse.urlparse(self.path)
            if parsed.path == "/":
                self.send_bytes(200, "text/html; charset=utf-8", panel_html.encode("utf-8"))
            elif parsed.path == "/api/readouts":
                self.send_json(200, build_readout_payload(handoff_dir, all_fields=False))
            else:
                self.send_text(404, "not found")

        def do_POST(self) -> None:
            parsed = urllib.parse.urlparse(self.path)
            length = int(self.headers.get("Content-Length", "0"))
            body = self.rfile.read(length).decode("utf-8", errors="replace")
            values = urllib.parse.parse_qs(body, keep_blank_values=True)

            if parsed.path == "/api/connect":
                endpoint = values.get("endpoint", [default_endpoint])[0].strip()
                password = values.get("password", [default_password])[0]
                if not endpoint:
                    self.send_text(400, "endpoint is required")
                    return

                path = write_connect_command(handoff_dir, endpoint, password)
                self.send_json(200, {"message": f"Wrote connect command: {path}"})
            elif parsed.path == "/api/disconnect":
                path = write_disconnect_command(handoff_dir)
                self.send_json(200, {"message": f"Wrote disconnect command: {path}"})
            else:
                self.send_text(404, "not found")

    server = http.server.ThreadingHTTPServer((args.host, args.port), Handler)
    host, port = server.server_address[:2]
    url = panel_url(str(host), int(port))
    print(f"Serving SkyrimTogetherVR companion panel at {url}")
    print(f"handoffDir={handoff_dir}")
    if args.open_browser:
        threading.Timer(0.25, webbrowser.open, args=(url,)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()
    finally:
        server.server_close()
    return 0


def command_self_test(args: argparse.Namespace) -> int:
    with tempfile.TemporaryDirectory(prefix="stvr-handoff-test-") as temp:
        handoff_dir = pathlib.Path(temp)
        status_path = handoff_dir / READOUT_FILES["status"]
        pose_path = handoff_dir / READOUT_FILES["pose"]
        avatar_path = handoff_dir / READOUT_FILES["avatar"]
        remoteplayers_path = handoff_dir / READOUT_FILES["remoteplayers"]
        movement_path = handoff_dir / READOUT_FILES["movement"]
        inventory_path = handoff_dir / READOUT_FILES["inventory"]
        playercell_path = handoff_dir / READOUT_FILES["playercell"]
        combat_path = handoff_dir / READOUT_FILES["combat"]
        grab_path = handoff_dir / READOUT_FILES["grab"]
        compat_path = handoff_dir / READOUT_FILES["compat"]
        higgs_path = handoff_dir / READOUT_FILES["higgs"]
        higgsnet_path = handoff_dir / READOUT_FILES["higgsnet"]
        planck_path = handoff_dir / READOUT_FILES["planck"]
        write_atomic(status_path, "state=online\nonline=1\nplayerId=7\n")
        write_atomic(
            pose_path,
            "localPoseAvailable=1\n"
            "local.vrik.detected=1\n"
            "local.vrik.interfaceAvailable=0\n"
            "remotePoseCount=1\n"
            "remote.7.ageSeconds=0.25\n"
            "remote.7.hmd.position=1,2,3\n"
            "remote.7.vrik.detected=1\n"
            "remote.7.vrik.interfaceAvailable=1\n",
        )
        write_atomic(
            avatar_path,
            "ready=1\n"
            "actorTargetsEnabled=1\n"
            "actorSkeletonWritesEnabled=0\n"
            "actorMovementAuthorityEnabled=0\n"
            "remotePlayerCount=1\n"
            "remotePoseMatchCount=1\n"
            "componentUpsertCount=1\n"
            "actorTargetAttemptCount=1\n"
            "sameSpaceCount=1\n"
            "sameCellCount=1\n"
            "sameWorldSpaceCount=1\n"
            "actorTargetSkippedNoLocalMovementCount=0\n"
            "actorTargetSkippedNoRemoteMovementCount=0\n"
            "actorTargetSkippedDifferentCellCount=0\n"
            "actorTargetSkippedDifferentWorldSpaceCount=0\n"
            "missingFormIdCount=0\n"
            "missingActorCount=0\n"
            "missingRootCount=0\n"
            "headNodeFoundCount=1\n"
            "leftHandNodeFoundCount=1\n"
            "rightHandNodeFoundCount=1\n"
            "hmdCopiedCount=0\n"
            "leftHandCopiedCount=0\n"
            "rightHandCopiedCount=0\n"
            "vrikDetectedCount=1\n"
            "vrikInterfaceAvailableCount=1\n"
            "invalidVrikCount=0\n"
            "movementAppliedCount=0\n"
            "hmdFallbackMovementCount=0\n"
            "invalidTransformCount=0\n"
            "invalidMovementCount=0\n"
            "spellOriginValidCount=1\n"
            "spellDestinationValidCount=1\n"
            "arrowOriginValidCount=1\n"
            "arrowDestinationValidCount=1\n"
            "bowAimValidCount=1\n"
            "bowRotationValidCount=0\n"
            "leftWeaponOffsetValidCount=1\n"
            "rightWeaponOffsetValidCount=0\n"
            "primaryMagicOffsetValidCount=1\n"
            "primaryMagicAimValidCount=1\n"
            "secondaryMagicOffsetValidCount=0\n"
            "secondaryMagicAimValidCount=0\n"
            "stalePoseRemovedCount=0\n"
            "remoteEquipmentMatchCount=1\n"
            "equipmentComponentUpsertCount=1\n"
            "staleEquipmentRemovedCount=0\n"
            "last.playerId=7\n"
            "last.formId=255\n"
            "last.poseSequence=3\n"
            "last.spellOriginValid=1\n"
            "last.spellDestinationValid=1\n"
            "last.arrowOriginValid=1\n"
            "last.arrowDestinationValid=1\n"
            "last.bowAimValid=1\n"
            "last.bowRotationValid=0\n"
            "last.leftWeaponOffsetValid=1\n"
            "last.rightWeaponOffsetValid=0\n"
            "last.primaryMagicOffsetValid=1\n"
            "last.primaryMagicAimValid=1\n"
            "last.secondaryMagicOffsetValid=0\n"
            "last.secondaryMagicAimValid=0\n"
            "lastEquipment.playerId=7\n"
            "lastEquipment.sequence=11\n"
            "lastEquipment.weaponDrawn=1\n"
            "lastEquipment.weaponFullyDrawn=0\n"
            "last.localMovementAvailable=1\n"
            "last.remoteMovementAvailable=1\n"
            "last.sameCellAvailable=1\n"
            "last.sameCell=1\n"
            "last.sameWorldSpaceAvailable=1\n"
            "last.sameWorldSpace=1\n"
            "last.sameSpaceForApply=1\n"
            "last.headNodeFound=1\n"
            "last.leftHandNodeFound=1\n"
            "last.rightHandNodeFound=1\n"
            "last.hmdCopied=0\n"
            "last.movementApplied=0\n"
            "last.hmdFallbackMovementApplied=0\n"
            "last.invalidTransformCount=0\n"
            "last.invalidMovement=0\n",
        )
        write_atomic(
            remoteplayers_path,
            "ready=1\n"
            "online=1\n"
            "localPlayerId=4\n"
            "trackedPlayerCount=1\n"
            "joinedPlayerCount=1\n"
            "remotePoseCount=1\n"
            "remoteMovementCount=1\n"
            "remoteEquipmentCount=1\n"
            "remoteActivationCount=1\n"
            "remoteMagicEffectCount=1\n"
            "remoteCombatHitCount=1\n"
            "remoteProjectileEventCount=1\n"
            "remoteGrabCount=1\n"
            "remoteHiggsCount=1\n"
            "poseMatchedCount=1\n"
            "movementMatchedCount=1\n"
            "equipmentMatchedCount=1\n"
            "activationMatchedCount=1\n"
            "magicMatchedCount=1\n"
            "combatMatchedCount=1\n"
            "projectileMatchedCount=1\n"
            "grabMatchedCount=1\n"
            "higgsMatchedCount=1\n"
            "vrikMatchedCount=1\n"
            "sameCellCount=1\n"
            "sameWorldSpaceCount=1\n"
            "sameSpaceCount=1\n"
            "avatarValidationReadyCount=1\n"
            "higgsAvatarValidationReadyCount=1\n"
            "remotePlayer.7.playerId=7\n"
            "remotePlayer.7.username=Remote Tester\n"
            "remotePlayer.7.level=12\n"
            "remotePlayer.7.ageSeconds=0.5\n"
            "remotePlayer.7.cell.serverModId=1\n"
            "remotePlayer.7.cell.serverBaseId=100\n"
            "remotePlayer.7.worldSpace.serverModId=1\n"
            "remotePlayer.7.worldSpace.serverBaseId=200\n"
            "remotePlayer.7.poseAvailable=1\n"
            "remotePlayer.7.poseSequence=3\n"
            "remotePlayer.7.vrikDetected=1\n"
            "remotePlayer.7.vrikInterfaceAvailable=1\n"
            "remotePlayer.7.vrikAvailable=1\n"
            "remotePlayer.7.hmdAvailable=1\n"
            "remotePlayer.7.leftHandAvailable=1\n"
            "remotePlayer.7.rightHandAvailable=1\n"
            "remotePlayer.7.movementAvailable=1\n"
            "remotePlayer.7.equipmentAvailable=1\n"
            "remotePlayer.7.activationAvailable=1\n"
            "remotePlayer.7.magicAvailable=1\n"
            "remotePlayer.7.combatAvailable=1\n"
            "remotePlayer.7.projectileAvailable=1\n"
            "remotePlayer.7.grabAvailable=1\n"
            "remotePlayer.7.higgsAvailable=1\n"
            "remotePlayer.7.sameCellAvailable=1\n"
            "remotePlayer.7.sameCell=1\n"
            "remotePlayer.7.sameWorldSpaceAvailable=1\n"
            "remotePlayer.7.sameWorldSpace=1\n"
            "remotePlayer.7.sameSpaceAvailable=1\n"
            "remotePlayer.7.sameSpace=1\n"
            "remotePlayer.7.avatarValidationReady=1\n"
            "remotePlayer.7.avatarValidationBlocker=ready\n"
            "remotePlayer.7.higgsAvatarValidationReady=1\n"
            "remotePlayer.7.higgsAvatarValidationBlocker=ready\n",
        )
        write_atomic(
            movement_path,
            "ready=1\n"
            "localMovementAvailable=1\n"
            "remoteMovementCount=2\n"
            "remoteMovement.7.ageSeconds=0.5\n"
            "remoteMovement.7.position=4,5,6\n"
            "remoteMovement.7.direction=90\n",
        )
        write_atomic(
            inventory_path,
            "ready=1\n"
            "policy=equipment_snapshot_only\n"
            "fullInventoryTraversal=0\n"
            "inventoryMutation=0\n"
            "remoteEquipmentMutation=0\n"
            "normalInventoryPackets=0\n"
            "remoteEquipmentCount=1\n"
            "remoteEquipment.0.playerId=7\n"
            "remoteEquipment.0.weaponDrawn=1\n"
            "remoteEquipment.0.rightWeapon.serverModId=2\n"
            "remoteEquipment.0.rightWeapon.serverBaseId=42\n"
            "remoteEquipment.0.leftWeapon.serverModId=0\n"
            "remoteEquipment.0.leftWeapon.serverBaseId=0\n",
        )
        write_atomic(
            playercell_path,
            "ready=1\n"
            "online=1\n"
            "localPlayerId=4\n"
            "playerFormId=20\n"
            "currentLevel=12\n"
            "cachedLevel=12\n"
            "lastLevelSent=12\n"
            "gridCellRequestCount=1\n"
            "exteriorCellRequestCount=1\n"
            "interiorCellRequestCount=0\n"
            "levelRequestCount=1\n"
            "offlineSkippedRequestCount=0\n"
            "worldSpaceTranslationFailureCount=0\n"
            "lastGrid.valid=1\n"
            "lastGrid.worldSpace.serverModId=1\n"
            "lastGrid.worldSpace.serverBaseId=60\n"
            "lastGrid.playerCell.serverModId=1\n"
            "lastGrid.playerCell.serverBaseId=100\n"
            "lastGrid.center=4,-3\n"
            "lastGrid.cellCount=25\n"
            "lastCell.valid=1\n"
            "lastCell.exterior=1\n"
            "lastCell.cell.serverModId=1\n"
            "lastCell.cell.serverBaseId=100\n"
            "lastCell.worldSpace.serverModId=1\n"
            "lastCell.worldSpace.serverBaseId=60\n"
            "lastCell.currentCoords=4,-3\n",
        )
        write_atomic(
            combat_path,
            "localCombatHitAvailable=1\n"
            "remoteCombatHitCount=1\n"
            "remoteCombatHit.7.ageSeconds=0.5\n"
            "remoteCombatHit.7.hitter.serverModId=0\n"
            "remoteCombatHit.7.hitter.serverBaseId=20\n"
            "remoteCombatHit.7.hittee.serverModId=0\n"
            "remoteCombatHit.7.hittee.serverBaseId=300\n"
            "remoteCombatHit.7.source.serverModId=0\n"
            "remoteCombatHit.7.source.serverBaseId=30\n"
            "remoteCombatHit.7.projectile.serverModId=0\n"
            "remoteCombatHit.7.projectile.serverBaseId=40\n"
            "remoteCombatHit.7.rawHitFlags=1502691329\n"
            "remoteCombatHit.7.planckHit=1\n"
            "remoteCombatHit.7.hitterIsPlayer=1\n"
            "remoteCombatHit.7.hitteeIsPlayer=0\n",
        )
        write_atomic(
            grab_path,
            "localGrabAvailable=1\n"
            "remoteGrabCount=1\n"
            "remoteGrab.7.ageSeconds=0.75\n"
            "remoteGrab.7.object.serverModId=3\n"
            "remoteGrab.7.object.serverBaseId=99\n"
            "remoteGrab.7.grabbed=1\n",
        )
        write_atomic(
            compat_path,
            "ready=1\n"
            "higgs.installed=1\n"
            "higgs.loaded=1\n"
            "planck.installed=1\n"
            "planck.loaded=1\n"
            "vrPhysicsCompatibilityModInstalled=1\n"
            "unvalidatedHooksCompiled=0\n"
            "unvalidatedGameplayHooksSuppressed=0\n"
            "hookMode=bringup_hooks\n"
            "gameplayMode=connection_only\n"
            "remoteAvatarPolicy=disabled\n"
            "remotePlayerProxyPolicy=readout_only\n"
            "movementPolicy=observation_only\n"
            "equipmentPolicy=observation_only\n"
            "activationPolicy=observation_only\n"
            "inventoryPolicy=equipment_snapshot_only\n"
            "magicPolicy=observation_only\n"
            "combatPolicy=observation_only\n"
            "projectilePolicy=observation_only\n"
            "grabPolicy=observation_only\n"
            "higgsRelayPolicy=observation_only\n"
            "saveLoadPolicy=observation_only\n"
            "discoveryPolicy=observation_only\n"
            "playerCellPolicy=network_only\n"
            "higgsPolicy=observation_only\n"
            "planckPolicy=observation_only\n",
        )
        write_atomic(
            higgs_path,
            "bridge.loaded=1\n"
            "bridge.sequence=4\n"
            "higgs.detected=1\n"
            "higgs.interfaceAvailable=1\n"
            "higgs.callbacksRegistered=1\n"
            "higgs.snapshotAvailable=1\n"
            "higgs.snapshotSequence=3\n"
            "higgs.twoHanding=0\n"
            "left.valid=0\n"
            "right.valid=0\n"
            "recentEventCount=2\n"
            "recentEvent.1.sequence=9\n"
            "recentEvent.1.type=grabbed\n"
            "recentEvent.1.hasHand=1\n"
            "recentEvent.1.hand=left\n"
            "recentEvent.1.formId=100\n"
            "recentEvent.1.mass=1.0\n"
            "recentEvent.1.separatingVelocity=0.0\n",
        )
        write_atomic(
            higgsnet_path,
            "ready=1\n"
            "localHiggsAvailable=1\n"
            "remoteHiggsCount=1\n"
            "remoteHiggs.7.ageSeconds=0.25\n"
            "remoteHiggs.7.interfaceAvailable=1\n"
            "remoteHiggs.7.callbacksRegistered=1\n"
            "remoteHiggs.7.snapshotAvailable=1\n"
            "remoteHiggs.7.snapshotSequence=3\n"
            "remoteHiggs.7.twoHanding=0\n"
            "remoteHiggs.7.left.grabbedObject.serverModId=4\n"
            "remoteHiggs.7.left.grabbedObject.serverBaseId=100\n"
            "remoteHiggs.7.right.grabbedObject.serverModId=0\n"
            "remoteHiggs.7.right.grabbedObject.serverBaseId=0\n"
            "remoteHiggs.7.lastEvent.kind=grabbed\n",
        )
        write_atomic(
            planck_path,
            "bridge.loaded=1\n"
            "bridge.sequence=4\n"
            "planck.detected=1\n"
            "planck.interfaceRequestAttempted=1\n"
            "planck.interfaceAvailable=1\n"
            "planck.buildNumber=8\n"
            "planck.currentHitEventAddress=0x1234\n"
            "planck.currentHitEventAvailable=1\n"
            "planck.currentHitEventObservationOnly=1\n"
            "planck.lastHitDataAvailable=0\n"
            "planck.lastHitDataProbeEnabled=0\n"
            "planck.lastHitDataReason=not_polled_nontrivial_return_boundary\n"
            "planck.lastHitDataBoundary=disabled_unvalidated_by_value_abi\n"
            "planck.policy=observation_only\n",
        )

        values = read_readouts(handoff_dir)
        assert values["status"]["state"] == "online"
        assert values["avatar"]["actorTargetsEnabled"] == "1"
        assert values["avatar"]["remoteEquipmentMatchCount"] == "1"
        assert values["remoteplayers"]["trackedPlayerCount"] == "1"
        assert values["movement"]["remoteMovementCount"] == "2"
        assert values["playercell"]["gridCellRequestCount"] == "1"
        assert values["combat"]["remoteCombatHit.7.planckHit"] == "1"
        assert values["grab"]["remoteGrabCount"] == "1"
        assert values["compat"]["planck.loaded"] == "1"
        assert values["higgs"]["higgs.interfaceAvailable"] == "1"
        assert values["higgsnet"]["remoteHiggsCount"] == "1"
        assert values["planck"]["planck.interfaceAvailable"] == "1"

        connect_path = write_connect_command(handoff_dir, "127.0.0.1:10578", "pw")
        connect_text = connect_path.read_text(encoding="utf-8")
        assert "action=connect" in connect_text
        assert "endpoint=127.0.0.1:10578" in connect_text
        assert "password=pw" in connect_text
        config = read_connection_config(handoff_dir)
        assert config["endpoint"] == "127.0.0.1:10578"
        assert config["password"] == "pw"

        disconnect_path = write_disconnect_command(handoff_dir)
        assert disconnect_path.read_text(encoding="utf-8") == "action=disconnect\n"

        payload = build_readout_payload(handoff_dir)
        assert payload["readouts"]["status"]["state"] == "online"
        assert "[movement]" in payload["summaries"]["movement"]
        assert "[avatar]" in payload["summaries"]["avatar"]
        assert "[remoteplayers]" in payload["summaries"]["remoteplayers"]
        assert "[grab]" in payload["summaries"]["grab"]
        assert payload["compatibility"]["planckLoaded"] == "yes"
        assert payload["higgs"]["higgsInterface"] == "yes"
        assert payload["higgs"]["higgsEventCount"] == "2"
        assert payload["higgs"]["higgsRelayRemote"] == "1"
        assert payload["planck"]["planckInterface"] == "yes"
        assert payload["planck"]["planckCurrentHitObservationOnly"] == "yes"
        assert payload["planck"]["planckLastHitProbe"] == "no"
        assert payload["planck"]["planckLastHitReason"] == "not_polled_nontrivial_return_boundary"
        assert payload["planck"]["planckLastHitBoundary"] == "disabled_unvalidated_by_value_abi"
        assert payload["connectionConfig"]["endpoint"] == "127.0.0.1:10578"
        assert payload["connectionConfig"]["passwordSet"] == "1"
        remote_players = payload["remotePlayers"]
        assert remote_players[0]["playerId"] == "7"
        assert "name=Remote Tester" in remote_players[0]["proxy"]
        assert "sameCell=yes" in remote_players[0]["proxy"]
        assert "sameSpace=yes" in remote_players[0]["proxy"]
        assert "hmd=yes" in remote_players[0]["proxy"]
        assert "leftHand=yes" in remote_players[0]["proxy"]
        assert "rightHand=yes" in remote_players[0]["proxy"]
        assert "vrik=yes" in remote_players[0]["proxy"]
        assert "activation=yes" in remote_players[0]["proxy"]
        assert "magic=yes" in remote_players[0]["proxy"]
        assert "combat=yes" in remote_players[0]["proxy"]
        assert "projectile=yes" in remote_players[0]["proxy"]
        assert "grab=yes" in remote_players[0]["proxy"]
        assert "avatarReady=yes" in remote_players[0]["proxy"]
        assert "blocker=ready" in remote_players[0]["proxy"]
        assert "higgsAvatarReady=yes" in remote_players[0]["proxy"]
        assert "higgsBlocker=ready" in remote_players[0]["proxy"]
        assert "hmd=1,2,3" in remote_players[0]["pose"]
        assert "targets=yes" in remote_players[0]["avatar"]
        assert "head=yes" in remote_players[0]["avatar"]
        assert "sameSpace=yes" in remote_players[0]["avatar"]
        assert "sameCell=yes" in remote_players[0]["avatar"]
        assert "invalidTransforms=0" in remote_players[0]["avatar"]
        assert "invalidMovement=no" in remote_players[0]["avatar"]
        assert "skipCell=0" in remote_players[0]["avatar"]
        assert "equipment=1" in remote_players[0]["avatar"]
        assert "equipSeq=11" in remote_players[0]["avatar"]
        assert "spell=yes/yes" in remote_players[0]["avatar"]
        assert "arrow=yes/yes" in remote_players[0]["avatar"]
        assert "weapon=yes/no" in remote_players[0]["avatar"]
        assert "magic=yes/no" in remote_players[0]["avatar"]
        assert "vrik=detected" in remote_players[0]["pose"]
        assert "drawn=yes" in remote_players[0]["equipment"]
        assert "planck=yes" in remote_players[0]["combat"]
        assert "grabbed=yes" in remote_players[0]["grab"]
        assert "api=yes" in remote_players[0]["higgs"]
        assert "event=grabbed" in remote_players[0]["higgs"]

        panel_html = render_panel_html("127.0.0.1:10578", "")
        assert "SkyrimTogetherVR Companion" in panel_html
        assert "Remote Players" in panel_html
        assert "HIGGS" in panel_html
        assert "PLANCK" in panel_html
        assert "/api/readouts" in panel_html
        assert panel_url("127.0.0.1", 8765) == "http://127.0.0.1:8765/"
        assert panel_url("0.0.0.0", 8765) == "http://127.0.0.1:8765/"
        assert panel_url("::", 8765) == "http://127.0.0.1:8765/"
        assert panel_url("::1", 8765) == "http://[::1]:8765/"

    print("vr_handoff self-test passed")
    return 0


def add_common_path_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--game-path",
        type=pathlib.Path,
        default=default_game_path(),
        help=vr_paths.skyrim_vr_help("--game-path"),
    )
    parser.add_argument(
        "--handoff-dir",
        type=pathlib.Path,
        help="Override Data/SkyrimTogetherReborn handoff directory.",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    add_common_path_args(parser)

    subparsers = parser.add_subparsers(dest="command", required=True)

    connect = subparsers.add_parser("connect", help="Write a connect command file.")
    connect.add_argument("endpoint", help="Server endpoint, for example 127.0.0.1:10578.")
    connect.add_argument("--password", default="", help="Optional server password.")
    connect.set_defaults(func=command_connect)

    config = subparsers.add_parser("config", help="Read or write the remembered in-game connection config.")
    config.add_argument("--endpoint", help="Server endpoint remembered by the in-game configured toggle.")
    config.add_argument("--password", help="Optional server password remembered by the in-game configured toggle.")
    config.set_defaults(func=command_config)

    disconnect = subparsers.add_parser("disconnect", help="Write a disconnect command file.")
    disconnect.set_defaults(func=command_disconnect)

    status = subparsers.add_parser("status", help="Read status and telemetry handoff files.")
    status.add_argument("--all", action="store_true", help="Print every parsed key instead of the compact summary.")
    status.add_argument("--watch", action="store_true", help="Keep polling the handoff files.")
    status.add_argument("--interval", type=float, default=1.0, help="Polling interval in seconds for --watch.")
    status.set_defaults(func=command_status)

    players = subparsers.add_parser("players", help="Print the consolidated remote-player readout.")
    players.add_argument("--watch", action="store_true", help="Keep polling the handoff files.")
    players.add_argument("--interval", type=float, default=1.0, help="Polling interval in seconds for --watch.")
    players.set_defaults(func=command_players)

    files = subparsers.add_parser("files", help="Print the command/readout file paths.")
    files.set_defaults(func=command_files)

    serve = subparsers.add_parser("serve", help="Serve a local browser companion panel.")
    serve.add_argument("--host", default="127.0.0.1", help="Bind address for the local panel.")
    serve.add_argument("--port", type=int, default=8765, help="Bind port for the local panel.")
    serve.add_argument("--endpoint", help="Default connect endpoint shown in the panel.")
    serve.add_argument("--password", help="Default password shown in the panel.")
    serve.add_argument(
        "--open-browser",
        dest="open_browser",
        action="store_true",
        default=False,
        help="Open the local panel URL in the default browser after the server starts.",
    )
    serve.add_argument(
        "--no-open-browser",
        dest="open_browser",
        action="store_false",
        help="Do not open the browser when the local panel starts.",
    )
    serve.set_defaults(func=command_serve)

    self_test = subparsers.add_parser("self-test", help="Run a temp-directory parser/writer smoke test.")
    self_test.set_defaults(func=command_self_test)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
