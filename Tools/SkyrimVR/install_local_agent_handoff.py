#!/usr/bin/env python3
"""Install an extracted private local-agent handoff into a second Linux client."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import pathlib
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile


sys.dont_write_bytecode = True

SKYRIM_VR_1_4_15_SHA256 = "6961efb4f4775a307b0fc9a3d637542c1e090be207d3b09467eab216b7f87971"
PROFILE_PATHS = {
    "Plugins.txt": pathlib.PurePosixPath("pfx/drive_c/users/steamuser/AppData/Local/Skyrim VR/Plugins.txt"),
    "loadorder.txt": pathlib.PurePosixPath("pfx/drive_c/users/steamuser/AppData/Local/Skyrim VR/loadorder.txt"),
    "SkyrimPrefs.ini": pathlib.PurePosixPath("pfx/drive_c/users/steamuser/Documents/My Games/Skyrim VR/SkyrimPrefs.ini"),
}
LAUNCHERS = (
    "launch-skyrim-together-vr.sh",
    "launch-skyrim-vr-offline.sh",
    "stvr-xrizer-input-compat.sh",
)


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def archive_child(root: pathlib.Path, relative: str | pathlib.PurePosixPath) -> pathlib.Path:
    pure = pathlib.PurePosixPath(relative)
    if pure.is_absolute() or ".." in pure.parts or not pure.parts:
        raise ValueError(f"unsafe handoff path: {relative}")
    path = root.joinpath(*pure.parts)
    try:
        path.relative_to(root)
    except ValueError as error:
        raise ValueError(f"handoff path escapes archive root: {relative}") from error
    return path


def safe_zip_members(archive: zipfile.ZipFile) -> dict[str, zipfile.ZipInfo]:
    """Return regular-file entries after rejecting traversal, aliases, and links."""

    members: dict[str, zipfile.ZipInfo] = {}
    for info in archive.infolist():
        raw_name = info.filename
        if raw_name.endswith(("/", "\\")):
            continue
        name = raw_name.replace("\\", "/")
        canonical = pathlib.PurePosixPath(name).as_posix()
        mode = info.external_attr >> 16
        if (
            raw_name != name
            or name.startswith("/")
            or ".." in pathlib.PurePosixPath(name).parts
            or canonical != name
            or stat.S_ISLNK(mode)
        ):
            raise ValueError(f"unsafe ZIP path: {raw_name}")
        if name in members:
            raise ValueError(f"duplicate ZIP path: {name}")
        members[name] = info
    return members


def load_artifact_helper(root: pathlib.Path):
    helper_path = archive_child(root, "source/Tools/SkyrimVR/local_handoff_artifacts.py")
    if not helper_path.is_file():
        raise FileNotFoundError(f"missing included artifact validator: {helper_path}")
    spec = importlib.util.spec_from_file_location("local_handoff_artifacts", helper_path)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load included artifact validator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.validate_artifact_pair


def load_manifest(root: pathlib.Path) -> dict[str, object]:
    manifest_path = archive_child(root, "LOCAL-MANIFEST.json")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid LOCAL-MANIFEST.json: {error}") from error
    if not isinstance(manifest, dict):
        raise ValueError("LOCAL-MANIFEST.json is not an object")
    if manifest.get("schema") != "skyrim_together_vr_local_agent_handoff_v1" or manifest.get("localOnly") is not True:
        raise ValueError("not a private local-agent handoff")
    return manifest


def manifest_record_map(manifest: dict[str, object]) -> dict[str, dict[str, object]]:
    records = manifest.get("records")
    if not isinstance(records, list):
        raise ValueError("LOCAL-MANIFEST.json has no records list")
    result: dict[str, dict[str, object]] = {}
    for record in records:
        if not isinstance(record, dict) or set(record) != {"path", "size", "sha256"}:
            raise ValueError("LOCAL-MANIFEST.json has a malformed record")
        path = record["path"]
        size = record["size"]
        digest = record["sha256"]
        if (
            not isinstance(path, str)
            or not path
            or not isinstance(size, int)
            or isinstance(size, bool)
            or size < 0
            or not isinstance(digest, str)
            or len(digest) != 64
            or any(char not in "0123456789abcdef" for char in digest)
        ):
            raise ValueError("LOCAL-MANIFEST.json has a malformed record")
        if path in result:
            raise ValueError(f"LOCAL-MANIFEST.json duplicates a record: {path}")
        result[path] = record
    return result


def extracted_regular_files(root: pathlib.Path) -> dict[str, pathlib.Path]:
    """Return handoff files while refusing links and non-regular payloads."""

    if root.is_symlink() or not root.is_dir():
        raise ValueError(f"handoff root is not a regular directory: {root}")
    files: dict[str, pathlib.Path] = {}

    def visit(directory: pathlib.Path) -> None:
        try:
            entries = list(os.scandir(directory))
        except OSError as error:
            raise ValueError(f"could not inspect handoff directory: {directory}: {error}") from error
        for entry in entries:
            path = pathlib.Path(entry.path)
            if entry.is_symlink():
                raise ValueError(f"handoff contains symbolic link: {path}")
            if entry.is_dir(follow_symlinks=False):
                visit(path)
            elif entry.is_file(follow_symlinks=False):
                relative = path.relative_to(root).as_posix()
                files[f"{root.name}/{relative}"] = path
            else:
                raise ValueError(f"handoff contains non-regular payload: {path}")

    visit(root)
    return files


def verify_handoff_payload(
    root: pathlib.Path,
    records: dict[str, dict[str, object]],
) -> None:
    """Require the manifest record set to exactly cover verified handoff files."""

    files = extracted_regular_files(root)
    manifest_path = f"{root.name}/LOCAL-MANIFEST.json"
    if manifest_path not in files:
        raise FileNotFoundError(f"missing LOCAL-MANIFEST.json: {root / 'LOCAL-MANIFEST.json'}")
    payload_files = {path: file for path, file in files.items() if path != manifest_path}
    expected = set(records)
    actual = set(payload_files)
    if expected != actual:
        missing = sorted(actual - expected)
        extra = sorted(expected - actual)
        detail = []
        if missing:
            detail.append(f"missing records: {', '.join(missing[:3])}")
        if extra:
            detail.append(f"unexpected records: {', '.join(extra[:3])}")
        raise ValueError("LOCAL-MANIFEST.json record set does not match handoff payload (" + "; ".join(detail) + ")")
    for path, source in payload_files.items():
        record = records[path]
        if source.stat().st_size != record["size"] or sha256(source) != record["sha256"]:
            raise ValueError(f"handoff payload does not match LOCAL-MANIFEST.json: {path}")


def verified_artifact(
    root: pathlib.Path,
    manifest: dict[str, object],
    records: dict[str, dict[str, object]],
    key: str,
) -> pathlib.Path:
    metadata = manifest.get(key)
    if not isinstance(metadata, dict) or not isinstance(metadata.get("name"), str):
        raise ValueError(f"LOCAL-MANIFEST.json has invalid {key} metadata")
    path = archive_child(root, pathlib.PurePosixPath("build") / metadata["name"])
    record_key = f"{root.name}/build/{metadata['name']}"
    record = records.get(record_key)
    expected_hash = metadata.get("sha256")
    if not path.is_file() or not isinstance(expected_hash, str) or record is None:
        raise ValueError(f"missing or unrecorded {key} payload")
    if record.get("sha256") != expected_hash or record.get("size") != path.stat().st_size:
        raise ValueError(f"{key} does not match its local handoff manifest record")
    if sha256(path) != expected_hash:
        raise ValueError(f"{key} SHA-256 does not match LOCAL-MANIFEST.json")
    return path


def verify_source_ancestry(root: pathlib.Path, build_revision: str, source_head: str) -> None:
    bundle = archive_child(root, "source.bundle")
    if not bundle.is_file():
        raise FileNotFoundError(f"missing source bundle: {bundle}")
    with tempfile.TemporaryDirectory(prefix="stvr-second-client-") as temp_dir:
        bare_repo = pathlib.Path(temp_dir) / "bundle.git"
        commands = (
            ["git", "init", "--bare", str(bare_repo)],
            ["git", "-C", str(bare_repo), "bundle", "unbundle", str(bundle)],
            ["git", "-C", str(bare_repo), "merge-base", "--is-ancestor", build_revision, source_head],
        )
        for command in commands:
            completed = subprocess.run(command, capture_output=True, text=True, check=False)
            if completed.returncode:
                detail = completed.stderr.strip() or completed.stdout.strip()
                raise ValueError(f"source bundle ancestry validation failed: {detail}")


def safe_target_path(root: pathlib.Path, relative: pathlib.PurePosixPath) -> pathlib.Path:
    if relative.is_absolute() or ".." in relative.parts or not relative.parts:
        raise ValueError(f"unsafe install path: {relative}")
    if root.is_symlink() or not root.is_dir():
        raise ValueError(f"unsafe install root: {root}")
    destination = root.joinpath(*relative.parts)
    current = root
    for part in relative.parts[:-1]:
        current = current / part
        if current.exists() and (current.is_symlink() or not current.is_dir()):
            raise ValueError(f"unsafe target directory: {current}")
    if destination.exists() and destination.is_symlink():
        raise ValueError(f"refusing to replace symlink: {destination}")
    return destination


def copy_file(source: pathlib.Path, destination: pathlib.Path, *, dry_run: bool) -> None:
    if source.is_symlink() or not source.is_file():
        raise ValueError(f"handoff source is not a regular file: {source}")
    if dry_run:
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def install_overlay(root: pathlib.Path, game_dir: pathlib.Path, *, dry_run: bool) -> tuple[int, int]:
    overlay = archive_child(root, "dependencies/current-game-overlay")
    if not overlay.is_dir():
        raise FileNotFoundError(f"missing current-game-overlay: {overlay}")
    copied = skipped_exe = 0
    for source in sorted(overlay.rglob("*")):
        if source.is_dir():
            if source.is_symlink():
                raise ValueError(f"handoff overlay contains symlinked directory: {source}")
            continue
        relative = pathlib.PurePosixPath(source.relative_to(overlay).as_posix())
        destination = safe_target_path(game_dir, relative)
        if relative.as_posix().casefold() == "skyrimvr.exe":
            skipped_exe += 1
            continue
        copy_file(source, destination, dry_run=dry_run)
        copied += 1
    if skipped_exe != 1:
        raise ValueError("current-game-overlay must contain exactly one SkyrimVR.exe to preserve")
    return copied, skipped_exe


def extract_gameplay_package(package: pathlib.Path, game_dir: pathlib.Path, *, dry_run: bool) -> int:
    extracted = 0
    with zipfile.ZipFile(package) as archive:
        members = safe_zip_members(archive)
        for name, info in sorted(members.items()):
            if not name.startswith("package/") or name == "package/":
                raise ValueError(f"gameplay package has unexpected entry: {name}")
            relative = pathlib.PurePosixPath(name.removeprefix("package/"))
            if relative.as_posix().casefold() == "skyrimvr.exe":
                raise ValueError("gameplay package must not replace the legal SkyrimVR.exe")
            destination = safe_target_path(game_dir, relative)
            if not dry_run:
                destination.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(info) as source, destination.open("wb") as output:
                    shutil.copyfileobj(source, output, length=1024 * 1024)
                mode = info.external_attr >> 16
                if mode:
                    destination.chmod(mode & 0o777)
            extracted += 1
    return extracted


def restore_profiles(root: pathlib.Path, compatdata: pathlib.Path, *, dry_run: bool) -> int:
    restored = 0
    for name, relative in PROFILE_PATHS.items():
        source = archive_child(root, pathlib.PurePosixPath("profiles/direct-proton") / name)
        destination = safe_target_path(compatdata, relative)
        copy_file(source, destination, dry_run=dry_run)
        restored += 1
    return restored


def mark_launchers_executable(game_dir: pathlib.Path, *, dry_run: bool) -> int:
    changed = 0
    for name in LAUNCHERS:
        path = safe_target_path(game_dir, pathlib.PurePosixPath(name))
        # The preceding overlay/package plan supplies these files.  A clean
        # second game root will not contain them yet during a dry run.
        if dry_run:
            changed += 1
            continue
        if not path.is_file():
            raise FileNotFoundError(f"installed launcher is missing: {path}")
        path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        changed += 1
    return changed


def self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="stvr-installer-self-test-") as temp_dir:
        path = pathlib.Path(temp_dir) / "unsafe.zip"
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr("../escape", "no")
        with zipfile.ZipFile(path) as archive:
            try:
                safe_zip_members(archive)
            except ValueError:
                pass
            else:
                raise AssertionError("unsafe ZIP traversal was accepted")
        with zipfile.ZipFile(path, "w") as archive:
            link = zipfile.ZipInfo("package/link")
            link.external_attr = 0o120777 << 16
            archive.writestr(link, "target")
        with zipfile.ZipFile(path) as archive:
            try:
                safe_zip_members(archive)
            except ValueError:
                pass
            else:
                raise AssertionError("unsafe ZIP symlink was accepted")
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr("package/ok.txt", "ok")
        with zipfile.ZipFile(path) as archive:
            assert list(safe_zip_members(archive)) == ["package/ok.txt"]
        game_dir = pathlib.Path(temp_dir) / "game"
        game_dir.mkdir()
        assert extract_gameplay_package(path, game_dir, dry_run=True) == 1
        assert not (game_dir / "ok.txt").exists(), "dry-run wrote a package file"
        overlay = pathlib.Path(temp_dir) / "handoff" / "dependencies/current-game-overlay"
        (overlay / "Data/SKSE").mkdir(parents=True)
        (overlay / "SkyrimVR.exe").write_bytes(b"preserve")
        (overlay / "Data/SKSE/Plugins.txt").write_text("portable", encoding="ascii")
        handoff_root = overlay.parents[1]
        copied, skipped = install_overlay(handoff_root, game_dir, dry_run=True)
        assert (copied, skipped) == (1, 1)
        assert not (game_dir / "Data/SKSE/Plugins.txt").exists(), "dry-run wrote an overlay file"
        manifest = {
            "gameplayPackage": {
                "name": "payload.zip",
                "sha256": "0" * 64,
            },
            "records": [
                {
                    "path": "handoff/build/payload.zip",
                    "size": 1,
                    "sha256": "0" * 64,
                }
            ]
        }
        payload = handoff_root / "build/payload.zip"
        payload.parent.mkdir()
        payload.write_bytes(b"x")
        try:
            verified_artifact(
                handoff_root,
                manifest,
                manifest_record_map(manifest),
                "gameplayPackage",
            )
        except ValueError:
            pass
        else:
            raise AssertionError("mismatched handoff artifact metadata was accepted")
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr("package/SkyrimVR.exe", "forbidden")
        try:
            extract_gameplay_package(path, game_dir, dry_run=True)
        except ValueError:
            pass
        else:
            raise AssertionError("gameplay package replacing SkyrimVR.exe was accepted")

        payload_root = pathlib.Path(temp_dir) / "verified-handoff"
        payload_root.mkdir()
        payload_file = payload_root / "payload.txt"
        payload_file.write_text("verified", encoding="ascii")
        payload_record = {
            "path": f"{payload_root.name}/payload.txt",
            "size": payload_file.stat().st_size,
            "sha256": sha256(payload_file),
        }
        (payload_root / "LOCAL-MANIFEST.json").write_text(
            json.dumps({"schema": "skyrim_together_vr_local_agent_handoff_v1", "localOnly": True, "records": [payload_record]}),
            encoding="utf-8",
        )
        verify_handoff_payload(payload_root, manifest_record_map(load_manifest(payload_root)))
        payload_file.write_text("corrupt", encoding="ascii")
        try:
            verify_handoff_payload(payload_root, manifest_record_map(load_manifest(payload_root)))
        except ValueError:
            pass
        else:
            raise AssertionError("corrupted handoff payload was accepted")
    print("installer self-test passed")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", type=pathlib.Path, help="second legal Skyrim VR game directory")
    parser.add_argument("--compatdata", type=pathlib.Path, help="second Steam app 611670 compatdata directory")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--install", "--apply", dest="install", action="store_true", help="write the validated handoff into both targets")
    mode.add_argument("--dry-run", action="store_true", help="compatibility alias for the default validation-only mode")
    parser.add_argument("--self-test", action="store_true", help="run focused ZIP path-safety checks")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        return self_test()
    if args.game_dir is None or args.compatdata is None:
        raise ValueError("--game-dir and --compatdata are required for installation")
    root = pathlib.Path(__file__).resolve().parent
    game_dir = args.game_dir.expanduser().absolute()
    compatdata = args.compatdata.expanduser().absolute()
    if game_dir.is_symlink() or compatdata.is_symlink() or not game_dir.is_dir() or not compatdata.is_dir():
        raise ValueError("--game-dir and --compatdata must name existing non-symlink directories")
    game_exe = game_dir / "SkyrimVR.exe"
    if not game_exe.is_file() or game_exe.is_symlink():
        raise ValueError(f"--game-dir does not contain a regular SkyrimVR.exe: {game_exe}")
    if sha256(game_exe) != SKYRIM_VR_1_4_15_SHA256:
        raise ValueError("SkyrimVR.exe is not the supported legal Skyrim VR 1.4.15 executable")

    manifest = load_manifest(root)
    records = manifest_record_map(manifest)
    verify_handoff_payload(root, records)
    package = verified_artifact(root, manifest, records, "gameplayPackage")
    evidence = verified_artifact(root, manifest, records, "buildEvidence")
    build_revision = manifest.get("buildSourceRevision")
    source_head = manifest.get("sourceHead")
    if not isinstance(build_revision, str) or not isinstance(source_head, str):
        raise ValueError("LOCAL-MANIFEST.json is missing source ancestry")
    validate_artifact_pair = load_artifact_helper(root)
    identity = validate_artifact_pair(package, evidence, build_revision)
    for key in ("gameplayPackage", "buildEvidence"):
        metadata = manifest[key]
        if any(metadata.get(name) != value for name, value in identity.items()):
            raise ValueError(f"{key} metadata does not match the paired gameplay artifacts")
    verify_source_ancestry(root, build_revision, source_head)

    dry_run = not args.install
    overlay_count, _ = install_overlay(root, game_dir, dry_run=dry_run)
    package_count = extract_gameplay_package(package, game_dir, dry_run=dry_run)
    profile_count = restore_profiles(root, compatdata, dry_run=dry_run)
    launcher_count = mark_launchers_executable(game_dir, dry_run=dry_run)
    action = "validated (dry run; no target files changed)" if dry_run else "installed"
    print(
        f"{action}: {overlay_count} overlay files, {package_count} gameplay files, "
        f"{profile_count} Proton profile files, {launcher_count} launchers; "
        f"build {build_revision[:8]}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print(f"install failed: {error}", file=sys.stderr)
        raise SystemExit(1)
