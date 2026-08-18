#!/usr/bin/env python3
"""Focused CLI and package-identity tests for runtime evidence scopes."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import pathlib
import sys
import tempfile
import time
import unittest
import zipfile
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[3]
TOOLS = ROOT / "Tools" / "SkyrimVR"
sys.path.insert(0, str(TOOLS))


def load_module(name: str, path: pathlib.Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


COLLECT = load_module("collect_runtime_evidence", TOOLS / "collect_runtime_evidence.py")
ARCHIVE_AUDIT = load_module("stvr_audit_runtime_evidence_zip", TOOLS / "audit_runtime_evidence_zip.py")


class RuntimeEvidenceScopeTests(unittest.TestCase):
    @staticmethod
    def package_manifest(*, gameplay: bool = False) -> dict[str, object]:
        return {
            "schema": COLLECT.BUILD_MANIFEST_SCHEMA,
            "mode": "releasedbg",
            "platform": "windows",
            "arch": "x64",
            "avatarSync": False,
            "gameplay": gameplay,
            "packageFlavor": "gameplay" if gameplay else "default",
            "targets": list(COLLECT.GAMEPLAY_EXPECTED_MANIFEST_TARGETS if gameplay else COLLECT.DEFAULT_EXPECTED_MANIFEST_TARGETS),
            "copiedArtifacts": list(COLLECT.GAMEPLAY_EXPECTED_RUNTIME_ARTIFACTS if gameplay else COLLECT.DEFAULT_EXPECTED_RUNTIME_ARTIFACTS),
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
        }

    def test_collector_gameplay_bootstrap_propagates_only_local_requirements(self) -> None:
        with mock.patch.object(COLLECT, "collect", return_value=pathlib.Path("fixture.zip")) as collect:
            with mock.patch.object(sys, "argv", ["collect_runtime_evidence.py", "--gameplay-bootstrap"]):
                self.assertEqual(COLLECT.main(), 0)

        args = collect.call_args.args[0]
        self.assertTrue(args.gameplay_bootstrap)
        self.assertFalse(args.gameplay)
        self.assertTrue(args.require_connected)
        self.assertTrue(args.require_vrik)
        self.assertTrue(args.require_higgs)
        self.assertFalse(args.require_remote_player)
        self.assertFalse(args.require_gameplay_relays)

    def test_collector_gameplay_remains_strict(self) -> None:
        with mock.patch.object(COLLECT, "collect", return_value=pathlib.Path("fixture.zip")) as collect:
            with mock.patch.object(sys, "argv", ["collect_runtime_evidence.py", "--gameplay"]):
                self.assertEqual(COLLECT.main(), 0)

        args = collect.call_args.args[0]
        self.assertTrue(args.require_remote_player)
        self.assertTrue(args.require_gameplay_relays)
        self.assertTrue(args.require_movement_relay)
        self.assertTrue(args.require_higgs_relay)

    def test_bootstrap_accepts_live_startup_breadcrumbs_without_shutdown(self) -> None:
        with tempfile.TemporaryDirectory(prefix="stvr-live-log-") as temp:
            log = pathlib.Path(temp) / "tp_client.log"
            breadcrumbs = [
                line
                for line in COLLECT.audit_runtime_handoff.LOG_BREADCRUMBS
                if "lifecycle shutdown hook reached" not in line
            ]
            log.write_text("\n".join(breadcrumbs), encoding="utf-8")

            self.assertFalse(COLLECT.log_breadcrumb_detail(log, skip_log=False)[0])
            self.assertTrue(COLLECT.log_breadcrumb_detail(log, skip_log=False, require_shutdown=False)[0])

    def test_local_avatar_bootstrap_requires_no_remote_avatar_rows(self) -> None:
        avatar = {
            "schema": "commonlib_bridge_v2",
            "ready": "1",
            "connected": "1",
            "bridgeReady": "1",
            "actorTargetsEnabled": "1",
            "animationGraphEnabled": "1",
            "localAnimationGraphReady": "1",
            "localSnapshotReady": "1",
            "localServerAssigned": "1",
            "actorSkeletonWritesEnabled": "0",
            "cleanupRequired": "0",
            "visualPolicy": "player_template_fallback",
            "lifecycleEpoch": "3",
            "localServerId": "7",
        }
        self.assertTrue(COLLECT.local_avatar_bootstrap_detail(avatar)[0])

    def test_collector_gameplay_identity_rejects_non_gameplay_flavor(self) -> None:
        manifest = self.package_manifest(gameplay=True)
        manifest["packageFlavor"] = "avatar-sync"
        ok, detail = COLLECT.validate_build_manifest_data(manifest, avatar_sync=False, gameplay=True)
        self.assertFalse(ok)
        self.assertIn("packageFlavor='avatar-sync' expected='gameplay'", detail)

    def test_archive_auditor_propagates_gameplay_bootstrap_scope(self) -> None:
        with mock.patch.object(ARCHIVE_AUDIT, "audit_archive", return_value=0) as audit:
            with mock.patch.object(
                sys,
                "argv",
                ["audit_runtime_evidence_zip.py", "fixture.zip", "--require-gameplay-bootstrap"],
            ):
                self.assertEqual(ARCHIVE_AUDIT.main(), 0)

        kwargs = audit.call_args.kwargs
        self.assertTrue(kwargs["require_gameplay_bootstrap"])
        self.assertFalse(kwargs["require_avatar_sync"])
        self.assertFalse(kwargs["require_gameplay"])
        self.assertFalse(kwargs["require_remote_player"])
        self.assertFalse(kwargs["require_movement_relay"])
        self.assertFalse(kwargs["require_higgs_relay"])

    def test_generic_and_crash_only_collections_are_untrusted(self) -> None:
        for crash_evidence in (False, True):
            with self.subTest(crash_evidence=crash_evidence), tempfile.TemporaryDirectory(prefix="stvr-untrusted-evidence-") as temp:
                root = pathlib.Path(temp)
                game = root / "SkyrimVR"
                handoff = game / "Data" / "SkyrimTogetherReborn"
                handoff.mkdir(parents=True)
                (game / COLLECT.BUILD_MANIFEST_NAME).write_text(
                    json.dumps(self.package_manifest()), encoding="utf-8"
                )
                args = COLLECT.build_collection_args(
                    game_path=game,
                    handoff_dir=handoff,
                    out=root / "evidence.zip",
                    crash_evidence=crash_evidence,
                    no_audit=True,
                )
                trusted_identity = {"schema": "skyrim_together_vr_runtime_identity_v1", "ok": True, "reasons": []}
                with mock.patch.object(COLLECT.vr_handoff, "evaluate_runtime_identity", return_value=trusted_identity):
                    archive = COLLECT.collect(args)
                with zipfile.ZipFile(archive) as zf:
                    manifest = json.loads(zf.read("manifest.json"))
                self.assertFalse(manifest["liveAdmissionRequested"])
                self.assertEqual(manifest["runtimeEvidenceTrust"], "untrusted")

    def test_failed_live_admission_returns_nonzero_and_preserves_archive(self) -> None:
        with tempfile.TemporaryDirectory(prefix="stvr-live-admission-cli-") as temp:
            archive = pathlib.Path(temp) / "failed.zip"
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr(
                    "manifest.json",
                    json.dumps({"liveAdmissionRequested": True, "runtimeEvidenceTrust": "untrusted"}),
                )
            with mock.patch.object(COLLECT, "collect", return_value=archive):
                with mock.patch.object(sys, "argv", ["collect_runtime_evidence.py", "--require-connected"]):
                    self.assertEqual(COLLECT.main(), 1)
            self.assertTrue(archive.is_file())

    def test_offline_identity_audit_rejects_package_network_version_mismatch(self) -> None:
        now_ns = time.time_ns()
        identity_readouts = {
            "status": (
                "online=1\nlaunchNonce=0123456789abcdef0123456789abcdef\nprocessId=42\n"
                "clientVersion=other\nserverVersion=other\ngameplayProtocolRevision=14\n"
                "serverInstanceNonce=1\nsessionId=1\nconnectionGeneration=1\ngamePath=/fixture\n"
            ),
            "lifecycle": "launchNonce=0123456789abcdef0123456789abcdef\nprocessId=42\ngamePath=/fixture\n",
            "playercell": "launchNonce=0123456789abcdef0123456789abcdef\nprocessId=42\ngamePath=/fixture\n",
            "avatar": "launchNonce=0123456789abcdef0123456789abcdef\nprocessId=42\ngamePath=/fixture\n",
        }
        with tempfile.TemporaryDirectory(prefix="stvr-offline-network-version-") as temp:
            archive = pathlib.Path(temp) / "evidence.zip"
            with zipfile.ZipFile(archive, "w") as zf:
                for name, contents in identity_readouts.items():
                    zf.writestr(f"handoff/{COLLECT.vr_handoff.READOUT_FILES[name]}", contents)
                failures: list[str] = []
                ARCHIVE_AUDIT.audit_archived_runtime_identity(
                    zf,
                    set(zf.namelist()),
                    {"gamePath": "/fixture", "files": []},
                    {
                        "maxReadoutAgeSeconds": 30,
                        "evaluatedAtNs": now_ns,
                        "readouts": {
                            name: {"mtimeNs": now_ns, "present": True}
                            for name in COLLECT.vr_handoff.RUNTIME_IDENTITY_READOUTS
                        },
                    },
                    "fixture",
                    failures,
                )
        self.assertIn("status clientVersion does not match package networkVersion", "\n".join(failures))

    def test_sparse_one_client_gameplay_bootstrap_collects_and_audits(self) -> None:
        with tempfile.TemporaryDirectory(prefix="stvr-sparse-bootstrap-") as temp:
            root = pathlib.Path(temp)
            game = root / "SkyrimVR"
            handoff = game / "Data" / "SkyrimTogetherReborn"
            out = root / "out"
            handoff.mkdir(parents=True)

            package_manifest = {
                "schema": COLLECT.BUILD_MANIFEST_SCHEMA,
                "mode": "releasedbg",
                "platform": "windows",
                "arch": "x64",
                "avatarSync": False,
                "gameplay": True,
                "packageFlavor": "gameplay",
                "targets": list(COLLECT.GAMEPLAY_EXPECTED_MANIFEST_TARGETS),
                "copiedArtifacts": list(COLLECT.GAMEPLAY_EXPECTED_RUNTIME_ARTIFACTS),
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
            }
            (game / COLLECT.BUILD_MANIFEST_NAME).write_text(json.dumps(package_manifest), encoding="utf-8")

            readouts = {
                "status": (
                    "state=online\nonline=1\nplayerId=4\nsessionId=123\nconnectionGeneration=1\n"
                    "launchNonce=0123456789abcdef0123456789abcdef\nprocessId=42\n"
                    "clientVersion=fixture\nserverVersion=fixture\ngameplayProtocolRevision=14\n"
                    "serverInstanceNonce=99\ngamePath={}\n".format(game)
                ),
                "lifecycle": (
                    "state=ready\nready=1\nepoch=3\nownerThreadId=1\nstableTickCount=4\n"
                    "playerFormId=20\nplayerBaseFormId=7\nplayerCellFormId=100\n"
                    "launchNonce=0123456789abcdef0123456789abcdef\nprocessId=42\n"
                    "gamePath={}\n".format(game)
                ),
                "pose": (
                    "localPoseAvailable=1\nlocal.hmd.valid=1\nlocal.leftHand.valid=1\n"
                    "local.rightHand.valid=1\nlocal.vrik.detected=1\nlocal.vrik.interfaceAvailable=1\n"
                ),
                "avatar": (
                    "schema=commonlib_bridge_v2\nready=1\nconnected=1\nbridgeReady=1\n"
                    "actorTargetsEnabled=1\nanimationGraphEnabled=1\nlocalAnimationGraphReady=1\n"
                    "localSnapshotReady=1\nlocalServerAssigned=1\nactorSkeletonWritesEnabled=0\n"
                    "cleanupRequired=0\nvisualPolicy=player_template_fallback\nlifecycleEpoch=3\nlocalServerId=7\n"
                    "launchNonce=0123456789abcdef0123456789abcdef\nprocessId=42\n"
                    "gamePath={}\n".format(game)
                ),
                "playercell": (
                    "ready=1\nonline=1\nlocalPlayerId=4\nsessionId=123\nconnectionGeneration=1\n"
                    "playerFormId=20\ncurrentLevel=1\ncachedLevel=1\nlastLevelSent=1\n"
                    "gridCellRequestCount=1\nexteriorCellRequestCount=1\ninteriorCellRequestCount=0\n"
                    "levelRequestCount=1\nofflineSkippedRequestCount=0\nworldSpaceTranslationFailureCount=0\n"
                    "lastGrid.valid=1\nlastGrid.cellCount=25\nlastGrid.connectionGeneration=1\n"
                    "lastGrid.center=0,0\nlastGrid.worldSpace.serverModId=1\nlastGrid.worldSpace.serverBaseId=60\n"
                    "lastGrid.playerCell.serverModId=1\nlastGrid.playerCell.serverBaseId=100\n"
                    "lastCell.valid=1\nlastCell.exterior=1\nlastCell.connectionGeneration=1\n"
                    "lastCell.currentCoords=0,0\nlastCell.cell.serverModId=1\nlastCell.cell.serverBaseId=100\n"
                    "lastCell.worldSpace.serverModId=1\nlastCell.worldSpace.serverBaseId=60\n"
                    "launchNonce=0123456789abcdef0123456789abcdef\nprocessId=42\n"
                    "gamePath={}\n".format(game)
                ),
                "higgs": "bridge.loaded=1\nbridge.sequence=4\nhiggs.detected=1\nhiggs.interfaceAvailable=1\n",
                # Present full-lane artifacts remain collectible but are not required to pass.
                "movement": "localMovementAvailable=1\nremoteMovementCount=0\n",
            }
            for name, content in readouts.items():
                (handoff / COLLECT.vr_handoff.READOUT_FILES[name]).write_text(content, encoding="utf-8")

            args = COLLECT.build_collection_args(
                game_path=game,
                handoff_dir=handoff,
                log=root / "missing-tp_client.log",
                gameplay_bridge_log=root / "missing-gameplay-bridge.log",
                out=out,
                require_connected=True,
                require_vrik=True,
                require_higgs=True,
                gameplay_bootstrap=True,
            )
            original_capture = COLLECT.vr_handoff.capture_readout_snapshots

            def capture_then_mutate(directory: pathlib.Path, names: tuple[str, ...]):
                snapshots = original_capture(directory, names)
                (handoff / COLLECT.vr_handoff.READOUT_FILES["pose"]).write_text("localPoseAvailable=0\n", encoding="utf-8")
                (handoff / COLLECT.vr_handoff.READOUT_FILES["higgs"]).write_text("bridge.loaded=0\n", encoding="utf-8")
                return snapshots

            with mock.patch.object(COLLECT.vr_handoff, "capture_readout_snapshots", side_effect=capture_then_mutate):
                archive = COLLECT.collect(args)

            with zipfile.ZipFile(archive) as zf:
                manifest = json.loads(zf.read("manifest.json"))
                checklist = json.loads(zf.read("runtime_checklist.json"))
                checks = {check["id"]: check for check in checklist["checks"]}
                files = {record["archiveName"]: record for record in manifest["files"]}
                self.assertEqual(manifest["missingRequired"], [])
                self.assertEqual(manifest["runtimeAuditExitCode"], 0)
                self.assertEqual(checklist["summary"][COLLECT.CHECK_FAIL], 0)
                for check_id in ("gameplay_bridge_log", "inventory_boundary", "planck_bridge"):
                    self.assertEqual(checks[check_id]["status"], COLLECT.CHECK_NOT_REQUIRED)
                self.assertIn(
                    f"handoff/{COLLECT.vr_handoff.READOUT_FILES['movement']}",
                    zf.namelist(),
                )
                movement_name = f"handoff/{COLLECT.vr_handoff.READOUT_FILES['movement']}"
                self.assertTrue(files[movement_name]["exists"])
                self.assertFalse(files[movement_name]["required"])
                for name in ("activation", "combat", "grab", "inventory", "magic", "projectile", "saveload", "planck"):
                    archive_name = f"handoff/{COLLECT.vr_handoff.READOUT_FILES[name]}"
                    self.assertFalse(files[archive_name]["exists"])
                    self.assertFalse(files[archive_name]["required"])
                self.assertNotIn(f"logs/{COLLECT.GAMEPLAY_BRIDGE_LOG_NAME}", zf.namelist())
                self.assertIn("localPoseAvailable=1", zf.read(f"handoff/{COLLECT.vr_handoff.READOUT_FILES['pose']}").decode("utf-8"))
                self.assertIn("bridge.loaded=1", zf.read(f"handoff/{COLLECT.vr_handoff.READOUT_FILES['higgs']}").decode("utf-8"))
                bridge_record = files[f"logs/{COLLECT.GAMEPLAY_BRIDGE_LOG_NAME}"]
                self.assertFalse(bridge_record["exists"])
                self.assertFalse(bridge_record["required"])

            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                result = ARCHIVE_AUDIT.audit_archive(
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
                    require_gameplay_bootstrap=True,
                )
            self.assertEqual(result, 0, output.getvalue())

    def test_gameplay_bootstrap_rejects_mismatched_package_flavor(self) -> None:
        with tempfile.TemporaryDirectory(prefix="stvr-runtime-evidence-scope-") as temp:
            archive = pathlib.Path(temp) / "mismatched-flavor.zip"
            package_manifest = {
                "schema": COLLECT.BUILD_MANIFEST_SCHEMA,
                "mode": "releasedbg",
                "platform": "windows",
                "arch": "x64",
                "avatarSync": False,
                "gameplay": True,
                "packageFlavor": "avatar-sync",
                "targets": list(COLLECT.GAMEPLAY_EXPECTED_MANIFEST_TARGETS),
                "copiedArtifacts": list(COLLECT.GAMEPLAY_EXPECTED_RUNTIME_ARTIFACTS),
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
            }
            check_ids = set(ARCHIVE_AUDIT.REQUIRED_CHECK_IDS)
            check_ids.update(check_id for check_id, _ in ARCHIVE_AUDIT.GAMEPLAY_BOOTSTRAP_CHECKS)
            checks = [{"id": check_id, "status": COLLECT.CHECK_PASS, "detail": "fixture"} for check_id in check_ids]
            checklist = {
                "schema": "skyrim_together_vr_runtime_checklist_v1",
                "summary": {COLLECT.CHECK_PASS: len(checks), COLLECT.CHECK_FAIL: 0, COLLECT.CHECK_NOT_REQUIRED: 0},
                "checks": checks,
            }
            manifest = {
                "schema": "skyrim_together_vr_runtime_evidence_v1",
                "runtimeAuditExitCode": 0,
                "missingRequired": [],
                "avatarSyncAudit": False,
                "gameplayAudit": False,
                "gameplayBootstrapAudit": True,
                "packageBuildManifest": package_manifest,
                "runtimeChecklist": checklist["summary"],
                "files": [],
            }
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr("manifest.json", json.dumps(manifest))
                zf.writestr(f"package/{COLLECT.BUILD_MANIFEST_NAME}", json.dumps(package_manifest))
                zf.writestr("runtime_checklist.json", json.dumps(checklist))
                zf.writestr("runtime_checklist.txt", "fixture\n")
                zf.writestr("runtime_audit.txt", "fixture\n")
                zf.writestr("logs/tp_client.log", "fixture\n")

            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                result = ARCHIVE_AUDIT.audit_archive(
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
                    require_gameplay_bootstrap=True,
                )
            self.assertEqual(result, 1)
            self.assertIn("packageFlavor='avatar-sync' expected='gameplay'", output.getvalue())

    def test_archive_rejects_individually_valid_package_manifest_tamper(self) -> None:
        with tempfile.TemporaryDirectory(prefix="stvr-package-manifest-tamper-") as temp:
            archive = pathlib.Path(temp) / "tampered-package-manifest.zip"
            package_manifest = self.package_manifest(gameplay=True)
            embedded_package_manifest = json.loads(json.dumps(package_manifest))
            embedded_package_manifest["sourceRevision"] = "2" * 40
            embedded_package_manifest["sourceProvenance"]["revision"] = "2" * 40
            self.assertTrue(COLLECT.validate_build_manifest_data(package_manifest, avatar_sync=False, gameplay=True)[0])
            self.assertTrue(
                COLLECT.validate_build_manifest_data(
                    embedded_package_manifest,
                    avatar_sync=False,
                    gameplay=True,
                )[0]
            )

            checks = [
                {"id": check_id, "status": COLLECT.CHECK_PASS, "detail": "fixture"}
                for check_id in ARCHIVE_AUDIT.REQUIRED_CHECK_IDS
            ]
            checklist = {
                "schema": "skyrim_together_vr_runtime_checklist_v1",
                "summary": {
                    COLLECT.CHECK_PASS: len(checks),
                    COLLECT.CHECK_FAIL: 0,
                    COLLECT.CHECK_NOT_REQUIRED: 0,
                },
                "checks": checks,
            }
            manifest = {
                "schema": "skyrim_together_vr_runtime_evidence_v1",
                "runtimeAuditExitCode": 0,
                "missingRequired": [],
                "avatarSyncAudit": False,
                "gameplayAudit": True,
                "gameplayBootstrapAudit": False,
                "packageBuildManifest": embedded_package_manifest,
                "runtimeChecklist": checklist["summary"],
                "runtimeIdentity": {
                    "schema": "skyrim_together_vr_runtime_identity_v1",
                    "ok": False,
                    "reasons": ["not requested"],
                },
                "runtimeEvidenceTrust": "untrusted",
                "liveAdmissionRequested": False,
                "files": [],
            }
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr("manifest.json", json.dumps(manifest))
                zf.writestr(f"package/{COLLECT.BUILD_MANIFEST_NAME}", json.dumps(package_manifest))
                zf.writestr("runtime_checklist.json", json.dumps(checklist))
                zf.writestr("runtime_checklist.txt", "fixture\n")
                zf.writestr("runtime_audit.txt", "fixture\n")
                zf.writestr("logs/tp_client.log", "fixture\n")

            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                result = ARCHIVE_AUDIT.audit_archive(
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
                    allow_failed_checks=True,
                )
            self.assertEqual(result, 1)
            self.assertIn(
                "archived package build manifest does not exactly match manifest.json packageBuildManifest",
                output.getvalue(),
            )


if __name__ == "__main__":
    unittest.main()
