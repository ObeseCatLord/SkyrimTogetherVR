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
from typing import Callable, Literal, NamedTuple


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
    "manage-monado-runtime.sh",
)
STATE_DIR_NAME = ".stvr-local-agent-handoff-state"
STATE_FILE_NAME = "state.json"
STATE_SCHEMA = "skyrim_together_vr_local_agent_install_state_v2"
PREPARING_FILE_NAME = "PREPARING"


class InstallOperation(NamedTuple):
    target_root: Literal["game", "compatdata"]
    relative: pathlib.PurePosixPath
    source: pathlib.Path | tuple[pathlib.Path, str]
    installed_hash: str
    mode: int


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


def source_hash(source: pathlib.Path | tuple[pathlib.Path, str]) -> str:
    if isinstance(source, pathlib.Path):
        if source.is_symlink() or not source.is_file():
            raise ValueError(f"handoff source is not a regular file: {source}")
        return sha256(source)
    package, member = source
    with zipfile.ZipFile(package) as archive, archive.open(member) as handle:
        digest = hashlib.sha256()
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
        return digest.hexdigest()


def operation_for_file(
    target_root: Literal["game", "compatdata"],
    relative: pathlib.PurePosixPath,
    source: pathlib.Path,
    *,
    executable: bool = False,
) -> InstallOperation:
    if source.is_symlink() or not source.is_file():
        raise ValueError(f"handoff source is not a regular file: {source}")
    mode = stat.S_IMODE(source.stat().st_mode)
    if executable:
        mode |= stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH
    return InstallOperation(target_root, relative, source, sha256(source), mode)


def collect_overlay(root: pathlib.Path) -> tuple[list[InstallOperation], tuple[int, int]]:
    overlay = archive_child(root, "dependencies/current-game-overlay")
    if not overlay.is_dir():
        raise FileNotFoundError(f"missing current-game-overlay: {overlay}")
    operations: list[InstallOperation] = []
    skipped_exe = 0
    skipped_openvr_api = 0
    for source in sorted(overlay.rglob("*")):
        if source.is_dir():
            if source.is_symlink():
                raise ValueError(f"handoff overlay contains symlinked directory: {source}")
            continue
        relative = pathlib.PurePosixPath(source.relative_to(overlay).as_posix())
        if relative.as_posix().casefold() == "skyrimvr.exe":
            skipped_exe += 1
            continue
        if relative.as_posix().casefold() == "openvr_api.dll":
            skipped_openvr_api += 1
            continue
        operations.append(operation_for_file("game", relative, source))
    if skipped_exe != 1:
        raise ValueError("current-game-overlay must contain exactly one SkyrimVR.exe to preserve")
    if skipped_openvr_api != 1:
        raise ValueError("current-game-overlay must contain exactly one openvr_api.dll to preserve")
    return operations, (skipped_exe, skipped_openvr_api)


def collect_gameplay_package(package: pathlib.Path) -> list[InstallOperation]:
    operations: list[InstallOperation] = []
    with zipfile.ZipFile(package) as archive:
        members = safe_zip_members(archive)
        for name, info in sorted(members.items()):
            if not name.startswith("package/") or name == "package/":
                raise ValueError(f"gameplay package has unexpected entry: {name}")
            relative = pathlib.PurePosixPath(name.removeprefix("package/"))
            if relative.as_posix().casefold() in {"skyrimvr.exe", "openvr_api.dll"}:
                raise ValueError(f"gameplay package must not replace {relative.name}")
            mode = (info.external_attr >> 16) & 0o777
            operations.append(
                InstallOperation("game", relative, (package, name), source_hash((package, name)), mode or 0o644)
            )
    return operations


def collect_profiles(root: pathlib.Path) -> list[InstallOperation]:
    operations: list[InstallOperation] = []
    for name, relative in PROFILE_PATHS.items():
        source = archive_child(root, pathlib.PurePosixPath("profiles/direct-proton") / name)
        operations.append(operation_for_file("compatdata", relative, source))
    return operations


def collect_canonical_launchers(root: pathlib.Path) -> list[InstallOperation]:
    """Return the portable launchers last so they override overlay copies."""

    operations: list[InstallOperation] = []
    for name in LAUNCHERS:
        source = archive_child(root, pathlib.PurePosixPath("source/Tools/SkyrimVR/linux") / name)
        operations.append(operation_for_file("game", pathlib.PurePosixPath(name), source, executable=True))
    return operations


def collect_openvr_runtimes(root: pathlib.Path) -> list[InstallOperation]:
    """Install complete portable OpenVR loader layouts and neutral registry."""

    runtimes = (
        (
            pathlib.PurePosixPath("dependencies/xrizer-runtime/libxrizer.so"),
            pathlib.PurePosixPath(".stvr-openvr/xrizer/libxrizer.so"),
            True,
        ),
        (
            pathlib.PurePosixPath("dependencies/xrizer-runtime/bin/linux64/vrclient.so"),
            pathlib.PurePosixPath(".stvr-openvr/xrizer/bin/linux64/vrclient.so"),
            True,
        ),
        (
            pathlib.PurePosixPath("dependencies/opencomposite-runtime/bin/linux64/vrclient.so"),
            pathlib.PurePosixPath(".stvr-openvr/opencomposite/bin/linux64/vrclient.so"),
            True,
        ),
        (
            pathlib.PurePosixPath("dependencies/openvrpaths.vrpath"),
            pathlib.PurePosixPath(".stvr-openvr/openvrpaths.vrpath"),
            False,
        ),
    )
    return [
        operation_for_file("game", destination, archive_child(root, source), executable=executable)
        for source, destination, executable in runtimes
    ]


def state_directory(game_dir: pathlib.Path) -> pathlib.Path:
    state_dir = game_dir / STATE_DIR_NAME
    if state_dir.is_symlink() or (state_dir.exists() and not state_dir.is_dir()):
        raise ValueError(f"unsafe installer state directory: {state_dir}")
    return state_dir


def state_file(state_dir: pathlib.Path) -> pathlib.Path:
    path = state_dir / STATE_FILE_NAME
    if path.is_symlink():
        raise ValueError(f"unsafe installer state file: {path}")
    return path


def atomic_json(path: pathlib.Path, value: dict[str, object]) -> None:
    if path.parent.is_symlink() or not path.parent.is_dir():
        raise ValueError(f"unsafe installer state directory: {path.parent}")
    descriptor, temporary = tempfile.mkstemp(prefix=".state-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            json.dump(value, handle, sort_keys=True, separators=(",", ":"))
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.chmod(temporary, 0o600)
        fsync_file(pathlib.Path(temporary))
        os.replace(temporary, path)
        fsync_directory(path.parent)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def target_for(
    game_dir: pathlib.Path,
    compatdata: pathlib.Path,
    target_root: str,
    relative: pathlib.PurePosixPath,
) -> pathlib.Path:
    if not relative.parts:
        raise ValueError("empty transaction target path")
    if target_root == "game":
        if relative.parts[0] == STATE_DIR_NAME:
            raise ValueError("handoff must not write its transaction state directory")
        if relative.as_posix().casefold() in {"skyrimvr.exe", "openvr_api.dll"}:
            raise ValueError(f"handoff must not replace {relative.name}")
        return safe_target_path(game_dir, relative)
    if target_root == "compatdata":
        return safe_target_path(compatdata, relative)
    raise ValueError(f"invalid transaction target root: {target_root}")


def planned_operations(root: pathlib.Path, package: pathlib.Path) -> tuple[list[InstallOperation], tuple[int, int, int, int, int]]:
    overlay, _skipped_root_files = collect_overlay(root)
    gameplay = collect_gameplay_package(package)
    profiles = collect_profiles(root)
    # Later values replace earlier ones for identical destinations.  In
    # particular, canonical portable launchers replace source-machine copies
    # from current-game-overlay or the gameplay package.
    selected: dict[tuple[str, str], InstallOperation] = {}
    runtimes = collect_openvr_runtimes(root)
    for operation in [*overlay, *gameplay, *profiles, *collect_canonical_launchers(root), *runtimes]:
        key = (operation.target_root, operation.relative.as_posix())
        if key in selected:
            del selected[key]
        selected[key] = operation
    return list(selected.values()), (len(overlay), len(gameplay), len(profiles), len(LAUNCHERS), len(runtimes))


def fsync_directory(path: pathlib.Path) -> None:
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def fsync_file(path: pathlib.Path) -> None:
    descriptor = os.open(path, os.O_RDONLY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def file_identity(path: pathlib.Path) -> tuple[str, int] | None:
    try:
        before = os.lstat(path)
    except FileNotFoundError:
        return None
    if not stat.S_ISREG(before.st_mode):
        raise ValueError(f"transaction target is not a regular file: {path}")
    digest = sha256(path)
    after = os.lstat(path)
    stable_fields = ("st_dev", "st_ino", "st_size", "st_mtime_ns", "st_ctime_ns", "st_mode")
    if any(getattr(before, field) != getattr(after, field) for field in stable_fields):
        raise ValueError(f"transaction target changed while it was being inspected: {path}")
    return digest, stat.S_IMODE(after.st_mode)


def write_operation(
    operation: InstallOperation,
    destination: pathlib.Path,
    *,
    pre_replace: Callable[[], None] | None = None,
) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".stvr-handoff-", dir=destination.parent)
    try:
        with os.fdopen(descriptor, "wb") as output:
            if isinstance(operation.source, pathlib.Path):
                with operation.source.open("rb") as source:
                    shutil.copyfileobj(source, output, length=1024 * 1024)
            else:
                package, member = operation.source
                with zipfile.ZipFile(package) as archive, archive.open(member) as source:
                    shutil.copyfileobj(source, output, length=1024 * 1024)
            output.flush()
            os.fsync(output.fileno())
        os.chmod(temporary, operation.mode)
        fsync_file(pathlib.Path(temporary))
        if file_identity(pathlib.Path(temporary)) != (operation.installed_hash, operation.mode):
            raise ValueError(f"prepared replacement does not match planned content or mode: {destination}")
        if pre_replace is not None:
            pre_replace()
        os.replace(temporary, destination)
        fsync_directory(destination.parent)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def load_state(state_dir: pathlib.Path) -> dict[str, object]:
    path = state_file(state_dir)
    if not path.is_file():
        raise ValueError(f"missing installer state: {path}")
    try:
        state = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid installer state: {error}") from error
    if not isinstance(state, dict) or state.get("schema") != STATE_SCHEMA:
        raise ValueError("incompatible installer state")
    return state


def valid_hash(value: object) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and not any(char not in "0123456789abcdef" for char in value)
    )


def validate_state(
    state: dict[str, object], game_dir: pathlib.Path, compatdata: pathlib.Path
) -> list[dict[str, object]]:
    targets = state.get("targets")
    records = state.get("records")
    if (
        set(state) != {"schema", "status", "handoff_id", "targets", "records"}
        or not isinstance(targets, dict)
        or targets != {"game": str(game_dir), "compatdata": str(compatdata)}
        or not isinstance(records, list)
        or not isinstance(state.get("handoff_id"), str)
        or not state.get("handoff_id")
        or state.get("status") not in {"prepared", "installing", "installed", "uninstalling"}
    ):
        raise ValueError("installer state belongs to incompatible targets")
    validated: list[dict[str, object]] = []
    seen: set[tuple[str, str]] = set()
    for record in records:
        if not isinstance(record, dict):
            raise ValueError("installer state has a malformed record")
        target_root = record.get("root")
        relative_text = record.get("path")
        installed_hash = record.get("installed_sha256")
        installed_mode = record.get("installed_mode")
        created = record.get("created")
        applied = record.get("applied")
        applying = record.get("applying")
        uninstalled = record.get("uninstalled")
        if (
            target_root not in {"game", "compatdata"}
            or not isinstance(relative_text, str)
            or not valid_hash(installed_hash)
            or not isinstance(installed_mode, int)
            or isinstance(installed_mode, bool)
            or not 0 <= installed_mode <= 0o777
            or not isinstance(created, bool)
            or not isinstance(applied, bool)
            or not isinstance(applying, bool)
            or not isinstance(uninstalled, bool)
        ):
            raise ValueError("installer state has a malformed record")
        relative = pathlib.PurePosixPath(relative_text)
        target_for(game_dir, compatdata, target_root, relative)
        key = (target_root, relative_text)
        if key in seen:
            raise ValueError("installer state duplicates a target")
        seen.add(key)
        if created:
            expected_keys = {
                "root", "path", "installed_sha256", "installed_mode", "created",
                "applied", "applying", "uninstalled",
            }
            if set(record) != expected_keys:
                raise ValueError("installer state has an invalid created-file record")
        else:
            expected_keys = {
                "root", "path", "installed_sha256", "installed_mode", "created",
                "applied", "applying", "uninstalled", "backup", "original_sha256", "original_mode",
            }
            if (
                set(record) != expected_keys
                or not isinstance(record.get("backup"), str)
                or not valid_hash(record.get("original_sha256"))
                or not isinstance(record.get("original_mode"), int)
                or isinstance(record.get("original_mode"), bool)
                or not 0 <= int(record["original_mode"]) <= 0o777
            ):
                raise ValueError("installer state has an invalid overwritten-file record")
        if (record["applied"] and record["applying"]) or (record["uninstalled"] and record["applying"]):
            raise ValueError("installer state has contradictory record flags")
        validated.append(record)
    backup_names = [str(record["backup"]) for record in validated if not record["created"]]
    if len(backup_names) != len(set(backup_names)):
        raise ValueError("installer state duplicates a backup")
    if state["status"] == "prepared" and any(
        record["applied"] or record["applying"] or record["uninstalled"] for record in validated
    ):
        raise ValueError("prepared installer state has progressed record flags")
    if state["status"] == "installed" and any(
        not record["applied"] or record["applying"] or record["uninstalled"] for record in validated
    ):
        raise ValueError("installed transaction state is incomplete")
    return validated


def backup_path(state_dir: pathlib.Path, name: str) -> pathlib.Path:
    if not name or pathlib.PurePosixPath(name).name != name:
        raise ValueError("unsafe backup name in installer state")
    path = state_dir / "backups" / name
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"missing or unsafe installer backup: {path}")
    return path


def remove_stale_state_temps(state_dir: pathlib.Path) -> None:
    for directory, prefix in ((state_dir, ".state-"), (state_dir / "backups", ".backup-")):
        if not directory.exists():
            continue
        if directory.is_symlink() or not directory.is_dir():
            raise ValueError(f"unsafe installer state directory: {directory}")
        for entry in directory.iterdir():
            if not entry.name.startswith(prefix):
                continue
            if entry.is_symlink() or not entry.is_file():
                raise ValueError(f"unsafe stale installer temporary file: {entry}")
            entry.unlink()


def verify_state_layout(
    state_dir: pathlib.Path,
    records: list[dict[str, object]],
    *,
    complete_backups: bool,
    allow_preparing_marker: bool = False,
) -> None:
    remove_stale_state_temps(state_dir)
    root_entries = {entry.name for entry in state_dir.iterdir()}
    allowed_entries = {STATE_FILE_NAME, "backups"}
    if allow_preparing_marker:
        allowed_entries.add(PREPARING_FILE_NAME)
    if root_entries - allowed_entries:
        raise ValueError("installer state directory contains unexpected files")
    if STATE_FILE_NAME not in root_entries or "backups" not in root_entries:
        raise ValueError("installer state layout is incomplete")
    if stat.S_IMODE(os.lstat(state_dir).st_mode) != 0o700:
        raise ValueError("installer state directory mode is unsafe")
    state_path = state_file(state_dir)
    if file_identity(state_path) is None or stat.S_IMODE(os.lstat(state_path).st_mode) != 0o600:
        raise ValueError("installer state file mode is unsafe")
    backups = state_dir / "backups"
    if backups.is_symlink() or not backups.is_dir() or stat.S_IMODE(os.lstat(backups).st_mode) != 0o700:
        raise ValueError("installer backup directory is unsafe")
    if PREPARING_FILE_NAME in root_entries:
        marker = state_dir / PREPARING_FILE_NAME
        if file_identity(marker) is None or stat.S_IMODE(os.lstat(marker).st_mode) != 0o600:
            raise ValueError("installer preparation marker is unsafe")
    expected = {str(record["backup"]) for record in records if not record["created"]}
    actual = {entry.name for entry in backups.iterdir()}
    if not actual <= expected or complete_backups and actual != expected:
        raise ValueError("installer backup directory does not match state")
    for name in actual:
        backup_path(state_dir, name)


def recover_unpublished_state(state_dir: pathlib.Path) -> bool:
    """Remove only a provably pre-publication state directory."""

    remove_stale_state_temps(state_dir)
    entries = {entry.name for entry in state_dir.iterdir()}
    if STATE_FILE_NAME in entries:
        return False
    if entries and PREPARING_FILE_NAME not in entries:
        raise ValueError("installer state is missing state.json and cannot be safely recovered")
    if entries - {PREPARING_FILE_NAME, "backups"}:
        raise ValueError("unpublished installer state contains unexpected files")
    backups = state_dir / "backups"
    if backups.exists():
        if backups.is_symlink() or not backups.is_dir():
            raise ValueError("unsafe unpublished installer backup directory")
        for entry in backups.iterdir():
            if entry.is_symlink() or not entry.is_file() or not entry.name.isdecimal():
                raise ValueError("unpublished installer state contains unsafe backups")
            entry.unlink()
        backups.rmdir()
    marker = state_dir / PREPARING_FILE_NAME
    if marker.exists():
        if marker.is_symlink() or not marker.is_file():
            raise ValueError("unsafe installer preparation marker")
        marker.unlink()
    state_dir.rmdir()
    return True


def atomic_backup(
    source: pathlib.Path,
    destination: pathlib.Path,
    expected_hash: str,
    expected_mode: int,
) -> None:
    expected = (expected_hash, expected_mode)
    if file_identity(source) != expected:
        raise ValueError(f"target changed before its backup was secured: {source}")
    descriptor, temporary = tempfile.mkstemp(prefix=".backup-", dir=destination.parent)
    try:
        with os.fdopen(descriptor, "wb") as output, source.open("rb") as input_file:
            shutil.copyfileobj(input_file, output, length=1024 * 1024)
            output.flush()
            os.fsync(output.fileno())
        os.chmod(temporary, expected_mode)
        fsync_file(pathlib.Path(temporary))
        if file_identity(pathlib.Path(temporary)) != expected or file_identity(source) != expected:
            raise ValueError(f"target changed while its backup was being secured: {source}")
        os.replace(temporary, destination)
        fsync_directory(destination.parent)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    if file_identity(destination) != expected:
        raise ValueError(f"backup verification failed: {destination}")


def original_identity(record: dict[str, object]) -> tuple[str, int] | None:
    if record["created"]:
        return None
    return str(record["original_sha256"]), int(record["original_mode"])


def installed_identity(record: dict[str, object]) -> tuple[str, int]:
    return str(record["installed_sha256"]), int(record["installed_mode"])


def require_identity(path: pathlib.Path, expected: tuple[str, int] | None, message: str) -> None:
    if file_identity(path) != expected:
        raise ValueError(f"{message}: {path}")


def require_record_identity(
    game_dir: pathlib.Path,
    compatdata: pathlib.Path,
    record: dict[str, object],
    destination: pathlib.Path,
    expected: tuple[str, int] | None,
    message: str,
) -> None:
    refreshed = target_for(game_dir, compatdata, str(record["root"]), pathlib.PurePosixPath(str(record["path"])))
    if refreshed != destination:
        raise ValueError(f"transaction target changed location: {destination}")
    require_identity(refreshed, expected, message)


def verify_backup(state_dir: pathlib.Path, record: dict[str, object]) -> pathlib.Path:
    backup = backup_path(state_dir, str(record["backup"]))
    require_identity(backup, original_identity(record), "installer backup does not match state")
    return backup


def records_match_operations(records: list[dict[str, object]], operations: list[InstallOperation]) -> bool:
    expected = {
        (operation.target_root, operation.relative.as_posix()): (operation.installed_hash, operation.mode)
        for operation in operations
    }
    actual = {
        (record["root"], record["path"]): installed_identity(record)
        for record in records
    }
    return actual == expected


def state_matches_install(
    state: dict[str, object], records: list[dict[str, object]], operations: list[InstallOperation],
    game_dir: pathlib.Path, compatdata: pathlib.Path, handoff_id: str,
) -> bool:
    if state.get("status") != "installed" or state.get("handoff_id") != handoff_id:
        return False
    if not records_match_operations(records, operations) or any(
        not record["applied"] or record["applying"] or record["uninstalled"] for record in records
    ):
        return False
    verify_state_layout(state_directory(game_dir), records, complete_backups=True)
    for record in records:
        destination = target_for(game_dir, compatdata, record["root"], pathlib.PurePosixPath(record["path"]))
        if file_identity(destination) != installed_identity(record):
            return False
        if not record["created"]:
            verify_backup(state_directory(game_dir), record)
    return True


def create_prepared_state(
    state_dir: pathlib.Path,
    state: dict[str, object],
) -> None:
    state_dir.mkdir(mode=0o700)
    marker = state_dir / PREPARING_FILE_NAME
    descriptor = os.open(marker, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        os.write(descriptor, b"STVR transaction preparation\n")
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
    (state_dir / "backups").mkdir(mode=0o700)
    fsync_directory(state_dir)
    atomic_json(state_file(state_dir), state)
    marker.unlink()
    fsync_directory(state_dir)


def install_transaction(
    operations: list[InstallOperation], game_dir: pathlib.Path, compatdata: pathlib.Path, handoff_id: str,
) -> bool:
    """Install once, returning False when an identical complete install exists."""

    destinations = [target_for(game_dir, compatdata, op.target_root, op.relative) for op in operations]
    state_dir = state_directory(game_dir)
    state: dict[str, object]
    records: list[dict[str, object]]
    if state_dir.exists() or state_dir.is_symlink():
        if recover_unpublished_state(state_dir):
            return install_transaction(operations, game_dir, compatdata, handoff_id)
        state = load_state(state_dir)
        records = validate_state(state, game_dir, compatdata)
        verify_state_layout(
            state_dir,
            records,
            complete_backups=state["status"] != "prepared",
            allow_preparing_marker=state["status"] == "prepared",
        )
        if state_matches_install(state, records, operations, game_dir, compatdata, handoff_id):
            return False
        if (
            state.get("handoff_id") != handoff_id
            or not records_match_operations(records, operations)
            or state["status"] not in {"prepared", "installing"}
        ):
            raise ValueError("an incompatible STVR install transaction is active; uninstall it first")
    else:
        records = []
        for index, (operation, destination) in enumerate(zip(operations, destinations, strict=True)):
            identity = file_identity(destination)
            record: dict[str, object] = {
                "root": operation.target_root,
                "path": operation.relative.as_posix(),
                "installed_sha256": operation.installed_hash,
                "installed_mode": operation.mode,
                "created": identity is None,
                "applied": False,
                "applying": False,
                "uninstalled": False,
            }
            if identity is not None:
                record.update({
                    "backup": f"{index:04d}",
                    "original_sha256": identity[0],
                    "original_mode": identity[1],
                })
            records.append(record)
        state = {
            "schema": STATE_SCHEMA,
            "status": "prepared",
            "handoff_id": handoff_id,
            "targets": {"game": str(game_dir), "compatdata": str(compatdata)},
            "records": records,
        }
        create_prepared_state(state_dir, state)

    marker = state_dir / PREPARING_FILE_NAME
    if marker.exists():
        marker.unlink()
        fsync_directory(state_dir)
    backups = state_dir / "backups"
    for record, destination in zip(records, destinations, strict=True):
        if not record["created"]:
            backup = backups / str(record["backup"])
            expected = original_identity(record)
            if backup.exists() and file_identity(backup) == expected:
                if state["status"] == "prepared":
                    require_identity(destination, expected, "target changed after transaction snapshot")
            else:
                atomic_backup(destination, backup, str(record["original_sha256"]), int(record["original_mode"]))
            verify_backup(state_dir, record)
    verify_state_layout(state_dir, records, complete_backups=True)
    if state["status"] == "prepared":
        state["status"] = "installing"
        atomic_json(state_file(state_dir), state)
    for record, operation, destination in zip(records, operations, destinations, strict=True):
        current = file_identity(destination)
        if record["applied"]:
            require_identity(destination, installed_identity(record), "installed target changed during recovery")
            continue
        if record["applying"]:
            if current == installed_identity(record):
                record["applied"] = True
                record["applying"] = False
                atomic_json(state_file(state_dir), state)
                continue
            require_identity(destination, original_identity(record), "target is ambiguous after interrupted replacement")
            record["applying"] = False
            atomic_json(state_file(state_dir), state)
        else:
            require_identity(destination, original_identity(record), "target changed after transaction snapshot")
        if not record["created"]:
            verify_backup(state_dir, record)
        record["applying"] = True
        atomic_json(state_file(state_dir), state)
        write_operation(
            operation,
            destination,
            pre_replace=lambda record=record, destination=destination: require_record_identity(
                game_dir, compatdata, record, destination, original_identity(record),
                "target changed immediately before install replacement",
            ),
        )
        require_identity(destination, installed_identity(record), "installed file does not match planned identity")
        record["applied"] = True
        record["applying"] = False
        atomic_json(state_file(state_dir), state)
    state["status"] = "installed"
    atomic_json(state_file(state_dir), state)
    return True


def cleanup_state(state_dir: pathlib.Path, records: list[dict[str, object]], *, partial_backups: bool = False) -> None:
    remove_stale_state_temps(state_dir)
    backups = state_dir / "backups"
    allowed_backups = {str(record["backup"]) for record in records if not record["created"]}
    if backups.exists() or backups.is_symlink():
        if backups.is_symlink() or not backups.is_dir():
            raise ValueError("unsafe installer backup directory")
        actual_backups = {entry.name for entry in backups.iterdir()}
        if not actual_backups <= allowed_backups or not partial_backups and actual_backups != allowed_backups:
            raise ValueError("installer backup directory contains unexpected files")
        for name in actual_backups:
            backup_path(state_dir, name).unlink()
        backups.rmdir()
    elif not partial_backups:
        raise ValueError("missing installer backup directory")
    marker = state_dir / PREPARING_FILE_NAME
    if marker.exists():
        marker.unlink()
    state_file(state_dir).unlink()
    state_dir.rmdir()


def remove_created_target(destination: pathlib.Path, *, pre_remove: Callable[[], None]) -> None:
    pre_remove()
    destination.unlink()
    fsync_directory(destination.parent)


def uninstall_transaction(game_dir: pathlib.Path, compatdata: pathlib.Path, *, force: bool) -> int:
    state_dir = state_directory(game_dir)
    if not state_dir.exists():
        raise ValueError("no STVR install transaction exists for these targets")
    if recover_unpublished_state(state_dir):
        return 0
    state = load_state(state_dir)
    records = validate_state(state, game_dir, compatdata)
    if state["status"] == "uninstalling" and all(record["uninstalled"] for record in records):
        cleanup_state(state_dir, records, partial_backups=True)
        return 0
    verify_state_layout(
        state_dir,
        records,
        complete_backups=state["status"] != "prepared",
        allow_preparing_marker=state["status"] == "prepared",
    )
    pending: list[dict[str, object]] = []
    mismatches: list[str] = []
    for record in records:
        destination = target_for(game_dir, compatdata, record["root"], pathlib.PurePosixPath(record["path"]))
        current = file_identity(destination)
        if record["uninstalled"]:
            continue
        if not (record["applied"] or record["applying"]):
            record["uninstalled"] = True
            continue
        interrupted_uninstall = state["status"] == "uninstalling" and current == original_identity(record)
        interrupted_install = (
            state["status"] == "installing"
            and record["applying"]
            and current == original_identity(record)
        )
        if interrupted_uninstall or interrupted_install:
            record["applying"] = False
            record["uninstalled"] = True
            continue
        pending.append(record)
        if current != installed_identity(record):
            mismatches.append(str(destination))
    for record in pending:
        if not record["created"]:
            verify_backup(state_dir, record)
    if mismatches and not force:
        raise ValueError(
            "refusing to uninstall user-modified or missing STVR targets; rerun with --uninstall --force: "
            + ", ".join(mismatches[:3])
        )
    if not pending:
        cleanup_state(state_dir, records, partial_backups=state["status"] == "prepared")
        return 0

    state["status"] = "uninstalling"
    atomic_json(state_file(state_dir), state)
    for record in pending:
        destination = target_for(game_dir, compatdata, record["root"], pathlib.PurePosixPath(record["path"]))
        if record["created"]:
            if force:
                current = file_identity(destination)
                if current is not None:
                    remove_created_target(
                        destination,
                        pre_remove=lambda record=record, destination=destination: require_record_identity(
                            game_dir, compatdata, record, destination, file_identity(destination),
                            "forced uninstall target changed location",
                        ),
                    )
            else:
                remove_created_target(
                    destination,
                    pre_remove=lambda record=record, destination=destination: require_record_identity(
                        game_dir, compatdata, record, destination, installed_identity(record),
                        "target changed immediately before non-force uninstall removal",
                    ),
                )
        else:
            backup = verify_backup(state_dir, record)
            replacement = InstallOperation(
                record["root"], pathlib.PurePosixPath(record["path"]), backup,
                str(record["original_sha256"]), int(record["original_mode"]),
            )
            write_operation(
                replacement,
                destination,
                pre_replace=(
                    (lambda record=record, destination=destination: require_record_identity(
                        game_dir, compatdata, record, destination, file_identity(destination),
                        "forced uninstall target changed location",
                    ))
                    if force
                    else lambda record=record, destination=destination: require_record_identity(
                        game_dir, compatdata, record, destination, installed_identity(record),
                        "target changed immediately before non-force uninstall restore",
                    )
                ),
            )
        require_identity(destination, original_identity(record), "uninstall mutation did not reach restored state")
        record["applying"] = False
        record["uninstalled"] = True
        atomic_json(state_file(state_dir), state)
    cleanup_state(state_dir, records)
    return len(pending)


def self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="stvr-installer-self-test-") as temp_dir:
        package = pathlib.Path(temp_dir) / "payload.zip"
        with zipfile.ZipFile(package, "w") as archive:
            archive.writestr("../escape", "no")
        with zipfile.ZipFile(package) as archive:
            try:
                safe_zip_members(archive)
            except ValueError:
                pass
            else:
                raise AssertionError("unsafe ZIP traversal was accepted")
        with zipfile.ZipFile(package, "w") as archive:
            link = zipfile.ZipInfo("package/link")
            link.external_attr = 0o120777 << 16
            archive.writestr(link, "target")
        with zipfile.ZipFile(package) as archive:
            try:
                safe_zip_members(archive)
            except ValueError:
                pass
            else:
                raise AssertionError("unsafe ZIP symlink was accepted")
        with zipfile.ZipFile(package, "w") as archive:
            archive.writestr("package/ok.txt", "ok")
        with zipfile.ZipFile(package) as archive:
            assert list(safe_zip_members(archive)) == ["package/ok.txt"]
        game_dir = pathlib.Path(temp_dir) / "game"
        game_dir.mkdir()
        assert len(collect_gameplay_package(package)) == 1
        assert not (game_dir / "ok.txt").exists(), "dry-run wrote a package file"
        overlay = pathlib.Path(temp_dir) / "handoff" / "dependencies/current-game-overlay"
        (overlay / "Data/SKSE").mkdir(parents=True)
        (overlay / "SkyrimVR.exe").write_bytes(b"preserve")
        (overlay / "openvr_api.dll").write_bytes(b"stock-valve-openvr")
        (overlay / "Data/SKSE/Plugins.txt").write_text("portable", encoding="ascii")
        handoff_root = overlay.parents[1]
        assert len(collect_overlay(handoff_root)[0]) == 1
        assert not (game_dir / "Data/SKSE/Plugins.txt").exists(), "planning wrote an overlay file"
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
        with zipfile.ZipFile(package, "w") as archive:
            archive.writestr("package/SkyrimVR.exe", "forbidden")
        try:
            collect_gameplay_package(package)
        except ValueError:
            pass
        else:
            raise AssertionError("gameplay package replacing SkyrimVR.exe was accepted")
        with zipfile.ZipFile(package, "w") as archive:
            archive.writestr("package/openvr_api.dll", "forbidden")
        try:
            collect_gameplay_package(package)
        except ValueError:
            pass
        else:
            raise AssertionError("gameplay package replacing openvr_api.dll was accepted")

        with zipfile.ZipFile(package, "w") as archive:
            archive.writestr("package/Data/gameplay.txt", "gameplay")
            archive.writestr("package/launch-skyrim-together-vr.sh", "source-machine-package")
        for name in LAUNCHERS:
            portable = handoff_root / "source/Tools/SkyrimVR/linux" / name
            portable.parent.mkdir(parents=True, exist_ok=True)
            portable.write_text(f"portable-{name}\n", encoding="ascii")
            portable.chmod(0o644)
            (overlay / name).write_text(f"source-machine-{name}\n", encoding="ascii")
        xrizer_root = handoff_root / "dependencies/xrizer-runtime/libxrizer.so"
        xrizer = handoff_root / "dependencies/xrizer-runtime/bin/linux64/vrclient.so"
        xrizer.parent.mkdir(parents=True)
        xrizer_root.write_bytes(b"xrizer-runtime")
        xrizer.write_bytes(b"xrizer-runtime")
        xrizer_root.chmod(0o755)
        xrizer.chmod(0o755)
        opencomposite = handoff_root / "dependencies/opencomposite-runtime/bin/linux64/vrclient.so"
        opencomposite.parent.mkdir(parents=True)
        opencomposite.write_bytes(b"opencomposite-runtime")
        opencomposite.chmod(0o755)
        openvrpaths = handoff_root / "dependencies/openvrpaths.vrpath"
        openvrpaths.write_text('{"runtime": [], "version": 1}\n', encoding="utf-8")
        profiles = handoff_root / "profiles/direct-proton"
        profiles.mkdir(parents=True)
        for name in PROFILE_PATHS:
            (profiles / name).write_text(f"new-{name}\n", encoding="ascii")
        compatdata = pathlib.Path(temp_dir) / "compatdata"
        compatdata.mkdir()
        original_profile = safe_target_path(compatdata, PROFILE_PATHS["Plugins.txt"])
        original_profile.parent.mkdir(parents=True)
        original_profile.write_text("original-profile\n", encoding="ascii")
        original_profile.chmod(0o640)
        (game_dir / "SkyrimVR.exe").write_bytes(b"legal-game-remains")
        user_openvr_api = game_dir / "openvr_api.dll"
        user_openvr_api.write_bytes(b"user-opencomposite-runtime")
        user_openvr_api.chmod(0o640)
        operations, counts = planned_operations(handoff_root, package)
        assert counts == (5, 2, 3, 4, 4)
        assert not (game_dir / "Data/gameplay.txt").exists(), "dry-run planning mutated game files"
        assert install_transaction(operations, game_dir, compatdata, "self-test")
        for name in LAUNCHERS:
            launcher = game_dir / name
            assert launcher.read_text(encoding="ascii") == f"portable-{name}\n"
            assert launcher.stat().st_mode & stat.S_IXUSR
        assert (game_dir / ".stvr-openvr/xrizer/libxrizer.so").read_bytes() == b"xrizer-runtime"
        assert (game_dir / ".stvr-openvr/xrizer/bin/linux64/vrclient.so").read_bytes() == b"xrizer-runtime"
        assert (game_dir / ".stvr-openvr/opencomposite/bin/linux64/vrclient.so").read_bytes() == b"opencomposite-runtime"
        assert (game_dir / ".stvr-openvr/openvrpaths.vrpath").read_text(encoding="utf-8") == '{"runtime": [], "version": 1}\n'
        assert (game_dir / ".stvr-openvr/xrizer/libxrizer.so").stat().st_mode & stat.S_IXUSR
        assert (game_dir / ".stvr-openvr/xrizer/bin/linux64/vrclient.so").stat().st_mode & stat.S_IXUSR
        assert (game_dir / "SkyrimVR.exe").read_bytes() == b"legal-game-remains"
        assert user_openvr_api.read_bytes() == b"user-opencomposite-runtime"
        assert stat.S_IMODE(user_openvr_api.stat().st_mode) == 0o640
        assert not install_transaction(operations, game_dir, compatdata, "self-test"), "same install was not idempotent"
        assert uninstall_transaction(game_dir, compatdata, force=False) > 0
        assert not (game_dir / "Data/gameplay.txt").exists()
        assert not (game_dir / LAUNCHERS[0]).exists()
        assert not (game_dir / ".stvr-openvr/xrizer/libxrizer.so").exists()
        assert not (game_dir / ".stvr-openvr/xrizer/bin/linux64/vrclient.so").exists()
        assert not (game_dir / ".stvr-openvr/opencomposite/bin/linux64/vrclient.so").exists()
        assert not (game_dir / ".stvr-openvr/openvrpaths.vrpath").exists()
        assert user_openvr_api.read_bytes() == b"user-opencomposite-runtime"
        assert stat.S_IMODE(user_openvr_api.stat().st_mode) == 0o640
        assert original_profile.read_text(encoding="ascii") == "original-profile\n"
        assert stat.S_IMODE(original_profile.stat().st_mode) == 0o640
        assert not state_directory(game_dir).exists()

        assert install_transaction(operations, game_dir, compatdata, "self-test")
        modified = game_dir / "Data/SKSE/Plugins.txt"
        modified.write_text("user edit\n", encoding="ascii")
        try:
            uninstall_transaction(game_dir, compatdata, force=False)
        except ValueError:
            pass
        else:
            raise AssertionError("uninstall accepted a user-modified target")
        assert modified.exists(), "modified file was removed without --force"
        uninstall_transaction(game_dir, compatdata, force=True)
        assert not modified.exists(), "forced uninstall did not remove created target"
        try:
            target_for(game_dir, compatdata, "game", pathlib.PurePosixPath(STATE_DIR_NAME) / "escape")
        except ValueError:
            pass
        else:
            raise AssertionError("transaction state path was accepted as a target")
        outside = pathlib.Path(temp_dir) / "outside"
        outside.mkdir()
        (game_dir / "linked").symlink_to(outside, target_is_directory=True)
        try:
            safe_target_path(game_dir, pathlib.PurePosixPath("linked") / "escape")
        except ValueError:
            pass
        else:
            raise AssertionError("symlinked target directory was accepted")

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
    mode.add_argument("--uninstall", action="store_true", help="remove only unchanged files recorded by this STVR install")
    mode.add_argument("--dry-run", action="store_true", help="compatibility alias for the default validation-only mode")
    parser.add_argument("--force", action="store_true", help="with --uninstall, restore/remove even if recorded targets changed")
    parser.add_argument("--self-test", action="store_true", help="run focused ZIP path-safety checks")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        return self_test()
    if args.game_dir is None or args.compatdata is None:
        raise ValueError("--game-dir and --compatdata are required")
    if args.force and not args.uninstall:
        raise ValueError("--force is only valid with --uninstall")
    root = pathlib.Path(__file__).resolve().parent
    game_dir = args.game_dir.expanduser().absolute()
    compatdata = args.compatdata.expanduser().absolute()
    if game_dir.is_symlink() or compatdata.is_symlink() or not game_dir.is_dir() or not compatdata.is_dir():
        raise ValueError("--game-dir and --compatdata must name existing non-symlink directories")
    game_exe = game_dir / "SkyrimVR.exe"
    if not game_exe.is_file() or game_exe.is_symlink():
        raise ValueError(f"--game-dir does not contain a regular SkyrimVR.exe: {game_exe}")
    if args.uninstall:
        removed = uninstall_transaction(game_dir, compatdata, force=args.force)
        print(f"uninstalled {removed} STVR-managed files; SkyrimVR.exe was preserved")
        return 0
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

    operations, (overlay_count, package_count, profile_count, launcher_count, runtime_count) = planned_operations(root, package)
    # Dry runs still prove every planned target is inside a safe target root,
    # but never create state, directories, backups, or target files.
    for operation in operations:
        target_for(game_dir, compatdata, operation.target_root, operation.relative)
    if args.install:
        changed = install_transaction(operations, game_dir, compatdata, sha256(root / "LOCAL-MANIFEST.json"))
        action = "installed" if changed else "already installed (identical transaction)"
    else:
        action = "validated (dry run; no target files changed)"
    print(
        f"{action}: {overlay_count} overlay files, {package_count} gameplay files, "
        f"{profile_count} Proton profile files, {launcher_count} launchers, {runtime_count} OpenVR runtime files; "
        f"build {build_revision[:8]}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print(f"install failed: {error}", file=sys.stderr)
        raise SystemExit(1)
