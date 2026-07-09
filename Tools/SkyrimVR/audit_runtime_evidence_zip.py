#!/usr/bin/env python3
"""Audit a SkyrimTogetherVR runtime evidence zip without needing the game folder."""

from __future__ import annotations

import argparse
import json
import pathlib
import tempfile
import zipfile

import collect_runtime_evidence


REQUIRED_ARCHIVE_ENTRIES = (
    "manifest.json",
    "package/SkyrimTogetherVR_BuildManifest.json",
    "runtime_audit.txt",
    "runtime_checklist.json",
    "runtime_checklist.txt",
    "logs/tp_client.log",
)

REQUIRED_CHECK_IDS = (
    "package_build_manifest",
    "startup_breadcrumbs",
    "connection_status",
    "local_pose",
    "local_vrik_api",
    "weapon_pose_context",
    "magic_pose_context",
    "projectile_pose_context",
    "remote_player_proxy",
    "discovery_schema",
    "player_cell_status",
    "remote_avatar_ready",
    "remote_higgs_avatar_ready",
    "movement_relay",
    "equipment_relay",
    "activation_relay",
    "magic_relay",
    "combat_relay",
    "projectile_relay",
    "grab_relay",
    "vr_physics_compatibility",
    "higgs_bridge",
    "planck_bridge",
    "higgs_relay",
    "saveload_observer",
    "avatar_sync_actor_targets",
)


MANIFEST_FLAG_REQUIRED_CHECKS = (
    ("requiredConnected", "connection_status", "connection status"),
    ("requiredVrik", "local_vrik_api", "local VRIK API"),
    ("requiredHiggs", "higgs_bridge", "HIGGS bridge"),
    ("requiredRemotePlayer", "remote_player_proxy", "remote-player proxy"),
    ("requiredRemotePlayer", "remote_avatar_ready", "remote VRIK avatar readiness"),
    ("requiredWeaponPose", "weapon_pose_context", "weapon pose context"),
    ("requiredMagicPose", "magic_pose_context", "magic pose context"),
    ("requiredProjectilePose", "projectile_pose_context", "projectile pose context"),
    ("requiredMovementRelay", "movement_relay", "movement relay"),
    ("requiredEquipmentRelay", "equipment_relay", "equipment relay"),
    ("requiredActivationRelay", "activation_relay", "activation relay"),
    ("requiredMagicRelay", "magic_relay", "magic relay"),
    ("requiredCombatRelay", "combat_relay", "combat relay"),
    ("requiredProjectileRelay", "projectile_relay", "projectile relay"),
    ("requiredGrabRelay", "grab_relay", "grab relay"),
    ("requiredHiggsRelay", "higgs_relay", "HIGGS relay"),
    ("requiredSaveloadObserver", "saveload_observer", "save/load observer"),
)


def load_json(zf: zipfile.ZipFile, name: str, failures: list[str]) -> dict[str, object]:
    try:
        return json.loads(zf.read(name).decode("utf-8"))
    except KeyError:
        failures.append(f"missing archive entry: {name}")
    except json.JSONDecodeError as exc:
        failures.append(f"{name} is not valid JSON: {exc}")
    except UnicodeDecodeError as exc:
        failures.append(f"{name} is not UTF-8: {exc}")
    return {}


def require_check_pass(
    checks_by_id: dict[str, dict[str, object]],
    check_id: str,
    label: str,
    failures: list[str],
) -> None:
    check = checks_by_id.get(check_id, {})
    if check.get("status") != collect_runtime_evidence.CHECK_PASS:
        failures.append(f"{label} checklist did not pass: {check.get('detail', '<missing detail>')}")


def require_manifest_requested_checks(
    manifest: dict[str, object],
    checks_by_id: dict[str, dict[str, object]],
    failures: list[str],
) -> None:
    for manifest_flag, check_id, label in MANIFEST_FLAG_REQUIRED_CHECKS:
        if bool(manifest.get(manifest_flag)):
            require_check_pass(checks_by_id, check_id, f"manifest-required {label}", failures)
    if bool(manifest.get("avatarSyncAudit")):
        require_check_pass(
            checks_by_id,
            "connection_status",
            "manifest avatar-sync connection status",
            failures,
        )
        require_check_pass(
            checks_by_id,
            "local_vrik_api",
            "manifest avatar-sync local VRIK API",
            failures,
        )
        require_check_pass(
            checks_by_id,
            "higgs_bridge",
            "manifest avatar-sync HIGGS bridge",
            failures,
        )
        require_check_pass(
            checks_by_id,
            "remote_player_proxy",
            "manifest avatar-sync remote-player proxy",
            failures,
        )
        require_check_pass(
            checks_by_id,
            "remote_avatar_ready",
            "manifest avatar-sync remote VRIK avatar readiness",
            failures,
        )
        require_check_pass(
            checks_by_id,
            "remote_higgs_avatar_ready",
            "manifest avatar-sync remote VRIK/HIGGS avatar readiness",
            failures,
        )
        require_check_pass(
            checks_by_id,
            "avatar_sync_actor_targets",
            "manifest avatar-sync actor target application",
            failures,
        )


def audit_archive(
    path: pathlib.Path,
    *,
    require_avatar_sync: bool,
    require_gameplay: bool,
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
    allow_failed_checks: bool,
) -> int:
    failures: list[str] = []
    warnings: list[str] = []
    if not path.exists():
        print(f"Evidence archive does not exist: {path}")
        return 1
    if not zipfile.is_zipfile(path):
        print(f"Evidence archive is not a zip file: {path}")
        return 1

    with zipfile.ZipFile(path) as zf:
        names = set(zf.namelist())
        for entry in REQUIRED_ARCHIVE_ENTRIES:
            if entry not in names:
                failures.append(f"missing archive entry: {entry}")

        manifest = load_json(zf, "manifest.json", failures) if "manifest.json" in names else {}
        package_manifest = (
            load_json(zf, "package/SkyrimTogetherVR_BuildManifest.json", failures)
            if "package/SkyrimTogetherVR_BuildManifest.json" in names
            else {}
        )
        checklist = load_json(zf, "runtime_checklist.json", failures) if "runtime_checklist.json" in names else {}

        manifest_schema = manifest.get("schema")
        if manifest_schema != "skyrim_together_vr_runtime_evidence_v1":
            failures.append(f"unexpected manifest schema: {manifest_schema!r}")

        checklist_schema = checklist.get("schema")
        if checklist_schema != "skyrim_together_vr_runtime_checklist_v1":
            failures.append(f"unexpected runtime checklist schema: {checklist_schema!r}")

        missing_required = manifest.get("missingRequired", [])
        if missing_required:
            failures.append("collector reported missing required file(s): " + ", ".join(map(str, missing_required)))

        runtime_audit_exit = manifest.get("runtimeAuditExitCode")
        if runtime_audit_exit not in (0, None):
            failures.append(f"embedded runtime audit failed with exit code {runtime_audit_exit}")

        manifest_avatar_sync = bool(manifest.get("avatarSyncAudit"))
        manifest_gameplay = bool(manifest.get("gameplayAudit"))
        if require_avatar_sync and not require_gameplay and (not manifest_avatar_sync or manifest_gameplay):
            failures.append("archive was not collected with --avatar-sync")
        elif require_avatar_sync and not manifest_avatar_sync:
            failures.append("archive was not collected with --avatar-sync")
        if require_gameplay and not manifest_gameplay:
            failures.append("archive was not collected with --gameplay")

        if package_manifest:
            package_manifest_ok, package_manifest_detail = collect_runtime_evidence.validate_build_manifest_data(
                package_manifest,
                avatar_sync=manifest_avatar_sync and not manifest_gameplay,
                gameplay=manifest_gameplay,
            )
            if not package_manifest_ok:
                failures.append("package build manifest validation failed: " + package_manifest_detail)
            embedded_package_manifest = manifest.get("packageBuildManifest")
            if isinstance(embedded_package_manifest, dict):
                embedded_ok, embedded_detail = collect_runtime_evidence.validate_build_manifest_data(
                    embedded_package_manifest,
                    avatar_sync=manifest_avatar_sync and not manifest_gameplay,
                    gameplay=manifest_gameplay,
                )
                if not embedded_ok:
                    failures.append("embedded package build manifest validation failed: " + embedded_detail)
            else:
                failures.append("manifest.json packageBuildManifest field is not an object")

        checks = checklist.get("checks", [])
        if not isinstance(checks, list):
            failures.append("runtime_checklist.json has no checks list")
            checks = []

        checks_by_id: dict[str, dict[str, object]] = {}
        for check in checks:
            if not isinstance(check, dict):
                warnings.append(f"ignored non-object checklist row: {check!r}")
                continue
            check_id = str(check.get("id", ""))
            if check_id:
                checks_by_id[check_id] = check

        for check_id in REQUIRED_CHECK_IDS:
            if check_id not in checks_by_id:
                failures.append(f"runtime checklist missing check id: {check_id}")

        failed_checks = [
            check
            for check in checks_by_id.values()
            if check.get("status") == collect_runtime_evidence.CHECK_FAIL
        ]
        if failed_checks and not allow_failed_checks:
            for check in failed_checks:
                failures.append(
                    "runtime checklist failed {id}: {detail}".format(
                        id=check.get("id", "<unknown>"),
                        detail=check.get("detail", ""),
                    )
                )

        manifest_checklist_summary = manifest.get("runtimeChecklist")
        checklist_summary = checklist.get("summary")
        if isinstance(manifest_checklist_summary, dict) and isinstance(checklist_summary, dict):
            if manifest_checklist_summary != checklist_summary:
                failures.append("manifest runtimeChecklist summary does not match runtime_checklist.json summary")
        else:
            failures.append("manifest/runtime checklist summary is not present in both JSON records")

        require_manifest_requested_checks(manifest, checks_by_id, failures)

        avatar_check = checks_by_id.get("avatar_sync_actor_targets", {})
        if require_avatar_sync and avatar_check.get("status") != collect_runtime_evidence.CHECK_PASS:
            failures.append(
                "avatar-sync actor target checklist did not pass: "
                + str(avatar_check.get("detail", "<missing detail>"))
            )
        if require_remote_player:
            require_check_pass(checks_by_id, "remote_player_proxy", "remote-player proxy", failures)
            require_check_pass(checks_by_id, "remote_avatar_ready", "remote VRIK avatar readiness", failures)
            if require_avatar_sync:
                require_check_pass(
                    checks_by_id,
                    "remote_higgs_avatar_ready",
                    "remote VRIK/HIGGS avatar readiness",
                    failures,
                )
        if require_weapon_pose:
            require_check_pass(checks_by_id, "weapon_pose_context", "weapon pose context", failures)
        if require_magic_pose:
            require_check_pass(checks_by_id, "magic_pose_context", "magic pose context", failures)
        if require_projectile_pose:
            require_check_pass(checks_by_id, "projectile_pose_context", "projectile pose context", failures)
        if require_movement_relay:
            require_check_pass(checks_by_id, "movement_relay", "movement relay", failures)
        if require_equipment_relay:
            require_check_pass(checks_by_id, "equipment_relay", "equipment relay", failures)
        if require_activation_relay:
            require_check_pass(checks_by_id, "activation_relay", "activation relay", failures)
        if require_magic_relay:
            require_check_pass(checks_by_id, "magic_relay", "magic relay", failures)
        if require_combat_relay:
            require_check_pass(checks_by_id, "combat_relay", "combat relay", failures)
        if require_projectile_relay:
            require_check_pass(checks_by_id, "projectile_relay", "projectile relay", failures)
        if require_grab_relay:
            require_check_pass(checks_by_id, "grab_relay", "grab relay", failures)
        if require_higgs_relay:
            require_check_pass(checks_by_id, "higgs_relay", "HIGGS relay", failures)
        if require_saveload_observer:
            require_check_pass(checks_by_id, "saveload_observer", "save/load observer", failures)

        manifest_files = manifest.get("files", [])
        if isinstance(manifest_files, list):
            for file_record in manifest_files:
                if not isinstance(file_record, dict):
                    continue
                archive_name = str(file_record.get("archiveName", ""))
                if file_record.get("exists") and archive_name and archive_name not in names:
                    failures.append(f"manifest says collected file is missing from zip: {archive_name}")
        else:
            failures.append("manifest files field is not a list")

        print(f"Evidence archive: {path}")
        print(f"Archive entries: {len(names)}")
        print(f"Manifest schema: {manifest_schema}")
        print(f"Runtime audit exit code: {runtime_audit_exit}")
        print(f"Avatar-sync audit: {manifest_avatar_sync}")
        print(f"Gameplay audit: {manifest_gameplay}")
        if package_manifest:
            print(f"Package build manifest schema: {package_manifest.get('schema')}")
            print(f"Package build manifest avatarSync: {package_manifest.get('avatarSync')}")
            print(f"Package build manifest gameplay: {package_manifest.get('gameplay')}")
        summary = checklist.get("summary", {})
        if isinstance(summary, dict):
            print(
                "Checklist summary: pass={pass_count} fail={fail_count} not_required={not_required_count}".format(
                    pass_count=summary.get(collect_runtime_evidence.CHECK_PASS, 0),
                    fail_count=summary.get(collect_runtime_evidence.CHECK_FAIL, 0),
                    not_required_count=summary.get(collect_runtime_evidence.CHECK_NOT_REQUIRED, 0),
                )
            )
        print(f"Checklist failed checks: {len(failed_checks)}")
        for check in failed_checks:
            print(f"- {check.get('id', '<unknown>')}: {check.get('detail', '')}")
        print(f"Warnings: {len(warnings)}")
        for warning in warnings:
            print(f"- {warning}")
        print(f"Failures: {len(failures)}")
        for failure in failures:
            print(f"- {failure}")

    return 1 if failures else 0


def command_self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="stvr-evidence-zip-audit-") as temp:
        root = pathlib.Path(temp)
        game = root / "SkyrimVR"
        handoff = game / "Data" / "SkyrimTogetherReborn"
        log = game / collect_runtime_evidence.DEFAULT_LOG_RELATIVE
        out_dir = root / "out"
        handoff.mkdir(parents=True)
        log.parent.mkdir(parents=True)
        log.write_text("\n".join(collect_runtime_evidence.audit_runtime_handoff.LOG_BREADCRUMBS) + "\n", encoding="utf-8")

        def write(name: str, contents: str) -> None:
            (handoff / collect_runtime_evidence.vr_handoff.READOUT_FILES[name]).write_text(contents, encoding="utf-8")

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
        build_manifest = {
            "schema": collect_runtime_evidence.BUILD_MANIFEST_SCHEMA,
            "mode": "releasedbg",
            "platform": "windows",
            "arch": "x64",
            "avatarSync": True,
            "gameplay": False,
            "packageFlavor": "avatar-sync",
            "targets": list(collect_runtime_evidence.AVATAR_SYNC_EXPECTED_MANIFEST_TARGETS),
            "copiedArtifacts": list(collect_runtime_evidence.AVATAR_SYNC_EXPECTED_RUNTIME_ARTIFACTS),
            "expectedArtifacts": list(collect_runtime_evidence.AVATAR_SYNC_EXPECTED_RUNTIME_ARTIFACTS),
            "packageRoot": str(game),
            "stagedGameFiles": True,
            "companionPanel": True,
            "generatedAtUtc": "2026-01-01T00:00:00.0000000Z",
        }
        (game / collect_runtime_evidence.BUILD_MANIFEST_NAME).write_text(
            json.dumps(build_manifest, indent=2),
            encoding="utf-8",
        )

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
        archive = collect_runtime_evidence.collect(args)
        result = audit_archive(
            archive,
            require_avatar_sync=True,
            require_gameplay=False,
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
            allow_failed_checks=False,
        )
        if result != 0:
            return result

        relaxed_result = audit_archive(
            archive,
            require_avatar_sync=False,
            require_gameplay=False,
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
            allow_failed_checks=False,
        )
        if relaxed_result != 0:
            print("Evidence zip audit self-test expected manifest-driven strict validation to pass without CLI flags.")
            return 1

        weakened_archive = out_dir / "weakened-runtime-evidence.zip"
        with zipfile.ZipFile(archive) as source_zip, zipfile.ZipFile(
            weakened_archive,
            "w",
            compression=zipfile.ZIP_DEFLATED,
        ) as weakened_zip:
            manifest = json.loads(source_zip.read("manifest.json").decode("utf-8"))
            checklist = json.loads(source_zip.read("runtime_checklist.json").decode("utf-8"))
            for check in checklist.get("checks", []):
                if isinstance(check, dict) and check.get("id") == "movement_relay":
                    check["status"] = collect_runtime_evidence.CHECK_NOT_REQUIRED
                    check["detail"] = "self-test intentionally weakened"
            checklist["summary"] = {
                collect_runtime_evidence.CHECK_PASS: sum(
                    1
                    for check in checklist.get("checks", [])
                    if isinstance(check, dict) and check.get("status") == collect_runtime_evidence.CHECK_PASS
                ),
                collect_runtime_evidence.CHECK_FAIL: sum(
                    1
                    for check in checklist.get("checks", [])
                    if isinstance(check, dict) and check.get("status") == collect_runtime_evidence.CHECK_FAIL
                ),
                collect_runtime_evidence.CHECK_NOT_REQUIRED: sum(
                    1
                    for check in checklist.get("checks", [])
                    if isinstance(check, dict) and check.get("status") == collect_runtime_evidence.CHECK_NOT_REQUIRED
                ),
            }
            manifest["runtimeChecklist"] = checklist["summary"]
            for name in source_zip.namelist():
                if name == "runtime_checklist.json":
                    weakened_zip.writestr(name, json.dumps(checklist, indent=2, sort_keys=True) + "\n")
                elif name == "manifest.json":
                    weakened_zip.writestr(name, json.dumps(manifest, indent=2, sort_keys=True) + "\n")
                else:
                    weakened_zip.writestr(name, source_zip.read(name))

        weakened_result = audit_archive(
            weakened_archive,
            require_avatar_sync=False,
            require_gameplay=False,
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
            allow_failed_checks=False,
        )
        if weakened_result == 0:
            print("Evidence zip audit self-test did not reject a manifest-required movement relay checklist downgrade.")
            return 1

        weakened_avatar_archive = out_dir / "weakened-avatar-runtime-evidence.zip"
        with zipfile.ZipFile(archive) as source_zip, zipfile.ZipFile(
            weakened_avatar_archive,
            "w",
            compression=zipfile.ZIP_DEFLATED,
        ) as weakened_zip:
            manifest = json.loads(source_zip.read("manifest.json").decode("utf-8"))
            manifest["requiredRemotePlayer"] = False
            checklist = json.loads(source_zip.read("runtime_checklist.json").decode("utf-8"))
            for check in checklist.get("checks", []):
                if isinstance(check, dict) and check.get("id") in {
                    "remote_player_proxy",
                    "remote_avatar_ready",
                    "remote_higgs_avatar_ready",
                }:
                    check["status"] = collect_runtime_evidence.CHECK_NOT_REQUIRED
                    check["detail"] = "self-test intentionally weakened avatar-sync remote-player lane"
            checklist["summary"] = {
                collect_runtime_evidence.CHECK_PASS: sum(
                    1
                    for check in checklist.get("checks", [])
                    if isinstance(check, dict) and check.get("status") == collect_runtime_evidence.CHECK_PASS
                ),
                collect_runtime_evidence.CHECK_FAIL: sum(
                    1
                    for check in checklist.get("checks", [])
                    if isinstance(check, dict) and check.get("status") == collect_runtime_evidence.CHECK_FAIL
                ),
                collect_runtime_evidence.CHECK_NOT_REQUIRED: sum(
                    1
                    for check in checklist.get("checks", [])
                    if isinstance(check, dict) and check.get("status") == collect_runtime_evidence.CHECK_NOT_REQUIRED
                ),
            }
            manifest["runtimeChecklist"] = checklist["summary"]
            for name in source_zip.namelist():
                if name == "runtime_checklist.json":
                    weakened_zip.writestr(name, json.dumps(checklist, indent=2, sort_keys=True) + "\n")
                elif name == "manifest.json":
                    weakened_zip.writestr(name, json.dumps(manifest, indent=2, sort_keys=True) + "\n")
                else:
                    weakened_zip.writestr(name, source_zip.read(name))

        weakened_avatar_result = audit_archive(
            weakened_avatar_archive,
            require_avatar_sync=False,
            require_gameplay=False,
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
            allow_failed_checks=False,
        )
        if weakened_avatar_result == 0:
            print("Evidence zip audit self-test did not reject an avatarSyncAudit archive with weakened VRIK/HIGGS remote-player lanes.")
            return 1

        print(f"Evidence zip audit self-test archive: {archive}")
        return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=pathlib.Path, nargs="?", help="runtime evidence zip to audit")
    parser.add_argument("--require-avatar-sync", action="store_true", help="require the archive to come from --avatar-sync collection")
    parser.add_argument("--require-gameplay", action="store_true", help="require the archive to come from --gameplay collection")
    parser.add_argument("--require-remote-player", action="store_true", help="require remote-player proxy and VRIK avatar readiness checklist lanes")
    parser.add_argument("--require-weapon-pose", action="store_true", help="require weapon offset pose nodes to pass")
    parser.add_argument("--require-magic-pose", action="store_true", help="require spell or magic aim/origin pose nodes to pass")
    parser.add_argument("--require-projectile-pose", action="store_true", help="require arrow/projectile pose nodes to pass")
    parser.add_argument("--require-vr-pose-context", action="store_true", help="require weapon, magic, and projectile pose context to pass")
    parser.add_argument("--require-movement-relay", action="store_true", help="require movement relay evidence to pass")
    parser.add_argument("--require-equipment-relay", action="store_true", help="require equipment relay evidence to pass")
    parser.add_argument("--require-activation-relay", action="store_true", help="require activation relay evidence to pass")
    parser.add_argument("--require-magic-relay", action="store_true", help="require magic relay evidence to pass")
    parser.add_argument("--require-combat-relay", action="store_true", help="require combat relay evidence to pass")
    parser.add_argument("--require-projectile-relay", action="store_true", help="require projectile relay evidence to pass")
    parser.add_argument("--require-grab-relay", action="store_true", help="require grab/release relay evidence to pass")
    parser.add_argument("--require-higgs-relay", action="store_true", help="require HIGGS relay evidence to pass")
    parser.add_argument("--require-saveload-observer", action="store_true", help="require save/load observer evidence to pass")
    parser.add_argument("--require-gameplay-relays", action="store_true", help="require all staged gameplay relay evidence to pass")
    parser.add_argument("--allow-failed-checks", action="store_true", help="report checklist failures without returning a failing exit code")
    parser.add_argument("--self-test", action="store_true", help="run a temp-directory evidence zip audit fixture")
    args = parser.parse_args()

    if args.require_vr_pose_context:
        args.require_weapon_pose = True
        args.require_magic_pose = True
        args.require_projectile_pose = True
    if args.require_avatar_sync:
        args.require_remote_player = True
    if args.require_gameplay:
        args.require_avatar_sync = True
        args.require_remote_player = True
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
        return command_self_test()
    if not args.archive:
        parser.error("archive is required unless --self-test is used")
    return audit_archive(
        args.archive.expanduser().resolve(),
        require_avatar_sync=args.require_avatar_sync,
        require_gameplay=args.require_gameplay,
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
        allow_failed_checks=args.allow_failed_checks,
    )


if __name__ == "__main__":
    raise SystemExit(main())
