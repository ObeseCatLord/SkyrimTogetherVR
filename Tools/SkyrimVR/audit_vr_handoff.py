#!/usr/bin/env python3
"""Audit the desktop-side SkyrimTogetherVR file handoff bridge."""

from __future__ import annotations

import argparse
import pathlib


REQUIRED_TOOL_TOKENS = (
    "SkyrimTogetherVR.command",
    "SkyrimTogetherVR.connection",
    "SkyrimTogetherVR.status",
    "SkyrimTogetherVR.pose",
    "SkyrimTogetherVR.avatar",
    "SkyrimTogetherVR.remoteplayers",
    "SkyrimTogetherVR.movement",
    "SkyrimTogetherVR.inventory",
    "SkyrimTogetherVR.playercell",
    "SkyrimTogetherVR.activation",
    "SkyrimTogetherVR.magic",
    "SkyrimTogetherVR.combat",
    "SkyrimTogetherVR.projectile",
    "SkyrimTogetherVR.grab",
    "SkyrimTogetherVR.compatibility",
    "SkyrimTogetherVR.higgs",
    "SkyrimTogetherVR.higgsnet",
    "SkyrimTogetherVR.planck",
    "SkyrimTogetherVR.saveload",
    "write_connect_command",
    "write_connection_config",
    "read_connection_config",
    "config_path",
    "write_disconnect_command",
    "read_key_value_file",
    "write_atomic",
    "render_panel_html",
    "build_readout_payload",
    "build_remote_players_payload",
    "command_serve",
    "command_config",
    "command_players",
    "panel_url",
    "serve",
    "config",
    "players",
    "--open-browser",
    "--no-open-browser",
    "open_browser",
    "threading.Timer",
    "webbrowser.open",
    "remotePlayers",
    "actorTargetsEnabled",
    "remoteEquipmentMatchCount",
    "vrikDetectedCount",
    "vrikInterfaceAvailableCount",
    "invalidVrikCount",
    "bodyPoseValidCount",
    "bodyPoseUnsafeCount",
    "trackedPlayerCount",
    "poseMatchedCount",
    "sameCellCount",
    "avatarValidationReadyCount",
    "spellOriginValidCount",
    "arrowOriginValidCount",
    "leftWeaponOffsetValidCount",
    "primaryMagicOffsetValidCount",
    "last.spellOriginValid",
    "last.arrowOriginValid",
    "lastEquipment.sequence",
    "Remote Players",
    '"proxy"',
    "remotePlayer.7.username=Remote Tester",
    "sameCell=yes",
    "avatarReady=yes",
    "build_higgs_summary",
    "build_planck_summary",
    "build_compatibility_summary",
    "planckLoaded",
    "fbtLoaded",
    "bodyPoseCapture",
    "fbtPolicy",
    "planckInterface",
    "unvalidatedSuppressed",
    "higgsBridgeSeq",
    "higgsSnapshot",
    "higgsSnapshotSeq",
    "higgsEventCount",
    "higgsRelayReady",
    "higgsRelayRemote",
    "bodyCaptureEndpointFaulted",
    "bodyCaptureAttempts",
    "bodyCaptureSuccesses",
    "bodyCaptureLastResult",
    "planckBridgeSeq",
    "HIGGS",
    "PLANCK",
    "/api/readouts",
    "/api/connect",
    "/api/disconnect",
    "self-test",
)

REQUIRED_LAUNCHER_TOKENS = (
    "#if TP_SKYRIM_VR",
    "LaunchCompanionPanel",
    "LaunchSkyrimTogetherVRCompanion.bat",
    "STVR_LAUNCH_COMPANION",
    "--companion",
    "--vr-companion",
    "--companion-only",
    "--no-companion",
    "ShellExecuteW",
    "parameters.c_str()",
    "acContext.gamePath.wstring()",
)

REQUIRED_PATH_ROUTING_TOKENS = (
    "SetEnvironmentVariableW(L\"STVR_GAME_PATH\"",
    "SetCurrentDirectoryW(gamePath.c_str())",
)

REQUIRED_COMPANION_LAUNCHER_TOKENS = (
    "GAME_PATH=%ROOT%",
    'if /I "%~1"=="--game-path"',
    'set "GAME_PATH=%~2"',
    'shift',
    '--game-path "%GAME_PATH%" serve',
    "--open-browser",
)

REQUIRED_PACKAGE_TOKENS = (
    "LaunchSkyrimTogetherVRCompanion.bat",
    "AuditSkyrimTogetherVRRuntime-Windows.bat",
    "AuditSkyrimTogetherVRAvatarSyncRuntime-Windows.bat",
    "AuditSkyrimTogetherVRGameplayRuntime-Windows.bat",
    "CollectSkyrimTogetherVREvidence-Windows.bat",
    "CollectSkyrimTogetherVRAvatarSyncEvidence-Windows.bat",
    "CollectSkyrimTogetherVRGameplayEvidence-Windows.bat",
    "AuditSkyrimTogetherVREvidence-Windows.bat",
    "AuditSkyrimTogetherVRAvatarSyncEvidence-Windows.bat",
    "AuditSkyrimTogetherVRGameplayEvidence-Windows.bat",
    "Tools\\SkyrimVR",
    "vr_handoff.py",
    "vr_paths.py",
    "audit_runtime_handoff.py",
    "collect_runtime_evidence.py",
    "audit_runtime_evidence_zip.py",
    "SkipCompanionPanel",
)

REQUIRED_RUNTIME_AUDIT_TOKENS = (
    "audit_pose_context",
    "audit_gameplay_relay_lanes",
    "weapon pose context",
    "spell/magic pose context",
    "arrow/projectile pose context",
    "movement relay",
    "equipment relay",
    "activation relay",
    "magic-effect relay",
    "combat-hit relay",
    "projectile intent relay",
    "grab/release relay",
    "HIGGS relay",
    "remote VRIK/HIGGS avatar readiness",
    "save/load observer",
    "--require-vr-pose-context",
    "--require-gameplay-relays",
    "--require-movement-relay",
    "--require-equipment-relay",
    "--require-activation-relay",
    "--require-magic-relay",
    "--require-combat-relay",
    "--require-projectile-relay",
    "--require-grab-relay",
    "--require-higgs-relay",
    "--require-saveload-observer",
)

REQUIRED_EVIDENCE_ZIP_AUDIT_TOKENS = (
    "REQUIRED_ARCHIVE_ENTRIES",
    "REQUIRED_CHECK_IDS",
    "audit_archive",
    "runtime_checklist.json",
    "runtime_checklist.txt",
    "manifest.json",
    "package/SkyrimTogetherVR_BuildManifest.json",
    "packageBuildManifest",
    "validate_build_manifest_data",
    "Package build manifest schema",
    "runtimeAuditExitCode",
    "missingRequired",
    "package_build_manifest",
    "avatar_sync_actor_targets",
    "weapon_pose_context",
    "magic_pose_context",
    "projectile_pose_context",
    "discovery_schema",
    "player_cell_status",
    "--require-avatar-sync",
    "--require-gameplay",
    "--require-remote-player",
    "--require-vr-pose-context",
    "--require-gameplay-relays",
    "--require-movement-relay",
    "--require-saveload-observer",
    "--allow-failed-checks",
    "--self-test",
)

REQUIRED_EVIDENCE_COLLECTOR_TOKENS = (
    "build_runtime_checklist",
    "format_runtime_checklist",
    "runtime_checklist.json",
    "runtime_checklist.txt",
    "runtimeChecklist",
    "BUILD_MANIFEST_NAME",
    "BUILD_MANIFEST_SCHEMA",
    "packageBuildManifest",
    "package_build_manifest",
    "validate_build_manifest_file",
    "validate_build_manifest_data",
    "package-build-manifest",
    "startup_breadcrumbs",
    "connection_status",
    "local_vrik_api",
    "weapon_pose_context",
    "magic_pose_context",
    "projectile_pose_context",
    "remote_avatar_ready",
    "remote_player_proxy",
    "require_remote_player=args.require_remote_player",
    "discovery_schema",
    "player_cell_status",
    "movement_relay",
    "equipment_relay",
    "activation_relay",
    "magic_relay",
    "combat_relay",
    "projectile_relay",
    "grab_relay",
    "higgs_bridge",
    "planck_bridge",
    "higgs_relay",
    "saveload_observer",
    "avatar_sync_actor_targets",
    "require_weapon_pose=args.require_weapon_pose",
    "require_magic_pose=args.require_magic_pose",
    "require_projectile_pose=args.require_projectile_pose",
    "require_movement_relay=args.require_movement_relay",
    "require_equipment_relay=args.require_equipment_relay",
    "require_activation_relay=args.require_activation_relay",
    "require_magic_relay=args.require_magic_relay",
    "require_combat_relay=args.require_combat_relay",
    "require_projectile_relay=args.require_projectile_relay",
    "require_grab_relay=args.require_grab_relay",
    "require_higgs_relay=args.require_higgs_relay",
    "require_saveload_observer=args.require_saveload_observer",
    "--require-vr-pose-context",
    "--gameplay",
    "--require-remote-player",
    "--require-gameplay-relays",
    "--require-movement-relay",
    "--require-saveload-observer",
)

REQUIRED_DOC_TOKENS = {
    "Docs/SkyrimVR/vr-handoff-bridge.md": (
        "Tools/SkyrimVR/vr_handoff.py",
        "Tools/SkyrimVR/audit_runtime_handoff.py",
        "Tools/SkyrimVR/collect_runtime_evidence.py",
        "Tools/SkyrimVR/audit_runtime_evidence_zip.py",
        "LaunchSkyrimTogetherVRCompanion.bat",
        "AuditSkyrimTogetherVRRuntime-Windows.bat",
        "AuditSkyrimTogetherVRAvatarSyncRuntime-Windows.bat",
        "AuditSkyrimTogetherVRGameplayRuntime-Windows.bat",
        "CollectSkyrimTogetherVREvidence-Windows.bat",
        "CollectSkyrimTogetherVRGameplayEvidence-Windows.bat",
        "AuditSkyrimTogetherVREvidence-Windows.bat",
        "AuditSkyrimTogetherVRGameplayEvidence-Windows.bat",
        "--companion",
        "--companion-only",
        "STVR_LAUNCH_COMPANION",
        "STVR_GAME_PATH",
        "selected Skyrim VR install",
        "SkyrimTogetherVR.command",
        "SkyrimTogetherVR.connection",
        "SkyrimTogetherVR.status",
        "SkyrimTogetherVR.avatar",
        "SkyrimTogetherVR.remoteplayers",
        "SkyrimTogetherVR.playercell",
        "SkyrimTogetherVR.grab",
        "SkyrimTogetherVR.compatibility",
        "SkyrimTogetherVR.higgs",
        "SkyrimTogetherVR.higgsnet",
        "SkyrimTogetherVR.planck",
        "connect",
        "config",
        "disconnect",
        "status",
        "players",
        "Remote Players",
        "proxy",
        "avatarReady",
        "SkyrimTogetherVR.remoteplayers",
        "avatar",
        "spell=yes/yes",
        "weapon=yes/no",
        "HIGGS",
        "PLANCK",
        "serve",
        "--open-browser",
        "--no-open-browser",
        "browser companion panel",
        "remembered endpoint",
        "self-test",
        "runtime handoff audit",
        "runtime evidence collector",
        "evidence zip audit",
        "SkyrimTogetherVR_BuildManifest.json",
        "package build manifest",
        "--require-vr-pose-context",
        "--require-gameplay",
        "--require-gameplay-relays",
        "--require-movement-relay",
        "runtime_checklist.json",
        "discovery schema",
        "player cell",
        "packaged runtime audit wrapper",
        "avatar-sync runtime audit wrapper",
        "packaged evidence collector wrapper",
        "packaged evidence zip audit wrapper",
    ),
    "Docs/SkyrimVR/connection-only-mode.md": (
        "Tools/SkyrimVR/vr_handoff.py",
        "Tools/SkyrimVR/collect_runtime_evidence.py",
        "Tools/SkyrimVR/audit_runtime_handoff.py",
        "desktop-side bridge",
        "local browser companion panel",
        "--companion",
        "STVR_LAUNCH_COMPANION",
        "STVR_GAME_PATH",
        "selected Skyrim VR install",
        "SkyrimTogetherVR.connection",
        "SkyrimTogetherVR.playercell",
        "SkyrimTogetherVR.planck",
        "--no-open-browser",
        "remembered endpoint",
        "avatar",
        "runtime evidence collector",
        "evidence zip audit",
        "runtime_checklist.json",
        "SkyrimTogetherVR_BuildManifest.json",
        "package build manifest",
        "--require-vr-pose-context",
        "--require-*-relay",
    ),
    "Docs/SkyrimVR/porting-status.md": (
        "Tools/SkyrimVR/vr_handoff.py",
        "Tools/SkyrimVR/collect_runtime_evidence.py",
        "runtime_checklist.json",
        "vr_handoff.py serve",
        "vr_handoff.py players",
        "audit_vr_handoff.py",
        "launcher-side bridge",
        "launcher-side companion panel link",
        "HIGGS bridge status",
        "HIGGS relay status",
        "PLANCK bridge status",
        "STVR_LAUNCH_COMPANION",
        "STVR_GAME_PATH",
        "selected Skyrim VR install",
        "SkyrimTogetherVR.connection",
        "SkyrimTogetherVR.playercell",
        "--open-browser",
        "remembered endpoint",
        "remote-player",
        "avatar",
        "runtime evidence collector",
        "evidence zip audit",
        "SkyrimTogetherVR_BuildManifest.json",
        "package build manifest",
        "runtime handoff audit",
        "--require-vr-pose-context",
        "--require-gameplay-relays",
        "--require-movement-relay",
    ),
}


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def missing_tokens(text: str, tokens: tuple[str, ...]) -> list[str]:
    return [token for token in tokens if token not in text]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=pathlib.Path, default=repo_root())
    args = parser.parse_args()

    root = args.repo.resolve()
    tool_path = root / "Tools" / "SkyrimVR" / "vr_handoff.py"
    tool_text = tool_path.read_text(encoding="utf-8", errors="replace") if tool_path.exists() else ""
    missing_tool = missing_tokens(tool_text, REQUIRED_TOOL_TOKENS)

    launcher_path = root / "Code" / "immersive_launcher" / "Launcher.cpp"
    launcher_text = launcher_path.read_text(encoding="utf-8", errors="replace") if launcher_path.exists() else ""
    missing_launcher = missing_tokens(launcher_text, REQUIRED_LAUNCHER_TOKENS)

    path_routing_path = root / "Code" / "immersive_launcher" / "loader" / "PathRerouting.cpp"
    path_routing_text = path_routing_path.read_text(encoding="utf-8", errors="replace") if path_routing_path.exists() else ""
    missing_path_routing = missing_tokens(path_routing_text, REQUIRED_PATH_ROUTING_TOKENS)

    companion_launcher_path = root / "LaunchSkyrimTogetherVRCompanion.bat"
    companion_launcher_text = companion_launcher_path.read_text(encoding="utf-8", errors="replace") if companion_launcher_path.exists() else ""
    missing_companion_launcher = missing_tokens(companion_launcher_text, REQUIRED_COMPANION_LAUNCHER_TOKENS)

    package_path = root / "BuildSkyrimTogetherVR-Windows.ps1"
    package_text = package_path.read_text(encoding="utf-8", errors="replace") if package_path.exists() else ""
    missing_package = missing_tokens(package_text, REQUIRED_PACKAGE_TOKENS)

    runtime_audit_path = root / "Tools" / "SkyrimVR" / "audit_runtime_handoff.py"
    runtime_audit_text = runtime_audit_path.read_text(encoding="utf-8", errors="replace") if runtime_audit_path.exists() else ""
    missing_runtime_audit = missing_tokens(runtime_audit_text, REQUIRED_RUNTIME_AUDIT_TOKENS)

    evidence_collector_path = root / "Tools" / "SkyrimVR" / "collect_runtime_evidence.py"
    evidence_collector_text = evidence_collector_path.read_text(encoding="utf-8", errors="replace") if evidence_collector_path.exists() else ""
    missing_evidence_collector = missing_tokens(evidence_collector_text, REQUIRED_EVIDENCE_COLLECTOR_TOKENS)

    evidence_zip_audit_path = root / "Tools" / "SkyrimVR" / "audit_runtime_evidence_zip.py"
    evidence_zip_audit_text = evidence_zip_audit_path.read_text(encoding="utf-8", errors="replace") if evidence_zip_audit_path.exists() else ""
    missing_evidence_zip_audit = missing_tokens(evidence_zip_audit_text, REQUIRED_EVIDENCE_ZIP_AUDIT_TOKENS)

    missing_docs: dict[str, list[str]] = {}
    for rel_path, tokens in REQUIRED_DOC_TOKENS.items():
        path = root / rel_path
        text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
        missing = missing_tokens(text, tokens)
        if missing:
            missing_docs[rel_path] = missing

    print(f"Missing vr_handoff.py tokens: {len(missing_tool)}")
    print(f"Missing launcher companion tokens: {len(missing_launcher)}")
    print(f"Missing launcher path-routing tokens: {len(missing_path_routing)}")
    print(f"Missing companion launcher tokens: {len(missing_companion_launcher)}")
    print(f"Missing package companion tokens: {len(missing_package)}")
    print(f"Missing runtime audit tokens: {len(missing_runtime_audit)}")
    print(f"Missing evidence collector tokens: {len(missing_evidence_collector)}")
    print(f"Missing evidence zip audit tokens: {len(missing_evidence_zip_audit)}")
    for rel_path, tokens in sorted(missing_docs.items()):
        print(f"Missing {rel_path} tokens: {len(tokens)}")
        for token in tokens:
            print(f"  {token}")

    if missing_tool:
        for token in missing_tool:
            print(f"Missing tool token: {token}")

    if missing_launcher:
        for token in missing_launcher:
            print(f"Missing launcher token: {token}")

    if missing_path_routing:
        for token in missing_path_routing:
            print(f"Missing launcher path-routing token: {token}")

    if missing_companion_launcher:
        for token in missing_companion_launcher:
            print(f"Missing companion launcher token: {token}")

    if missing_package:
        for token in missing_package:
            print(f"Missing package token: {token}")

    if missing_runtime_audit:
        for token in missing_runtime_audit:
            print(f"Missing runtime audit token: {token}")

    if missing_evidence_collector:
        for token in missing_evidence_collector:
            print(f"Missing evidence collector token: {token}")

    if missing_evidence_zip_audit:
        for token in missing_evidence_zip_audit:
            print(f"Missing evidence zip audit token: {token}")

    return 1 if (
        missing_tool
        or missing_launcher
        or missing_path_routing
        or missing_companion_launcher
        or missing_package
        or missing_runtime_audit
        or missing_evidence_collector
        or missing_evidence_zip_audit
        or missing_docs
    ) else 0


if __name__ == "__main__":
    raise SystemExit(main())
