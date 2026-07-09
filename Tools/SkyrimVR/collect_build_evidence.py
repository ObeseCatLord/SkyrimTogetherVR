#!/usr/bin/env python3
"""Collect no-build Windows package/build handoff evidence for SkyrimTogetherVR."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import zipfile

import audit_built_package
import vr_paths


SCHEMA = "skyrim_together_vr_build_evidence_v1"
DEFAULT_PACKAGE = pathlib.Path("artifacts/SkyrimTogetherVR/releasedbg")
DEFAULT_OUT_DIR = pathlib.Path("artifacts/SkyrimTogetherVR/build-evidence")
COMMAND_TIMEOUT_SECONDS = 45
TEXT_PACKAGE_FILES = (
    "SkyrimTogetherVR_BuildManifest.json",
    "Data/SKSE/Plugins/SkyrimTogetherVR_AE_to_SE.csv",
    "Data/SKSE/Plugins/SkyrimTogetherVR_AddressOverrides.csv",
)
SOURCE_EVIDENCE_FILES = (
    "SetupSkyrimTogetherVRBuildEnv-Windows.bat",
    "BuildSkyrimTogetherVR-Windows.ps1",
    "BuildSkyrimTogetherVR-Windows.bat",
    "BuildSkyrimTogetherVR-AvatarSync-Windows.bat",
    "BuildSkyrimTogetherVR-Gameplay-Windows.bat",
    "BuildSkyrimTogetherVR-DLL-Windows.bat",
    "BuildSkyrimTogetherVR-DLLs-Windows.bat",
    "BuildSkyrimTogetherVR-ClientDLL-Windows.bat",
    "BuildAndAuditSkyrimTogetherVR-Windows.bat",
    "BuildAndAuditSkyrimTogetherVR-DLL-Windows.bat",
    "BuildAuditCollectSkyrimTogetherVR-Windows.bat",
    "PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat",
    "VerifySkyrimTogetherVRWindowsPackages-Windows.bat",
    "CollectSkyrimTogetherVRBuildEvidence-Windows.bat",
    "AuditSkyrimTogetherVRBuildEvidence-Windows.bat",
    "AuditSkyrimTogetherVRFinalHandoff-Windows.bat",
    "AuditSkyrimTogetherVRGameplayRuntime-Windows.bat",
    "CollectSkyrimTogetherVRGameplayEvidence-Windows.bat",
    "AuditSkyrimTogetherVRGameplayEvidence-Windows.bat",
    "BuildSkyrimTogetherVR-Wine.sh",
    "xmake.lua",
    "Tools/SkyrimVR/audit_built_package.py",
    "Tools/SkyrimVR/audit_gamefiles.py",
    "Tools/SkyrimVR/compile_papyrus.py",
    "Tools/SkyrimVR/TESV_Papyrus_Flags.flg",
    "Tools/SkyrimVR/PapyrusImports/ActiveMagicEffect.psc",
    "Tools/SkyrimVR/PapyrusImports/Actor.psc",
    "Tools/SkyrimVR/PapyrusImports/Debug.psc",
    "Tools/SkyrimVR/PapyrusImports/Form.psc",
    "Tools/SkyrimVR/PapyrusImports/Game.psc",
    "Tools/SkyrimVR/PapyrusImports/Quest.psc",
    "Tools/SkyrimVR/PapyrusImports/ReferenceAlias.psc",
    "Tools/SkyrimVR/PapyrusImports/Spell.psc",
    "Tools/SkyrimVR/PapyrusImports/Utility.psc",
    "Code/xmake.lua",
    "Code/vr_common/VRHandoffPath.h",
    "Code/client/xmake.lua",
    "Code/immersive_launcher/xmake.lua",
    "Code/immersive_launcher/loader/PathRerouting.cpp",
    "Code/vrik_bridge/xmake.lua",
    "Code/higgs_bridge/xmake.lua",
    "Code/planck_bridge/xmake.lua",
    "Code/immersive_elf/xmake.lua",
    "Code/tp_process/xmake.lua",
    "Docs/SkyrimVR/inline-patch-audit.md",
    "Docs/SkyrimVR/inline-patch-manifest.json",
    "Docs/SkyrimVR/address-audit.md",
    "Docs/SkyrimVR/address-audit.json",
)


def repo_root() -> pathlib.Path:
    cwd = pathlib.Path.cwd()
    if (cwd / "xmake.lua").exists() and (cwd / "Tools" / "SkyrimVR" / "collect_build_evidence.py").exists():
        return cwd
    return pathlib.Path(__file__).absolute().parents[2]


def utc_stamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%d-%H%M%SZ")


def mode_name(avatar_sync: bool = False, dll_only: bool = False, gameplay: bool = False) -> str:
    if dll_only:
        return "dll-only"
    if gameplay:
        return "gameplay"
    return "avatar-sync" if avatar_sync else "default"


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_text(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def run_command(root: pathlib.Path, name: str, command: list[str], output_dir: pathlib.Path) -> dict[str, object]:
    output_file = output_dir / f"{name}.txt"
    command_text = " ".join(command)
    try:
        result = subprocess.run(
            command,
            cwd=root,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=COMMAND_TIMEOUT_SECONDS,
            check=False,
        )
        exit_code = result.returncode
        output = result.stdout
    except FileNotFoundError as exc:
        exit_code = 127
        output = f"Command not found: {exc}\n"
    except subprocess.TimeoutExpired as exc:
        exit_code = 124
        output = (exc.stdout or "") + f"\nTimed out after {COMMAND_TIMEOUT_SECONDS} seconds.\n"

    write_text(output_file, f"$ {command_text}\nexit_code={exit_code}\n\n{output}")
    return {
        "name": name,
        "command": command,
        "exitCode": exit_code,
        "output": output_file.relative_to(output_dir.parent).as_posix(),
    }


def package_file_inventory(package: pathlib.Path) -> list[dict[str, object]]:
    if not package.exists() or not package.is_dir():
        return []

    files: list[dict[str, object]] = []
    for path in sorted(p for p in package.rglob("*") if p.is_file()):
        relative = path.relative_to(package).as_posix()
        files.append(
            {
                "path": relative,
                "size": path.stat().st_size,
                "sha256": sha256(path),
            }
        )
    return files


def copy_text_package_files(package: pathlib.Path, evidence_root: pathlib.Path) -> list[str]:
    copied: list[str] = []
    for relative in TEXT_PACKAGE_FILES:
        source = package / relative
        if not source.exists() or not source.is_file():
            continue
        destination = evidence_root / "package" / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        copied.append(destination.relative_to(evidence_root).as_posix())
    return copied


def copy_source_evidence_files(root: pathlib.Path, evidence_root: pathlib.Path) -> list[str]:
    copied: list[str] = []
    for relative in SOURCE_EVIDENCE_FILES:
        source = root / relative
        if not source.exists() or not source.is_file():
            continue
        destination = evidence_root / "source" / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        copied.append(destination.relative_to(evidence_root).as_posix())
    return copied


def audit_command(
    package: pathlib.Path,
    skyrim_vr: pathlib.Path,
    avatar_sync: bool = False,
    dll_only: bool = False,
    gameplay: bool = False,
    require_installed_prerequisites: bool = False,
    require_vrik: bool = False,
    require_higgs: bool = False,
    require_planck: bool = False,
) -> list[str]:
    command = [
        sys.executable,
        "Tools/SkyrimVR/audit_built_package.py",
        "--package",
        str(package),
        "--skyrim-vr",
        str(skyrim_vr),
    ]
    if avatar_sync:
        command.append("--avatar-sync")
    if dll_only:
        command.append("--dll-only")
    if gameplay:
        command.append("--gameplay")
    if require_installed_prerequisites:
        command.append("--require-installed-prerequisites")
    if require_vrik:
        command.append("--require-vrik")
    if require_higgs:
        command.append("--require-higgs")
    if require_planck:
        command.append("--require-planck")
    return command


def collect_evidence(
    root: pathlib.Path,
    package: pathlib.Path,
    skyrim_vr: pathlib.Path,
    out_dir: pathlib.Path,
    avatar_sync: bool = False,
    dll_only: bool = False,
    gameplay: bool = False,
    require_installed_prerequisites: bool = False,
    require_vrik: bool = False,
    require_higgs: bool = False,
    require_planck: bool = False,
) -> pathlib.Path:
    stamp = utc_stamp()
    mode = mode_name(avatar_sync=avatar_sync, dll_only=dll_only, gameplay=gameplay)
    evidence_root = out_dir / f"SkyrimTogetherVR-build-evidence-{mode}-{stamp}"
    commands_dir = evidence_root / "commands"
    evidence_root.mkdir(parents=True, exist_ok=True)

    commands = [
        run_command(root, "python-version", [sys.executable, "--version"], commands_dir),
        run_command(root, "xmake-version", ["xmake", "--version"], commands_dir),
        run_command(root, "xmake-targets", ["xmake", "show", "-l", "targets"], commands_dir),
        run_command(root, "windows-build-audit", [sys.executable, "Tools/SkyrimVR/audit_windows_build.py"], commands_dir),
    ]

    if package.exists():
        commands.append(
            run_command(
                root,
                f"built-package-audit-{mode}",
        audit_command(
            package,
            skyrim_vr,
            avatar_sync=avatar_sync,
            dll_only=dll_only,
            gameplay=gameplay,
                    require_installed_prerequisites=require_installed_prerequisites,
                    require_vrik=require_vrik,
                    require_higgs=require_higgs,
                    require_planck=require_planck,
                ),
                commands_dir,
            )
        )
    else:
        write_text(commands_dir / f"built-package-audit-{mode}.txt", f"Package does not exist: {package}\n")

    package_files = package_file_inventory(package)
    copied_package_files = copy_text_package_files(package, evidence_root)
    copied_source_files = copy_source_evidence_files(root, evidence_root)
    listing_lines = [
        f"{entry['sha256']}  {entry['size']:>12}  {entry['path']}"
        for entry in package_files
    ]
    write_text(evidence_root / "package_file_listing.txt", "\n".join(listing_lines) + ("\n" if listing_lines else ""))

    manifest = {
        "schema": SCHEMA,
        "generatedAtUtc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "repoRoot": str(root),
        "package": str(package),
        "packageExists": package.exists(),
        "mode": mode,
        "skyrimVr": str(skyrim_vr),
        "commands": commands,
        "packageFileCount": len(package_files),
        "packageFiles": package_files,
        "copiedPackageTextFiles": copied_package_files,
        "copiedSourceEvidenceFiles": copied_source_files,
        "notes": [
            "This evidence collector does not build targets, install files, or launch Skyrim.",
            "Runtime DLL/EXE binaries are represented by package_file_listing.txt hashes, not copied into the evidence zip.",
            "Source-side build wrappers, xmake target files, and audit artifacts are copied under source/ so the Windows handoff can be audited later.",
        ],
    }
    write_text(evidence_root / "manifest.json", json.dumps(manifest, indent=2, sort_keys=True))

    summary = [
        "SkyrimTogetherVR build evidence",
        f"mode={mode}",
        f"package={package}",
        f"packageExists={package.exists()}",
        f"packageFileCount={len(package_files)}",
        f"skyrimVr={skyrim_vr}",
        "",
        "Commands:",
    ]
    for command in commands:
        summary.append(f"- {command['name']}: exit={command['exitCode']} output={command['output']}")
    write_text(evidence_root / "summary.txt", "\n".join(summary) + "\n")

    zip_path = out_dir / f"SkyrimTogetherVR-build-evidence-{mode}-{stamp}.zip"
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(p for p in evidence_root.rglob("*") if p.is_file()):
            archive.write(path, path.relative_to(evidence_root).as_posix())

    return zip_path


def run_self_test() -> int:
    root = repo_root()
    with tempfile.TemporaryDirectory(prefix="stvr-build-evidence-test-") as tmp:
        tmp_root = pathlib.Path(tmp)
        package = tmp_root / "package"
        skyrim_vr = tmp_root / "SkyrimVR"
        out_dir = tmp_root / "out"
        audit_built_package.populate_test_package(package, dll_only=True)
        zip_path = collect_evidence(root, package, skyrim_vr, out_dir, dll_only=True)
        if not zip_path.exists():
            print(f"self-test failed: missing evidence zip {zip_path}")
            return 1
        with zipfile.ZipFile(zip_path) as archive:
            names = set(archive.namelist())
            required = {
                "manifest.json",
                "summary.txt",
                "package_file_listing.txt",
                "commands/built-package-audit-dll-only.txt",
            }
            required_source_entries = {f"source/{relative}" for relative in SOURCE_EVIDENCE_FILES}
            required.update(required_source_entries)
            missing = sorted(required - names)
            if missing:
                print("self-test failed: evidence zip missing entries: " + ", ".join(missing))
                return 1
            manifest = json.loads(archive.read("manifest.json").decode("utf-8"))
            if manifest.get("schema") != SCHEMA:
                print("self-test failed: wrong manifest schema")
                return 1
            if manifest.get("mode") != "dll-only":
                print("self-test failed: wrong evidence mode")
                return 1
            copied_source_files = set(manifest.get("copiedSourceEvidenceFiles", []))
            missing_source_manifest_entries = sorted(required_source_entries - copied_source_files)
            if missing_source_manifest_entries:
                print("self-test failed: manifest missing copied source evidence entries: " + ", ".join(missing_source_manifest_entries))
                return 1
    print(f"Build evidence collector self-test archive: {zip_path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true", help="run temporary evidence-zip self-test")
    parser.add_argument("--package", type=pathlib.Path, default=DEFAULT_PACKAGE, help="built package directory to inspect")
    parser.add_argument("--out-dir", type=pathlib.Path, default=DEFAULT_OUT_DIR, help="directory for evidence zip output")
    parser.add_argument("--skyrim-vr", type=pathlib.Path, default=vr_paths.default_skyrim_vr_path(), help=vr_paths.skyrim_vr_help())
    parser.add_argument("--avatar-sync", action="store_true", help="audit package as the avatar-sync validation build")
    parser.add_argument("--dll-only", action="store_true", help="audit package as the DLL-only partial build")
    parser.add_argument("--gameplay", action="store_true", help="audit package as the gameplay build")
    parser.add_argument("--require-installed-prerequisites", action="store_true", help="pass installed SKSEVR/Address Library requirement to package audit")
    parser.add_argument("--require-vrik", action="store_true", help="pass VRIK requirement to package audit")
    parser.add_argument("--require-higgs", action="store_true", help="pass HIGGS requirement to package audit")
    parser.add_argument("--require-planck", action="store_true", help="pass PLANCK requirement to package audit")
    args = parser.parse_args()

    if args.self_test:
        return run_self_test()

    selected_modes = sum(1 for selected in (args.avatar_sync, args.dll_only, args.gameplay) if selected)
    if selected_modes > 1:
        parser.error("--avatar-sync, --dll-only, and --gameplay cannot be combined")

    root = repo_root()
    package = (root / args.package).resolve() if not args.package.is_absolute() else args.package.resolve()
    out_dir = (root / args.out_dir).resolve() if not args.out_dir.is_absolute() else args.out_dir.resolve()
    skyrim_vr = args.skyrim_vr.expanduser().resolve()

    zip_path = collect_evidence(
        root,
        package,
        skyrim_vr,
        out_dir,
        avatar_sync=args.avatar_sync,
        dll_only=args.dll_only,
        gameplay=args.gameplay,
        require_installed_prerequisites=args.require_installed_prerequisites,
        require_vrik=args.require_vrik,
        require_higgs=args.require_higgs,
        require_planck=args.require_planck,
    )
    print(f"Build evidence archive: {zip_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
