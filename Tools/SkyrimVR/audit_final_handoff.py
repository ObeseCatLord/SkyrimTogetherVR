#!/usr/bin/env python3
"""Audit final SkyrimTogetherVR handoff evidence bundles."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import pathlib
import tempfile
import zipfile

import audit_build_evidence_zip
import audit_built_package
import audit_runtime_evidence_zip
import collect_build_evidence
import collect_runtime_evidence


GAMEPLAY_RELAY_FLAGS = (
    "requiredMovementRelay",
    "requiredEquipmentRelay",
    "requiredActivationRelay",
    "requiredMagicRelay",
    "requiredCombatRelay",
    "requiredProjectileRelay",
    "requiredSaveloadObserver",
)


def load_zip_json(path: pathlib.Path, entry: str) -> dict[str, object]:
    with zipfile.ZipFile(path) as archive:
        return json.loads(archive.read(entry).decode("utf-8"))


def latest_zip(paths: list[pathlib.Path]) -> pathlib.Path | None:
    if not paths:
        return None
    return max(paths, key=lambda path: (path.stat().st_mtime_ns, path.name))


def latest_build_evidence(build_dir: pathlib.Path, mode: str) -> pathlib.Path | None:
    if not build_dir.exists():
        return None
    return latest_zip(sorted(build_dir.glob(f"SkyrimTogetherVR-build-evidence-{mode}-*.zip")))


def runtime_manifest_matches(manifest: dict[str, object], role: str) -> bool:
    if manifest.get("schema") != "skyrim_together_vr_runtime_evidence_v1":
        return False
    if manifest.get("liveAdmissionRequested") is not True:
        return False
    if manifest.get("runtimeEvidenceTrust") != "trusted":
        return False
    avatar_sync = bool(manifest.get("avatarSyncAudit"))
    gameplay_audit = bool(manifest.get("gameplayAudit"))
    remote_player = bool(manifest.get("requiredRemotePlayer"))
    pose_context = all(
        bool(manifest.get(key))
        for key in ("requiredWeaponPose", "requiredMagicPose", "requiredProjectilePose")
    )
    gameplay_relays = all(bool(manifest.get(key)) for key in GAMEPLAY_RELAY_FLAGS)
    connected_stack = (
        bool(manifest.get("requiredConnected"))
        and bool(manifest.get("requiredVrik"))
        and bool(manifest.get("requiredHiggs"))
    )
    if role == "default":
        return connected_stack and not avatar_sync and not gameplay_audit
    if role == "avatar-sync":
        return connected_stack and avatar_sync and not gameplay_audit and remote_player and pose_context and not gameplay_relays
    if role == "gameplay":
        return connected_stack and avatar_sync and gameplay_audit and remote_player and pose_context and gameplay_relays
    return False


def matching_runtime_evidence(runtime_dir: pathlib.Path, role: str) -> list[pathlib.Path]:
    if not runtime_dir.exists():
        return []
    matches: list[pathlib.Path] = []
    for path in sorted(runtime_dir.glob("SkyrimTogetherVR-evidence-*.zip")):
        if not zipfile.is_zipfile(path):
            continue
        try:
            manifest = load_zip_json(path, "manifest.json")
        except (KeyError, json.JSONDecodeError, UnicodeDecodeError, zipfile.BadZipFile):
            continue
        if runtime_manifest_matches(manifest, role):
            matches.append(path)
    return sorted(matches, key=lambda path: (path.stat().st_mtime_ns, path.name), reverse=True)


def latest_runtime_evidence(
    runtime_dir: pathlib.Path,
    role: str,
    *,
    exclude: pathlib.Path | None = None,
) -> pathlib.Path | None:
    excluded = exclude.expanduser().resolve() if exclude is not None else None
    for path in matching_runtime_evidence(runtime_dir, role):
        if excluded is None or not same_existing_path(path, excluded):
            return path
    return None


def same_existing_path(left: pathlib.Path | None, right: pathlib.Path | None) -> bool:
    if left is None or right is None:
        return left is right
    try:
        return left.samefile(right)
    except OSError:
        return left == right


def path_lists_match(left: tuple[pathlib.Path | None, ...], right: tuple[pathlib.Path | None, ...]) -> bool:
    return len(left) == len(right) and all(same_existing_path(a, b) for a, b in zip(left, right))


def discover_missing_evidence(args: argparse.Namespace) -> None:
    build_dir = args.build_evidence_dir.expanduser().resolve()
    runtime_dir = args.runtime_evidence_dir.expanduser().resolve()
    if args.build_default is None:
        args.build_default = latest_build_evidence(build_dir, "default")
    if args.build_avatar_sync is None:
        args.build_avatar_sync = latest_build_evidence(build_dir, "avatar-sync")
    if args.build_gameplay is None:
        args.build_gameplay = latest_build_evidence(build_dir, "gameplay")
    if args.build_dll_only is None:
        args.build_dll_only = latest_build_evidence(build_dir, "dll-only")
    if args.runtime_default is None:
        args.runtime_default = latest_runtime_evidence(runtime_dir, "default")
    if args.runtime_avatar_sync is None:
        args.runtime_avatar_sync = latest_runtime_evidence(runtime_dir, "avatar-sync")
    if args.runtime_gameplay is None:
        args.runtime_gameplay = latest_runtime_evidence(
            runtime_dir,
            "gameplay",
            exclude=getattr(args, "runtime_gameplay_peer", None),
        )
    if getattr(args, "runtime_gameplay_peer", None) is None:
        args.runtime_gameplay_peer = latest_runtime_evidence(
            runtime_dir,
            "gameplay",
            exclude=args.runtime_gameplay,
        )


def role_path(path: pathlib.Path | None, role: str, failures: list[str]) -> pathlib.Path | None:
    if path is None:
        failures.append(f"missing required evidence path: {role}")
        return None
    resolved = path.expanduser().resolve()
    if not resolved.exists():
        failures.append(f"{role} evidence does not exist: {resolved}")
        return None
    return resolved


def require_manifest_flags(
    path: pathlib.Path,
    role: str,
    expected: dict[str, bool],
    failures: list[str],
) -> None:
    try:
        manifest = load_zip_json(path, "manifest.json")
    except (KeyError, json.JSONDecodeError, UnicodeDecodeError, zipfile.BadZipFile) as exc:
        failures.append(f"{role} manifest could not be read: {exc}")
        return

    for key, expected_value in expected.items():
        actual = bool(manifest.get(key))
        if actual != expected_value:
            failures.append(f"{role} manifest {key}={actual} expected={expected_value}")
    if manifest.get("liveAdmissionRequested") is not True:
        failures.append(f"{role} manifest liveAdmissionRequested is not exactly true")
    if manifest.get("runtimeEvidenceTrust") != "trusted":
        failures.append(f"{role} manifest runtimeEvidenceTrust is not exactly 'trusted'")


def audit_build_bundle(path: pathlib.Path, role: str, mode: str, failures: list[str]) -> None:
    print(f"\n== Build Evidence: {role} ==")
    result = audit_build_evidence_zip.audit_archive(
        path,
        require_package=True,
        require_mode=mode,
        allow_command_failures=False,
    )
    if result != 0:
        failures.append(f"{role} build evidence audit failed")


def audit_runtime_bundle(
    path: pathlib.Path,
    role: str,
    failures: list[str],
    *,
    require_avatar_sync: bool,
    require_gameplay: bool,
    require_remote_player: bool,
    require_pose_context: bool,
    require_gameplay_relays: bool,
    peer_gameplay: pathlib.Path | None = None,
) -> None:
    print(f"\n== Runtime Evidence: {role} ==")
    result = audit_runtime_evidence_zip.audit_archive(
        path,
        require_avatar_sync=require_avatar_sync,
        require_gameplay=require_gameplay,
        require_remote_player=require_remote_player,
        require_weapon_pose=require_pose_context,
        require_magic_pose=require_pose_context,
        require_projectile_pose=require_pose_context,
        require_movement_relay=require_gameplay_relays,
        require_equipment_relay=require_gameplay_relays,
        require_activation_relay=require_gameplay_relays,
        require_magic_relay=require_gameplay_relays,
        require_combat_relay=require_gameplay_relays,
        require_projectile_relay=require_gameplay_relays,
        require_grab_relay=False,
        require_higgs_relay=False,
        require_saveload_observer=require_gameplay_relays,
        allow_failed_checks=False,
        peer_archive=peer_gameplay,
    )
    if result != 0:
        failures.append(f"{role} runtime evidence audit failed")

    expected_flags = {
        "requiredConnected": True,
        "requiredVrik": True,
        "requiredHiggs": True,
        "avatarSyncAudit": require_avatar_sync,
        "requiredRemotePlayer": require_remote_player,
        "requiredWeaponPose": require_pose_context,
        "requiredMagicPose": require_pose_context,
        "requiredProjectilePose": require_pose_context,
        "requiredMovementRelay": require_gameplay_relays,
        "requiredEquipmentRelay": require_gameplay_relays,
        "requiredActivationRelay": require_gameplay_relays,
        "requiredMagicRelay": require_gameplay_relays,
        "requiredCombatRelay": require_gameplay_relays,
        "requiredProjectileRelay": require_gameplay_relays,
        "requiredGrabRelay": False,
        "requiredHiggsRelay": False,
        "requiredSaveloadObserver": require_gameplay_relays,
    }
    require_manifest_flags(path, role, expected_flags, failures)


def build_manifest(avatar_sync: bool, gameplay: bool = False) -> dict[str, object]:
    return {
        "schema": collect_runtime_evidence.BUILD_MANIFEST_SCHEMA,
        "mode": "releasedbg",
        "platform": "windows",
        "arch": "x64",
        "avatarSync": avatar_sync,
        "gameplay": gameplay,
        "packageFlavor": "gameplay" if gameplay else ("avatar-sync" if avatar_sync else "default"),
        "targets": list(
            collect_runtime_evidence.expected_manifest_targets(avatar_sync=avatar_sync, gameplay=gameplay)
        ),
        "copiedArtifacts": list(
            collect_runtime_evidence.expected_runtime_artifacts(avatar_sync=avatar_sync, gameplay=gameplay)
        ),
        "expectedArtifacts": list(
            collect_runtime_evidence.expected_runtime_artifacts(avatar_sync=avatar_sync, gameplay=gameplay)
        ),
        "packageRoot": "self-test",
        "stagedGameFiles": True,
        "companionPanel": True,
        "buildVersion": "fixture",
        "networkVersion": "fixture",
        "sourceRevision": "0" * 40,
        "sourceProvenance": {
            "revision": "0" * 40,
            "sourceTreeSha256": "1" * 64,
            "dirty": False,
            "dirtyApproved": False,
        },
        "generatedAtUtc": "2026-01-01T00:00:00.0000000Z",
    }


def checklist(*, gameplay_ready: bool) -> dict[str, object]:
    checks = []
    for check_id in audit_runtime_evidence_zip.REQUIRED_CHECK_IDS:
        status = (
            collect_runtime_evidence.CHECK_NOT_REQUIRED
            if check_id == "gameplay_readiness" and not gameplay_ready
            else collect_runtime_evidence.CHECK_PASS
        )
        checks.append(
            {
                "id": check_id,
                "objective": "self-test",
                "label": check_id.replace("_", " "),
                "status": status,
                "detail": "self-test",
            }
        )
    return {
        "schema": "skyrim_together_vr_runtime_checklist_v1",
        "summary": {
            collect_runtime_evidence.CHECK_PASS: sum(
                1 for check in checks if check["status"] == collect_runtime_evidence.CHECK_PASS
            ),
            collect_runtime_evidence.CHECK_FAIL: 0,
            collect_runtime_evidence.CHECK_NOT_REQUIRED: sum(
                1 for check in checks if check["status"] == collect_runtime_evidence.CHECK_NOT_REQUIRED
            ),
        },
        "checks": checks,
    }


def write_runtime_evidence_entry(
    archive: zipfile.ZipFile,
    manifest: dict[str, object],
    archive_name: str,
    payload: bytes,
) -> None:
    archive.writestr(archive_name, payload)
    for file_record in manifest["files"]:
        if file_record["archiveName"] == archive_name:
            file_record["size"] = len(payload)
            file_record["sha256"] = hashlib.sha256(payload).hexdigest()
            return
    raise ValueError(f"runtime evidence manifest has no file record for {archive_name}")


def fixture_runtime_identity(
    game_path: str,
    network_version: str,
    *,
    launch_nonce: str = "0123456789abcdef0123456789abcdef",
    process_id: int = 42,
    player_id: int = 4,
    session_id: int = 8,
    server_instance_nonce: int = 7,
    connection_generation: int = 9,
    lifecycle_epoch: int = 3,
    gameplay_ready: bool,
) -> tuple[dict[str, object], dict[str, bytes], int]:
    """Build sealed, mutually consistent readouts for a trusted live fixture."""
    evaluated_at_ns = 1_767_225_600_000_000_000
    readout_mtime_ns = evaluated_at_ns - 1_000_000_000
    common = f"launchNonce={launch_nonce}\nprocessId={process_id}\n"
    payloads = {
        "status": (
            common
            + f"online=1\nplayerId={player_id}\nclientVersion={network_version}\nserverVersion={network_version}\n"
            + f"gameplayProtocolRevision={collect_runtime_evidence.vr_handoff.GAMEPLAY_PROTOCOL_REVISION}\n"
            + f"serverInstanceNonce={server_instance_nonce}\nsessionId={session_id}\n"
            + f"connectionGeneration={connection_generation}\ngamePath={game_path}\n"
        ).encode("utf-8"),
        "lifecycle": f"{common}epoch={lifecycle_epoch}\ngamePath={game_path}\n".encode("utf-8"),
        "playercell": f"{common}gamePath={game_path}\n".encode("utf-8"),
        "avatar": f"{common}gamePath={game_path}\n".encode("utf-8"),
        "gameplay": collect_runtime_evidence.vr_handoff.gameplay_snapshot_fixture(
            pathlib.Path(game_path),
            ready=gameplay_ready,
            launch_nonce=launch_nonce,
            process_id=process_id,
            session_id=session_id,
            server_instance_nonce=server_instance_nonce,
            connection_generation=connection_generation,
            lifecycle_epoch=lifecycle_epoch,
        ).encode("utf-8"),
    }
    identity_readouts = {
        name: collect_runtime_evidence.vr_handoff.parse_key_value_bytes(payload)
        for name, payload in payloads.items()
    }
    readout_metadata = {
        name: {"mtimeNs": readout_mtime_ns, "present": True}
        for name in collect_runtime_evidence.vr_handoff.RUNTIME_IDENTITY_READOUTS
    }
    runtime_identity = collect_runtime_evidence.vr_handoff.evaluate_runtime_identity(
        identity_readouts,
        pathlib.Path("."),
        pathlib.Path(game_path),
        max_age_seconds=30,
        now_ns=evaluated_at_ns,
        readout_metadata=readout_metadata,
        expected_network_version=network_version,
        require_gameplay_ready=gameplay_ready,
    )
    if not runtime_identity["ok"]:
        raise ValueError("self-test runtime identity fixture is invalid")
    return runtime_identity, payloads, readout_mtime_ns


def fixture_scenario_evidence(
    *,
    primary: dict[str, str],
    peer: dict[str, str],
    server_instance_nonce: str,
) -> bytes:
    """Build the one shared, engine-correlated scenario record used by both fixtures."""
    evidence = audit_runtime_evidence_zip.scenario_evidence_fixture(
        collect_runtime_evidence.vr_handoff.GAMEPLAY_MANDATORY_CANONICAL_DOMAINS,
        primary=primary,
        peer=peer,
        server_instance_nonce=server_instance_nonce,
        now=dt.datetime(2026, 1, 1, tzinfo=dt.timezone.utc),
    )
    return (json.dumps(evidence, indent=2, sort_keys=True) + "\n").encode("utf-8")


def create_runtime_evidence_zip(
    path: pathlib.Path,
    *,
    avatar_sync: bool,
    gameplay: bool = False,
    pose_context: bool,
    gameplay_relays: bool,
    live_admission: bool = True,
    launch_nonce: str = "0123456789abcdef0123456789abcdef",
    process_id: int = 42,
    player_id: int = 4,
    session_id: int = 8,
    server_instance_nonce: int = 7,
    connection_generation: int = 9,
    scenario_evidence: bytes | None = None,
) -> pathlib.Path:
    package_manifest = build_manifest(avatar_sync, gameplay=gameplay)
    avatar_runtime_checks = avatar_sync or gameplay
    runtime_checklist = checklist(gameplay_ready=gameplay)
    game_path = "self-test"
    runtime_identity, identity_payloads, identity_mtime_ns = fixture_runtime_identity(
        game_path,
        str(package_manifest["networkVersion"]),
        launch_nonce=launch_nonce,
        process_id=process_id,
        player_id=player_id,
        session_id=session_id,
        server_instance_nonce=server_instance_nonce,
        connection_generation=connection_generation,
        gameplay_ready=gameplay,
    )
    runtime_trust = "trusted" if live_admission and bool(runtime_identity["ok"]) else "untrusted"
    runtime_manifest = {
        "schema": "skyrim_together_vr_runtime_evidence_v1",
        "createdUtc": "2026-01-01T00:00:00+00:00",
        "gamePath": game_path,
        "handoffDir": "self-test",
        "clientLog": "self-test",
        "packageBuildManifestPath": collect_runtime_evidence.BUILD_MANIFEST_NAME,
        "packageBuildManifest": package_manifest,
        "avatarSyncAudit": avatar_runtime_checks,
        "gameplayAudit": gameplay,
        "requiredConnected": True,
        "requiredVrik": True,
        "requiredHiggs": True,
        "requiredRemotePlayer": avatar_runtime_checks,
        "requiredWeaponPose": pose_context,
        "requiredMagicPose": pose_context,
        "requiredProjectilePose": pose_context,
        "requiredMovementRelay": gameplay_relays,
        "requiredEquipmentRelay": gameplay_relays,
        "requiredActivationRelay": gameplay_relays,
        "requiredMagicRelay": gameplay_relays,
        "requiredCombatRelay": gameplay_relays,
        "requiredProjectileRelay": gameplay_relays,
        "requiredGrabRelay": False,
        "requiredHiggsRelay": False,
        "requiredSaveloadObserver": gameplay_relays,
        "runtimeIdentity": runtime_identity,
        "runtimeEvidenceTrust": runtime_trust,
        "liveAdmissionRequested": live_admission,
        "runtimeAuditExitCode": 0,
        "runtimeChecklist": runtime_checklist["summary"],
        "missingRequired": [],
        "files": [
            {
                "category": "package-build-manifest",
                "source": "self-test",
                "archiveName": "package/SkyrimTogetherVR_BuildManifest.json",
                "required": True,
                "exists": True,
            },
            {
                "category": "client-log",
                "source": "self-test",
                "archiveName": "logs/tp_client.log",
                "required": True,
                "exists": True,
            },
            *[
                {
                    "category": "handoff",
                    "source": "self-test",
                    "archiveName": f"handoff/{collect_runtime_evidence.vr_handoff.READOUT_FILES[name]}",
                    "required": True,
                    "exists": True,
                    "mtimeNs": identity_mtime_ns,
                }
                for name in collect_runtime_evidence.vr_handoff.RUNTIME_IDENTITY_READOUTS
            ],
        ],
    }
    if scenario_evidence is not None:
        runtime_manifest["files"].append(
            {
                "category": "explicit-scenario-evidence",
                "source": "self-test",
                "archiveName": audit_runtime_evidence_zip.SCENARIO_EVIDENCE_ARCHIVE_ENTRY,
                "required": True,
                "exists": True,
            }
        )
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        write_runtime_evidence_entry(
            archive,
            runtime_manifest,
            "package/SkyrimTogetherVR_BuildManifest.json",
            (json.dumps(package_manifest, indent=2) + "\n").encode("utf-8"),
        )
        archive.writestr("runtime_audit.txt", "Runtime handoff audit failures: 0\n")
        archive.writestr("runtime_checklist.json", json.dumps(runtime_checklist, indent=2, sort_keys=True) + "\n")
        archive.writestr("runtime_checklist.txt", "SkyrimTogetherVR runtime checklist\n")
        write_runtime_evidence_entry(archive, runtime_manifest, "logs/tp_client.log", b"self-test\n")
        for name in collect_runtime_evidence.vr_handoff.RUNTIME_IDENTITY_READOUTS:
            write_runtime_evidence_entry(
                archive,
                runtime_manifest,
                f"handoff/{collect_runtime_evidence.vr_handoff.READOUT_FILES[name]}",
                identity_payloads[name],
            )
        if scenario_evidence is not None:
            write_runtime_evidence_entry(
                archive,
                runtime_manifest,
                audit_runtime_evidence_zip.SCENARIO_EVIDENCE_ARCHIVE_ENTRY,
                scenario_evidence,
            )
        archive.writestr("manifest.json", json.dumps(runtime_manifest, indent=2, sort_keys=True) + "\n")
    return path


def copy_zip_without_entry(source: pathlib.Path, destination: pathlib.Path, omitted_entry: str) -> pathlib.Path:
    with zipfile.ZipFile(source) as src, zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as dst:
        for item in src.infolist():
            if item.filename == omitted_entry:
                continue
            dst.writestr(item, src.read(item.filename))
    return destination


def run_self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="stvr-final-handoff-") as temp:
        root = collect_build_evidence.repo_root()
        temp_root = pathlib.Path(temp)
        skyrim_vr = temp_root / "SkyrimVR"
        build_out = temp_root / "build-evidence"
        runtime_out = temp_root / "runtime-evidence"
        build_out.mkdir()
        runtime_out.mkdir()

        default_package = temp_root / "package-default"
        avatar_package = temp_root / "package-avatar"
        gameplay_package = temp_root / "package-gameplay"
        dll_package = temp_root / "package-dll"
        audit_built_package.populate_test_package(default_package)
        audit_built_package.populate_test_package(avatar_package, avatar_sync=True)
        audit_built_package.populate_test_package(gameplay_package, gameplay=True)
        audit_built_package.populate_test_package(dll_package, dll_only=True)

        build_default = collect_build_evidence.collect_evidence(root, default_package, skyrim_vr, build_out)
        build_avatar = collect_build_evidence.collect_evidence(root, avatar_package, skyrim_vr, build_out, avatar_sync=True)
        build_gameplay = collect_build_evidence.collect_evidence(root, gameplay_package, skyrim_vr, build_out, gameplay=True)
        build_dll = collect_build_evidence.collect_evidence(root, dll_package, skyrim_vr, build_out, dll_only=True)
        runtime_default = create_runtime_evidence_zip(
            runtime_out / "SkyrimTogetherVR-evidence-default.zip",
            avatar_sync=False,
            gameplay=False,
            pose_context=False,
            gameplay_relays=False,
        )
        runtime_avatar = create_runtime_evidence_zip(
            runtime_out / "SkyrimTogetherVR-evidence-avatar-sync.zip",
            avatar_sync=True,
            gameplay=False,
            pose_context=True,
            gameplay_relays=False,
        )
        scenario_evidence = fixture_scenario_evidence(
            primary={
                "playerId": "4",
                "sessionId": "8",
                "connectionGeneration": "9",
                "launchNonce": "0123456789abcdef0123456789abcdef",
                "processId": "42",
            },
            peer={
                "playerId": "5",
                "sessionId": "9",
                "connectionGeneration": "10",
                "launchNonce": "fedcba9876543210fedcba9876543210",
                "processId": "43",
            },
            server_instance_nonce="7",
        )
        runtime_gameplay = create_runtime_evidence_zip(
            runtime_out / "SkyrimTogetherVR-evidence-gameplay.zip",
            avatar_sync=False,
            gameplay=True,
            pose_context=True,
            gameplay_relays=True,
            scenario_evidence=scenario_evidence,
        )
        runtime_gameplay_peer = create_runtime_evidence_zip(
            runtime_out / "SkyrimTogetherVR-evidence-gameplay-peer.zip",
            avatar_sync=False,
            gameplay=True,
            pose_context=True,
            gameplay_relays=True,
            launch_nonce="fedcba9876543210fedcba9876543210",
            process_id=43,
            player_id=5,
            session_id=9,
            server_instance_nonce=7,
            connection_generation=10,
            scenario_evidence=scenario_evidence,
        )
        untrusted_runtime_default = create_runtime_evidence_zip(
            runtime_out / "SkyrimTogetherVR-evidence-default-generic.zip",
            avatar_sync=False,
            gameplay=False,
            pose_context=False,
            gameplay_relays=False,
            live_admission=False,
        )
        generic_runtime_result = audit_runtime_evidence_zip.audit_archive(
            untrusted_runtime_default,
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
        if generic_runtime_result != 0:
            print("Final handoff self-test failed: generic runtime fixture is not audit-valid.")
            return 1
        args = argparse.Namespace(
            build_default=None,
            build_avatar_sync=None,
            build_gameplay=None,
            build_dll_only=None,
            runtime_default=None,
            runtime_avatar_sync=None,
            runtime_gameplay=runtime_gameplay,
            runtime_gameplay_peer=None,
            build_evidence_dir=build_out,
            runtime_evidence_dir=runtime_out,
            no_auto_discover=False,
            require_gameplay_runtime=True,
        )
        discover_missing_evidence(args)
        expected_paths = (
            build_default,
            build_avatar,
            build_gameplay,
            build_dll,
            runtime_default,
            runtime_avatar,
        )
        discovered_paths = (
            args.build_default,
            args.build_avatar_sync,
            args.build_gameplay,
            args.build_dll_only,
            args.runtime_default,
            args.runtime_avatar_sync,
        )
        discovered_gameplay_paths = {args.runtime_gameplay, args.runtime_gameplay_peer}
        expected_gameplay_paths = {runtime_gameplay, runtime_gameplay_peer}
        if not path_lists_match(expected_paths, discovered_paths) or discovered_gameplay_paths != expected_gameplay_paths:
            print("Final handoff self-test discovery mismatch.")
            for expected, discovered in zip(expected_paths, discovered_paths):
                print(f"expected={expected} discovered={discovered}")
            print(f"expected gameplay evidence={expected_gameplay_paths} discovered={discovered_gameplay_paths}")
            return 1
        positive_result = run_audit(args)
        if positive_result != 0:
            return positive_result

        missing_peer_args = argparse.Namespace(
            build_default=build_default,
            build_avatar_sync=build_avatar,
            build_gameplay=build_gameplay,
            build_dll_only=build_dll,
            runtime_default=runtime_default,
            runtime_avatar_sync=runtime_avatar,
            runtime_gameplay=runtime_gameplay,
            runtime_gameplay_peer=None,
            build_evidence_dir=build_out,
            runtime_evidence_dir=runtime_out,
            no_auto_discover=True,
            require_gameplay_runtime=True,
        )
        if run_audit(missing_peer_args) == 0:
            print("Final handoff self-test failed: gameplay runtime evidence unexpectedly passed without a peer archive.")
            return 1

        untrusted_runtime_args = argparse.Namespace(
            build_default=build_default,
            build_avatar_sync=build_avatar,
            build_gameplay=build_gameplay,
            build_dll_only=build_dll,
            runtime_default=untrusted_runtime_default,
            runtime_avatar_sync=runtime_avatar,
            runtime_gameplay=runtime_gameplay,
            runtime_gameplay_peer=runtime_gameplay_peer,
            build_evidence_dir=build_out,
            runtime_evidence_dir=runtime_out,
            no_auto_discover=True,
            require_gameplay_runtime=True,
        )
        untrusted_runtime_result = run_audit(untrusted_runtime_args)
        if untrusted_runtime_result == 0:
            print("Final handoff self-test failed: untrusted generic runtime evidence unexpectedly passed.")
            return 1

        weakened_build_default = copy_zip_without_entry(
            build_default,
            build_out / "SkyrimTogetherVR-build-evidence-default-weakened-missing-source-wrapper.zip",
            "source/SetupSkyrimTogetherVRBuildEnv-Windows.bat",
        )
        weakened_args = argparse.Namespace(
            build_default=weakened_build_default,
            build_avatar_sync=build_avatar,
            build_gameplay=build_gameplay,
            build_dll_only=build_dll,
            runtime_default=runtime_default,
            runtime_avatar_sync=runtime_avatar,
            runtime_gameplay=runtime_gameplay,
            runtime_gameplay_peer=runtime_gameplay_peer,
            build_evidence_dir=build_out,
            runtime_evidence_dir=runtime_out,
            no_auto_discover=True,
            require_gameplay_runtime=True,
        )
        weakened_result = run_audit(weakened_args)
        if weakened_result == 0:
            print("Final handoff self-test failed: weakened build evidence unexpectedly passed.")
            return 1

        wrong_default_runtime_args = argparse.Namespace(
            build_default=build_default,
            build_avatar_sync=build_avatar,
            build_gameplay=build_gameplay,
            build_dll_only=build_dll,
            runtime_default=runtime_avatar,
            runtime_avatar_sync=runtime_avatar,
            runtime_gameplay=runtime_gameplay,
            runtime_gameplay_peer=runtime_gameplay_peer,
            build_evidence_dir=build_out,
            runtime_evidence_dir=runtime_out,
            no_auto_discover=True,
            require_gameplay_runtime=True,
        )
        wrong_default_runtime_result = run_audit(wrong_default_runtime_args)
        if wrong_default_runtime_result == 0:
            print("Final handoff self-test failed: avatar-sync runtime evidence unexpectedly passed as default runtime evidence.")
            return 1

        wrong_avatar_runtime_args = argparse.Namespace(
            build_default=build_default,
            build_avatar_sync=build_avatar,
            build_gameplay=build_gameplay,
            build_dll_only=build_dll,
            runtime_default=runtime_default,
            runtime_avatar_sync=runtime_gameplay,
            runtime_gameplay=runtime_gameplay,
            runtime_gameplay_peer=runtime_gameplay_peer,
            build_evidence_dir=build_out,
            runtime_evidence_dir=runtime_out,
            no_auto_discover=True,
            require_gameplay_runtime=True,
        )
        wrong_avatar_runtime_result = run_audit(wrong_avatar_runtime_args)
        if wrong_avatar_runtime_result == 0:
            print("Final handoff self-test failed: gameplay runtime evidence unexpectedly passed as avatar-sync runtime evidence.")
            return 1

        return 0


def run_audit(args: argparse.Namespace) -> int:
    failures: list[str] = []
    build_default = role_path(args.build_default, "default build", failures)
    build_avatar = role_path(args.build_avatar_sync, "avatar-sync build", failures)
    build_gameplay = role_path(args.build_gameplay, "gameplay build", failures)
    build_dll = role_path(args.build_dll_only, "DLL-only build", failures)
    runtime_default = role_path(args.runtime_default, "default runtime", failures)
    runtime_avatar = role_path(args.runtime_avatar_sync, "avatar-sync runtime", failures)
    runtime_gameplay = args.runtime_gameplay.expanduser().resolve() if args.runtime_gameplay else None
    runtime_gameplay_peer_value = getattr(args, "runtime_gameplay_peer", None)
    runtime_gameplay_peer = (
        runtime_gameplay_peer_value.expanduser().resolve()
        if runtime_gameplay_peer_value is not None
        else None
    )
    gameplay_runtime_requested = args.require_gameplay_runtime or runtime_gameplay is not None
    if gameplay_runtime_requested and runtime_gameplay is None:
        failures.append("missing required evidence path: gameplay runtime")
    if gameplay_runtime_requested and runtime_gameplay_peer is None:
        failures.append("missing required evidence path: gameplay runtime peer")
    if runtime_gameplay is not None and not runtime_gameplay.exists():
        failures.append(f"gameplay runtime evidence does not exist: {runtime_gameplay}")
    if runtime_gameplay_peer is not None and not runtime_gameplay_peer.exists():
        failures.append(f"gameplay runtime peer evidence does not exist: {runtime_gameplay_peer}")
    if runtime_gameplay is None and runtime_gameplay_peer is not None:
        failures.append("gameplay runtime peer was supplied without gameplay runtime evidence")
    elif runtime_gameplay is not None and runtime_gameplay_peer is not None and same_existing_path(
        runtime_gameplay,
        runtime_gameplay_peer,
    ):
        failures.append("gameplay runtime and peer evidence archives must be distinct")

    if build_default:
        audit_build_bundle(build_default, "default", "default", failures)
    if build_avatar:
        audit_build_bundle(build_avatar, "avatar-sync", "avatar-sync", failures)
    if build_gameplay:
        audit_build_bundle(build_gameplay, "gameplay", "gameplay", failures)
    if build_dll:
        audit_build_bundle(build_dll, "DLL-only", "dll-only", failures)
    if runtime_default:
        audit_runtime_bundle(
            runtime_default,
            "default",
            failures,
            require_avatar_sync=False,
            require_gameplay=False,
            require_remote_player=False,
            require_pose_context=False,
            require_gameplay_relays=False,
        )
    if runtime_avatar:
        audit_runtime_bundle(
            runtime_avatar,
            "avatar-sync",
            failures,
            require_avatar_sync=True,
            require_gameplay=False,
            require_remote_player=True,
            require_pose_context=True,
            require_gameplay_relays=False,
        )
    if runtime_gameplay and runtime_gameplay.exists():
        audit_runtime_bundle(
            runtime_gameplay,
            "gameplay relay",
            failures,
            require_avatar_sync=True,
            require_gameplay=True,
            require_remote_player=True,
            require_pose_context=True,
            require_gameplay_relays=True,
            peer_gameplay=runtime_gameplay_peer,
        )

    print("\n== Final Handoff Summary ==")
    print(f"Failures: {len(failures)}")
    for failure in failures:
        print(f"- {failure}")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-default", type=pathlib.Path, help="default-mode build evidence zip")
    parser.add_argument("--build-avatar-sync", type=pathlib.Path, help="avatar-sync build evidence zip")
    parser.add_argument("--build-gameplay", type=pathlib.Path, help="gameplay build evidence zip")
    parser.add_argument("--build-dll-only", type=pathlib.Path, help="DLL-only build evidence zip")
    parser.add_argument("--runtime-default", type=pathlib.Path, help="default runtime evidence zip")
    parser.add_argument("--runtime-avatar-sync", type=pathlib.Path, help="avatar-sync runtime evidence zip")
    parser.add_argument("--runtime-gameplay", type=pathlib.Path, help="strict gameplay relay runtime evidence zip")
    parser.add_argument(
        "--runtime-gameplay-peer",
        type=pathlib.Path,
        help="second distinct strict gameplay runtime evidence zip for paired proof",
    )
    parser.add_argument(
        "--build-evidence-dir",
        type=pathlib.Path,
        default=collect_build_evidence.DEFAULT_OUT_DIR,
        help="directory used to discover latest build evidence zips for omitted --build-* paths",
    )
    parser.add_argument(
        "--runtime-evidence-dir",
        type=pathlib.Path,
        default=collect_runtime_evidence.default_output_dir(),
        help="directory used to discover latest runtime evidence zips for omitted --runtime-* paths",
    )
    parser.add_argument(
        "--no-auto-discover",
        action="store_true",
        help="require every evidence zip path to be supplied explicitly",
    )
    parser.add_argument(
        "--require-gameplay-runtime",
        action="store_true",
        help="fail unless both --runtime-gameplay and --runtime-gameplay-peer are supplied or discovered",
    )
    parser.add_argument("--self-test", action="store_true", help="run temporary final-handoff evidence fixtures")
    args = parser.parse_args()

    if args.self_test:
        return run_self_test()
    if not args.no_auto_discover:
        discover_missing_evidence(args)
    return run_audit(args)


if __name__ == "__main__":
    raise SystemExit(main())
