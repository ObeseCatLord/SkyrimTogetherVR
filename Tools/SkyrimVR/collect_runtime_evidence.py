#!/usr/bin/env python3
"""Collect no-launch SkyrimTogetherVR runtime evidence after a user-run VR test."""

from __future__ import annotations

import argparse
import contextlib
import datetime as dt
import io
import json
import os
import pathlib
import tempfile
import zipfile

import audit_runtime_handoff
import vr_handoff
import vr_paths


DEFAULT_LOG_RELATIVE = pathlib.Path("logs/tp_client.log")
BUILD_MANIFEST_NAME = "SkyrimTogetherVR_BuildManifest.json"
BUILD_MANIFEST_SCHEMA = "skyrim_together_vr_build_package_v2"
WINDOWS_MY_GAMES_RELATIVE = pathlib.Path("Documents") / "My Games" / "Skyrim VR" / "SKSE"
HANDOFF_EXTRA_FILES = (
    vr_handoff.COMMAND_FILE,
    vr_handoff.CONFIG_FILE,
)
SKSE_LOG_NAMES = (
    "sksevr.log",
    "sksevr_loader.log",
    "sksevr_steam_loader.log",
)
CHECK_PASS = "pass"
CHECK_FAIL = "fail"
CHECK_NOT_REQUIRED = "not_required"
DEFAULT_EXPECTED_MANIFEST_TARGETS = (
    "SkyrimTogetherVRClient",
    "SkyrimTogetherVRVrikBridge",
    "SkyrimTogetherVRHiggsBridge",
    "SkyrimTogetherVRPlanckBridge",
    "SkyrimTogetherVRTickBridge",
    "SkyrimVRImmersiveLauncher",
    "ImmersiveElf",
    "TPProcess",
)
AVATAR_SYNC_EXPECTED_MANIFEST_TARGETS = (
    "SkyrimTogetherVRClientAvatarSync",
    "SkyrimVRImmersiveLauncherAvatarSync",
    "SkyrimTogetherVRVrikBridge",
    "SkyrimTogetherVRHiggsBridge",
    "SkyrimTogetherVRPlanckBridge",
    "SkyrimTogetherVRTickBridge",
    "ImmersiveElf",
    "TPProcess",
)
GAMEPLAY_EXPECTED_MANIFEST_TARGETS = (
    "SkyrimTogetherVRGameplayClient",
    "SkyrimVRImmersiveLauncherGameplay",
    "SkyrimTogetherVRVrikBridge",
    "SkyrimTogetherVRHiggsBridge",
    "SkyrimTogetherVRPlanckBridge",
    "SkyrimTogetherVRTickBridge",
    "ImmersiveElf",
    "TPProcess",
)
DEFAULT_EXPECTED_RUNTIME_ARTIFACTS = (
    "SkyrimTogetherVR.exe",
    "EarlyLoad.dll",
    "TPProcess.exe",
    "SkyrimTogetherVRVrikBridge.dll",
    "SkyrimTogetherVRHiggsBridge.dll",
    "SkyrimTogetherVRPlanckBridge.dll",
    "SkyrimTogetherVRTickBridge.dll",
)
AVATAR_SYNC_EXPECTED_RUNTIME_ARTIFACTS = (
    "SkyrimTogetherVRAvatarSync.exe",
    "EarlyLoad.dll",
    "TPProcess.exe",
    "SkyrimTogetherVRVrikBridge.dll",
    "SkyrimTogetherVRHiggsBridge.dll",
    "SkyrimTogetherVRPlanckBridge.dll",
    "SkyrimTogetherVRTickBridge.dll",
)
GAMEPLAY_EXPECTED_RUNTIME_ARTIFACTS = (
    "SkyrimTogetherVRGameplay.exe",
    "EarlyLoad.dll",
    "TPProcess.exe",
    "SkyrimTogetherVRVrikBridge.dll",
    "SkyrimTogetherVRHiggsBridge.dll",
    "SkyrimTogetherVRPlanckBridge.dll",
    "SkyrimTogetherVRTickBridge.dll",
)


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def default_game_path() -> pathlib.Path:
    return vr_paths.default_skyrim_vr_path()


def default_output_dir() -> pathlib.Path:
    root = repo_root()
    if (root / ".git").exists():
        return root / "artifacts" / "SkyrimTogetherVR" / "runtime-evidence"

    documents = pathlib.Path.home() / "Documents"
    if documents.exists() or pathlib.Path.home().exists():
        return documents / "SkyrimTogetherVRRuntimeEvidence"
    return pathlib.Path.cwd()


def timestamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%d-%H%M%SZ")


def resolve_output_path(out: pathlib.Path | None) -> pathlib.Path:
    target = out.expanduser() if out else default_output_dir()
    if target.suffix.lower() != ".zip":
        target.mkdir(parents=True, exist_ok=True)
        return target / f"SkyrimTogetherVR-evidence-{timestamp()}.zip"

    target.parent.mkdir(parents=True, exist_ok=True)
    return target


def resolve_handoff_dir(args: argparse.Namespace) -> pathlib.Path:
    if args.handoff_dir:
        return args.handoff_dir.expanduser().resolve()
    return args.game_path.expanduser().resolve() / "Data" / "SkyrimTogetherReborn"


def add_file(
    zf: zipfile.ZipFile,
    source: pathlib.Path,
    archive_name: str,
    files: list[dict[str, object]],
    *,
    category: str,
    required: bool,
) -> None:
    record: dict[str, object] = {
        "category": category,
        "source": str(source),
        "archiveName": archive_name,
        "required": required,
        "exists": source.exists(),
    }
    if source.exists() and source.is_file():
        record["size"] = source.stat().st_size
        zf.write(source, archive_name)
    files.append(record)


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


def game_id_available(values: dict[str, str], prefix: str) -> bool:
    return audit_runtime_handoff.game_id_available(values, prefix)


def remote_prefixes(values: dict[str, str], root: str) -> list[str]:
    return audit_runtime_handoff.remote_prefixes(values, root)


def any_remote_sequence(values: dict[str, str], root: str) -> bool:
    return audit_runtime_handoff.any_remote_sequence(values, root)


def magic_effect_context(values: dict[str, str], prefix: str) -> bool:
    return audit_runtime_handoff.magic_effect_context(values, prefix)


def combat_hit_context(values: dict[str, str], prefix: str) -> bool:
    return audit_runtime_handoff.combat_hit_context(values, prefix)


def projectile_intent_context(values: dict[str, str], prefix: str) -> bool:
    return audit_runtime_handoff.projectile_intent_context(values, prefix)


def higgs_observation_context(values: dict[str, str], prefix: str) -> bool:
    return audit_runtime_handoff.higgs_observation_context(values, prefix)


def any_remote_context(values: dict[str, str], root: str, predicate) -> bool:
    return audit_runtime_handoff.any_remote_context(values, root, predicate)


def add_check(
    checks: list[dict[str, str]],
    check_id: str,
    objective: str,
    label: str,
    passed: bool,
    detail: str,
) -> None:
    checks.append(
        {
            "id": check_id,
            "objective": objective,
            "label": label,
            "status": CHECK_PASS if passed else CHECK_FAIL,
            "detail": detail,
        }
    )


def add_not_required(
    checks: list[dict[str, str]],
    check_id: str,
    objective: str,
    label: str,
    detail: str,
) -> None:
    checks.append(
        {
            "id": check_id,
            "objective": objective,
            "label": label,
            "status": CHECK_NOT_REQUIRED,
            "detail": detail,
        }
    )


def add_conditional_check(
    checks: list[dict[str, str]],
    check_id: str,
    objective: str,
    label: str,
    passed: bool,
    detail: str,
    *,
    required: bool,
    not_required_detail: str,
) -> None:
    if passed or required:
        add_check(checks, check_id, objective, label, passed, detail)
    else:
        add_not_required(checks, check_id, objective, label, not_required_detail)


def log_breadcrumb_detail(log_path: pathlib.Path, skip_log: bool) -> tuple[bool, str]:
    if skip_log:
        return True, "skipped by --skip-log"
    if not log_path.exists():
        return False, f"missing: {log_path}"
    text = log_path.read_text(encoding="utf-8", errors="replace")
    missing = [token for token in audit_runtime_handoff.LOG_BREADCRUMBS if token not in text]
    if missing:
        return False, "missing: " + ", ".join(missing)
    return True, "all startup/main-loop/render breadcrumbs present"


def load_json_file(path: pathlib.Path) -> tuple[dict[str, object], str]:
    if not path.exists():
        return {}, f"missing: {path}"
    try:
        return json.loads(path.read_text(encoding="utf-8-sig")), "loaded"
    except json.JSONDecodeError as exc:
        return {}, f"invalid JSON: {exc}"
    except OSError as exc:
        return {}, f"could not read: {exc}"


def expected_manifest_targets(*, avatar_sync: bool, gameplay: bool) -> tuple[str, ...]:
    if gameplay:
        return GAMEPLAY_EXPECTED_MANIFEST_TARGETS
    if avatar_sync:
        return AVATAR_SYNC_EXPECTED_MANIFEST_TARGETS
    return DEFAULT_EXPECTED_MANIFEST_TARGETS


def expected_runtime_artifacts(*, avatar_sync: bool, gameplay: bool) -> tuple[str, ...]:
    if gameplay:
        return GAMEPLAY_EXPECTED_RUNTIME_ARTIFACTS
    if avatar_sync:
        return AVATAR_SYNC_EXPECTED_RUNTIME_ARTIFACTS
    return DEFAULT_EXPECTED_RUNTIME_ARTIFACTS


def expected_package_flavor(*, avatar_sync: bool, gameplay: bool) -> str:
    if gameplay:
        return "gameplay"
    if avatar_sync:
        return "avatar-sync"
    return "default"


def validate_build_manifest_data(
    manifest: dict[str, object],
    *,
    avatar_sync: bool,
    gameplay: bool = False,
) -> tuple[bool, str]:
    errors: list[str] = []
    if manifest.get("schema") != BUILD_MANIFEST_SCHEMA:
        errors.append(f"schema={manifest.get('schema')!r}")
    if manifest.get("platform") != "windows":
        errors.append(f"platform={manifest.get('platform')!r}")
    if manifest.get("arch") != "x64":
        errors.append(f"arch={manifest.get('arch')!r}")
    if bool(manifest.get("avatarSync")) != bool(avatar_sync):
        errors.append(f"avatarSync={manifest.get('avatarSync')!r} expected={avatar_sync}")
    if bool(manifest.get("gameplay")) != bool(gameplay):
        errors.append(f"gameplay={manifest.get('gameplay')!r} expected={gameplay}")
    expected_flavor = expected_package_flavor(avatar_sync=avatar_sync, gameplay=gameplay)
    if str(manifest.get("packageFlavor", "")) != expected_flavor:
        errors.append(f"packageFlavor={manifest.get('packageFlavor')!r} expected={expected_flavor!r}")
    if manifest.get("stagedGameFiles") is not True:
        errors.append("stagedGameFiles is not true")
    if manifest.get("companionPanel") is not True:
        errors.append("companionPanel is not true")

    targets = manifest.get("targets")
    if not isinstance(targets, list):
        errors.append("targets is not a list")
        target_set = set()
    else:
        target_set = {str(target) for target in targets}
    required_targets = expected_manifest_targets(avatar_sync=avatar_sync, gameplay=gameplay)
    missing_targets = [target for target in required_targets if target not in target_set]
    if missing_targets:
        errors.append("missing targets: " + ", ".join(missing_targets))

    copied_artifacts = manifest.get("copiedArtifacts")
    if not isinstance(copied_artifacts, list):
        errors.append("copiedArtifacts is not a list")
        artifact_set = set()
    else:
        artifact_set = {str(artifact) for artifact in copied_artifacts}
    required_artifacts = expected_runtime_artifacts(avatar_sync=avatar_sync, gameplay=gameplay)
    missing_artifacts = [artifact for artifact in required_artifacts if artifact not in artifact_set]
    if missing_artifacts:
        errors.append("missing copied artifacts: " + ", ".join(missing_artifacts))

    mode = str(manifest.get("mode", "<missing>"))
    target_summary = ",".join(sorted(target_set)) if target_set else "<missing>"
    if errors:
        return False, f"mode={mode} targets={target_summary} errors=" + "; ".join(errors)
    return True, f"mode={mode} avatarSync={int(avatar_sync)} gameplay={int(gameplay)} targets={target_summary}"


def validate_build_manifest_file(
    path: pathlib.Path,
    *,
    avatar_sync: bool,
    gameplay: bool = False,
) -> tuple[bool, str, dict[str, object]]:
    manifest, load_detail = load_json_file(path)
    if not manifest:
        return False, load_detail, manifest
    ok, detail = validate_build_manifest_data(manifest, avatar_sync=avatar_sync, gameplay=gameplay)
    return ok, detail, manifest


def build_runtime_checklist(
    readouts: dict[str, dict[str, str]],
    log_path: pathlib.Path,
    *,
    skip_log: bool,
    avatar_sync: bool,
    build_manifest_ok: bool,
    build_manifest_detail: str,
    require_higgs: bool,
    require_remote_player: bool,
    require_weapon_pose: bool,
    require_magic_pose: bool,
    require_projectile_pose: bool,
    require_movement_relay: bool,
    require_equipment_relay: bool,
    require_activation_relay: bool,
    require_magic_relay: bool,
    require_combat_relay: bool,
    require_projectile_relay: bool,
    require_grab_relay: bool,
    require_higgs_relay: bool,
    require_saveload_observer: bool,
) -> dict[str, object]:
    checks: list[dict[str, str]] = []
    status = readouts.get("status", {})
    pose = readouts.get("pose", {})
    movement = readouts.get("movement", {})
    inventory = readouts.get("inventory", {})
    discovery = readouts.get("discovery", {})
    playercell = readouts.get("playercell", {})
    activation = readouts.get("activation", {})
    magic = readouts.get("magic", {})
    combat = readouts.get("combat", {})
    projectile = readouts.get("projectile", {})
    grab = readouts.get("grab", {})
    compat = readouts.get("compat", {})
    higgs = readouts.get("higgs", {})
    higgsnet = readouts.get("higgsnet", {})
    planck = readouts.get("planck", {})
    saveload = readouts.get("saveload", {})
    remoteplayers = readouts.get("remoteplayers", {})
    avatar = readouts.get("avatar", {})

    log_ok, log_detail = log_breadcrumb_detail(log_path, skip_log)
    add_check(
        checks,
        "package_build_manifest",
        "1,8,9",
        "installed package build manifest",
        build_manifest_ok,
        build_manifest_detail,
    )
    add_check(
        checks,
        "startup_breadcrumbs",
        "5",
        "startup/main-loop/render logging",
        log_ok,
        log_detail,
    )
    add_check(
        checks,
        "connection_status",
        "8",
        "basic connection/session flow",
        get_bool(status, "online"),
        f"state={status.get('state', '<missing>')} online={status.get('online', '<missing>')}",
    )
    add_check(
        checks,
        "local_pose",
        "9",
        "local HMD and hand pose capture",
        get_bool(pose, "localPoseAvailable")
        and get_bool(pose, "local.hmd.valid")
        and get_bool(pose, "local.leftHand.valid")
        and get_bool(pose, "local.rightHand.valid"),
        "localPoseAvailable={} hmd={} left={} right={}".format(
            pose.get("localPoseAvailable", "<missing>"),
            pose.get("local.hmd.valid", "<missing>"),
            pose.get("local.leftHand.valid", "<missing>"),
            pose.get("local.rightHand.valid", "<missing>"),
        ),
    )
    add_check(
        checks,
        "local_vrik_api",
        "9",
        "mandatory local VRIK IK lane",
        get_bool(pose, "local.vrik.detected") and get_bool(pose, "local.vrik.interfaceAvailable"),
        "local.vrik.detected={} local.vrik.interfaceAvailable={}".format(
            pose.get("local.vrik.detected", "<missing>"),
            pose.get("local.vrik.interfaceAvailable", "<missing>"),
        ),
    )

    weapon_pose_available = get_bool(pose, "local.leftWeaponOffset.valid") or get_bool(
        pose, "local.rightWeaponOffset.valid"
    )
    add_conditional_check(
        checks,
        "weapon_pose_context",
        "9",
        "weapon pose context",
        weapon_pose_available,
        "leftWeaponOffset={} rightWeaponOffset={}".format(
            pose.get("local.leftWeaponOffset.valid", "<missing>"),
            pose.get("local.rightWeaponOffset.valid", "<missing>"),
        ),
        required=require_weapon_pose,
        not_required_detail="run collection with --require-weapon-pose after drawing/equipping a weapon",
    )

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
    add_conditional_check(
        checks,
        "magic_pose_context",
        "9",
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
        required=require_magic_pose,
        not_required_detail="run collection with --require-magic-pose after equipping/casting magic",
    )

    projectile_pose_available = (
        get_bool(pose, "local.arrowOrigin.valid")
        and get_bool(pose, "local.arrowDestination.valid")
    ) or get_bool(pose, "local.bowAim.valid")
    add_conditional_check(
        checks,
        "projectile_pose_context",
        "9",
        "arrow/projectile pose context",
        projectile_pose_available,
        "arrowOrigin={} arrowDestination={} bowAim={} bowRotation={}".format(
            pose.get("local.arrowOrigin.valid", "<missing>"),
            pose.get("local.arrowDestination.valid", "<missing>"),
            pose.get("local.bowAim.valid", "<missing>"),
            pose.get("local.bowRotation.valid", "<missing>"),
        ),
        required=require_projectile_pose,
        not_required_detail="run collection with --require-projectile-pose after drawing/firing a bow or projectile spell",
    )

    remote_player_proxy_available = get_bool(remoteplayers, "ready") and get_int(remoteplayers, "trackedPlayerCount") > 0
    require_remote_avatar = require_remote_player or avatar_sync
    add_conditional_check(
        checks,
        "remote_player_proxy",
        "8,9",
        "remote-player proxy readout",
        remote_player_proxy_available,
        "ready={} trackedPlayerCount={}".format(
            remoteplayers.get("ready", "<missing>"),
            remoteplayers.get("trackedPlayerCount", "<missing>"),
        ),
        required=require_remote_avatar,
        not_required_detail="run collection with --require-remote-player or --avatar-sync after a two-client test",
    )
    discovery_ok, discovery_detail = audit_runtime_handoff.discovery_schema_detail(discovery)
    add_check(
        checks,
        "discovery_schema",
        "8,12",
        "discovery readout schema",
        discovery_ok,
        discovery_detail,
    )
    playercell_ok, playercell_detail = audit_runtime_handoff.player_cell_schema_detail(playercell)
    add_check(
        checks,
        "player_cell_status",
        "8",
        "VR player cell/grid/level status",
        playercell_ok,
        playercell_detail,
    )
    remote_avatar_ready = (
        get_int(remoteplayers, "avatarValidationReadyCount") > 0
        and any(
            key.startswith("remotePlayer.")
            and key.endswith(".avatarValidationReady")
            and value == "1"
            and get_bool(remoteplayers, key.removesuffix(".avatarValidationReady") + ".hmdAvailable")
            and get_bool(remoteplayers, key.removesuffix(".avatarValidationReady") + ".leftHandAvailable")
            and get_bool(remoteplayers, key.removesuffix(".avatarValidationReady") + ".rightHandAvailable")
            for key, value in remoteplayers.items()
        )
    )
    add_conditional_check(
        checks,
        "remote_avatar_ready",
        "9",
        "remote VRIK avatar readiness",
        remote_avatar_ready,
        "avatarValidationReadyCount={} hmd={} leftHand={} rightHand={}".format(
            remoteplayers.get("avatarValidationReadyCount", "<missing>"),
            next(
                (
                    value
                    for key, value in remoteplayers.items()
                    if key.startswith("remotePlayer.") and key.endswith(".hmdAvailable")
                ),
                "<missing>",
            ),
            next(
                (
                    value
                    for key, value in remoteplayers.items()
                    if key.startswith("remotePlayer.") and key.endswith(".leftHandAvailable")
                ),
                "<missing>",
            ),
            next(
                (
                    value
                    for key, value in remoteplayers.items()
                    if key.startswith("remotePlayer.") and key.endswith(".rightHandAvailable")
                ),
                "<missing>",
            ),
        ),
        required=require_remote_avatar,
        not_required_detail="run collection with --require-remote-player or --avatar-sync after a two-client VRIK/avatar test",
    )
    remote_higgs_avatar_ready = (
        get_int(remoteplayers, "higgsAvatarValidationReadyCount") > 0
        and any(
            key.startswith("remotePlayer.")
            and key.endswith(".higgsAvatarValidationReady")
            and value == "1"
            and get_bool(remoteplayers, key.removesuffix(".higgsAvatarValidationReady") + ".higgsAvailable")
            for key, value in remoteplayers.items()
        )
    )
    add_conditional_check(
        checks,
        "remote_higgs_avatar_ready",
        "9,12",
        "remote VRIK/HIGGS avatar readiness",
        remote_higgs_avatar_ready,
        "higgsAvatarValidationReadyCount={} higgsAvailable={}".format(
            remoteplayers.get("higgsAvatarValidationReadyCount", "<missing>"),
            next(
                (
                    value
                    for key, value in remoteplayers.items()
                    if key.startswith("remotePlayer.") and key.endswith(".higgsAvailable")
                ),
                "<missing>",
            ),
        ),
        required=avatar_sync and require_higgs,
        not_required_detail="run collection with --avatar-sync and --require-higgs after a two-client HIGGS/avatar test",
    )
    movement_relay_available = (
        get_bool(movement, "localMovementAvailable")
        and get_int(movement, "localMovement.sequence") > 0
        and get_int(movement, "remoteMovementCount") > 0
        and any_remote_sequence(movement, "remoteMovement")
    )
    add_conditional_check(
        checks,
        "movement_relay",
        "12",
        "movement relay",
        movement_relay_available,
        "localMovementAvailable={} localSequence={} remoteMovementCount={} remoteSequencePresent={}".format(
            movement.get("localMovementAvailable", "<missing>"),
            movement.get("localMovement.sequence", "<missing>"),
            movement.get("remoteMovementCount", "<missing>"),
            int(any_remote_sequence(movement, "remoteMovement")),
        ),
        required=require_movement_relay,
        not_required_detail="run collection with --require-movement-relay after a two-client movement test",
    )
    equipment_relay_available = (
        get_bool(inventory, "localEquipmentAvailable")
        and get_int(inventory, "localEquipment.sequence") > 0
        and get_int(inventory, "remoteEquipmentCount") > 0
        and any_remote_sequence(inventory, "remoteEquipment")
    )
    inventory_boundary_ok = (
        inventory.get("policy") == "equipment_snapshot_only"
        and inventory.get("fullInventoryTraversal") == "0"
        and inventory.get("inventoryMutation") == "0"
        and inventory.get("remoteEquipmentMutation") == "0"
        and inventory.get("normalInventoryPackets") == "0"
    )
    add_check(
        checks,
        "inventory_boundary",
        "12",
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
    add_conditional_check(
        checks,
        "equipment_relay",
        "12",
        "equipment relay",
        equipment_relay_available,
        "localEquipmentAvailable={} localSequence={} remoteEquipmentCount={} remoteSequencePresent={}".format(
            inventory.get("localEquipmentAvailable", "<missing>"),
            inventory.get("localEquipment.sequence", "<missing>"),
            inventory.get("remoteEquipmentCount", "<missing>"),
            int(any_remote_sequence(inventory, "remoteEquipment")),
        ),
        required=require_equipment_relay,
        not_required_detail="run collection with --require-equipment-relay after changing weapons/spells/ammo/powers",
    )
    activation_relay_available = (
        get_bool(activation, "localActivationAvailable")
        and game_id_available(activation, "localActivation.object")
        and get_int(activation, "remoteActivationCount") > 0
        and any_remote_context(
            activation,
            "remoteActivation",
            lambda values, prefix: game_id_available(values, f"{prefix}.object"),
        )
    )
    add_conditional_check(
        checks,
        "activation_relay",
        "12",
        "activation relay",
        activation_relay_available,
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
        required=require_activation_relay,
        not_required_detail="run collection with --require-activation-relay after activating objects",
    )
    magic_relay_available = (
        get_bool(magic, "localMagicEffectAvailable")
        and magic_effect_context(magic, "localMagicEffect")
        and get_int(magic, "remoteMagicEffectCount") > 0
        and any_remote_context(magic, "remoteMagicEffect", magic_effect_context)
    )
    add_conditional_check(
        checks,
        "magic_relay",
        "12",
        "magic-effect relay",
        magic_relay_available,
        "localMagicEffectAvailable={} localEffect={} casterIsPlayer={} targetIsPlayer={} remoteMagicEffectCount={} remoteContextPresent={}".format(
            magic.get("localMagicEffectAvailable", "<missing>"),
            magic.get("localMagicEffect.effect.serverBaseId", "<missing>"),
            magic.get("localMagicEffect.casterIsPlayer", "<missing>"),
            magic.get("localMagicEffect.targetIsPlayer", "<missing>"),
            magic.get("remoteMagicEffectCount", "<missing>"),
            int(any_remote_context(magic, "remoteMagicEffect", magic_effect_context)),
        ),
        required=require_magic_relay,
        not_required_detail="run collection with --require-magic-relay after applying magic effects",
    )
    combat_relay_available = (
        get_bool(combat, "localCombatHitAvailable")
        and combat_hit_context(combat, "localCombatHit")
        and get_int(combat, "remoteCombatHitCount") > 0
        and any_remote_context(combat, "remoteCombatHit", combat_hit_context)
    )
    add_conditional_check(
        checks,
        "combat_relay",
        "12",
        "combat-hit relay",
        combat_relay_available,
        "localCombatHitAvailable={} hitterIsPlayer={} hitteeIsPlayer={} localPlanckHit={} remoteCombatHitCount={} remoteContextPresent={} remotePlanckContextPresent={}".format(
            combat.get("localCombatHitAvailable", "<missing>"),
            combat.get("localCombatHit.hitterIsPlayer", "<missing>"),
            combat.get("localCombatHit.hitteeIsPlayer", "<missing>"),
            combat.get("localCombatHit.planckHit", "<missing>"),
            combat.get("remoteCombatHitCount", "<missing>"),
            int(any_remote_context(combat, "remoteCombatHit", combat_hit_context)),
            int(any_remote_context(combat, "remoteCombatHit", lambda values, prefix: get_bool(values, f"{prefix}.planckHit"))),
        ),
        required=require_combat_relay,
        not_required_detail="run collection with --require-combat-relay after hit/being-hit events",
    )
    projectile_relay_available = (
        get_bool(projectile, "localProjectileEventAvailable")
        and projectile_intent_context(projectile, "localProjectileEvent")
        and get_int(projectile, "remoteProjectileEventCount") > 0
        and any_remote_context(projectile, "remoteProjectileEvent", projectile_intent_context)
    )
    add_conditional_check(
        checks,
        "projectile_relay",
        "12",
        "projectile intent relay",
        projectile_relay_available,
        "localProjectileEventAvailable={} localSource={} originValid={} destinationValid={} remoteProjectileEventCount={} remoteContextPresent={}".format(
            projectile.get("localProjectileEventAvailable", "<missing>"),
            projectile.get("localProjectileEvent.source.serverBaseId", "<missing>"),
            projectile.get("localProjectileEvent.originValid", "<missing>"),
            projectile.get("localProjectileEvent.destinationValid", "<missing>"),
            projectile.get("remoteProjectileEventCount", "<missing>"),
            int(any_remote_context(projectile, "remoteProjectileEvent", projectile_intent_context)),
        ),
        required=require_projectile_relay,
        not_required_detail="run collection with --require-projectile-relay after bow shots or projectile spell casts",
    )
    grab_relay_available = (
        get_bool(grab, "localGrabAvailable")
        and game_id_available(grab, "localGrab.object")
        and get_int(grab, "remoteGrabCount") > 0
        and any_remote_context(
            grab,
            "remoteGrab",
            lambda values, prefix: game_id_available(values, f"{prefix}.object"),
        )
    )
    add_conditional_check(
        checks,
        "grab_relay",
        "12",
        "grab/release relay",
        grab_relay_available,
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
        required=require_grab_relay,
        not_required_detail="run collection with --require-grab-relay after vanilla/HIGGS grab and release events",
    )
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
    physics_policy_available = (
        get_bool(compat, "ready")
        and compat.get("higgsPolicy") == "observation_only"
        and compat.get("planckPolicy") == "observation_only"
        and lane_policies_available
        and (not (physics_compat_installed and unvalidated_hooks_compiled) or unvalidated_hooks_suppressed)
    )
    add_check(
        checks,
        "vr_physics_compatibility",
        "12",
        "HIGGS/PLANCK compatibility policy",
        physics_policy_available,
        "ready={} higgsInstalled={} higgsLoaded={} planckInstalled={} planckLoaded={} physicsCompat={} hookMode={} gameplayMode={} avatarPolicy={} proxyPolicy={} discoveryPolicy={} playerCellPolicy={} movementPolicy={} equipmentPolicy={} activationPolicy={} inventoryPolicy={} magicPolicy={} combatPolicy={} projectilePolicy={} grabPolicy={} higgsRelayPolicy={} saveLoadPolicy={} unvalidatedHooks={} suppressed={} higgsPolicy={} planckPolicy={}".format(
            compat.get("ready", "<missing>"),
            compat.get("higgs.installed", "<missing>"),
            compat.get("higgs.loaded", "<missing>"),
            compat.get("planck.installed", "<missing>"),
            compat.get("planck.loaded", "<missing>"),
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
    add_check(
        checks,
        "higgs_bridge",
        "12",
        "HIGGS bridge compatibility",
        get_bool(higgs, "bridge.loaded")
        and get_int(higgs, "bridge.sequence") > 0
        and get_bool(higgs, "higgs.detected")
        and get_bool(higgs, "higgs.interfaceAvailable"),
        "bridge.loaded={} bridge.sequence={} higgs.detected={} higgs.interfaceAvailable={}".format(
            higgs.get("bridge.loaded", "<missing>"),
            higgs.get("bridge.sequence", "<missing>"),
            higgs.get("higgs.detected", "<missing>"),
            higgs.get("higgs.interfaceAvailable", "<missing>"),
        ),
    )
    planck_required_by_compat = get_bool(compat, "planck.installed") or get_bool(compat, "planck.loaded")
    planck_bridge_available = (
        get_bool(planck, "bridge.loaded")
        and get_int(planck, "bridge.sequence") > 0
        and get_bool(planck, "planck.interfaceRequestAttempted")
        and planck.get("planck.policy") == "observation_only"
        and get_bool(planck, "planck.currentHitEventObservationOnly")
        and planck.get("planck.lastHitDataAvailable") == "0"
        and planck.get("planck.lastHitDataProbeEnabled") == "0"
        and planck.get("planck.lastHitDataReason") == "not_polled_nontrivial_return_boundary"
        and planck.get("planck.lastHitDataBoundary") == "disabled_unvalidated_by_value_abi"
    )
    planck_api_available = get_bool(planck, "planck.interfaceAvailable")
    add_check(
        checks,
        "planck_bridge",
        "12",
        "PLANCK bridge compatibility",
        planck_bridge_available and (not planck_required_by_compat or planck_api_available),
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
    higgs_relay_available = (
        get_bool(higgsnet, "ready")
        and get_bool(higgsnet, "localHiggsAvailable")
        and higgs_observation_context(higgsnet, "localHiggs")
        and get_int(higgsnet, "remoteHiggsCount") > 0
        and any_remote_context(higgsnet, "remoteHiggs", higgs_observation_context)
    )
    add_conditional_check(
        checks,
        "higgs_relay",
        "12",
        "HIGGS relay",
        higgs_relay_available,
        "ready={} localHiggsAvailable={} localDetected={} localInterface={} remoteHiggsCount={} remoteContextPresent={}".format(
            higgsnet.get("ready", "<missing>"),
            higgsnet.get("localHiggsAvailable", "<missing>"),
            higgsnet.get("localHiggs.detected", "<missing>"),
            higgsnet.get("localHiggs.interfaceAvailable", "<missing>"),
            higgsnet.get("remoteHiggsCount", "<missing>"),
            int(any_remote_context(higgsnet, "remoteHiggs", higgs_observation_context)),
        ),
        required=require_higgs_relay,
        not_required_detail="run collection with --require-higgs-relay after a two-client HIGGS state test",
    )
    saveload_observer_available = get_bool(saveload, "ready")
    add_conditional_check(
        checks,
        "saveload_observer",
        "12",
        "save/load observer",
        saveload_observer_available,
        audit_runtime_handoff.saveload_detail(saveload),
        required=require_saveload_observer,
        not_required_detail="run collection with --require-saveload-observer after loading saves during the test",
    )

    if avatar_sync:
        skeleton_writes_enabled = get_bool(avatar, "actorSkeletonWritesEnabled")
        skeleton_write_ready = (
            not skeleton_writes_enabled
            or (
                get_int(avatar, "hmdCopiedCount") > 0
                and get_int(avatar, "leftHandCopiedCount") > 0
                and get_int(avatar, "rightHandCopiedCount") > 0
            )
        )
        add_check(
            checks,
            "avatar_sync_actor_targets",
            "9",
            "avatar-sync pose cache and actor write policy",
            get_bool(avatar, "ready")
            and get_bool(avatar, "actorTargetsEnabled")
            and get_int(avatar, "sameSpaceCount") > 0
            and skeleton_write_ready
            and get_int(avatar, "vrikDetectedCount") > 0
            and get_int(avatar, "vrikInterfaceAvailableCount") > 0
            and get_int(avatar, "invalidVrikCount") == 0
            and get_int(avatar, "invalidTransformCount") == 0
            and get_int(avatar, "invalidMovementCount") == 0,
            "ready={} actorTargetsEnabled={} actorSkeletonWritesEnabled={} sameSpaceCount={} hmdCopiedCount={} leftHandCopiedCount={} rightHandCopiedCount={} vrikDetectedCount={} vrikInterfaceAvailableCount={} invalidVrikCount={} invalidTransformCount={} invalidMovementCount={}".format(
                avatar.get("ready", "<missing>"),
                avatar.get("actorTargetsEnabled", "<missing>"),
                avatar.get("actorSkeletonWritesEnabled", "<missing>"),
                avatar.get("sameSpaceCount", "<missing>"),
                avatar.get("hmdCopiedCount", "<missing>"),
                avatar.get("leftHandCopiedCount", "<missing>"),
                avatar.get("rightHandCopiedCount", "<missing>"),
                avatar.get("vrikDetectedCount", "<missing>"),
                avatar.get("vrikInterfaceAvailableCount", "<missing>"),
                avatar.get("invalidVrikCount", "<missing>"),
                avatar.get("invalidTransformCount", "<missing>"),
                avatar.get("invalidMovementCount", "<missing>"),
            ),
        )
    else:
        add_not_required(
            checks,
            "avatar_sync_actor_targets",
            "9",
            "avatar-sync actor target application",
            "run with --avatar-sync for the explicit VRIK/HIGGS remote-avatar validation build",
        )

    summary = {
        CHECK_PASS: sum(1 for check in checks if check["status"] == CHECK_PASS),
        CHECK_FAIL: sum(1 for check in checks if check["status"] == CHECK_FAIL),
        CHECK_NOT_REQUIRED: sum(1 for check in checks if check["status"] == CHECK_NOT_REQUIRED),
    }
    return {
        "schema": "skyrim_together_vr_runtime_checklist_v1",
        "summary": summary,
        "checks": checks,
    }


def format_runtime_checklist(checklist: dict[str, object]) -> str:
    lines = ["SkyrimTogetherVR runtime checklist"]
    summary = checklist.get("summary", {})
    if isinstance(summary, dict):
        lines.append(
            "summary pass={pass_count} fail={fail_count} not_required={not_required_count}".format(
                pass_count=summary.get(CHECK_PASS, 0),
                fail_count=summary.get(CHECK_FAIL, 0),
                not_required_count=summary.get(CHECK_NOT_REQUIRED, 0),
            )
        )
    for check in checklist.get("checks", []):
        if not isinstance(check, dict):
            continue
        lines.append(
            "[{status}] objective={objective} id={check_id} label={label} detail={detail}".format(
                status=check.get("status", "?"),
                objective=check.get("objective", "?"),
                check_id=check.get("id", "?"),
                label=check.get("label", "?"),
                detail=check.get("detail", ""),
            )
        )
    return "\n".join(lines) + "\n"


def skse_log_candidates(args: argparse.Namespace) -> list[pathlib.Path]:
    roots = []
    if args.skse_log_root:
        roots.append(args.skse_log_root.expanduser().resolve())
    roots.append(args.game_path.expanduser().resolve())
    roots.append(pathlib.Path.home() / WINDOWS_MY_GAMES_RELATIVE)
    user_profile = os.environ.get("USERPROFILE")
    if user_profile:
        roots.append(pathlib.Path(user_profile) / WINDOWS_MY_GAMES_RELATIVE)

    candidates: list[pathlib.Path] = []
    seen: set[pathlib.Path] = set()
    for root in roots:
        if not str(root):
            continue
        for name in SKSE_LOG_NAMES:
            candidate = (root / name).resolve()
            if candidate not in seen:
                seen.add(candidate)
                candidates.append(candidate)
    return candidates


def write_runtime_audit(
    zf: zipfile.ZipFile,
    args: argparse.Namespace,
    handoff_dir: pathlib.Path,
    log_path: pathlib.Path,
) -> int | None:
    if args.no_audit:
        return None

    audit_args = argparse.Namespace(
        game_path=args.game_path,
        handoff_dir=handoff_dir,
        log=log_path,
        skip_log=args.skip_log,
        require_connected=args.require_connected,
        require_vrik=args.require_vrik,
        require_higgs=args.require_higgs,
        require_remote_player=args.require_remote_player,
        require_weapon_pose=args.require_weapon_pose,
        require_magic_pose=args.require_magic_pose,
        require_projectile_pose=args.require_projectile_pose,
        require_movement_relay=args.require_movement_relay,
        require_equipment_relay=args.require_equipment_relay,
        require_activation_relay=args.require_activation_relay,
        require_magic_relay=args.require_magic_relay,
        require_combat_relay=args.require_combat_relay,
        require_projectile_relay=args.require_projectile_relay,
        require_grab_relay=args.require_grab_relay,
        require_higgs_relay=args.require_higgs_relay,
        require_saveload_observer=args.require_saveload_observer,
        avatar_sync=args.avatar_sync or args.gameplay,
    )
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        result = audit_runtime_handoff.run_audit(audit_args)
    zf.writestr("runtime_audit.txt", output.getvalue())
    return result


def collect(args: argparse.Namespace) -> pathlib.Path:
    game_path = args.game_path.expanduser().resolve()
    args.game_path = game_path
    handoff_dir = resolve_handoff_dir(args)
    log_path = args.log.expanduser().resolve() if args.log else game_path / DEFAULT_LOG_RELATIVE
    build_manifest_path = game_path / BUILD_MANIFEST_NAME
    build_manifest_ok, build_manifest_detail, build_manifest = validate_build_manifest_file(
        build_manifest_path,
        avatar_sync=args.avatar_sync,
        gameplay=args.gameplay,
    )
    avatar_runtime_checks = args.avatar_sync or args.gameplay
    output_path = resolve_output_path(args.out)
    collected_files: list[dict[str, object]] = []
    manifest: dict[str, object] = {
        "schema": "skyrim_together_vr_runtime_evidence_v1",
        "createdUtc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "gamePath": str(game_path),
        "handoffDir": str(handoff_dir),
        "clientLog": str(log_path),
        "packageBuildManifestPath": str(build_manifest_path),
        "packageBuildManifest": build_manifest,
        "avatarSyncAudit": avatar_runtime_checks,
        "gameplayAudit": args.gameplay,
        "requiredConnected": args.require_connected,
        "requiredVrik": args.require_vrik,
        "requiredHiggs": args.require_higgs,
        "requiredRemotePlayer": args.require_remote_player,
        "requiredWeaponPose": args.require_weapon_pose,
        "requiredMagicPose": args.require_magic_pose,
        "requiredProjectilePose": args.require_projectile_pose,
        "requiredMovementRelay": args.require_movement_relay,
        "requiredEquipmentRelay": args.require_equipment_relay,
        "requiredActivationRelay": args.require_activation_relay,
        "requiredMagicRelay": args.require_magic_relay,
        "requiredCombatRelay": args.require_combat_relay,
        "requiredProjectileRelay": args.require_projectile_relay,
        "requiredGrabRelay": args.require_grab_relay,
        "requiredHiggsRelay": args.require_higgs_relay,
        "requiredSaveloadObserver": args.require_saveload_observer,
    }
    readouts = vr_handoff.read_readouts(handoff_dir)
    runtime_checklist = build_runtime_checklist(
        readouts,
        log_path,
        skip_log=args.skip_log,
        avatar_sync=avatar_runtime_checks,
        build_manifest_ok=build_manifest_ok,
        build_manifest_detail=build_manifest_detail,
        require_higgs=args.require_higgs,
        require_remote_player=args.require_remote_player,
        require_weapon_pose=args.require_weapon_pose,
        require_magic_pose=args.require_magic_pose,
        require_projectile_pose=args.require_projectile_pose,
        require_movement_relay=args.require_movement_relay,
        require_equipment_relay=args.require_equipment_relay,
        require_activation_relay=args.require_activation_relay,
        require_magic_relay=args.require_magic_relay,
        require_combat_relay=args.require_combat_relay,
        require_projectile_relay=args.require_projectile_relay,
        require_grab_relay=args.require_grab_relay,
        require_higgs_relay=args.require_higgs_relay,
        require_saveload_observer=args.require_saveload_observer,
    )

    with zipfile.ZipFile(output_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        add_file(
            zf,
            build_manifest_path,
            f"package/{BUILD_MANIFEST_NAME}",
            collected_files,
            category="package-build-manifest",
            required=True,
        )

        add_file(
            zf,
            log_path,
            "logs/tp_client.log",
            collected_files,
            category="client-log",
            required=not args.skip_log,
        )

        for name, file_name in sorted(vr_handoff.READOUT_FILES.items()):
            required = name != "avatar" or avatar_runtime_checks
            add_file(
                zf,
                handoff_dir / file_name,
                f"handoff/{file_name}",
                collected_files,
                category="handoff",
                required=required,
            )

        for file_name in HANDOFF_EXTRA_FILES:
            add_file(
                zf,
                handoff_dir / file_name,
                f"handoff/{file_name}",
                collected_files,
                category="handoff-control",
                required=False,
            )

        for index, candidate in enumerate(skse_log_candidates(args), start=1):
            add_file(
                zf,
                candidate,
                f"skse/{index:02d}-{candidate.name}",
                collected_files,
                category="skse-log",
                required=False,
            )

        for extra_file in args.extra_file:
            source = extra_file.expanduser().resolve()
            add_file(
                zf,
                source,
                f"extra/{source.name}",
                collected_files,
                category="extra",
                required=False,
            )

        audit_result = write_runtime_audit(zf, args, handoff_dir, log_path)
        manifest["runtimeAuditExitCode"] = audit_result
        manifest["runtimeChecklist"] = runtime_checklist["summary"]
        manifest["files"] = collected_files
        manifest["missingRequired"] = [
            record["archiveName"]
            for record in collected_files
            if record["required"] and not record["exists"]
        ]
        zf.writestr("runtime_checklist.json", json.dumps(runtime_checklist, indent=2, sort_keys=True) + "\n")
        zf.writestr("runtime_checklist.txt", format_runtime_checklist(runtime_checklist))
        zf.writestr("manifest.json", json.dumps(manifest, indent=2, sort_keys=True) + "\n")

    return output_path


def command_self_test(_: argparse.Namespace) -> int:
    with tempfile.TemporaryDirectory(prefix="stvr-evidence-test-") as temp:
        root = pathlib.Path(temp)
        game = root / "SkyrimVR"
        handoff = game / "Data" / "SkyrimTogetherReborn"
        log = game / DEFAULT_LOG_RELATIVE
        out_dir = root / "out"
        handoff.mkdir(parents=True)
        log.parent.mkdir(parents=True)
        log.write_text("\n".join(audit_runtime_handoff.LOG_BREADCRUMBS) + "\n", encoding="utf-8")

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
            "local.vrik.detected=1\nlocal.vrik.interfaceAvailable=1\nremotePoseCount=1\n"
            "remote.7.vrik.detected=1\nremote.7.vrik.interfaceAvailable=1\n",
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
            "remotePlayer.7.poseAvailable=1\nremotePlayer.7.vrikDetected=1\n"
            "remotePlayer.7.vrikInterfaceAvailable=1\nremotePlayer.7.vrikAvailable=1\n"
            "remotePlayer.7.hmdAvailable=1\nremotePlayer.7.leftHandAvailable=1\nremotePlayer.7.rightHandAvailable=1\n"
            "remotePlayer.7.movementAvailable=1\nremotePlayer.7.sameSpace=1\n"
            "remotePlayer.7.higgsAvailable=1\n"
            "remotePlayer.7.avatarValidationReady=1\nremotePlayer.7.avatarValidationBlocker=ready\n"
            "remotePlayer.7.higgsAvatarValidationReady=1\nremotePlayer.7.higgsAvatarValidationBlocker=ready\n",
        )
        write("avatar", "ready=1\nactorTargetsEnabled=1\nactorSkeletonWritesEnabled=0\nsameSpaceCount=1\nhmdCopiedCount=0\nleftHandCopiedCount=0\nrightHandCopiedCount=0\nvrikDetectedCount=1\nvrikInterfaceAvailableCount=1\ninvalidVrikCount=0\ninvalidTransformCount=0\ninvalidMovementCount=0\n")
        (handoff / vr_handoff.CONFIG_FILE).write_text("endpoint=127.0.0.1:10578\n", encoding="utf-8")
        build_manifest = {
            "schema": BUILD_MANIFEST_SCHEMA,
            "mode": "releasedbg",
            "platform": "windows",
            "arch": "x64",
            "avatarSync": True,
            "gameplay": False,
            "packageFlavor": "avatar-sync",
            "targets": list(AVATAR_SYNC_EXPECTED_MANIFEST_TARGETS),
            "copiedArtifacts": list(AVATAR_SYNC_EXPECTED_RUNTIME_ARTIFACTS),
            "expectedArtifacts": list(AVATAR_SYNC_EXPECTED_RUNTIME_ARTIFACTS),
            "packageRoot": str(game),
            "stagedGameFiles": True,
            "companionPanel": True,
            "generatedAtUtc": "2026-01-01T00:00:00.0000000Z",
        }
        (game / BUILD_MANIFEST_NAME).write_text(json.dumps(build_manifest, indent=2), encoding="utf-8")

        args = argparse.Namespace(
            game_path=game,
            handoff_dir=None,
            log=None,
            out=out_dir,
            skse_log_root=None,
            extra_file=[],
            skip_log=False,
            no_audit=False,
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
            gameplay=False,
        )
        archive = collect(args)
        with zipfile.ZipFile(archive) as zf:
            names = set(zf.namelist())
            required_names = {
                "manifest.json",
                "runtime_audit.txt",
                "runtime_checklist.json",
                "runtime_checklist.txt",
                f"package/{BUILD_MANIFEST_NAME}",
                "logs/tp_client.log",
                "handoff/SkyrimTogetherVR.status",
                "handoff/SkyrimTogetherVR.pose",
                "handoff/SkyrimTogetherVR.avatar",
                "handoff/SkyrimTogetherVR.remoteplayers",
                "handoff/SkyrimTogetherVR.compatibility",
                "handoff/SkyrimTogetherVR.planck",
                "handoff/SkyrimTogetherVR.connection",
            }
            missing = sorted(required_names - names)
            if missing:
                print(f"Evidence collector self-test missing archive entries: {', '.join(missing)}")
                return 1
            manifest = json.loads(zf.read("manifest.json").decode("utf-8"))
            if manifest.get("runtimeAuditExitCode") != 0:
                print(f"Evidence collector self-test runtime audit failed: {manifest.get('runtimeAuditExitCode')}")
                return 1
            if manifest.get("packageBuildManifest", {}).get("schema") != BUILD_MANIFEST_SCHEMA:
                print("Evidence collector self-test did not embed package build manifest metadata.")
                return 1
            checklist = json.loads(zf.read("runtime_checklist.json").decode("utf-8"))
            checklist_summary = checklist.get("summary", {})
            if checklist_summary.get(CHECK_FAIL) != 0:
                print(f"Evidence collector self-test checklist failures: {checklist_summary}")
                return 1

        baseline_readouts = {
            "status": {"state": "online", "online": "1"},
            "compat": {
                "ready": "1",
                "higgs.installed": "1",
                "higgs.loaded": "1",
                "planck.installed": "1",
                "planck.loaded": "1",
                "vrPhysicsCompatibilityModInstalled": "1",
                "unvalidatedHooksCompiled": "0",
                "unvalidatedGameplayHooksSuppressed": "0",
                "gameplayMode": "connection_only",
                "remoteAvatarPolicy": "disabled",
                "remotePlayerProxyPolicy": "readout_only",
                "discoveryPolicy": "observation_only",
                "playerCellPolicy": "network_only",
                "movementPolicy": "observation_only",
                "equipmentPolicy": "observation_only",
                "activationPolicy": "observation_only",
                "inventoryPolicy": "equipment_snapshot_only",
                "magicPolicy": "observation_only",
                "combatPolicy": "observation_only",
                "projectilePolicy": "observation_only",
                "grabPolicy": "observation_only",
                "higgsRelayPolicy": "observation_only",
                "saveLoadPolicy": "observation_only",
                "higgsPolicy": "observation_only",
                "planckPolicy": "observation_only",
            },
            "pose": {
                "localPoseAvailable": "1",
                "local.hmd.valid": "1",
                "local.leftHand.valid": "1",
                "local.rightHand.valid": "1",
                "local.vrik.detected": "1",
                "local.vrik.interfaceAvailable": "1",
            },
            "movement": {"localMovementAvailable": "1"},
            "inventory": {
                "ready": "1",
                "policy": "equipment_snapshot_only",
                "fullInventoryTraversal": "0",
                "inventoryMutation": "0",
                "remoteEquipmentMutation": "0",
                "normalInventoryPackets": "0",
                "localEquipmentAvailable": "1",
                "remoteEquipmentCount": "0",
            },
            "discovery": {
                "ready": "1",
                "actorCount": "0",
                "actorLimit": "32",
                "currentGrid": "0,0",
                "centerGrid": "0,0",
                "cachedWorldSpaceFormId": "60",
                "cachedInteriorCellFormId": "0",
                "playerCellFormId": "100",
                "playerWorldSpaceFormId": "60",
                "locationFormId": "0",
            },
            "playercell": {
                "ready": "1",
                "online": "1",
                "localPlayerId": "4",
                "playerFormId": "20",
                "currentLevel": "1",
                "cachedLevel": "1",
                "lastLevelSent": "1",
                "gridCellRequestCount": "1",
                "exteriorCellRequestCount": "1",
                "interiorCellRequestCount": "0",
                "levelRequestCount": "1",
                "offlineSkippedRequestCount": "0",
                "worldSpaceTranslationFailureCount": "0",
                "lastGrid.valid": "1",
                "lastGrid.worldSpace.serverModId": "1",
                "lastGrid.worldSpace.serverBaseId": "60",
                "lastGrid.playerCell.serverModId": "1",
                "lastGrid.playerCell.serverBaseId": "100",
                "lastGrid.center": "0,0",
                "lastGrid.cellCount": "25",
                "lastCell.valid": "1",
                "lastCell.exterior": "1",
                "lastCell.cell.serverModId": "1",
                "lastCell.cell.serverBaseId": "100",
                "lastCell.worldSpace.serverModId": "1",
                "lastCell.worldSpace.serverBaseId": "60",
                "lastCell.currentCoords": "0,0",
            },
            "remoteplayers": {
                "ready": "1",
                "trackedPlayerCount": "0",
                "avatarValidationReadyCount": "0",
                "higgsAvatarValidationReadyCount": "0",
            },
            "higgs": {
                "bridge.loaded": "1",
                "bridge.sequence": "4",
                "higgs.detected": "1",
                "higgs.interfaceAvailable": "1",
            },
            "planck": {
                "bridge.loaded": "1",
                "bridge.sequence": "4",
                "planck.detected": "1",
                "planck.interfaceRequestAttempted": "1",
                "planck.interfaceAvailable": "1",
                "planck.buildNumber": "8",
                "planck.currentHitEventAvailable": "0",
                "planck.currentHitEventObservationOnly": "1",
                "planck.lastHitDataAvailable": "0",
                "planck.lastHitDataProbeEnabled": "0",
                "planck.lastHitDataReason": "not_polled_nontrivial_return_boundary",
                "planck.lastHitDataBoundary": "disabled_unvalidated_by_value_abi",
                "planck.policy": "observation_only",
            },
            "saveload": {
                "ready": "1",
                "online": "1",
                "localPlayerId": "4",
                "connectionState": "online",
                "loadGameObserved": "1",
                "loadGameCount": "1",
                "readyAfterLastLoad": "1",
                "waitingForReadyAfterLoad": "0",
                "secondsSinceLastLoad": "0.5",
                "playerFormId": "20",
                "playerCellFormId": "100",
                "playerWorldSpaceFormId": "60",
                "player.serverModId": "0",
                "player.serverBaseId": "20",
                "playerCell.serverModId": "1",
                "playerCell.serverBaseId": "100",
                "playerWorldSpace.serverModId": "1",
                "playerWorldSpace.serverBaseId": "60",
            },
        }
        baseline_checklist = build_runtime_checklist(
            baseline_readouts,
            log,
            skip_log=False,
            avatar_sync=False,
            build_manifest_ok=True,
            build_manifest_detail="baseline",
            require_higgs=False,
            require_remote_player=False,
            require_weapon_pose=False,
            require_magic_pose=False,
            require_projectile_pose=False,
            require_movement_relay=False,
            require_equipment_relay=False,
            require_activation_relay=False,
            require_magic_relay=False,
            require_combat_relay=False,
            require_projectile_relay=False,
            require_grab_relay=False,
            require_higgs_relay=False,
            require_saveload_observer=False,
        )
        baseline_checks = {
            str(check.get("id")): check
            for check in baseline_checklist.get("checks", [])
            if isinstance(check, dict)
        }
        for check_id in ("remote_player_proxy", "remote_avatar_ready", "remote_higgs_avatar_ready"):
            if baseline_checks.get(check_id, {}).get("status") != CHECK_NOT_REQUIRED:
                print(f"Evidence collector self-test expected {check_id} to be not_required.")
                return 1

        print(f"Evidence collector self-test archive: {archive}")
        return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-path", type=pathlib.Path, default=default_game_path())
    parser.add_argument("--handoff-dir", type=pathlib.Path, help="override Data/SkyrimTogetherReborn handoff directory")
    parser.add_argument("--log", type=pathlib.Path, help="override client log path")
    parser.add_argument("--out", type=pathlib.Path, help="output zip path or output directory")
    parser.add_argument("--skse-log-root", type=pathlib.Path, help="directory containing sksevr.log files")
    parser.add_argument("--extra-file", type=pathlib.Path, action="append", default=[], help="extra file to include in the evidence zip")
    parser.add_argument("--skip-log", action="store_true", help="do not require tp_client.log in the embedded runtime audit")
    parser.add_argument("--no-audit", action="store_true", help="collect files without embedding runtime_audit.txt")
    parser.add_argument("--require-connected", action="store_true", help="embedded audit requires online connection status")
    parser.add_argument("--require-vrik", action="store_true", help="embedded audit requires local VRIK bridge API lane data")
    parser.add_argument("--require-higgs", action="store_true", help="embedded audit requires HIGGS bridge and relay data")
    parser.add_argument("--require-remote-player", action="store_true", help="embedded audit requires remote pose/movement/equipment/proxy data")
    parser.add_argument("--require-weapon-pose", action="store_true", help="runtime checklist requires weapon offset pose nodes")
    parser.add_argument("--require-magic-pose", action="store_true", help="runtime checklist requires spell or magic aim/origin pose nodes")
    parser.add_argument("--require-projectile-pose", action="store_true", help="runtime checklist requires arrow/projectile pose nodes")
    parser.add_argument("--require-vr-pose-context", action="store_true", help="runtime checklist requires weapon, magic, and projectile pose context")
    parser.add_argument("--require-movement-relay", action="store_true", help="runtime checklist requires movement relay evidence")
    parser.add_argument("--require-equipment-relay", action="store_true", help="runtime checklist requires equipment relay evidence")
    parser.add_argument("--require-activation-relay", action="store_true", help="runtime checklist requires activation relay evidence")
    parser.add_argument("--require-magic-relay", action="store_true", help="runtime checklist requires magic-effect relay evidence")
    parser.add_argument("--require-combat-relay", action="store_true", help="runtime checklist requires combat-hit relay evidence")
    parser.add_argument("--require-projectile-relay", action="store_true", help="runtime checklist requires projectile relay evidence")
    parser.add_argument("--require-grab-relay", action="store_true", help="runtime checklist requires grab/release relay evidence")
    parser.add_argument("--require-higgs-relay", action="store_true", help="runtime checklist requires HIGGS relay evidence")
    parser.add_argument("--require-saveload-observer", action="store_true", help="runtime checklist requires save/load observer evidence")
    parser.add_argument("--require-gameplay-relays", action="store_true", help="runtime checklist requires all staged gameplay relay evidence")
    parser.add_argument("--avatar-sync", action="store_true", help="embedded audit requires explicit avatar-sync runtime data")
    parser.add_argument("--gameplay", action="store_true", help="embedded audit validates the full gameplay package manifest and avatar/relay runtime data")
    parser.add_argument("--self-test", action="store_true", help="run a temp-directory evidence collection fixture")
    args = parser.parse_args()

    if args.avatar_sync and args.gameplay:
        parser.error("--avatar-sync and --gameplay cannot be combined")
    if args.require_vr_pose_context:
        args.require_weapon_pose = True
        args.require_magic_pose = True
        args.require_projectile_pose = True
    if args.avatar_sync:
        args.require_connected = True
        args.require_remote_player = True
        args.require_vrik = True
        args.require_higgs = True
    if args.gameplay:
        args.require_connected = True
        args.require_remote_player = True
        args.require_vrik = True
        args.require_higgs = True
        args.require_weapon_pose = True
        args.require_magic_pose = True
        args.require_projectile_pose = True
        args.require_gameplay_relays = True
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

    archive = collect(args)
    print("SkyrimTogetherVR runtime evidence collection does not launch Skyrim or mutate the game install.")
    print(f"Evidence archive: {archive}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
