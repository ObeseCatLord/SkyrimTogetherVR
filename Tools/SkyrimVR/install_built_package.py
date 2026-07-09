#!/usr/bin/env python3
"""Install a Windows-built SkyrimTogetherVR package into a Skyrim VR folder."""

from __future__ import annotations

import argparse
import filecmp
import pathlib
import shutil
import subprocess
import sys
import tempfile

import vr_paths


DEFAULT_PACKAGE = pathlib.Path("artifacts/SkyrimTogetherVR/packages/default")
AVATAR_SYNC_PACKAGE = pathlib.Path("artifacts/SkyrimTogetherVR/packages/avatar-sync")
GAMEPLAY_PACKAGE = pathlib.Path("artifacts/SkyrimTogetherVR/packages/gameplay")
STALE_ROOT_GAME_FILE_PATHS = (
    pathlib.Path("SkyrimTogether.esp"),
    pathlib.Path("scripts"),
    pathlib.Path("meshes"),
    pathlib.Path("SkyrimTogetherRebornBehaviors"),
)


def default_skyrim_vr() -> pathlib.Path:
    return vr_paths.default_skyrim_vr_path()


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def default_package_for_mode(*, avatar_sync: bool, gameplay: bool) -> pathlib.Path:
    if gameplay:
        return GAMEPLAY_PACKAGE
    if avatar_sync:
        return AVATAR_SYNC_PACKAGE
    return DEFAULT_PACKAGE


def package_files(package: pathlib.Path) -> list[pathlib.Path]:
    if not package.exists() or not package.is_dir():
        return []
    return sorted(path for path in package.rglob("*") if path.is_file())


def relative_to_package(package: pathlib.Path, path: pathlib.Path) -> pathlib.Path:
    return path.relative_to(package)


def is_same_file(source: pathlib.Path, destination: pathlib.Path) -> bool:
    if not destination.exists() or not destination.is_file():
        return False
    try:
        return filecmp.cmp(source, destination, shallow=False)
    except OSError:
        return False


def find_stale_target_paths(skyrim_vr: pathlib.Path) -> list[tuple[pathlib.Path, pathlib.Path]]:
    stale_paths = []
    for relative in STALE_ROOT_GAME_FILE_PATHS:
        path = skyrim_vr / relative
        if path.exists():
            stale_paths.append((relative, path))
    return stale_paths


def cleanup_stale_root_files(stale_paths: list[tuple[pathlib.Path, pathlib.Path]], dry_run: bool) -> int:
    cleaned = 0
    for _, path in stale_paths:
        if dry_run:
            cleaned += 1
            continue
        if path.is_dir() and not path.is_symlink():
            shutil.rmtree(path)
        else:
            path.unlink()
        cleaned += 1
    return cleaned


def stale_root_files_block_handoff(
    stale_paths: list[tuple[pathlib.Path, pathlib.Path]], install: bool, cleanup_stale: bool
) -> bool:
    return bool(stale_paths) and not (install and cleanup_stale)


def build_package_audit_command(args: argparse.Namespace) -> list[str]:
    command = [
        sys.executable,
        "Tools/SkyrimVR/audit_built_package.py",
        "--package",
        str(args.package),
        "--skyrim-vr",
        str(args.skyrim_vr),
    ]
    if args.avatar_sync:
        command.append("--avatar-sync")
    if args.gameplay:
        command.append("--gameplay")

    if not args.package_only:
        command.append("--require-installed-prerequisites")
        if args.require_vrik:
            command.append("--require-vrik")
        if args.require_higgs:
            command.append("--require-higgs")
        if args.require_planck:
            command.append("--require-planck")

    return command


def run_package_audit(root: pathlib.Path, args: argparse.Namespace) -> int:
    command = build_package_audit_command(args)
    print("> " + " ".join(command))
    result = subprocess.run(command, cwd=root, text=True, check=False)
    return result.returncode


def write_file(path: pathlib.Path, content: str = "test") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def populate_stale_target_tree(skyrim_vr: pathlib.Path) -> None:
    write_file(skyrim_vr / "SkyrimTogether.esp")
    write_file(skyrim_vr / "scripts" / "SkyrimTogetherUtils.pex")
    write_file(skyrim_vr / "meshes" / "SkyrimTogetherVR" / "placeholder.nif")
    write_file(skyrim_vr / "SkyrimTogetherRebornBehaviors" / "placeholder.hkx")


def run_self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="stvr-install-") as temp_root:
        skyrim_vr = pathlib.Path(temp_root) / "SkyrimVR"
        populate_stale_target_tree(skyrim_vr)

        stale_paths = find_stale_target_paths(skyrim_vr)
        stale_relatives = [relative for relative, _ in stale_paths]
        if stale_relatives != list(STALE_ROOT_GAME_FILE_PATHS):
            print("Install self-test did not detect the expected stale root files.")
            return 1

        cleaned = cleanup_stale_root_files(stale_paths, dry_run=True)
        if cleaned != len(STALE_ROOT_GAME_FILE_PATHS):
            print("Install self-test dry-run cleanup count is incorrect.")
            return 1
        if not all(path.exists() for _, path in stale_paths):
            print("Install self-test dry-run cleanup removed files.")
            return 1

        cleaned = cleanup_stale_root_files(stale_paths, dry_run=False)
        if cleaned != len(STALE_ROOT_GAME_FILE_PATHS):
            print("Install self-test cleanup count is incorrect.")
            return 1
        remaining = [relative.as_posix() for relative, path in stale_paths if path.exists()]
        if remaining:
            print("Install self-test cleanup left stale paths: " + ", ".join(remaining))
            return 1

        if not stale_root_files_block_handoff(stale_paths, install=False, cleanup_stale=False):
            print("Install self-test stale-root handoff guard did not block dry-run without cleanup.")
            return 1
        if not stale_root_files_block_handoff(stale_paths, install=True, cleanup_stale=False):
            print("Install self-test stale-root handoff guard did not block install without cleanup.")
            return 1
        if not stale_root_files_block_handoff(stale_paths, install=False, cleanup_stale=True):
            print("Install self-test stale-root handoff guard did not block dry-run cleanup.")
            return 1
        if stale_root_files_block_handoff(stale_paths, install=True, cleanup_stale=True):
            print("Install self-test stale-root handoff guard blocked install cleanup.")
            return 1

        command_args = argparse.Namespace(
            package=pathlib.Path("package"),
            skyrim_vr=skyrim_vr,
            avatar_sync=False,
            gameplay=False,
            package_only=False,
            require_vrik=True,
            require_higgs=True,
            require_planck=True,
        )
        strict_command = build_package_audit_command(command_args)
        for token in (
            "--require-installed-prerequisites",
            "--require-vrik",
            "--require-higgs",
            "--require-planck",
        ):
            if token not in strict_command:
                print(f"Install self-test strict audit command missing {token}.")
                return 1

        command_args.package_only = True
        package_only_command = build_package_audit_command(command_args)
        forbidden_tokens = {
            "--require-installed-prerequisites",
            "--require-vrik",
            "--require-higgs",
            "--require-planck",
        }
        present_forbidden = sorted(token for token in forbidden_tokens if token in package_only_command)
        if present_forbidden:
            print("Install self-test package-only audit command kept strict prerequisite token(s): " + ", ".join(present_forbidden))
            return 1

    print("Install self-test passed.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package", type=pathlib.Path)
    parser.add_argument("--skyrim-vr", type=pathlib.Path, default=default_skyrim_vr())
    parser.add_argument("--install", action="store_true", help="copy package files into the Skyrim VR folder")
    parser.add_argument("--no-overwrite", action="store_true", help="do not overwrite changed existing files")
    parser.add_argument("--skip-audit", action="store_true", help="skip audit_built_package.py before copying")
    parser.add_argument(
        "--package-only",
        action="store_true",
        help="audit the built package layout and copy plan without requiring target Skyrim VR prerequisites; dry-run only",
    )
    parser.add_argument("--avatar-sync", action="store_true", help="audit/install the avatar-sync validation package")
    parser.add_argument("--gameplay", action="store_true", help="audit/install the gameplay package")
    parser.add_argument(
        "--cleanup-stale-root-files",
        action="store_true",
        help="remove root-level files left by older bad package layouts; only mutates with --install",
    )
    parser.add_argument("--self-test", action="store_true", help="run installer cleanup self-test and exit")
    parser.add_argument("--no-require-vrik", dest="require_vrik", action="store_false", default=True)
    parser.add_argument("--no-require-higgs", dest="require_higgs", action="store_false", default=True)
    parser.add_argument("--no-require-planck", dest="require_planck", action="store_false", default=True)
    args = parser.parse_args()

    if args.self_test:
        return run_self_test()
    if args.package_only and args.install:
        print("--package-only is a dry-run/preflight mode and cannot be combined with --install.")
        print("Install without --package-only after the target Skyrim VR prerequisites pass.")
        return 1
    if args.avatar_sync and args.gameplay:
        print("--avatar-sync and --gameplay cannot be combined.")
        return 1

    if args.package is None:
        args.package = default_package_for_mode(avatar_sync=args.avatar_sync, gameplay=args.gameplay)

    root = repo_root()
    package = (root / args.package).resolve() if not args.package.is_absolute() else args.package.resolve()
    skyrim_vr = args.skyrim_vr.resolve()
    args.package = package
    args.skyrim_vr = skyrim_vr

    print(f"Package root: {package}")
    print(f"Skyrim VR root: {skyrim_vr}")
    print(f"Mode: {'install' if args.install else 'dry-run'}")
    if args.package_only:
        print("Package-only preflight: target SKSEVR/VRIK/HIGGS/PLANCK prerequisite checks are skipped.")
        print("Package-only preflight is a layout/copy-plan check only; run without --package-only for strict install readiness.")
    else:
        required_mods = [
            name
            for name, enabled in (
                ("VRIK", args.require_vrik),
                ("HIGGS", args.require_higgs),
                ("PLANCK", args.require_planck),
            )
            if enabled
        ]
        print("Strict install readiness audit: target SKSEVR and VR Address Library are required.")
        if required_mods:
            print("Strict install readiness audit: required VR mod prerequisites: " + ", ".join(required_mods))

    if not args.skip_audit:
        audit_result = run_package_audit(root, args)
        if audit_result != 0:
            print("Package audit failed; refusing to install.")
            return audit_result

    files = package_files(package)
    if not files:
        print(f"No package files found under: {package}")
        return 1

    stale_target_paths = find_stale_target_paths(skyrim_vr)
    if stale_target_paths:
        print("Stale root-level staged files detected in the Skyrim VR folder:")
        for relative, _ in stale_target_paths:
            print(f"stale-root-file: {relative.as_posix()}")
        if args.cleanup_stale_root_files:
            cleaned = cleanup_stale_root_files(stale_target_paths, dry_run=not args.install)
            if args.install:
                print(f"Removed stale root-level staged files: {cleaned}")
            else:
                print(f"Dry-run cleanup count: {cleaned}")
                print("Re-run with --install --cleanup-stale-root-files to remove these paths.")
        else:
            print("Use --cleanup-stale-root-files with --install to remove these old package paths.")

        if stale_root_files_block_handoff(stale_target_paths, args.install, args.cleanup_stale_root_files):
            print("Refusing to continue while stale root-level staged files are present.")
            print("Run once with --install --cleanup-stale-root-files to remove these old package paths.")
            return 1

    planned_copy = 0
    planned_update = 0
    planned_skip_same = 0
    planned_skip_existing = 0

    for source in files:
        relative = relative_to_package(package, source)
        destination = skyrim_vr / relative
        same = is_same_file(source, destination)
        if same:
            planned_skip_same += 1
            action = "same"
        elif destination.exists() and args.no_overwrite:
            planned_skip_existing += 1
            action = "skip-existing"
        elif destination.exists():
            planned_update += 1
            action = "update"
        else:
            planned_copy += 1
            action = "copy"

        print(f"{action}: {relative.as_posix()}")

        if args.install and action in {"copy", "update"}:
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)

    print(f"Package files scanned: {len(files)}")
    print(f"Files to copy: {planned_copy}")
    print(f"Files to update: {planned_update}")
    print(f"Files already identical: {planned_skip_same}")
    print(f"Existing files skipped: {planned_skip_existing}")

    if not args.install:
        print("Dry run complete. Re-run with --install to copy files.")
    elif planned_skip_existing:
        print("Install completed with skipped existing files because --no-overwrite was used.")
        return 1
    else:
        print("Install completed.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
