#!/usr/bin/env python3
"""Focused no-install checks for the local-agent handoff installers and audit."""

from __future__ import annotations

import importlib.util
import hashlib
import json
import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import unittest
import zipfile
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
    def test_xrizer_provenance_constants_match_reviewed_artifacts(self) -> None:
        creator = load_module("create_local_agent_handoff_xrizer_constants", TOOLS / "create_local_agent_handoff.py")
        audit = load_module("audit_local_agent_handoff_xrizer_constants", TOOLS / "audit_local_agent_handoff.py")
        reviewed = {
            "XRIZER_BASE_REVISION": "31319560c1bd0f1e5c16936a946bb1c7295dbfd9",
            "XRIZER_RUNTIME_SHA256": "b278c4695f15bba7c554aaac5303520247cc8ab3bcae3f8b55e934e2b114ccaf",
            "XRIZER_COMPATIBILITY_PATCH_SHA256": "64d837980afd29cc3d557f4326eee34a165f0bb49888c247fc2af36361990142",
        }
        for name, expected in reviewed.items():
            self.assertEqual(getattr(creator, name), expected)
            self.assertEqual(getattr(audit, name), expected)

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
        runtime_builder = (TOOLS / "build_portable_openvr_runtimes.sh").read_text(encoding="utf-8")
        audit = load_module("audit_local_agent_handoff", TOOLS / "audit_local_agent_handoff.py")

        self.assertIn('f"{root}/INSTALL-SECOND-CLIENT-WINDOWS.ps1"', generator)
        self.assertIn('f"{root}/INSTALL-SECOND-CLIENT-WINDOWS.bat"', generator)
        self.assertIn("INSTALL-SECOND-CLIENT-WINDOWS.ps1", audit.REQUIRED_PATHS)
        self.assertIn("INSTALL-SECOND-CLIENT-WINDOWS.bat", audit.REQUIRED_PATHS)
        self.assertIn(
            "source/Docs/SkyrimVR/local-agent-complete-handoff.md",
            audit.REQUIRED_PATHS,
        )
        self.assertIn("dependencies/xrizer-runtime/libxrizer.so", audit.REQUIRED_PATHS)
        self.assertIn("dependencies/xrizer-runtime/bin/linux64/vrclient.so", audit.REQUIRED_PATHS)
        self.assertIn("dependencies/opencomposite-runtime/bin/linux64/vrclient.so", audit.REQUIRED_PATHS)
        self.assertIn("dependencies/openvrpaths.vrpath", audit.REQUIRED_PATHS)
        self.assertIn("dependencies/source-references/OpenComposite/LICENSE.txt", audit.REQUIRED_PATHS)
        self.assertIn("source/Tools/SkyrimVR/build_portable_openvr_runtimes.sh", audit.REQUIRED_PATHS)
        self.assertIn("source/Tools/SkyrimVR/finalize_local_agent_handoff.sh", audit.REQUIRED_PATHS)
        self.assertIn("docker run --rm --interactive", runtime_builder.replace("\n", " "))
        self.assertIn("source/Tools/SkyrimVR/opencomposite-bullseye.patch", audit.REQUIRED_PATHS)
        self.assertIn("dependencies/source-references/OpenComposite/", audit.REQUIRED_PREFIXES)
        self.assertIn("--opencomposite-root", generator)
        self.assertIn("openCompositeRuntime", generator)
        self.assertNotIn("machinePaths", generator)
        readme = (ROOT / "Docs/SkyrimVR/local-agent-complete-handoff.md").read_text(encoding="utf-8")
        self.assertIn("## Quick Start", readme)
        self.assertIn("STVR_OPENVR_RUNTIME=opencomposite", readme)
        self.assertIn("$env:STVR_AUTOCONNECT", readme)

    def test_handoff_finalizer_seals_and_verifies_local_and_remote_archives(self) -> None:
        finalizer = (TOOLS / "finalize_local_agent_handoff.sh").read_text(encoding="utf-8")
        build_helper = (TOOLS / "build_winboat_gameplay.sh").read_text(encoding="utf-8")
        result = subprocess.run(
            [str(TOOLS / "finalize_local_agent_handoff.sh"), "--self-test-pins"],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("XRIZER_RUNTIME_SHA256=", result.stdout)
        self.assertIn("OPENCOMPOSITE_RUNTIME_SHA256=", result.stdout)
        self.assertIn("build_portable_openvr_runtimes.sh", finalizer)
        self.assertIn("validate_runtime_dir", finalizer)
        self.assertIn("audit_local_agent_handoff.py", finalizer)
        self.assertIn("unzip -tq", finalizer)
        self.assertIn('cd -- "$(dirname -- "$output")"', finalizer)
        self.assertIn("rsync -ah --partial", finalizer)
        self.assertIn("sha256sum -c '${remote_name}.sha256.txt'", finalizer)
        self.assertIn("finalize_local_agent_handoff.sh", build_helper)
        self.assertIn("STVR_HANDOFF_UPLOAD_TARGET", build_helper)

    def test_auditor_requires_matching_executable_xrizer_runtime_pair(self) -> None:
        audit = load_module("audit_local_agent_handoff_pair", TOOLS / "audit_local_agent_handoff.py")
        with tempfile.TemporaryDirectory() as temp_dir:
            archive_path = pathlib.Path(temp_dir) / "handoff.zip"
            root = "handoff"
            root_name = f"{root}/{audit.XRIZER_ROOT_RUNTIME_PATH}"
            loader_name = f"{root}/{audit.XRIZER_RUNTIME_PATH}"

            def write_entry(archive: zipfile.ZipFile, name: str, payload: bytes, mode: int = 0o100755) -> None:
                entry = zipfile.ZipInfo(name)
                entry.create_system = 3
                entry.external_attr = mode << 16
                archive.writestr(entry, payload)

            with zipfile.ZipFile(archive_path, "w") as archive:
                write_entry(archive, root_name, b"xrizer")
                write_entry(archive, loader_name, b"xrizer")
            with zipfile.ZipFile(archive_path) as archive:
                records = {root_name: {"sha256": audit.XRIZER_RUNTIME_SHA256}}
                self.assertFalse(audit.xrizer_runtime_pair_failures(archive, root, archive.namelist(), records))

            with zipfile.ZipFile(archive_path, "w") as archive:
                write_entry(archive, root_name, b"xrizer", 0o100644)
                write_entry(archive, loader_name, b"different")
            with zipfile.ZipFile(archive_path) as archive:
                failures = audit.xrizer_runtime_pair_failures(archive, root, archive.namelist(), {})
                self.assertTrue(any("hash" in failure for failure in failures))
                self.assertTrue(any("regular executable" in failure for failure in failures))
                self.assertTrue(any("differ" in failure for failure in failures))

    def test_generator_accepts_only_reviewed_opencomposite_loader(self) -> None:
        creator = load_module("create_local_agent_handoff_opencomposite", TOOLS / "create_local_agent_handoff.py")
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            runtime = root / "build/bin/linux64/vrclient.so"
            runtime.parent.mkdir(parents=True)
            runtime.write_bytes(b"\x7fELF\x02\x01\x01" + b"\0" * 9 + (3).to_bytes(2, "little") + (62).to_bytes(2, "little"))

            def validate(*, origin="https://github.com/znixian/OpenOVR.git", status="", revision=None, digest=None):
                def fake_git(repo, *args):
                    if args == ("rev-parse", "--show-toplevel"):
                        return f"{root}\n"
                    if args == ("remote", "get-url", "origin"):
                        return f"{origin}\n"
                    if args == ("status", "--porcelain=v1", "--untracked-files=all"):
                        return status
                    if args == ("rev-parse", "HEAD"):
                        return f"{revision or creator.OPENCOMPOSITE_REVISION}\n"
                    raise AssertionError(args)

                with mock.patch.object(creator, "run_git", side_effect=fake_git), mock.patch.object(
                    creator, "sha256", return_value=digest or creator.OPENCOMPOSITE_RUNTIME_SHA256
                ), mock.patch.object(
                    creator,
                    "validate_portable_loader",
                    return_value={
                        "elfClass": "ELF64",
                        "elfMachine": "x86_64",
                        "elfType": "ET_DYN",
                        "maxGlibc": "GLIBC_2.31",
                        "neededLibraries": ["libc.so.6"],
                    },
                ):
                    return creator.validated_opencomposite_runtime(root)

            discovered, provenance = validate()
            self.assertEqual(discovered, runtime)
            self.assertEqual(provenance["sourceRevision"], creator.OPENCOMPOSITE_REVISION)
            self.assertEqual(provenance["sha256"], creator.OPENCOMPOSITE_RUNTIME_SHA256)
            self.assertEqual(provenance["buildPatchStatus"], creator.OPENCOMPOSITE_BUILD_PATCH_STATUS)
            self.assertEqual(provenance["buildPatchSha256"], creator.OPENCOMPOSITE_BUILD_PATCH_SHA256)
            self.assertTrue(provenance["checkoutTopLevel"])
            self.assertTrue(provenance["checkoutClean"])
            with self.assertRaisesRegex(ValueError, "official"):
                validate(origin="https://example.invalid/fork/OpenOVR.git")
            with self.assertRaisesRegex(ValueError, "clean"):
                validate(status=" M local-change\n")
            with self.assertRaisesRegex(ValueError, "pinned"):
                validate(revision="a" * 40)
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                validate(digest="0" * 64)

    def test_generator_accepts_only_reviewed_xrizer_compatibility_build(self) -> None:
        creator = load_module("create_local_agent_handoff_xrizer", TOOLS / "create_local_agent_handoff.py")
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            runtime = root / creator.XRIZER_RUNTIME_PATH
            runtime.parent.mkdir(parents=True)
            runtime.write_bytes(b"placeholder")

            def validate(
                *, origin="https://github.com/Supreeeme/xrizer", revision=None, patch=None, expected_patch=None, digest=None
            ):
                patch_text = patch if patch is not None else "reviewed patch\n"

                def fake_git(repo, *args):
                    responses = {
                        ("rev-parse", "--show-toplevel"): f"{root}\n",
                        ("remote", "get-url", "origin"): f"{origin}\n",
                        ("rev-parse", "HEAD"): f"{revision or creator.XRIZER_BASE_REVISION}\n",
                        ("diff", "--cached", "--binary", "HEAD"): "",
                        ("ls-files", "--others", "--exclude-standard"): "",
                        ("diff", "--binary", "--no-ext-diff", "--full-index", "HEAD"): patch_text,
                    }
                    return responses[args]

                reviewed_patch = hashlib.sha256((expected_patch or patch_text).encode("utf-8")).hexdigest()
                with mock.patch.object(creator, "run_git", side_effect=fake_git), mock.patch.object(
                    creator, "sha256", return_value=digest or creator.XRIZER_RUNTIME_SHA256
                ), mock.patch.object(
                    creator, "XRIZER_COMPATIBILITY_PATCH_SHA256", reviewed_patch
                ), mock.patch.object(
                    creator,
                    "validate_portable_loader",
                    return_value={
                        "elfClass": "ELF64",
                        "elfMachine": "x86_64",
                        "elfType": "ET_DYN",
                        "maxGlibc": "GLIBC_2.31",
                        "neededLibraries": ["libc.so.6"],
                    },
                ):
                    return creator.validated_xrizer_runtime(root)

            _, provenance = validate()
            self.assertEqual(provenance["baseRevision"], creator.XRIZER_BASE_REVISION)
            self.assertEqual(provenance["compatibilityPatchStatus"], creator.XRIZER_COMPATIBILITY_PATCH_STATUS)
            with self.assertRaisesRegex(ValueError, "official"):
                validate(origin="https://example.invalid/fork/xrizer.git")
            with self.assertRaisesRegex(ValueError, "pinned"):
                validate(revision="a" * 40)
            with self.assertRaisesRegex(ValueError, "patch SHA-256"):
                validate(patch="other patch\n", expected_patch="reviewed patch\n")
            with self.assertRaisesRegex(ValueError, "runtime SHA-256"):
                validate(digest="0" * 64)

    def test_elf_loader_parser_rejects_nonportable_or_malformed_loaders(self) -> None:
        creator = load_module("create_local_agent_handoff_elf", TOOLS / "create_local_agent_handoff.py")
        audit = load_module("audit_local_agent_handoff_elf", TOOLS / "audit_local_agent_handoff.py")
        self.assertEqual(creator.PORTABLE_GLIBC_MAX, audit.PORTABLE_GLIBC_MAX)
        self.assertEqual(creator.PORTABLE_NEEDED_LIBRARIES, audit.PORTABLE_NEEDED_LIBRARIES)
        et_exec = bytearray(64)
        et_exec[:7] = b"\x7fELF\x02\x01\x01"
        et_exec[16:18] = (2).to_bytes(2, "little")
        et_exec[18:20] = (62).to_bytes(2, "little")
        with self.assertRaisesRegex(ValueError, "ET_DYN"):
            creator.analyze_elf_loader(bytes(et_exec))
        with self.assertRaisesRegex(ValueError, "truncated ELF header"):
            audit.analyze_elf_loader(b"\x7fELF")

        above_ceiling = {
            "elfClass": "ELF64",
            "elfMachine": "x86_64",
            "elfType": "ET_DYN",
            "maxGlibc": "GLIBC_2.32",
            "neededLibraries": ["libc.so.6"],
        }
        with mock.patch.object(creator, "analyze_elf_loader", return_value=above_ceiling), tempfile.NamedTemporaryFile() as loader:
            with self.assertRaisesRegex(ValueError, "portable glibc ceiling GLIBC_2.31"):
                creator.validate_portable_loader(pathlib.Path(loader.name))
        self.assertIn("portable glibc ceiling GLIBC_2.31", audit.portable_loader_failure(above_ceiling))

        bad_dependency = {**above_ceiling, "maxGlibc": "GLIBC_2.31", "neededLibraries": ["libevil.so.1"]}
        with mock.patch.object(creator, "analyze_elf_loader", return_value=bad_dependency), tempfile.NamedTemporaryFile() as loader:
            with self.assertRaisesRegex(ValueError, "non-portable DT_NEEDED dependency"):
                creator.validate_portable_loader(pathlib.Path(loader.name))
        self.assertIn("libevil.so.1", audit.portable_loader_failure(bad_dependency))

    def test_auditor_rejects_forged_xrizer_provenance_metadata(self) -> None:
        audit = load_module("audit_local_agent_handoff_xrizer", TOOLS / "audit_local_agent_handoff.py")
        expected = {
            "path": audit.XRIZER_RUNTIME_PATH,
            "sha256": audit.XRIZER_RUNTIME_SHA256,
            "baseRevision": audit.XRIZER_BASE_REVISION,
            "sourceRevision": audit.XRIZER_BASE_REVISION,
            "compatibilityPatchStatus": audit.XRIZER_COMPATIBILITY_PATCH_STATUS,
            "compatibilityPatchSha256": audit.XRIZER_COMPATIBILITY_PATCH_SHA256,
            "elfClass": "ELF64",
            "elfMachine": "x86_64",
            "elfType": "ET_DYN",
            "maxGlibc": "GLIBC_2.31",
            "neededLibraries": ["libc.so.6"],
        }
        forged = {
            **expected,
            "sourceRevision": "a" * 40,
            "compatibilityPatchStatus": "missing",
            "compatibilityPatchSha256": "0" * 64,
        }
        identity = {key: expected[key] for key in ("elfClass", "elfMachine", "elfType", "maxGlibc", "neededLibraries")}
        failures = audit.runtime_provenance_failures(
            "XRizer", forged, expected, audit.XRIZER_OFFICIAL_ORIGINS, identity
        )
        self.assertTrue(any("not the reviewed" in failure for failure in failures))
        forged_identity = {**identity, "maxGlibc": "GLIBC_2.32"}
        self.assertTrue(any("does not match payload" in failure for failure in audit.runtime_provenance_failures(
            "XRizer", expected, expected, audit.XRIZER_OFFICIAL_ORIGINS, forged_identity
        )))

    def test_creator_records_neutral_openvr_registry_bytes(self) -> None:
        creator = load_module("create_local_agent_handoff_registry", TOOLS / "create_local_agent_handoff.py")
        with tempfile.TemporaryDirectory() as temp_dir:
            archive_path = pathlib.Path(temp_dir) / "handoff.zip"
            with zipfile.ZipFile(archive_path, "w") as archive:
                writer = creator.Writer(archive, (2026, 1, 1, 0, 0, 0))
                writer.add_bytes(b'{"runtime": [], "version": 1}\n', "handoff/dependencies/openvrpaths.vrpath")
                self.assertEqual(
                    writer.records,
                    [{
                        "path": "handoff/dependencies/openvrpaths.vrpath",
                        "size": 30,
                        "sha256": "93949a53f3b9217ef8c4d4a16944d3d87d0ff8652c0dda9cde6f0fcee9fee3cb",
                    }],
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
        self.assertIn('$relative -ieq "SkyrimVR.exe" -or $relative -ieq "openvr_api.dll"', script)
        self.assertIn('$relative -ieq "openvr_api.dll"', script)
        self.assertIn("$openVrApiCount", script)
        self.assertIn("exactly one openvr_api.dll to preserve", script)
        self.assertIn("OPENVR_API.DLL", script)
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

    def test_linux_overlay_preserves_existing_opencomposite_dll(self) -> None:
        installer = load_module("install_local_agent_handoff_openvr_api", INSTALLER)
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            overlay = root / "handoff/dependencies/current-game-overlay"
            (overlay / "Data").mkdir(parents=True)
            (overlay / "SkyrimVR.exe").write_bytes(b"handoff-game")
            (overlay / "openvr_api.dll").write_bytes(b"stock-valve-openvr")
            (overlay / "Data/portable.txt").write_bytes(b"portable")
            operations, skipped = installer.collect_overlay(root / "handoff")
            self.assertEqual(skipped, (1, 1))
            self.assertNotIn("openvr_api.dll", {operation.relative.as_posix() for operation in operations})

            game = root / "game"
            compatdata = root / "compatdata"
            game.mkdir()
            compatdata.mkdir()
            user_opencomposite = game / "openvr_api.dll"
            original = b"user-opencomposite-runtime"
            user_opencomposite.write_bytes(original)
            user_opencomposite.chmod(0o640)
            self.assertTrue(installer.install_transaction(operations, game, compatdata, "handoff"))
            self.assertEqual(user_opencomposite.read_bytes(), original)
            self.assertEqual(stat.S_IMODE(user_opencomposite.stat().st_mode), 0o640)
            self.assertEqual(installer.uninstall_transaction(game, compatdata, force=False), 1)
            self.assertEqual(user_opencomposite.read_bytes(), original)
            self.assertEqual(stat.S_IMODE(user_opencomposite.stat().st_mode), 0o640)

            package = root / "malicious.zip"
            with installer.zipfile.ZipFile(package, "w") as archive:
                archive.writestr("package/openvr_api.dll", "malicious")
            with self.assertRaisesRegex(ValueError, "openvr_api.dll"):
                installer.collect_gameplay_package(package)

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

    def test_linux_runtime_operations_install_rollback_and_uninstall(self) -> None:
        installer = load_module("install_local_agent_handoff_runtimes", INSTALLER)
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            handoff = root / "handoff"
            xrizer_root = handoff / "dependencies/xrizer-runtime/libxrizer.so"
            xrizer = handoff / "dependencies/xrizer-runtime/bin/linux64/vrclient.so"
            opencomposite = handoff / "dependencies/opencomposite-runtime/bin/linux64/vrclient.so"
            pathreg = handoff / "dependencies/openvrpaths.vrpath"
            xrizer.parent.mkdir(parents=True)
            xrizer_root.parent.mkdir(parents=True, exist_ok=True)
            xrizer_root.write_bytes(b"new-xrizer")
            opencomposite.parent.mkdir(parents=True)
            xrizer.write_bytes(b"new-xrizer")
            opencomposite.write_bytes(b"new-opencomposite")
            pathreg.write_text('{"runtime": [], "version": 1}\n', encoding="utf-8")
            xrizer_root.chmod(0o755)
            xrizer.chmod(0o755)
            opencomposite.chmod(0o755)
            game = root / "game"
            compatdata = root / "compatdata"
            game.mkdir()
            compatdata.mkdir()
            installed_xrizer_root = game / ".stvr-openvr/xrizer/libxrizer.so"
            installed_xrizer = game / ".stvr-openvr/xrizer/bin/linux64/vrclient.so"
            installed_xrizer.parent.mkdir(parents=True)
            installed_xrizer.write_bytes(b"old-xrizer")
            installed_xrizer.chmod(0o640)

            operations = installer.collect_openvr_runtimes(handoff)
            self.assertEqual([operation.relative.as_posix() for operation in operations], [
                ".stvr-openvr/xrizer/libxrizer.so",
                ".stvr-openvr/xrizer/bin/linux64/vrclient.so",
                ".stvr-openvr/opencomposite/bin/linux64/vrclient.so",
                ".stvr-openvr/openvrpaths.vrpath",
            ])
            self.assertTrue(installer.install_transaction(operations, game, compatdata, "handoff"))
            self.assertEqual(installed_xrizer_root.read_bytes(), b"new-xrizer")
            self.assertEqual(stat.S_IMODE(installed_xrizer_root.stat().st_mode), 0o755)
            self.assertEqual(installed_xrizer.read_bytes(), b"new-xrizer")
            self.assertEqual(stat.S_IMODE(installed_xrizer.stat().st_mode), 0o755)
            installed_opencomposite = game / ".stvr-openvr/opencomposite/bin/linux64/vrclient.so"
            self.assertEqual(installed_opencomposite.read_bytes(), b"new-opencomposite")
            self.assertEqual(stat.S_IMODE(installed_opencomposite.stat().st_mode), 0o755)
            self.assertEqual(
                (game / ".stvr-openvr/openvrpaths.vrpath").read_text(encoding="utf-8"),
                '{"runtime": [], "version": 1}\n',
            )
            self.assertEqual(
                stat.S_IMODE((game / ".stvr-openvr/openvrpaths.vrpath").stat().st_mode),
                0o644,
            )
            self.assertFalse(installer.install_transaction(operations, game, compatdata, "handoff"))
            self.assertEqual(installer.uninstall_transaction(game, compatdata, force=False), 4)
            self.assertFalse(installed_xrizer_root.exists())
            self.assertEqual(installed_xrizer.read_bytes(), b"old-xrizer")
            self.assertEqual(stat.S_IMODE(installed_xrizer.stat().st_mode), 0o640)
            self.assertFalse(installed_opencomposite.exists())
            self.assertFalse((game / ".stvr-openvr/openvrpaths.vrpath").exists())

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
