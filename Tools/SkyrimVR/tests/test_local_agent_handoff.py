#!/usr/bin/env python3
"""Focused no-install checks for the local-agent handoff installers and audit."""

from __future__ import annotations

import importlib.util
import json
import os
import pathlib
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
            installer.copy_file(source, target, dry_run=True)
            self.assertFalse(target.exists())
            installer.copy_file(source, target, dry_run=False)
            self.assertEqual(target.read_text(encoding="ascii"), "payload")

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
