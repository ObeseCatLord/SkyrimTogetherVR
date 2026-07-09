#!/usr/bin/env python3
"""Audit SkyrimTogetherVR runtime handoff files after a user-run VR smoke test."""

from __future__ import annotations

import argparse
import pathlib
import re
import tempfile

import vr_handoff
import vr_paths


DEFAULT_LOG_RELATIVE = pathlib.Path("logs/tp_client.log")
PLANCK_READOUT_NAME = "SkyrimTogetherVR.planck"

LOG_BREADCRUMBS = (
    "SkyrimTogetherVR runtime flags:",
    "Installing SkyrimTogetherVR startup/main-loop/render bring-up hooks",
    "Installing SkyrimTogetherVR VM/main-loop bring-up hooks:",
    "SkyrimTogetherVR client startup hook reached",
    "SkyrimTogetherVR main-loop hook reached",
    "SkyrimTogetherVR VM update hook reached;",
    "Installing SkyrimTogetherVR renderer bring-up hook:",
)

REQUIRED_HANDOFF_FILES = (
    "status",
    "pose",
    "movement",
    "inventory",
    "discovery",
    "playercell",
    "activation",
    "magic",
    "combat",
    "projectile",
    "grab",
    "compat",
    "planck",
    "higgsnet",
    "saveload",
    "remoteplayers",
)

ACTOR_FORM_ID_PATTERN = re.compile(r"^actor\.(\d+)\.formId$")


def resolve_handoff_dir(args: argparse.Namespace) -> pathlib.Path:
    if args.handoff_dir:
        return args.handoff_dir.expanduser().resolve()
    return args.game_path.expanduser().resolve() / "Data" / "SkyrimTogetherReborn"


def resolve_log_path(args: argparse.Namespace) -> pathlib.Path:
    if args.log:
        return args.log.expanduser().resolve()
    return args.game_path.expanduser().resolve() / DEFAULT_LOG_RELATIVE


def get_bool(values: dict[str, str], key: str) -> bool:
    value = values.get(key, "").strip().lower()
    return value in {"1", "true", "yes"}


def get_int(values: dict[str, str], key: str) -> int:
    value = values.get(key, "").strip()
    if not value:
        return 0
    try:
        return int(value, 0)
    except ValueError:
        return 0


def saveload_detail(values: dict[str, str]) -> str:
    return (
        "ready={} loadGameCount={} playerCell={} playerWorldSpace={} "
        "serverCell={}:{} serverWorldSpace={}:{}"
    ).format(
        values.get("ready", "<missing>"),
        values.get("loadGameCount", "<missing>"),
        values.get("playerCellFormId", "<missing>"),
        values.get("playerWorldSpaceFormId", "<missing>"),
        values.get("playerCell.serverModId", "<missing>"),
        values.get("playerCell.serverBaseId", "<missing>"),
        values.get("playerWorldSpace.serverModId", "<missing>"),
        values.get("playerWorldSpace.serverBaseId", "<missing>"),
    )


def parse_int_field(values: dict[str, str], key: str) -> tuple[bool, int]:
    raw = values.get(key)
    if raw is None:
        return False, 0
    try:
        return True, int(raw.strip(), 0)
    except ValueError:
        return False, 0


def parse_grid_field(values: dict[str, str], key: str) -> tuple[bool, tuple[int, int]]:
    raw = values.get(key, "")
    parts = [part.strip() for part in raw.split(",")]
    if len(parts) != 2:
        return False, (0, 0)
    try:
        return True, (int(parts[0], 0), int(parts[1], 0))
    except ValueError:
        return False, (0, 0)


def discovery_schema_detail(values: dict[str, str]) -> tuple[bool, str]:
    if not values:
        return False, "missing discovery readout"

    errors: list[str] = []
    ready_raw = values.get("ready")
    ready_normalized = ready_raw.strip().lower() if ready_raw is not None else ""
    if ready_normalized not in {"0", "1", "true", "false", "yes", "no"}:
        errors.append("ready missing/invalid")

    int_fields = (
        "actorCount",
        "actorLimit",
        "cachedWorldSpaceFormId",
        "cachedInteriorCellFormId",
        "playerCellFormId",
        "playerWorldSpaceFormId",
        "locationFormId",
    )
    parsed_ints: dict[str, int] = {}
    for key in int_fields:
        ok, value = parse_int_field(values, key)
        if not ok:
            errors.append(f"{key} missing/invalid")
        parsed_ints[key] = value

    for key in ("currentGrid", "centerGrid"):
        ok, value = parse_grid_field(values, key)
        if not ok:
            errors.append(f"{key} missing/invalid")
        parsed_ints[f"{key}.x"] = value[0]
        parsed_ints[f"{key}.y"] = value[1]

    actor_count = parsed_ints.get("actorCount", 0)
    actor_limit = parsed_ints.get("actorLimit", 0)
    if actor_count < 0:
        errors.append("actorCount negative")
    if actor_limit <= 0:
        errors.append("actorLimit must be positive")

    actor_rows: list[tuple[int, int]] = []
    for key, raw in values.items():
        match = ACTOR_FORM_ID_PATTERN.match(key)
        if not match:
            continue
        try:
            form_id = int(raw.strip(), 0)
        except ValueError:
            errors.append(f"{key} invalid")
            continue
        if form_id <= 0:
            errors.append(f"{key} must be positive")
        actor_rows.append((int(match.group(1)), form_id))

    actor_rows.sort()
    expected_indices = list(range(len(actor_rows)))
    actual_indices = [index for index, _ in actor_rows]
    if actual_indices != expected_indices:
        errors.append("actor rows are not contiguous from zero")
    if actor_limit > 0 and len(actor_rows) > actor_limit:
        errors.append("actor rows exceed actorLimit")
    if actor_count >= 0 and len(actor_rows) > actor_count:
        errors.append("actor rows exceed actorCount")
    if get_bool(values, "ready") and parsed_ints.get("playerCellFormId", 0) <= 0:
        errors.append("ready discovery is missing playerCellFormId")

    detail = (
        "ready={} actorCount={} actorRows={} actorLimit={} currentGrid={},{} centerGrid={},{} "
        "playerCellFormId={} playerWorldSpaceFormId={}"
    ).format(
        values.get("ready", "<missing>"),
        values.get("actorCount", "<missing>"),
        len(actor_rows),
        values.get("actorLimit", "<missing>"),
        parsed_ints.get("currentGrid.x", 0),
        parsed_ints.get("currentGrid.y", 0),
        parsed_ints.get("centerGrid.x", 0),
        parsed_ints.get("centerGrid.y", 0),
        values.get("playerCellFormId", "<missing>"),
        values.get("playerWorldSpaceFormId", "<missing>"),
    )
    if errors:
        detail += " errors=" + "; ".join(errors)
    return not errors, detail


def game_id_fields_valid(values: dict[str, str], prefix: str) -> bool:
    mod_ok, _ = parse_int_field(values, f"{prefix}.serverModId")
    base_ok, _ = parse_int_field(values, f"{prefix}.serverBaseId")
    return mod_ok and base_ok


def game_id_available(values: dict[str, str], prefix: str) -> bool:
    mod_ok, mod_id = parse_int_field(values, f"{prefix}.serverModId")
    base_ok, base_id = parse_int_field(values, f"{prefix}.serverBaseId")
    return mod_ok and base_ok and (mod_id != 0 or base_id != 0)


def remote_prefixes(values: dict[str, str], root: str) -> list[str]:
    prefixes: set[str] = set()
    marker = f"{root}."
    for key in values:
        if not key.startswith(marker):
            continue
        rest = key[len(marker) :]
        player_id = rest.split(".", 1)[0]
        if player_id:
            prefixes.add(f"{root}.{player_id}")
    return sorted(prefixes)


def any_remote_sequence(values: dict[str, str], root: str) -> bool:
    return any(get_int(values, f"{prefix}.sequence") > 0 for prefix in remote_prefixes(values, root))


def magic_effect_context(values: dict[str, str], prefix: str) -> bool:
    return game_id_available(values, f"{prefix}.effect") and (
        get_bool(values, f"{prefix}.casterIsPlayer")
        or get_bool(values, f"{prefix}.targetIsPlayer")
        or game_id_available(values, f"{prefix}.caster")
        or game_id_available(values, f"{prefix}.target")
    )


def combat_hit_context(values: dict[str, str], prefix: str) -> bool:
    return (
        get_bool(values, f"{prefix}.hitterIsPlayer")
        or get_bool(values, f"{prefix}.hitteeIsPlayer")
    ) and (game_id_available(values, f"{prefix}.hitter") or game_id_available(values, f"{prefix}.hittee"))


def projectile_intent_context(values: dict[str, str], prefix: str) -> bool:
    return game_id_available(values, f"{prefix}.source") and (
        get_bool(values, f"{prefix}.originValid")
        or get_bool(values, f"{prefix}.destinationValid")
        or game_id_available(values, f"{prefix}.weapon")
        or game_id_available(values, f"{prefix}.ammo")
        or game_id_available(values, f"{prefix}.spell")
        or get_bool(values, f"{prefix}.isSunGazing")
        or get_int(values, f"{prefix}.power") > 0
    )


def higgs_observation_context(values: dict[str, str], prefix: str) -> bool:
    return (
        get_bool(values, f"{prefix}.bridgeLoaded")
        or get_bool(values, f"{prefix}.detected")
        or get_bool(values, f"{prefix}.interfaceAvailable")
        or get_bool(values, f"{prefix}.callbacksRegistered")
        or get_bool(values, f"{prefix}.snapshotAvailable")
        or get_bool(values, f"{prefix}.left.valid")
        or get_bool(values, f"{prefix}.right.valid")
        or get_bool(values, f"{prefix}.lastEvent.valid")
    )


def any_remote_context(values: dict[str, str], root: str, predicate) -> bool:
    return any(predicate(values, prefix) for prefix in remote_prefixes(values, root))


def player_cell_schema_detail(values: dict[str, str]) -> tuple[bool, str]:
    if not values:
        return False, "missing player-cell readout"

    errors: list[str] = []
    ready_raw = values.get("ready")
    ready_normalized = ready_raw.strip().lower() if ready_raw is not None else ""
    if ready_normalized not in {"0", "1", "true", "false", "yes", "no"}:
        errors.append("ready missing/invalid")

    int_fields = (
        "localPlayerId",
        "playerFormId",
        "currentLevel",
        "cachedLevel",
        "lastLevelSent",
        "gridCellRequestCount",
        "exteriorCellRequestCount",
        "interiorCellRequestCount",
        "levelRequestCount",
        "offlineSkippedRequestCount",
        "worldSpaceTranslationFailureCount",
        "lastGrid.cellCount",
    )
    parsed: dict[str, int] = {}
    for key in int_fields:
        ok, value = parse_int_field(values, key)
        if not ok:
            errors.append(f"{key} missing/invalid")
        elif value < 0:
            errors.append(f"{key} negative")
        parsed[key] = value

    for key in ("online", "lastGrid.valid", "lastCell.valid", "lastCell.exterior"):
        raw = values.get(key)
        normalized = raw.strip().lower() if raw is not None else ""
        if normalized not in {"0", "1", "true", "false", "yes", "no"}:
            errors.append(f"{key} missing/invalid")

    for key in ("lastGrid.center", "lastCell.currentCoords"):
        ok, _ = parse_grid_field(values, key)
        if not ok:
            errors.append(f"{key} missing/invalid")

    for prefix in (
        "lastGrid.worldSpace",
        "lastGrid.playerCell",
        "lastCell.cell",
        "lastCell.worldSpace",
    ):
        if not game_id_fields_valid(values, prefix):
            errors.append(f"{prefix} missing/invalid")

    request_total = (
        parsed.get("gridCellRequestCount", 0)
        + parsed.get("exteriorCellRequestCount", 0)
        + parsed.get("interiorCellRequestCount", 0)
        + parsed.get("levelRequestCount", 0)
    )
    if get_bool(values, "lastGrid.valid") and parsed.get("gridCellRequestCount", 0) <= 0:
        errors.append("lastGrid is valid but gridCellRequestCount is zero")
    if get_bool(values, "lastCell.valid") and (
        parsed.get("exteriorCellRequestCount", 0) + parsed.get("interiorCellRequestCount", 0)
    ) <= 0:
        errors.append("lastCell is valid but cell request counts are zero")

    detail = (
        "ready={} online={} player={} level={} grid={} exterior={} interior={} levelRequests={} "
        "skipped={} translationFailures={} requestTotal={}"
    ).format(
        values.get("ready", "<missing>"),
        values.get("online", "<missing>"),
        values.get("localPlayerId", "<missing>"),
        values.get("currentLevel", "<missing>"),
        values.get("gridCellRequestCount", "<missing>"),
        values.get("exteriorCellRequestCount", "<missing>"),
        values.get("interiorCellRequestCount", "<missing>"),
        values.get("levelRequestCount", "<missing>"),
        values.get("offlineSkippedRequestCount", "<missing>"),
        values.get("worldSpaceTranslationFailureCount", "<missing>"),
        request_total,
    )
    if errors:
        detail += " errors=" + "; ".join(errors)
    return not errors, detail
    try:
        return int(value, 0)
    except ValueError:
        return 0


def add_check(results: list[tuple[str, bool, str]], name: str, ok: bool, detail: str) -> None:
    results.append((name, ok, detail))


def summarize_remote_avatar_rows(remoteplayers: dict[str, str]) -> tuple[bool, str]:
    player_ids = vr_handoff.collect_remote_ids(remoteplayers, "remotePlayer")
    if not player_ids:
        return False, "no remotePlayer.* rows"

    summaries: list[str] = []
    ready = False
    for player_id in player_ids:
        prefix = f"remotePlayer.{player_id}"
        hmd = remoteplayers.get(f"{prefix}.hmdAvailable", "<missing>")
        left_hand = remoteplayers.get(f"{prefix}.leftHandAvailable", "<missing>")
        right_hand = remoteplayers.get(f"{prefix}.rightHandAvailable", "<missing>")
        player_ready = (
            get_bool(remoteplayers, f"{prefix}.avatarValidationReady")
            and get_bool(remoteplayers, f"{prefix}.hmdAvailable")
            and get_bool(remoteplayers, f"{prefix}.leftHandAvailable")
            and get_bool(remoteplayers, f"{prefix}.rightHandAvailable")
        )
        ready = ready or player_ready
        blocker = remoteplayers.get(f"{prefix}.avatarValidationBlocker", "<missing>")
        higgs_ready = remoteplayers.get(f"{prefix}.higgsAvatarValidationReady", "<missing>")
        higgs_blocker = remoteplayers.get(f"{prefix}.higgsAvatarValidationBlocker", "<missing>")
        pose = remoteplayers.get(f"{prefix}.poseAvailable", "<missing>")
        vrik_detected = remoteplayers.get(f"{prefix}.vrikDetected", "<missing>")
        vrik_api = remoteplayers.get(f"{prefix}.vrikInterfaceAvailable", "<missing>")
        vrik = remoteplayers.get(f"{prefix}.vrikAvailable", "<missing>")
        movement = remoteplayers.get(f"{prefix}.movementAvailable", "<missing>")
        same_space = remoteplayers.get(f"{prefix}.sameSpace", "<missing>")
        summaries.append(
            f"{player_id}:ready={int(player_ready)} blocker={blocker} "
            f"higgsReady={higgs_ready} higgsBlocker={higgs_blocker} "
            f"pose={pose} hmd={hmd} leftHand={left_hand} rightHand={right_hand} "
            f"vrikDetected={vrik_detected} vrikApi={vrik_api} "
            f"vrik={vrik} movement={movement} sameSpace={same_space}"
        )

    return ready, "; ".join(summaries)


def audit_log(results: list[tuple[str, bool, str]], log_path: pathlib.Path, *, skip_log: bool) -> None:
    if skip_log:
        add_check(results, "client log", True, "skipped by --skip-log")
        return

    if not log_path.exists():
        add_check(results, "client log", False, f"missing: {log_path}")
        return

    text = log_path.read_text(encoding="utf-8", errors="replace")
    missing = [token for token in LOG_BREADCRUMBS if token not in text]
    add_check(
        results,
        "client log breadcrumbs",
        not missing,
        "all expected breadcrumbs present" if not missing else "missing: " + ", ".join(missing),
    )


def audit_handoff_files(results: list[tuple[str, bool, str]], handoff_dir: pathlib.Path) -> dict[str, dict[str, str]]:
    add_check(results, "handoff directory", handoff_dir.exists(), str(handoff_dir))
    readouts = vr_handoff.read_readouts(handoff_dir)

    for name in REQUIRED_HANDOFF_FILES:
        path = handoff_dir / vr_handoff.READOUT_FILES[name]
        add_check(results, f"{name} file", path.exists(), str(path))

    return readouts


def audit_avatar_file(results: list[tuple[str, bool, str]], handoff_dir: pathlib.Path) -> None:
    path = handoff_dir / vr_handoff.READOUT_FILES["avatar"]
    add_check(results, "avatar file", path.exists(), str(path))


def audit_default_runtime(results: list[tuple[str, bool, str]], readouts: dict[str, dict[str, str]]) -> None:
    status = readouts.get("status", {})
    pose = readouts.get("pose", {})
    movement = readouts.get("movement", {})
    inventory = readouts.get("inventory", {})
    discovery = readouts.get("discovery", {})
    playercell = readouts.get("playercell", {})
    remoteplayers = readouts.get("remoteplayers", {})
    compat = readouts.get("compat", {})
    planck = readouts.get("planck", {})
    saveload = readouts.get("saveload", {})
    discovery_ok, discovery_detail = discovery_schema_detail(discovery)
    playercell_ok, playercell_detail = player_cell_schema_detail(playercell)
    physics_compat_installed = get_bool(compat, "vrPhysicsCompatibilityModInstalled")
    unvalidated_hooks_compiled = get_bool(compat, "unvalidatedHooksCompiled")
    unvalidated_hooks_suppressed = get_bool(compat, "unvalidatedGameplayHooksSuppressed")
    lane_policy_keys = (
        "gameplayMode",
        "remoteAvatarPolicy",
        "remotePlayerProxyPolicy",
        "discoveryPolicy",
        "playerCellPolicy",
        "movementPolicy",
        "equipmentPolicy",
        "activationPolicy",
        "inventoryPolicy",
        "magicPolicy",
        "combatPolicy",
        "projectilePolicy",
        "grabPolicy",
        "higgsRelayPolicy",
        "saveLoadPolicy",
    )
    lane_policies_available = (
        compat.get("gameplayMode") in {"connection_only", "gameplay_services"}
        and all(compat.get(key) for key in lane_policy_keys)
    )
    compatibility_ok = (
        get_bool(compat, "ready")
        and compat.get("higgsPolicy") == "observation_only"
        and compat.get("planckPolicy") == "observation_only"
        and lane_policies_available
        and (not (physics_compat_installed and unvalidated_hooks_compiled) or unvalidated_hooks_suppressed)
    )

    add_check(results, "connection status", bool(status.get("state")), f"state={status.get('state', '<missing>')}")
    add_check(results, "local pose", get_bool(pose, "localPoseAvailable"), f"localPoseAvailable={pose.get('localPoseAvailable', '<missing>')}")
    add_check(results, "local HMD node", get_bool(pose, "local.hmd.valid"), f"local.hmd.valid={pose.get('local.hmd.valid', '<missing>')}")
    add_check(results, "local left hand node", get_bool(pose, "local.leftHand.valid"), f"local.leftHand.valid={pose.get('local.leftHand.valid', '<missing>')}")
    add_check(results, "local right hand node", get_bool(pose, "local.rightHand.valid"), f"local.rightHand.valid={pose.get('local.rightHand.valid', '<missing>')}")
    add_check(results, "local movement", get_bool(movement, "localMovementAvailable"), f"localMovementAvailable={movement.get('localMovementAvailable', '<missing>')}")
    inventory_boundary_ok = (
        inventory.get("policy") == "equipment_snapshot_only"
        and inventory.get("fullInventoryTraversal") == "0"
        and inventory.get("inventoryMutation") == "0"
        and inventory.get("remoteEquipmentMutation") == "0"
        and inventory.get("normalInventoryPackets") == "0"
    )
    add_check(
        results,
        "inventory mutation boundary",
        inventory_boundary_ok,
        "policy={} fullTraversal={} inventoryMutation={} remoteEquipmentMutation={} normalInventoryPackets={}".format(
            inventory.get("policy", "<missing>"),
            inventory.get("fullInventoryTraversal", "<missing>"),
            inventory.get("inventoryMutation", "<missing>"),
            inventory.get("remoteEquipmentMutation", "<missing>"),
            inventory.get("normalInventoryPackets", "<missing>"),
        ),
    )
    add_check(results, "discovery schema", discovery_ok, discovery_detail)
    add_check(results, "player-cell schema", playercell_ok, playercell_detail)
    add_check(results, "remote-player proxy", get_bool(remoteplayers, "ready"), f"ready={remoteplayers.get('ready', '<missing>')}")
    add_check(
        results,
        "HIGGS/PLANCK compatibility policy",
        compatibility_ok,
        "ready={} physicsCompat={} hookMode={} gameplayMode={} avatarPolicy={} proxyPolicy={} discoveryPolicy={} playerCellPolicy={} movementPolicy={} equipmentPolicy={} activationPolicy={} inventoryPolicy={} magicPolicy={} combatPolicy={} projectilePolicy={} grabPolicy={} higgsRelayPolicy={} saveLoadPolicy={} unvalidatedHooks={} suppressed={} higgsPolicy={} planckPolicy={}".format(
            compat.get("ready", "<missing>"),
            compat.get("vrPhysicsCompatibilityModInstalled", "<missing>"),
            compat.get("hookMode", "<missing>"),
            compat.get("gameplayMode", "<missing>"),
            compat.get("remoteAvatarPolicy", "<missing>"),
            compat.get("remotePlayerProxyPolicy", "<missing>"),
            compat.get("discoveryPolicy", "<missing>"),
            compat.get("playerCellPolicy", "<missing>"),
            compat.get("movementPolicy", "<missing>"),
            compat.get("equipmentPolicy", "<missing>"),
            compat.get("activationPolicy", "<missing>"),
            compat.get("inventoryPolicy", "<missing>"),
            compat.get("magicPolicy", "<missing>"),
            compat.get("combatPolicy", "<missing>"),
            compat.get("projectilePolicy", "<missing>"),
            compat.get("grabPolicy", "<missing>"),
            compat.get("higgsRelayPolicy", "<missing>"),
            compat.get("saveLoadPolicy", "<missing>"),
            compat.get("unvalidatedHooksCompiled", "<missing>"),
            compat.get("unvalidatedGameplayHooksSuppressed", "<missing>"),
            compat.get("higgsPolicy", "<missing>"),
            compat.get("planckPolicy", "<missing>"),
        ),
    )
    planck_required_by_compat = get_bool(compat, "planck.installed") or get_bool(compat, "planck.loaded")
    planck_bridge_ok = (
        get_bool(planck, "bridge.loaded")
        and get_int(planck, "bridge.sequence") > 0
        and get_bool(planck, "planck.interfaceRequestAttempted")
        and planck.get("planck.policy") == "observation_only"
        and get_bool(planck, "planck.currentHitEventObservationOnly")
        and planck.get("planck.lastHitDataAvailable") == "0"
        and planck.get("planck.lastHitDataProbeEnabled") == "0"
        and planck.get("planck.lastHitDataReason") == "not_polled_nontrivial_return_boundary"
        and planck.get("planck.lastHitDataBoundary") == "disabled_unvalidated_by_value_abi"
        and (not planck_required_by_compat or get_bool(planck, "planck.interfaceAvailable"))
    )
    add_check(
        results,
        "PLANCK bridge policy",
        planck_bridge_ok,
        "bridge.loaded={} bridge.sequence={} detected={} request={} interfaceAvailable={} build={} currentHit={} currentHitObservationOnly={} lastHitData={} lastHitProbe={} lastHitBoundary={} policy={} planckRequired={}".format(
            planck.get("bridge.loaded", "<missing>"),
            planck.get("bridge.sequence", "<missing>"),
            planck.get("planck.detected", "<missing>"),
            planck.get("planck.interfaceRequestAttempted", "<missing>"),
            planck.get("planck.interfaceAvailable", "<missing>"),
            planck.get("planck.buildNumber", "<missing>"),
            planck.get("planck.currentHitEventAvailable", "<missing>"),
            planck.get("planck.currentHitEventObservationOnly", "<missing>"),
            planck.get("planck.lastHitDataAvailable", "<missing>"),
            planck.get("planck.lastHitDataProbeEnabled", "<missing>"),
            planck.get("planck.lastHitDataBoundary", "<missing>"),
            planck.get("planck.policy", "<missing>"),
            int(planck_required_by_compat),
        ),
    )
    add_check(results, "save/load observer", get_bool(saveload, "ready"), saveload_detail(saveload))


def audit_connection(results: list[tuple[str, bool, str]], readouts: dict[str, dict[str, str]]) -> None:
    status = readouts.get("status", {})
    pose = readouts.get("pose", {})
    add_check(results, "connected status", get_bool(status, "online"), f"state={status.get('state', '<missing>')} online={status.get('online', '<missing>')}")
    add_check(results, "pose stream online", get_bool(pose, "online"), f"online={pose.get('online', '<missing>')}")


def audit_vrik(results: list[tuple[str, bool, str]], readouts: dict[str, dict[str, str]], *, require_remote: bool) -> None:
    pose = readouts.get("pose", {})
    add_check(results, "local VRIK detection", get_bool(pose, "local.vrik.detected"), f"local.vrik.detected={pose.get('local.vrik.detected', '<missing>')}")
    add_check(
        results,
        "local VRIK API",
        get_bool(pose, "local.vrik.interfaceAvailable"),
        f"local.vrik.interfaceAvailable={pose.get('local.vrik.interfaceAvailable', '<missing>')}",
    )
    if require_remote:
        remote_vrik_detected = any(
            key.startswith("remote.") and key.endswith(".vrik.detected") and value == "1"
            for key, value in pose.items()
        )
        remote_vrik_api = any(
            key.startswith("remote.") and key.endswith(".vrik.interfaceAvailable") and value == "1"
            for key, value in pose.items()
        )
        add_check(results, "remote VRIK detection", remote_vrik_detected, "remote.*.vrik.detected=1")
        add_check(results, "remote VRIK API", remote_vrik_api, "remote.*.vrik.interfaceAvailable=1")


def audit_higgs(results: list[tuple[str, bool, str]], readouts: dict[str, dict[str, str]], *, require_remote: bool) -> None:
    bridge = readouts.get("higgs", {})
    higgsnet = readouts.get("higgsnet", {})
    remoteplayers = readouts.get("remoteplayers", {})
    add_check(
        results,
        "HIGGS bridge loaded",
        get_bool(bridge, "bridge.loaded") and get_int(bridge, "bridge.sequence") > 0,
        "bridge.loaded={} bridge.sequence={}".format(
            bridge.get("bridge.loaded", "<missing>"),
            bridge.get("bridge.sequence", "<missing>"),
        ),
    )
    add_check(results, "HIGGS bridge detected", get_bool(bridge, "higgs.detected"), f"higgs.detected={bridge.get('higgs.detected', '<missing>')}")
    add_check(results, "HIGGS bridge API", get_bool(bridge, "higgs.interfaceAvailable"), f"higgs.interfaceAvailable={bridge.get('higgs.interfaceAvailable', '<missing>')}")
    add_check(results, "HIGGS relay ready", get_bool(higgsnet, "ready"), f"ready={higgsnet.get('ready', '<missing>')}")
    add_check(results, "HIGGS relay local", get_bool(higgsnet, "localHiggsAvailable"), f"localHiggsAvailable={higgsnet.get('localHiggsAvailable', '<missing>')}")
    if require_remote:
        count = get_int(higgsnet, "remoteHiggsCount")
        add_check(results, "HIGGS relay remote", count > 0, f"remoteHiggsCount={count}")
        add_check(
            results,
            "remote VRIK/HIGGS avatar readiness",
            get_int(remoteplayers, "higgsAvatarValidationReadyCount") > 0,
            "higgsAvatarValidationReadyCount={}".format(
                remoteplayers.get("higgsAvatarValidationReadyCount", "<missing>")
            ),
        )


def audit_remote_player(results: list[tuple[str, bool, str]], readouts: dict[str, dict[str, str]]) -> None:
    pose = readouts.get("pose", {})
    movement = readouts.get("movement", {})
    inventory = readouts.get("inventory", {})
    remoteplayers = readouts.get("remoteplayers", {})
    avatar_row_ready, avatar_row_detail = summarize_remote_avatar_rows(remoteplayers)

    add_check(results, "remote pose", get_int(pose, "remotePoseCount") > 0, f"remotePoseCount={pose.get('remotePoseCount', '<missing>')}")
    add_check(results, "remote movement", get_int(movement, "remoteMovementCount") > 0, f"remoteMovementCount={movement.get('remoteMovementCount', '<missing>')}")
    add_check(results, "remote equipment", get_int(inventory, "remoteEquipmentCount") > 0, f"remoteEquipmentCount={inventory.get('remoteEquipmentCount', '<missing>')}")
    add_check(results, "remote proxy row", get_int(remoteplayers, "trackedPlayerCount") > 0, f"trackedPlayerCount={remoteplayers.get('trackedPlayerCount', '<missing>')}")
    add_check(
        results,
        "remote avatar readiness",
        get_int(remoteplayers, "avatarValidationReadyCount") > 0 and avatar_row_ready,
        f"avatarValidationReadyCount={remoteplayers.get('avatarValidationReadyCount', '<missing>')} rows={avatar_row_detail}",
    )


def audit_pose_context(
    results: list[tuple[str, bool, str]],
    readouts: dict[str, dict[str, str]],
    *,
    require_weapon_pose: bool,
    require_magic_pose: bool,
    require_projectile_pose: bool,
) -> None:
    pose = readouts.get("pose", {})
    if require_weapon_pose:
        add_check(
            results,
            "weapon pose context",
            get_bool(pose, "local.leftWeaponOffset.valid") or get_bool(pose, "local.rightWeaponOffset.valid"),
            "leftWeaponOffset={} rightWeaponOffset={}".format(
                pose.get("local.leftWeaponOffset.valid", "<missing>"),
                pose.get("local.rightWeaponOffset.valid", "<missing>"),
            ),
        )
    if require_magic_pose:
        magic_pose_available = (
            get_bool(pose, "local.spellOrigin.valid")
            and get_bool(pose, "local.spellDestination.valid")
        ) or (
            get_bool(pose, "local.primaryMagicOffset.valid")
            and get_bool(pose, "local.primaryMagicAim.valid")
        ) or (
            get_bool(pose, "local.secondaryMagicOffset.valid")
            and get_bool(pose, "local.secondaryMagicAim.valid")
        )
        add_check(
            results,
            "spell/magic pose context",
            magic_pose_available,
            "spellOrigin={} spellDestination={} primaryOffset={} primaryAim={} secondaryOffset={} secondaryAim={}".format(
                pose.get("local.spellOrigin.valid", "<missing>"),
                pose.get("local.spellDestination.valid", "<missing>"),
                pose.get("local.primaryMagicOffset.valid", "<missing>"),
                pose.get("local.primaryMagicAim.valid", "<missing>"),
                pose.get("local.secondaryMagicOffset.valid", "<missing>"),
                pose.get("local.secondaryMagicAim.valid", "<missing>"),
            ),
        )
    if require_projectile_pose:
        add_check(
            results,
            "arrow/projectile pose context",
            (
                get_bool(pose, "local.arrowOrigin.valid")
                and get_bool(pose, "local.arrowDestination.valid")
            )
            or get_bool(pose, "local.bowAim.valid"),
            "arrowOrigin={} arrowDestination={} bowAim={} bowRotation={}".format(
                pose.get("local.arrowOrigin.valid", "<missing>"),
                pose.get("local.arrowDestination.valid", "<missing>"),
                pose.get("local.bowAim.valid", "<missing>"),
                pose.get("local.bowRotation.valid", "<missing>"),
            ),
        )


def audit_gameplay_relay_lanes(args: argparse.Namespace, results: list[tuple[str, bool, str]], readouts: dict[str, dict[str, str]]) -> None:
    movement = readouts.get("movement", {})
    inventory = readouts.get("inventory", {})
    activation = readouts.get("activation", {})
    magic = readouts.get("magic", {})
    combat = readouts.get("combat", {})
    projectile = readouts.get("projectile", {})
    grab = readouts.get("grab", {})
    higgsnet = readouts.get("higgsnet", {})
    saveload = readouts.get("saveload", {})

    if args.require_movement_relay:
        movement_context = get_int(movement, "localMovement.sequence") > 0 and any_remote_sequence(movement, "remoteMovement")
        add_check(
            results,
            "movement relay",
            get_bool(movement, "localMovementAvailable")
            and get_int(movement, "remoteMovementCount") > 0
            and movement_context,
            "localMovementAvailable={} localSequence={} remoteMovementCount={} remoteSequencePresent={}".format(
                movement.get("localMovementAvailable", "<missing>"),
                movement.get("localMovement.sequence", "<missing>"),
                movement.get("remoteMovementCount", "<missing>"),
                int(any_remote_sequence(movement, "remoteMovement")),
            ),
        )
    if args.require_equipment_relay:
        equipment_context = get_int(inventory, "localEquipment.sequence") > 0 and any_remote_sequence(inventory, "remoteEquipment")
        add_check(
            results,
            "equipment relay",
            get_bool(inventory, "localEquipmentAvailable")
            and get_int(inventory, "remoteEquipmentCount") > 0
            and equipment_context,
            "localEquipmentAvailable={} localSequence={} remoteEquipmentCount={} remoteSequencePresent={}".format(
                inventory.get("localEquipmentAvailable", "<missing>"),
                inventory.get("localEquipment.sequence", "<missing>"),
                inventory.get("remoteEquipmentCount", "<missing>"),
                int(any_remote_sequence(inventory, "remoteEquipment")),
            ),
        )
    if args.require_activation_relay:
        activation_context = game_id_available(activation, "localActivation.object") and any_remote_context(
            activation, "remoteActivation", lambda values, prefix: game_id_available(values, f"{prefix}.object")
        )
        add_check(
            results,
            "activation relay",
            get_bool(activation, "localActivationAvailable")
            and get_int(activation, "remoteActivationCount") > 0
            and activation_context,
            "localActivationAvailable={} localObject={} remoteActivationCount={} remoteObjectPresent={}".format(
                activation.get("localActivationAvailable", "<missing>"),
                activation.get("localActivation.object.serverBaseId", "<missing>"),
                activation.get("remoteActivationCount", "<missing>"),
                int(
                    any_remote_context(
                        activation,
                        "remoteActivation",
                        lambda values, prefix: game_id_available(values, f"{prefix}.object"),
                    )
                ),
            ),
        )
    if args.require_magic_relay:
        magic_context = magic_effect_context(magic, "localMagicEffect") and any_remote_context(
            magic, "remoteMagicEffect", magic_effect_context
        )
        add_check(
            results,
            "magic-effect relay",
            get_bool(magic, "localMagicEffectAvailable")
            and get_int(magic, "remoteMagicEffectCount") > 0
            and magic_context,
            "localMagicEffectAvailable={} localEffect={} casterIsPlayer={} targetIsPlayer={} remoteMagicEffectCount={} remoteContextPresent={}".format(
                magic.get("localMagicEffectAvailable", "<missing>"),
                magic.get("localMagicEffect.effect.serverBaseId", "<missing>"),
                magic.get("localMagicEffect.casterIsPlayer", "<missing>"),
                magic.get("localMagicEffect.targetIsPlayer", "<missing>"),
                magic.get("remoteMagicEffectCount", "<missing>"),
                int(any_remote_context(magic, "remoteMagicEffect", magic_effect_context)),
            ),
        )
    if args.require_combat_relay:
        combat_context = combat_hit_context(combat, "localCombatHit") and any_remote_context(
            combat, "remoteCombatHit", combat_hit_context
        )
        add_check(
            results,
            "combat-hit relay",
            get_bool(combat, "localCombatHitAvailable")
            and get_int(combat, "remoteCombatHitCount") > 0
            and combat_context,
            "localCombatHitAvailable={} hitterIsPlayer={} hitteeIsPlayer={} localPlanckHit={} remoteCombatHitCount={} remoteContextPresent={} remotePlanckContextPresent={}".format(
                combat.get("localCombatHitAvailable", "<missing>"),
                combat.get("localCombatHit.hitterIsPlayer", "<missing>"),
                combat.get("localCombatHit.hitteeIsPlayer", "<missing>"),
                combat.get("localCombatHit.planckHit", "<missing>"),
                combat.get("remoteCombatHitCount", "<missing>"),
                int(any_remote_context(combat, "remoteCombatHit", combat_hit_context)),
                int(any_remote_context(combat, "remoteCombatHit", lambda values, prefix: get_bool(values, f"{prefix}.planckHit"))),
            ),
        )
    if args.require_projectile_relay:
        projectile_context = projectile_intent_context(projectile, "localProjectileEvent") and any_remote_context(
            projectile, "remoteProjectileEvent", projectile_intent_context
        )
        add_check(
            results,
            "projectile intent relay",
            get_bool(projectile, "localProjectileEventAvailable")
            and get_int(projectile, "remoteProjectileEventCount") > 0
            and projectile_context,
            "localProjectileEventAvailable={} localSource={} originValid={} destinationValid={} remoteProjectileEventCount={} remoteContextPresent={}".format(
                projectile.get("localProjectileEventAvailable", "<missing>"),
                projectile.get("localProjectileEvent.source.serverBaseId", "<missing>"),
                projectile.get("localProjectileEvent.originValid", "<missing>"),
                projectile.get("localProjectileEvent.destinationValid", "<missing>"),
                projectile.get("remoteProjectileEventCount", "<missing>"),
                int(any_remote_context(projectile, "remoteProjectileEvent", projectile_intent_context)),
            ),
        )
    if args.require_grab_relay:
        grab_context = game_id_available(grab, "localGrab.object") and any_remote_context(
            grab, "remoteGrab", lambda values, prefix: game_id_available(values, f"{prefix}.object")
        )
        add_check(
            results,
            "grab/release relay",
            get_bool(grab, "localGrabAvailable") and get_int(grab, "remoteGrabCount") > 0 and grab_context,
            "localGrabAvailable={} localObject={} remoteGrabCount={} remoteObjectPresent={}".format(
                grab.get("localGrabAvailable", "<missing>"),
                grab.get("localGrab.object.serverBaseId", "<missing>"),
                grab.get("remoteGrabCount", "<missing>"),
                int(
                    any_remote_context(
                        grab,
                        "remoteGrab",
                        lambda values, prefix: game_id_available(values, f"{prefix}.object"),
                    )
                ),
            ),
        )
    if args.require_higgs_relay:
        higgs_context = higgs_observation_context(higgsnet, "localHiggs") and any_remote_context(
            higgsnet, "remoteHiggs", higgs_observation_context
        )
        add_check(
            results,
            "HIGGS relay",
            get_bool(higgsnet, "localHiggsAvailable") and get_int(higgsnet, "remoteHiggsCount") > 0 and higgs_context,
            "localHiggsAvailable={} localDetected={} localInterface={} remoteHiggsCount={} remoteContextPresent={}".format(
                higgsnet.get("localHiggsAvailable", "<missing>"),
                higgsnet.get("localHiggs.detected", "<missing>"),
                higgsnet.get("localHiggs.interfaceAvailable", "<missing>"),
                higgsnet.get("remoteHiggsCount", "<missing>"),
                int(any_remote_context(higgsnet, "remoteHiggs", higgs_observation_context)),
            ),
        )
    if args.require_saveload_observer:
        add_check(
            results,
            "save/load observer",
            get_bool(saveload, "ready"),
            saveload_detail(saveload),
        )


def audit_avatar_sync(results: list[tuple[str, bool, str]], readouts: dict[str, dict[str, str]]) -> None:
    avatar = readouts.get("avatar", {})
    skeleton_writes_enabled = get_bool(avatar, "actorSkeletonWritesEnabled")
    skeleton_write_ready = (
        not skeleton_writes_enabled
        or (
            get_int(avatar, "hmdCopiedCount") > 0
            and get_int(avatar, "leftHandCopiedCount") > 0
            and get_int(avatar, "rightHandCopiedCount") > 0
        )
    )
    add_check(results, "avatar handoff", get_bool(avatar, "ready"), f"ready={avatar.get('ready', '<missing>')}")
    add_check(results, "avatar actor targets", get_bool(avatar, "actorTargetsEnabled"), f"actorTargetsEnabled={avatar.get('actorTargetsEnabled', '<missing>')}")
    add_check(results, "avatar same-space guard", get_int(avatar, "sameSpaceCount") > 0, f"sameSpaceCount={avatar.get('sameSpaceCount', '<missing>')}")
    add_check(
        results,
        "avatar skeleton write policy",
        skeleton_write_ready,
        "actorSkeletonWritesEnabled={} hmdCopiedCount={} leftHandCopiedCount={} rightHandCopiedCount={}".format(
            avatar.get("actorSkeletonWritesEnabled", "<missing>"),
            avatar.get("hmdCopiedCount", "<missing>"),
            avatar.get("leftHandCopiedCount", "<missing>"),
            avatar.get("rightHandCopiedCount", "<missing>"),
        ),
    )
    add_check(
        results,
        "avatar VRIK payload",
        get_int(avatar, "vrikDetectedCount") > 0
        and get_int(avatar, "vrikInterfaceAvailableCount") > 0
        and get_int(avatar, "invalidVrikCount") == 0,
        "vrikDetectedCount={} vrikInterfaceAvailableCount={} invalidVrikCount={}".format(
            avatar.get("vrikDetectedCount", "<missing>"),
            avatar.get("vrikInterfaceAvailableCount", "<missing>"),
            avatar.get("invalidVrikCount", "<missing>"),
        ),
    )
    add_check(results, "avatar transform validity", get_int(avatar, "invalidTransformCount") == 0, f"invalidTransformCount={avatar.get('invalidTransformCount', '<missing>')}")
    add_check(results, "avatar movement validity", get_int(avatar, "invalidMovementCount") == 0, f"invalidMovementCount={avatar.get('invalidMovementCount', '<missing>')}")


def print_results(results: list[tuple[str, bool, str]]) -> int:
    failures = [(name, detail) for name, ok, detail in results if not ok]
    for name, ok, detail in results:
        print(f"[{'PASS' if ok else 'FAIL'}] {name}: {detail}")
    print(f"Runtime handoff audit failures: {len(failures)}")
    for name, detail in failures:
        print(f"- {name}: {detail}")
    return 1 if failures else 0


def run_audit(args: argparse.Namespace) -> int:
    results: list[tuple[str, bool, str]] = []
    handoff_dir = resolve_handoff_dir(args)
    log_path = resolve_log_path(args)

    print("SkyrimTogetherVR runtime handoff audit does not launch Skyrim or mutate the game install.")
    print(f"Game path: {args.game_path.expanduser().resolve()}")
    print(f"Handoff directory: {handoff_dir}")
    print(f"Client log: {log_path}")

    audit_log(results, log_path, skip_log=args.skip_log)
    readouts = audit_handoff_files(results, handoff_dir)
    if args.avatar_sync:
        audit_avatar_file(results, handoff_dir)
    audit_default_runtime(results, readouts)
    if args.require_connected:
        audit_connection(results, readouts)
    if args.require_vrik:
        audit_vrik(results, readouts, require_remote=args.require_remote_player)
    if args.require_higgs:
        audit_higgs(results, readouts, require_remote=args.require_remote_player)
    if args.require_remote_player:
        audit_remote_player(results, readouts)
    audit_pose_context(
        results,
        readouts,
        require_weapon_pose=args.require_weapon_pose,
        require_magic_pose=args.require_magic_pose,
        require_projectile_pose=args.require_projectile_pose,
    )
    audit_gameplay_relay_lanes(args, results, readouts)
    if args.avatar_sync:
        audit_avatar_sync(results, readouts)

    return print_results(results)


def command_self_test(_: argparse.Namespace) -> int:
    with tempfile.TemporaryDirectory(prefix="stvr-runtime-audit-test-") as temp:
        root = pathlib.Path(temp)
        game = root / "SkyrimVR"
        handoff = game / "Data" / "SkyrimTogetherReborn"
        log = game / DEFAULT_LOG_RELATIVE
        handoff.mkdir(parents=True)
        log.parent.mkdir(parents=True)
        log.write_text("\n".join(LOG_BREADCRUMBS) + "\n", encoding="utf-8")

        def write(name: str, contents: str) -> None:
            (handoff / vr_handoff.READOUT_FILES[name]).write_text(contents, encoding="utf-8")

        write("status", "state=online\nonline=1\n")
        write(
            "pose",
            "online=1\nlocalPoseAvailable=1\nlocal.hmd.valid=1\nlocal.leftHand.valid=1\nlocal.rightHand.valid=1\n"
            "local.spellOrigin.valid=1\nlocal.spellDestination.valid=1\n"
            "local.arrowOrigin.valid=1\nlocal.arrowDestination.valid=1\nlocal.bowAim.valid=1\nlocal.bowRotation.valid=1\n"
            "local.leftWeaponOffset.valid=1\nlocal.rightWeaponOffset.valid=1\n"
            "local.primaryMagicOffset.valid=1\nlocal.primaryMagicAim.valid=1\n"
            "local.secondaryMagicOffset.valid=1\nlocal.secondaryMagicAim.valid=1\n"
            "local.vrik.detected=1\nlocal.vrik.interfaceAvailable=1\n"
            "remotePoseCount=1\nremote.7.vrik.detected=1\nremote.7.vrik.interfaceAvailable=1\n",
        )
        write("movement", "ready=1\nlocalMovementAvailable=1\nremoteMovementCount=1\nlocalMovement.sequence=1\nremoteMovement.7.sequence=2\n")
        write(
            "inventory",
            "ready=1\n"
            "policy=equipment_snapshot_only\n"
            "fullInventoryTraversal=0\n"
            "inventoryMutation=0\n"
            "remoteEquipmentMutation=0\n"
            "normalInventoryPackets=0\n"
            "localEquipmentAvailable=1\n"
            "remoteEquipmentCount=1\n"
            "localEquipment.sequence=1\n"
            "remoteEquipment.7.sequence=2\n",
        )
        write(
            "discovery",
            "ready=1\n"
            "actorCount=2\n"
            "actorLimit=32\n"
            "currentGrid=4,-3\n"
            "centerGrid=4,-2\n"
            "cachedWorldSpaceFormId=60\n"
            "cachedInteriorCellFormId=0\n"
            "playerCellFormId=100\n"
            "playerWorldSpaceFormId=60\n"
            "locationFormId=200\n"
            "actor.0.formId=300\n"
            "actor.1.formId=301\n",
        )
        write(
            "playercell",
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
        write(
            "activation",
            "localActivationAvailable=1\nremoteActivationCount=1\n"
            "localActivation.sequence=1\nlocalActivation.object.serverModId=0\nlocalActivation.object.serverBaseId=12345\n"
            "remoteActivation.7.sequence=2\nremoteActivation.7.object.serverModId=0\nremoteActivation.7.object.serverBaseId=12345\n",
        )
        write(
            "magic",
            "localMagicEffectAvailable=1\nremoteMagicEffectCount=1\n"
            "localMagicEffect.sequence=1\nlocalMagicEffect.effect.serverModId=0\nlocalMagicEffect.effect.serverBaseId=111\n"
            "localMagicEffect.caster.serverModId=0\nlocalMagicEffect.caster.serverBaseId=20\n"
            "localMagicEffect.casterIsPlayer=1\nlocalMagicEffect.targetIsPlayer=0\n"
            "remoteMagicEffect.7.sequence=2\nremoteMagicEffect.7.effect.serverModId=0\nremoteMagicEffect.7.effect.serverBaseId=111\n"
            "remoteMagicEffect.7.caster.serverModId=0\nremoteMagicEffect.7.caster.serverBaseId=20\n"
            "remoteMagicEffect.7.casterIsPlayer=1\nremoteMagicEffect.7.targetIsPlayer=0\n",
        )
        write(
            "combat",
            "localCombatHitAvailable=1\nremoteCombatHitCount=1\n"
            "localCombatHit.sequence=1\nlocalCombatHit.hitter.serverModId=0\nlocalCombatHit.hitter.serverBaseId=20\n"
            "localCombatHit.hittee.serverModId=0\nlocalCombatHit.hittee.serverBaseId=300\n"
            "localCombatHit.source.serverModId=0\nlocalCombatHit.source.serverBaseId=30\n"
            "localCombatHit.projectile.serverModId=0\nlocalCombatHit.projectile.serverBaseId=40\n"
            "localCombatHit.rawHitFlags=1502691329\nlocalCombatHit.planckHit=1\n"
            "localCombatHit.hitterIsPlayer=1\nlocalCombatHit.hitteeIsPlayer=0\n"
            "remoteCombatHit.7.sequence=2\nremoteCombatHit.7.hitter.serverModId=0\nremoteCombatHit.7.hitter.serverBaseId=20\n"
            "remoteCombatHit.7.hittee.serverModId=0\nremoteCombatHit.7.hittee.serverBaseId=300\n"
            "remoteCombatHit.7.source.serverModId=0\nremoteCombatHit.7.source.serverBaseId=30\n"
            "remoteCombatHit.7.projectile.serverModId=0\nremoteCombatHit.7.projectile.serverBaseId=40\n"
            "remoteCombatHit.7.rawHitFlags=1\nremoteCombatHit.7.planckHit=0\n"
            "remoteCombatHit.7.hitterIsPlayer=1\nremoteCombatHit.7.hitteeIsPlayer=0\n",
        )
        write(
            "projectile",
            "localProjectileEventAvailable=1\nremoteProjectileEventCount=1\n"
            "localProjectileEvent.sequence=1\nlocalProjectileEvent.source.serverModId=0\nlocalProjectileEvent.source.serverBaseId=20\n"
            "localProjectileEvent.weapon.serverModId=0\nlocalProjectileEvent.weapon.serverBaseId=30\n"
            "localProjectileEvent.originValid=1\nlocalProjectileEvent.destinationValid=1\n"
            "remoteProjectileEvent.7.sequence=2\nremoteProjectileEvent.7.source.serverModId=0\nremoteProjectileEvent.7.source.serverBaseId=20\n"
            "remoteProjectileEvent.7.weapon.serverModId=0\nremoteProjectileEvent.7.weapon.serverBaseId=30\n"
            "remoteProjectileEvent.7.originValid=1\nremoteProjectileEvent.7.destinationValid=1\n",
        )
        write(
            "grab",
            "localGrabAvailable=1\nremoteGrabCount=1\n"
            "localGrab.sequence=1\nlocalGrab.object.serverModId=0\nlocalGrab.object.serverBaseId=12345\n"
            "remoteGrab.7.sequence=2\nremoteGrab.7.object.serverModId=0\nremoteGrab.7.object.serverBaseId=12345\n",
        )
        write(
            "compat",
            "ready=1\n"
            "higgs.installed=1\nhiggs.loaded=1\n"
            "planck.installed=1\nplanck.loaded=1\n"
            "vrPhysicsCompatibilityModInstalled=1\n"
            "bringupHooksCompiled=1\nunvalidatedHooksCompiled=0\n"
            "connectionOnly=1\nflatOverlay=0\nvalidatedInlinePatches=0\n"
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
            "higgsPolicy=observation_only\nplanckPolicy=observation_only\n",
        )
        write(
            "higgs",
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
            "recentEventCount=0\n",
        )
        write(
            "planck",
            "bridge.loaded=1\n"
            "bridge.sequence=4\n"
            "planck.detected=1\n"
            "planck.interfaceRequestAttempted=1\n"
            "planck.interfaceAvailable=1\n"
            "planck.buildNumber=8\n"
            "planck.currentHitEventAvailable=1\n"
            "planck.currentHitEventObservationOnly=1\n"
            "planck.lastHitDataAvailable=0\n"
            "planck.lastHitDataProbeEnabled=0\n"
            "planck.lastHitDataReason=not_polled_nontrivial_return_boundary\n"
            "planck.lastHitDataBoundary=disabled_unvalidated_by_value_abi\n"
            "planck.policy=observation_only\n",
        )
        write(
            "higgsnet",
            "ready=1\nlocalHiggsAvailable=1\nremoteHiggsCount=1\n"
            "localHiggs.sequence=1\nlocalHiggs.detected=1\nlocalHiggs.interfaceAvailable=1\n"
            "remoteHiggs.7.sequence=2\nremoteHiggs.7.detected=1\nremoteHiggs.7.interfaceAvailable=1\n",
        )
        write(
            "saveload",
            "ready=1\nonline=1\nlocalPlayerId=4\nconnectionState=online\n"
            "loadGameObserved=1\nloadGameCount=1\nreadyAfterLastLoad=1\n"
            "waitingForReadyAfterLoad=0\nsecondsSinceLastLoad=0.5\n"
            "playerFormId=20\nplayerCellFormId=100\nplayerWorldSpaceFormId=60\n"
            "player.serverModId=0\nplayer.serverBaseId=20\n"
            "playerCell.serverModId=1\nplayerCell.serverBaseId=100\n"
            "playerWorldSpace.serverModId=1\nplayerWorldSpace.serverBaseId=60\n",
        )
        write(
            "remoteplayers",
            "ready=1\ntrackedPlayerCount=1\navatarValidationReadyCount=1\nhiggsAvatarValidationReadyCount=1\n"
            "remotePlayer.7.poseAvailable=1\nremotePlayer.7.vrikDetected=1\nremotePlayer.7.vrikInterfaceAvailable=1\n"
            "remotePlayer.7.vrikAvailable=1\n"
            "remotePlayer.7.hmdAvailable=1\nremotePlayer.7.leftHandAvailable=1\nremotePlayer.7.rightHandAvailable=1\n"
            "remotePlayer.7.movementAvailable=1\n"
            "remotePlayer.7.higgsAvailable=1\n"
            "remotePlayer.7.sameSpace=1\nremotePlayer.7.avatarValidationReady=1\nremotePlayer.7.avatarValidationBlocker=ready\n"
            "remotePlayer.7.higgsAvatarValidationReady=1\nremotePlayer.7.higgsAvatarValidationBlocker=ready\n",
        )
        write("avatar", "ready=1\nactorTargetsEnabled=1\nactorSkeletonWritesEnabled=0\nsameSpaceCount=1\nhmdCopiedCount=0\nleftHandCopiedCount=0\nrightHandCopiedCount=0\nvrikDetectedCount=1\nvrikInterfaceAvailableCount=1\ninvalidVrikCount=0\ninvalidTransformCount=0\ninvalidMovementCount=0\n")

        args = argparse.Namespace(
            game_path=game,
            handoff_dir=None,
            log=None,
            skip_log=False,
            require_connected=True,
            require_vrik=True,
            require_higgs=True,
            require_remote_player=True,
            require_weapon_pose=True,
            require_magic_pose=True,
            require_projectile_pose=True,
            require_movement_relay=True,
            require_equipment_relay=True,
            require_activation_relay=True,
            require_magic_relay=True,
            require_combat_relay=True,
            require_projectile_relay=True,
            require_grab_relay=True,
            require_higgs_relay=True,
            require_saveload_observer=True,
            avatar_sync=True,
        )
        return run_audit(args)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-path", type=pathlib.Path, default=vr_paths.default_skyrim_vr_path(), help=vr_paths.skyrim_vr_help("--game-path"))
    parser.add_argument("--handoff-dir", type=pathlib.Path, help="override Data/SkyrimTogetherReborn handoff directory")
    parser.add_argument("--log", type=pathlib.Path, help="override client log path")
    parser.add_argument("--skip-log", action="store_true", help="do not require tp_client.log breadcrumbs")
    parser.add_argument("--require-connected", action="store_true", help="require online connection status")
    parser.add_argument("--require-vrik", action="store_true", help="require local VRIK bridge API lane data")
    parser.add_argument("--require-higgs", action="store_true", help="require HIGGS bridge and relay data")
    parser.add_argument("--require-remote-player", action="store_true", help="require remote pose/movement/equipment/proxy data")
    parser.add_argument("--require-weapon-pose", action="store_true", help="require weapon offset pose nodes")
    parser.add_argument("--require-magic-pose", action="store_true", help="require spell or magic aim/origin pose nodes")
    parser.add_argument("--require-projectile-pose", action="store_true", help="require arrow/projectile pose nodes")
    parser.add_argument("--require-vr-pose-context", action="store_true", help="require weapon, magic, and projectile pose context")
    parser.add_argument("--require-movement-relay", action="store_true", help="require movement relay evidence")
    parser.add_argument("--require-equipment-relay", action="store_true", help="require equipment relay evidence")
    parser.add_argument("--require-activation-relay", action="store_true", help="require activation relay evidence")
    parser.add_argument("--require-magic-relay", action="store_true", help="require magic-effect relay evidence")
    parser.add_argument("--require-combat-relay", action="store_true", help="require combat-hit relay evidence")
    parser.add_argument("--require-projectile-relay", action="store_true", help="require projectile intent relay evidence")
    parser.add_argument("--require-grab-relay", action="store_true", help="require grab/release relay evidence")
    parser.add_argument("--require-higgs-relay", action="store_true", help="require HIGGS relay evidence")
    parser.add_argument("--require-saveload-observer", action="store_true", help="require save/load observer evidence")
    parser.add_argument("--require-gameplay-relays", action="store_true", help="require all staged gameplay relay evidence")
    parser.add_argument("--avatar-sync", action="store_true", help="require explicit avatar-sync runtime handoff data")
    parser.add_argument("--gameplay", action="store_true", help="require full gameplay runtime handoff data")
    parser.add_argument("--self-test", action="store_true", help="run a temp-directory runtime audit fixture")
    args = parser.parse_args()

    if args.gameplay:
        args.avatar_sync = True
        args.require_connected = True
        args.require_remote_player = True
        args.require_vrik = True
        args.require_higgs = True
        args.require_vr_pose_context = True
        args.require_gameplay_relays = True
    if args.require_vr_pose_context:
        args.require_weapon_pose = True
        args.require_magic_pose = True
        args.require_projectile_pose = True
    if args.require_gameplay_relays:
        args.require_movement_relay = True
        args.require_equipment_relay = True
        args.require_activation_relay = True
        args.require_magic_relay = True
        args.require_combat_relay = True
        args.require_projectile_relay = True
        args.require_grab_relay = True
        args.require_higgs_relay = True
        args.require_saveload_observer = True

    if args.self_test:
        return command_self_test(args)
    return run_audit(args)


if __name__ == "__main__":
    raise SystemExit(main())
