#!/usr/bin/env python3
"""Focused transactional behavior tests for install_built_package.py."""

from __future__ import annotations

import argparse
import contextlib
import importlib.util
import io
import pathlib
import shutil
import sys
import tempfile
import unittest
from unittest import mock


INSTALLER_PATH = pathlib.Path(__file__).resolve().parents[1] / "install_built_package.py"
sys.path.insert(0, str(INSTALLER_PATH.parent))
SPEC = importlib.util.spec_from_file_location("install_built_package", INSTALLER_PATH)
assert SPEC and SPEC.loader
installer = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = installer
SPEC.loader.exec_module(installer)


class InstallBuiltPackageTransactionTests(unittest.TestCase):
    def make_package(self, root: pathlib.Path) -> pathlib.Path:
        package = root / "package"
        (package / "Data").mkdir(parents=True)
        (package / "Data" / "A.txt").write_text("new-a", encoding="utf-8")
        (package / "Data" / "B.txt").write_text("new-b", encoding="utf-8")
        return package

    def test_no_overwrite_rejects_every_conflict_before_copy(self) -> None:
        with tempfile.TemporaryDirectory() as temp_root:
            root = pathlib.Path(temp_root)
            package = self.make_package(root)
            skyrim_vr = root / "SkyrimVR"
            (skyrim_vr / "Data").mkdir(parents=True)
            (skyrim_vr / "Data" / "A.txt").write_text("old-a", encoding="utf-8")
            (skyrim_vr / "Data" / "B.txt").write_text("old-b", encoding="utf-8")

            output = io.StringIO()
            argv = [
                "install_built_package.py",
                "--package",
                str(package),
                "--skyrim-vr",
                str(skyrim_vr),
                "--install",
                "--no-overwrite",
                "--skip-audit",
            ]
            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(installer.shutil, "copy2") as copy_file,
                contextlib.redirect_stdout(output),
            ):
                self.assertEqual(installer.main(), 1)

            self.assertFalse(copy_file.called)
            self.assertIn("conflict: Data/A.txt", output.getvalue())
            self.assertIn("conflict: Data/B.txt", output.getvalue())
            self.assertEqual((skyrim_vr / "Data" / "A.txt").read_text(encoding="utf-8"), "old-a")
            self.assertEqual((skyrim_vr / "Data" / "B.txt").read_text(encoding="utf-8"), "old-b")

    def test_pre_and_post_audits_use_different_papyrus_checks(self) -> None:
        args = argparse.Namespace(
            package=pathlib.Path("package"),
            skyrim_vr=pathlib.Path("SkyrimVR"),
            avatar_sync=False,
            gameplay=False,
            package_only=False,
            require_vrik=False,
            require_higgs=False,
            require_planck=False,
        )

        pre_install = installer.build_package_audit_command(
            args, verify_installed_papyrus=False
        )
        post_install = installer.build_package_audit_command(
            args, verify_installed_papyrus=True
        )

        self.assertIn("--skip-installed-papyrus-verification", pre_install)
        self.assertNotIn("--skip-installed-papyrus-verification", post_install)

    def test_copy_failure_restores_overwrites_and_removes_new_files(self) -> None:
        with tempfile.TemporaryDirectory() as temp_root:
            root = pathlib.Path(temp_root)
            package = self.make_package(root)
            skyrim_vr = root / "SkyrimVR"
            (skyrim_vr / "Data").mkdir(parents=True)
            old_a = skyrim_vr / "Data" / "A.txt"
            old_a.write_text("old-a", encoding="utf-8")
            plan = installer.build_copy_plan(installer.package_files(package), package, skyrim_vr)

            def failing_copy(source: pathlib.Path, destination: pathlib.Path) -> object:
                if source == package / "Data" / "B.txt":
                    raise OSError("injected copy failure")
                return shutil.copy2(source, destination)

            with self.assertRaises(installer.InstallTransactionError):
                installer.apply_install_transaction(plan, [], copy_file=failing_copy)

            self.assertEqual(old_a.read_text(encoding="utf-8"), "old-a")
            self.assertFalse((skyrim_vr / "Data" / "B.txt").exists())

    def test_post_audit_failure_rolls_back_install(self) -> None:
        with tempfile.TemporaryDirectory() as temp_root:
            root = pathlib.Path(temp_root)
            package = self.make_package(root)
            skyrim_vr = root / "SkyrimVR"
            (skyrim_vr / "Data").mkdir(parents=True)
            old_a = skyrim_vr / "Data" / "A.txt"
            old_a.write_text("old-a", encoding="utf-8")
            plan = installer.build_copy_plan(installer.package_files(package), package, skyrim_vr)

            with self.assertRaises(installer.InstallTransactionError) as raised:
                installer.apply_install_transaction(
                    plan, [], post_install_audit=lambda: 9
                )

            self.assertEqual(raised.exception.return_code, 9)
            self.assertEqual(old_a.read_text(encoding="utf-8"), "old-a")
            self.assertFalse((skyrim_vr / "Data" / "B.txt").exists())

    def test_keyboard_interrupt_rolls_back_then_propagates(self) -> None:
        with tempfile.TemporaryDirectory() as temp_root:
            root = pathlib.Path(temp_root)
            package = self.make_package(root)
            skyrim_vr = root / "SkyrimVR"
            (skyrim_vr / "Data").mkdir(parents=True)
            old_a = skyrim_vr / "Data" / "A.txt"
            old_a.write_text("old-a", encoding="utf-8")
            plan = installer.build_copy_plan(installer.package_files(package), package, skyrim_vr)

            with self.assertRaises(KeyboardInterrupt):
                installer.apply_install_transaction(
                    plan, [], post_install_audit=lambda: (_ for _ in ()).throw(KeyboardInterrupt())
                )

            self.assertEqual(old_a.read_text(encoding="utf-8"), "old-a")
            self.assertFalse((skyrim_vr / "Data" / "B.txt").exists())

    def test_stale_directory_symlink_is_restored_as_a_symlink(self) -> None:
        with tempfile.TemporaryDirectory() as temp_root:
            root = pathlib.Path(temp_root)
            package = self.make_package(root)
            skyrim_vr = root / "SkyrimVR"
            (skyrim_vr / "Data").mkdir(parents=True)
            external = root / "external-scripts"
            external.mkdir()
            stale_link = skyrim_vr / "Scripts"
            stale_link.symlink_to(external, target_is_directory=True)
            plan = installer.build_copy_plan(installer.package_files(package), package, skyrim_vr)

            with self.assertRaises(installer.InstallTransactionError):
                installer.apply_install_transaction(
                    plan,
                    [(pathlib.Path("Scripts"), stale_link)],
                    post_install_audit=lambda: 1,
                )

            self.assertTrue(stale_link.is_symlink())
            self.assertEqual(stale_link.resolve(), external.resolve())


if __name__ == "__main__":
    unittest.main()
