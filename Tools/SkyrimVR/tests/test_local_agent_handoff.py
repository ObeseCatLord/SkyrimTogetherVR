#!/usr/bin/env python3
"""Focused no-install checks for the local-agent handoff installers and audit."""

from __future__ import annotations

import importlib.util
import json
import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[3]
TOOLS = ROOT / "Tools" / "SkyrimVR"
INSTALLER = TOOLS / "install_local_agent_handoff.py"
WINDOWS_INSTALLER = TOOLS / "install_local_agent_handoff_windows.ps1"
WINDOWS_WRAPPER = TOOLS / "install_local_agent_handoff_windows.bat"

sys.path.insert(0, str(TOOLS))


def load_module(name: str, path: pathlib.Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def transaction_fixture(installer, root: pathlib.Path, order: tuple[str, ...] = ("existing", "created")):
    game = root / "game"
    compatdata = root / "compatdata"
    sources = root / "sources"
    game.mkdir()
    compatdata.mkdir()
    sources.mkdir()
    targets: dict[str, pathlib.Path] = {}
    operations = []
    for name in order:
        target = game / f"{name}.txt"
        source = sources / f"{name}.txt"
        source.write_text(f"installed-{name}\n", encoding="ascii")
        source.chmod(0o600 if name.startswith("existing") else 0o644)
        if name.startswith("existing"):
            target.write_text(f"original-{name}\n", encoding="ascii")
            target.chmod(0o640)
        targets[name] = target
        operations.append(installer.operation_for_file("game", pathlib.PurePosixPath(target.name), source))
    return game, compatdata, operations, targets


class LocalAgentHandoffTests(unittest.TestCase):
    def test_linux_installer_self_test_covers_dry_run_and_rejection(self) -> None:
        result = subprocess.run(
            [sys.executable, str(INSTALLER), "--self-test"],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("installer self-test passed", result.stdout)

    def test_generator_and_audit_require_windows_root_installers(self) -> None:
        generator = (TOOLS / "create_local_agent_handoff.py").read_text(encoding="utf-8")
        audit = load_module("audit_local_agent_handoff", TOOLS / "audit_local_agent_handoff.py")

        self.assertIn('f"{root}/INSTALL-SECOND-CLIENT-WINDOWS.ps1"', generator)
        self.assertIn('f"{root}/INSTALL-SECOND-CLIENT-WINDOWS.bat"', generator)
        self.assertIn("INSTALL-SECOND-CLIENT-WINDOWS.ps1", audit.REQUIRED_PATHS)
        self.assertIn("INSTALL-SECOND-CLIENT-WINDOWS.bat", audit.REQUIRED_PATHS)
        self.assertIn(
            "source/Docs/SkyrimVR/local-agent-complete-handoff.md",
            audit.REQUIRED_PATHS,
        )

    def test_windows_installer_is_dry_run_by_default_and_fail_closed(self) -> None:
        script = WINDOWS_INSTALLER.read_text(encoding="utf-8")

        self.assertTrue(WINDOWS_WRAPPER.is_file())
        self.assertIn("[switch]$Install", script)
        self.assertIn("if ($Install)", script)
        self.assertIn("validated (dry run; no target files changed)", script)
        self.assertIn("SkyrimVR.exe is not the supported legal Skyrim VR 1.4.15 executable", script)
        self.assertIn("ZIP entry is a symbolic link or reparse target", script)
        self.assertIn("LOCAL-MANIFEST.json record does not match payload", script)
        self.assertIn("gameplay package must not replace the legal SkyrimVR.exe", script)
        self.assertIn('$parts = @(Assert-SafeRelativePath $Relative "install path")', script)
        self.assertIn("$overlayPlan = @(Get-WindowsOverlayPlan $root $game)", script)
        self.assertIn("$packagePlan = @(Get-GameplayPackagePlan", script)
        self.assertIn("$Value -is [int] -or $Value -is [long]", script)
        self.assertIn("one-segment dry-run target handling failed", script)
        self.assertIn("PowerShell JSON integer typing is unsupported", script)
        self.assertIn("dependencies/xrizer-runtime", (ROOT / "Docs/SkyrimVR/local-agent-complete-handoff.md").read_text(encoding="utf-8"))

    def test_exact_legal_executable_hash_is_shared_by_both_installers(self) -> None:
        installer = load_module("install_local_agent_handoff", INSTALLER)
        script = WINDOWS_INSTALLER.read_text(encoding="utf-8")

        self.assertEqual(
            installer.SKYRIM_VR_1_4_15_SHA256,
            "6961efb4f4775a307b0fc9a3d637542c1e090be207d3b09467eab216b7f87971",
        )
        self.assertIn(installer.SKYRIM_VR_1_4_15_SHA256, script)

    def test_linux_installer_defaults_to_no_target_mutation_and_requires_install(self) -> None:
        installer = load_module("install_local_agent_handoff_modes", INSTALLER)
        with mock.patch.object(sys, "argv", [str(INSTALLER)]):
            self.assertFalse(installer.parse_args().install)
        with mock.patch.object(sys, "argv", [str(INSTALLER), "--dry-run"]):
            self.assertFalse(installer.parse_args().install)
        with mock.patch.object(sys, "argv", [str(INSTALLER), "--install"]):
            self.assertTrue(installer.parse_args().install)
        with mock.patch.object(sys, "argv", [str(INSTALLER), "--apply"]):
            self.assertTrue(installer.parse_args().install)
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            source = root / "source"
            target = root / "target"
            source.write_text("payload", encoding="ascii")
            operation = installer.operation_for_file("game", pathlib.PurePosixPath("target"), source)
            self.assertFalse(target.exists())
            installer.write_operation(operation, target)
            self.assertEqual(target.read_text(encoding="ascii"), "payload")

    def test_linux_transaction_recovers_unpublished_state_and_partial_backups(self) -> None:
        installer = load_module("install_local_agent_handoff_prepare_recovery", INSTALLER)
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            game, compatdata, operations, targets = transaction_fixture(installer, root, ("existing-a", "existing-b"))
            with mock.patch.object(installer, "atomic_json", side_effect=RuntimeError("crash before state publish")):
                with self.assertRaises(RuntimeError):
                    installer.install_transaction(operations, game, compatdata, "handoff")
            self.assertFalse((installer.state_directory(game) / installer.STATE_FILE_NAME).exists())
            self.assertTrue(installer.install_transaction(operations, game, compatdata, "handoff"))
            installer.uninstall_transaction(game, compatdata, force=False)
            self.assertEqual(targets["existing-a"].read_text(encoding="ascii"), "original-existing-a\n")

        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            game, compatdata, operations, targets = transaction_fixture(installer, root, ("existing-a", "existing-b"))
            real_backup = installer.atomic_backup
            calls = 0

            def crash_on_second_backup(*args, **kwargs):
                nonlocal calls
                calls += 1
                if calls == 2:
                    raise RuntimeError("crash during backup preparation")
                return real_backup(*args, **kwargs)

            with mock.patch.object(installer, "atomic_backup", side_effect=crash_on_second_backup):
                with self.assertRaises(RuntimeError):
                    installer.install_transaction(operations, game, compatdata, "handoff")
            self.assertTrue(installer.install_transaction(operations, game, compatdata, "handoff"))
            installer.uninstall_transaction(game, compatdata, force=False)
            self.assertEqual(targets["existing-b"].read_text(encoding="ascii"), "original-existing-b\n")

    def test_linux_transaction_reconciles_install_and_uninstall_state_lag(self) -> None:
        installer = load_module("install_local_agent_handoff_state_lag", INSTALLER)
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            game, compatdata, operations, targets = transaction_fixture(installer, root)
            real_atomic_json = installer.atomic_json
            crashed = False

            def crash_after_install_replace(path, state):
                nonlocal crashed
                if not crashed and any(record["applied"] for record in state["records"]):
                    crashed = True
                    raise RuntimeError("crash after install replacement")
                return real_atomic_json(path, state)

            with mock.patch.object(installer, "atomic_json", side_effect=crash_after_install_replace):
                with self.assertRaises(RuntimeError):
                    installer.install_transaction(operations, game, compatdata, "handoff")
            self.assertEqual(targets["existing"].read_text(encoding="ascii"), "installed-existing\n")
            self.assertTrue(installer.install_transaction(operations, game, compatdata, "handoff"))

            for crash_name in ("existing", "created"):
                if not installer.state_directory(game).exists():
                    installer.install_transaction(operations, game, compatdata, "handoff")
                real_atomic_json = installer.atomic_json
                crashed = False

                def crash_after_uninstall_mutation(path, state, crash_name=crash_name):
                    nonlocal crashed
                    matching = next(record for record in state["records"] if record["path"] == f"{crash_name}.txt")
                    if not crashed and matching["uninstalled"]:
                        crashed = True
                        raise RuntimeError("crash after uninstall mutation")
                    return real_atomic_json(path, state)

                with mock.patch.object(installer, "atomic_json", side_effect=crash_after_uninstall_mutation):
                    with self.assertRaises(RuntimeError):
                        installer.uninstall_transaction(game, compatdata, force=False)
                installer.uninstall_transaction(game, compatdata, force=False)
                self.assertEqual(targets["existing"].read_text(encoding="ascii"), "original-existing\n")
                self.assertFalse(targets["created"].exists())

    def test_linux_uninstall_resumes_interrupted_state_cleanup(self) -> None:
        installer = load_module("install_local_agent_handoff_cleanup_recovery", INSTALLER)
        for crash_after in ("backup", "directory"):
            with self.subTest(crash_after=crash_after), tempfile.TemporaryDirectory() as temp_dir:
                root = pathlib.Path(temp_dir)
                game, compatdata, operations, targets = transaction_fixture(installer, root)
                installer.install_transaction(operations, game, compatdata, "handoff")
                real_cleanup = installer.cleanup_state

                def interrupted_cleanup(state_dir, records, *, partial_backups=False):
                    backups = state_dir / "backups"
                    for entry in list(backups.iterdir()):
                        entry.unlink()
                    if crash_after == "directory":
                        backups.rmdir()
                    raise RuntimeError("crash during state cleanup")

                with mock.patch.object(installer, "cleanup_state", side_effect=interrupted_cleanup):
                    with self.assertRaises(RuntimeError):
                        installer.uninstall_transaction(game, compatdata, force=False)
                self.assertEqual(targets["existing"].read_text(encoding="ascii"), "original-existing\n")
                self.assertFalse(targets["created"].exists())
                self.assertEqual(installer.uninstall_transaction(game, compatdata, force=False), 0)
                self.assertFalse(installer.state_directory(game).exists())

    def test_linux_install_revalidates_targets_immediately_before_replace(self) -> None:
        installer = load_module("install_local_agent_handoff_install_toctou", INSTALLER)
        for raced_name in ("existing", "created"):
            with self.subTest(raced_name=raced_name), tempfile.TemporaryDirectory() as temp_dir:
                root = pathlib.Path(temp_dir)
                game, compatdata, operations, targets = transaction_fixture(installer, root)
                real_write = installer.write_operation

                def racing_write(operation, destination, **kwargs):
                    if destination == targets[raced_name]:
                        destination.write_text("concurrent-change\n", encoding="ascii")
                        destination.chmod(0o666)
                    return real_write(operation, destination, **kwargs)

                with mock.patch.object(installer, "write_operation", side_effect=racing_write):
                    with self.assertRaisesRegex(ValueError, "changed immediately before install replacement"):
                        installer.install_transaction(operations, game, compatdata, "handoff")
                self.assertEqual(targets[raced_name].read_text(encoding="ascii"), "concurrent-change\n")

    def test_linux_uninstall_revalidates_targets_immediately_before_mutation(self) -> None:
        installer = load_module("install_local_agent_handoff_uninstall_toctou", INSTALLER)
        for raced_name in ("existing", "created"):
            with self.subTest(raced_name=raced_name), tempfile.TemporaryDirectory() as temp_dir:
                root = pathlib.Path(temp_dir)
                game, compatdata, operations, targets = transaction_fixture(installer, root)
                installer.install_transaction(operations, game, compatdata, "handoff")
                if raced_name == "existing":
                    real_write = installer.write_operation

                    def racing_restore(operation, destination, **kwargs):
                        if destination == targets["existing"]:
                            destination.write_text("concurrent-uninstall-change\n", encoding="ascii")
                            destination.chmod(0o666)
                        return real_write(operation, destination, **kwargs)

                    patcher = mock.patch.object(installer, "write_operation", side_effect=racing_restore)
                else:
                    real_remove = installer.remove_created_target

                    def racing_remove(destination, **kwargs):
                        destination.write_text("concurrent-uninstall-change\n", encoding="ascii")
                        destination.chmod(0o666)
                        return real_remove(destination, **kwargs)

                    patcher = mock.patch.object(installer, "remove_created_target", side_effect=racing_remove)
                with patcher:
                    with self.assertRaisesRegex(ValueError, "changed immediately before non-force uninstall"):
                        installer.uninstall_transaction(game, compatdata, force=False)
                self.assertEqual(targets[raced_name].read_text(encoding="ascii"), "concurrent-uninstall-change\n")

    def test_linux_idempotency_requires_modes_backups_and_clean_state(self) -> None:
        installer = load_module("install_local_agent_handoff_idempotency", INSTALLER)
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            game, compatdata, operations, targets = transaction_fixture(installer, root)
            installer.install_transaction(operations, game, compatdata, "handoff")
            targets["created"].chmod(0o600)
            with self.assertRaises(ValueError):
                installer.install_transaction(operations, game, compatdata, "handoff")
            targets["created"].chmod(0o644)

            state_dir = installer.state_directory(game)
            state = installer.load_state(state_dir)
            record = next(record for record in state["records"] if record["path"] == "existing.txt")
            backup = state_dir / "backups" / record["backup"]
            original_bytes = backup.read_bytes()
            original_mode = stat.S_IMODE(backup.stat().st_mode)
            backup.write_bytes(b"corrupt")
            with self.assertRaises(ValueError):
                installer.install_transaction(operations, game, compatdata, "handoff")
            backup.write_bytes(original_bytes)
            backup.chmod(original_mode ^ stat.S_IXUSR)
            with self.assertRaises(ValueError):
                installer.install_transaction(operations, game, compatdata, "handoff")
            backup.chmod(original_mode)
            (state_dir / "unexpected").write_text("foreign", encoding="ascii")
            with self.assertRaises(ValueError):
                installer.install_transaction(operations, game, compatdata, "handoff")

    def test_linux_installer_requires_exact_verified_handoff_record_set(self) -> None:
        installer = load_module("install_local_agent_handoff_payload", INSTALLER)

        def write_handoff(root: pathlib.Path, records: list[dict[str, object]] | None = None) -> pathlib.Path:
            root.mkdir()
            payload = root / "payload.txt"
            payload.write_text("verified", encoding="ascii")
            record = {
                "path": f"{root.name}/payload.txt",
                "size": payload.stat().st_size,
                "sha256": installer.sha256(payload),
            }
            manifest = {
                "schema": "skyrim_together_vr_local_agent_handoff_v1",
                "localOnly": True,
                "records": [record] if records is None else records,
            }
            (root / "LOCAL-MANIFEST.json").write_text(json.dumps(manifest), encoding="utf-8")
            return payload

        with tempfile.TemporaryDirectory() as temp_dir:
            base = pathlib.Path(temp_dir)
            root = base / "handoff"
            payload = write_handoff(root)
            records = installer.manifest_record_map(installer.load_manifest(root))
            installer.verify_handoff_payload(root, records)

            payload.write_text("corrupt", encoding="ascii")
            with self.assertRaises(ValueError):
                installer.verify_handoff_payload(root, records)

        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir) / "handoff"
            write_handoff(root)
            (root / "unrecorded.txt").write_text("extra", encoding="ascii")
            with self.assertRaises(ValueError):
                installer.verify_handoff_payload(root, installer.manifest_record_map(installer.load_manifest(root)))

        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir) / "handoff"
            extra_record = {"path": f"{root.name}/missing.txt", "size": 0, "sha256": "0" * 64}
            write_handoff(root, [extra_record])
            with self.assertRaises(ValueError):
                installer.verify_handoff_payload(root, installer.manifest_record_map(installer.load_manifest(root)))

    def test_linux_installer_rejects_handoff_symlinks_before_payload_use(self) -> None:
        installer = load_module("install_local_agent_handoff_links", INSTALLER)
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir) / "handoff"
            root.mkdir()
            (root / "LOCAL-MANIFEST.json").write_text(
                json.dumps({"schema": "skyrim_together_vr_local_agent_handoff_v1", "localOnly": True, "records": []}),
                encoding="utf-8",
            )
            os.symlink("LOCAL-MANIFEST.json", root / "linked-manifest.json")
            with self.assertRaises(ValueError):
                installer.verify_handoff_payload(root, installer.manifest_record_map(installer.load_manifest(root)))


if __name__ == "__main__":
    unittest.main()
