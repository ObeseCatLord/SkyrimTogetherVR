#!/usr/bin/env python3
"""Create the private machine-local Skyrim Together VR agent handoff."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import struct
import subprocess
import tempfile
import zipfile
from datetime import datetime, timezone

from local_handoff_artifacts import validate_artifact_pair


MOD_NAMES = (
    "SKSE files",
    "VR Address Library for SKSEVR",
    "HIGGS - Hand Interaction and Gravity Gloves for Skyrim VR",
    "PLANCK - Physical Animation and Character Kinetics",
    "VRIK Player Avatar",
    "Arctal's VRIK Tweaks",
    "Neutral VR Animations for VRIK and PCEA2",
    "Controller Fix VR",
    "Realm of Lorkhan - Freeform Alternate Start",
    "Skyrim VR Tools",
    "SkyUI (REQUIRES Skyrim VR Tools!)",
    "OpenComposite_SkyUIVR_Fix_No_Search",
    "VRIK Default Controller Bindings",
    "Controller Bindings - Kvite",
)

ROOT_GAME_FILES = (
    "SkyrimVR.exe",
    "SkyrimSE.exe",
    "sksevr_loader.exe",
    "sksevr_1_4_15.dll",
    "sksevr_steam_loader.dll",
    "openvr_api.dll",
    "SkyrimTogetherVR_BuildManifest.json",
)

DOWNLOAD_PATTERNS = (
    "HIGGS 1.10.10-*.zip",
    "PLANCK 0.8.0*.zip",
    "SkyrimVR FBT 185070*.zip",
    "SkyrimVR FBT Source 185070*.zip",
    "SkyrimVRTools-27782-*.zip",
    "ClibDT-154240-*.zip",
    "ClibDT (EXE)-154240-*.zip",
    "ClibDT (SOURCE)-154240-*.zip",
    "Visual Studio Import-154240-*.zip",
)

REFERENCE_SOURCES = {
    "devbench": "devbench",
    "SkyrimVRTools": "_refs/SkyrimVRTools",
    "VRCustomQuickslots": "_refs/VRCustomQuickslots",
    "PLANCK-activeragdoll": "_refs/activeragdoll",
    "HIGGS": "_refs/higgs",
    "CommonLibSSE-NG": "_refs/CommonLibSSE-NG",
    "CommonLibSSE-legacy": "_refs/CommonLibSSE-old",
    "CommonLibSSE-sample-plugin": "_refs/commonlibsse-sample-plugin",
    "SKSE-Menu-Framework-3": "_refs/SKSE-Menu-Framework-3",
    "PapyrusTweaks": "_refs/PapyrusTweaks",
}

EXTERNAL_REVIEW_NOTES = {
    "REVIEW-AGENT-HANDOFF.md": "REVIEW-AGENT-HANDOFF.md",
    "REVIEW-FRIEND-SUMMARY.md": "REVIEW-FRIEND-SUMMARY.md",
    "deep-research-report-1.md": "Backup/Downloads/deep-research-report(1).md",
}

EXCLUDED_DIRS = {
    ".git",
    "artifacts",
    "build",
    "target",
    "__pycache__",
    ".vs",
    ".vscode",
    "node_modules",
    "stvr-backups",
    "codex-disabled-connection-test",
    "codex-disabled-menu-mouse-fix",
}
EXCLUDED_SUFFIXES = {".pdb", ".lib", ".exp", ".log", ".dmp", ".pyc", ".obj", ".o"}
CORE_DATA_NAMES = {
    "skyrim.esm",
    "skyrimvr.esm",
    "update.esm",
    "dawnguard.esm",
    "hearthfires.esm",
    "dragonborn.esm",
}
RUNTIME_HANDOFF_DIR = pathlib.PurePosixPath("Data/SkyrimTogetherReborn")
OPENCOMPOSITE_RUNTIME_PATH = pathlib.PurePosixPath("build/bin/linux64/vrclient.so")
OPENCOMPOSITE_REVISION = "cff07db75c4823afe93ed7027b03d5f7bc86f164"
OPENCOMPOSITE_RUNTIME_SHA256 = "a703fdd1eaff092d28d91798b0ad1afb1611523da1456563dd51f2892b471751"
OPENCOMPOSITE_BUILD_PATCH_SHA256 = "529453480dc9ff838b9cabcb341109672fb961dc0676d4006d238c4662558a67"
OPENCOMPOSITE_BUILD_PATCH_STATUS = "applied-to-isolated-build-copy"
OPENCOMPOSITE_OFFICIAL_ORIGINS = frozenset(
    {
        "https://github.com/znixian/OpenOVR.git",
        "https://github.com/znixian/OpenOVR",
        "git@github.com:znixian/OpenOVR.git",
        "git@github.com:znixian/OpenOVR",
        "ssh://git@github.com/znixian/OpenOVR.git",
        "ssh://git@github.com/znixian/OpenOVR",
        "https://gitlab.com/znixian/OpenOVR.git",
        "https://gitlab.com/znixian/OpenOVR",
        "git@gitlab.com:znixian/OpenOVR.git",
        "git@gitlab.com:znixian/OpenOVR",
        "ssh://git@gitlab.com/znixian/OpenOVR.git",
        "ssh://git@gitlab.com/znixian/OpenOVR",
    }
)
XRIZER_RUNTIME_PATH = pathlib.PurePosixPath("target/release/libxrizer.so")
XRIZER_BASE_REVISION = "31319560c1bd0f1e5c16936a946bb1c7295dbfd9"
XRIZER_RUNTIME_SHA256 = "b278c4695f15bba7c554aaac5303520247cc8ab3bcae3f8b55e934e2b114ccaf"
XRIZER_COMPATIBILITY_PATCH_SHA256 = "64d837980afd29cc3d557f4326eee34a165f0bb49888c247fc2af36361990142"
XRIZER_COMPATIBILITY_PATCH_STATUS = "applied-worktree-patch"
XRIZER_OFFICIAL_ORIGINS = frozenset(
    {
        "https://github.com/Supreeeme/xrizer.git",
        "https://github.com/Supreeeme/xrizer",
        "git@github.com:Supreeeme/xrizer.git",
        "git@github.com:Supreeeme/xrizer",
        "ssh://git@github.com/Supreeeme/xrizer.git",
        "ssh://git@github.com/Supreeeme/xrizer",
    }
)
# Steam's Soldier runtime is based on Ubuntu 20.04; portable loaders must not
# require a newer glibc ABI than its GLIBC_2.31 baseline.
PORTABLE_GLIBC_MAX = (2, 31)
PORTABLE_NEEDED_LIBRARIES = frozenset(
    {
        "ld-linux-x86-64.so.2",
        "libOpenGL.so.0",
        "libGLU.so.1",
        "libGLX.so.0",
        "libGL.so.1",
        "libX11.so.6",
        "libc.so.6",
        "libdl.so.2",
        "libgcc_s.so.1",
        "libm.so.6",
        "libpthread.so.0",
        "libstdc++.so.6",
        "libvulkan.so.1",
    }
)
NEUTRAL_OPENVR_PATHS = {"version": 1, "runtime": []}


def run_git(repo: pathlib.Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo), *args],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip())
    return result.stdout


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_json_sha256(value: dict[str, object]) -> str:
    payload = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("ascii")
    return hashlib.sha256(payload).hexdigest()


def validate_runtime_evidence(
    path: pathlib.Path,
    expected_build_identity: dict[str, str],
) -> dict[str, object]:
    """Bind accepted gameplay-bootstrap evidence to the exact gameplay build manifest."""
    if not zipfile.is_zipfile(path):
        raise ValueError(f"runtime evidence is not a ZIP archive: {path}")
    with zipfile.ZipFile(path) as archive:
        bad_entry = archive.testzip()
        if bad_entry is not None:
            raise ValueError(f"runtime evidence CRC failure: {bad_entry}")
        names = {name.replace("\\", "/"): name for name in archive.namelist() if not name.endswith(("/", "\\"))}
        for required in ("manifest.json", "package/SkyrimTogetherVR_BuildManifest.json"):
            if required not in names:
                raise ValueError(f"runtime evidence is missing {required}")
        try:
            runtime_manifest = json.loads(archive.read(names["manifest.json"]).decode("utf-8-sig"))
            package_manifest = json.loads(
                archive.read(names["package/SkyrimTogetherVR_BuildManifest.json"]).decode("utf-8-sig")
            )
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ValueError(f"runtime evidence contains invalid JSON: {error}") from error
    if not isinstance(runtime_manifest, dict) or not isinstance(package_manifest, dict):
        raise ValueError("runtime evidence manifests must be JSON objects")
    if runtime_manifest.get("gameplayBootstrapAudit") is not True:
        raise ValueError("runtime evidence was not collected with --gameplay-bootstrap")
    if package_manifest.get("packageFlavor") != "gameplay" or package_manifest.get("gameplay") is not True:
        raise ValueError("runtime evidence package manifest is not gameplay")
    planck = package_manifest.get("patchedPlanckArtifact")
    if not isinstance(planck, dict) or planck.get("interface") != "interface002" or \
            planck.get("packagePath") != "Data/SKSE/Plugins/activeragdoll.dll":
        raise ValueError("runtime evidence package manifest is missing patched PLANCK interface002 provenance")
    source_revision = package_manifest.get("sourceRevision")
    expected_revision = expected_build_identity.get("sourceRevision")
    if source_revision != expected_revision:
        raise ValueError(
            f"runtime evidence source revision {source_revision!r} does not match gameplay build {expected_revision!r}"
        )
    build_manifest_sha256 = canonical_json_sha256(package_manifest)
    expected_manifest_sha256 = expected_build_identity.get("buildManifestSha256")
    if build_manifest_sha256 != expected_manifest_sha256:
        raise ValueError("runtime evidence build manifest does not match the gameplay package/build evidence")
    embedded_manifest = runtime_manifest.get("packageBuildManifest")
    if not isinstance(embedded_manifest, dict) or canonical_json_sha256(embedded_manifest) != build_manifest_sha256:
        raise ValueError("runtime evidence embedded package manifest does not match its packaged manifest")
    generated_at = str(package_manifest.get("generatedAtUtc", ""))
    if generated_at != expected_build_identity.get("generatedAtUtc"):
        raise ValueError("runtime evidence generated build identity does not match the gameplay artifacts")
    return {
        "gameplayBootstrapAudit": True,
        "packageFlavor": "gameplay",
        "sourceRevision": source_revision,
        "buildManifestSha256": build_manifest_sha256,
        "generatedAtUtc": generated_at,
    }


def newest(root: pathlib.Path, pattern: str) -> pathlib.Path:
    matches = [path for path in root.glob(pattern) if path.is_file()]
    if not matches:
        raise FileNotFoundError(f"no file matching {pattern} under {root}")
    return max(matches, key=lambda path: path.stat().st_mtime_ns)


def skip(path: pathlib.Path, root: pathlib.Path) -> bool:
    parts = {part.lower() for part in path.relative_to(root).parts}
    return bool(parts & EXCLUDED_DIRS) or path.suffix.lower() in EXCLUDED_SUFFIXES


def iter_tree(root: pathlib.Path, *, devbench: bool = False) -> list[pathlib.Path]:
    files: list[pathlib.Path] = []
    for path in root.rglob("*"):
        if not path.is_file() or skip(path, root):
            continue
        if devbench and "lib" in {part.lower() for part in path.relative_to(root).parts}:
            continue
        files.append(path)
    return sorted(files, key=lambda item: item.relative_to(root).as_posix())


def iter_game_overlay(game_dir: pathlib.Path) -> list[pathlib.Path]:
    files: list[pathlib.Path] = []
    for name in ROOT_GAME_FILES:
        path = game_dir / name
        if not path.is_file():
            raise FileNotFoundError(f"missing required game-root file: {path}")
        files.append(path)

    data = game_dir / "Data"
    for path in data.rglob("*"):
        if not path.is_file() or skip(path, data):
            continue
        rel = path.relative_to(data)
        # These files are generated by one Skyrim process and describe or
        # control that process's current session.  They are not installable
        # payload: the authoritative gameplay package supplies code/config,
        # while carrying these over would make a new client trust stale proof.
        if (
            pathlib.PurePosixPath("Data", *rel.parts).parent == RUNTIME_HANDOFF_DIR
            and path.name.startswith("SkyrimTogetherVR.")
        ):
            continue
        if len(rel.parts) == 1:
            lower = path.name.lower()
            if lower in CORE_DATA_NAMES or lower.endswith(".bsa") or lower.startswith("cc") or lower.startswith("_resourcepack"):
                continue
        files.append(path)
    return sorted(files, key=lambda item: item.as_posix())


class Writer:
    def __init__(self, archive: zipfile.ZipFile, timestamp: tuple[int, int, int, int, int, int]):
        self.archive = archive
        self.timestamp = timestamp
        self.records: list[dict[str, object]] = []
        self.names: set[str] = set()

    def add(self, source: pathlib.Path, name: str, *, executable: bool = False) -> None:
        name = name.replace(os.sep, "/")
        if name in self.names:
            raise ValueError(f"duplicate archive path: {name}")
        self.names.add(name)
        info = zipfile.ZipInfo(name, self.timestamp)
        info.create_system = 3
        info.external_attr = (0o100755 if executable or source.stat().st_mode & 0o111 else 0o100644) << 16
        info.compress_type = zipfile.ZIP_STORED if source.suffix.lower() == ".zip" else zipfile.ZIP_DEFLATED
        digest = hashlib.sha256()
        size = 0
        with source.open("rb") as inp, self.archive.open(info, "w") as out:
            for chunk in iter(lambda: inp.read(1024 * 1024), b""):
                out.write(chunk)
                digest.update(chunk)
                size += len(chunk)
        self.records.append({"path": name, "size": size, "sha256": digest.hexdigest()})

    def add_bytes(self, payload: bytes, name: str, *, record: bool = True) -> None:
        name = name.replace(os.sep, "/")
        if name in self.names:
            raise ValueError(f"duplicate archive path: {name}")
        self.names.add(name)
        info = zipfile.ZipInfo(name, self.timestamp)
        info.create_system = 3
        info.external_attr = 0o100644 << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        self.archive.writestr(info, payload)
        if record:
            self.records.append({"path": name, "size": len(payload), "sha256": hashlib.sha256(payload).hexdigest()})


def parse_args() -> argparse.Namespace:
    home = pathlib.Path.home()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=pathlib.Path, default=pathlib.Path.cwd())
    parser.add_argument("--game-dir", type=pathlib.Path, default=home / "FasterGames/SteamLibrary/steamapps/common/SkyrimVR")
    parser.add_argument("--compatdata", type=pathlib.Path, default=home / "FasterGames/SteamLibrary/steamapps/compatdata/611670")
    parser.add_argument("--fus-root", type=pathlib.Path, default=home / "LargeGames/FUS")
    parser.add_argument("--downloads", type=pathlib.Path, default=home / "Backup/Downloads")
    parser.add_argument("--xrizer-root", type=pathlib.Path, default=home / ".local/share/envision/ovr_comp")
    parser.add_argument(
        "--opencomposite-root",
        type=pathlib.Path,
        default=home / ".local/share/envision/opencomposite",
        help="local OpenComposite checkout containing build/bin/linux64/vrclient.so",
    )
    parser.add_argument("--reference-root", type=pathlib.Path, default=home / "Documents/SkyrimModding")
    parser.add_argument("--public-assets", type=pathlib.Path)
    parser.add_argument("--gameplay-package", type=pathlib.Path)
    parser.add_argument("--build-evidence", type=pathlib.Path)
    parser.add_argument("--runtime-evidence", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path)
    return parser.parse_args()


def _elf_range(payload: bytes, offset: int, size: int, label: str) -> bytes:
    if offset < 0 or size < 0 or offset > len(payload) or size > len(payload) - offset:
        raise ValueError(f"truncated ELF {label}")
    return payload[offset : offset + size]


def _elf_string(table: bytes, offset: int, label: str) -> str:
    if offset < 0 or offset >= len(table):
        raise ValueError(f"invalid ELF {label} string offset")
    end = table.find(b"\0", offset)
    if end < 0:
        raise ValueError(f"unterminated ELF {label} string")
    return table[offset:end].decode("ascii", "strict")


def _glibc_version(value: str) -> tuple[int, int] | None:
    match = re.fullmatch(r"GLIBC_(\d+)\.(\d+)", value)
    return (int(match.group(1)), int(match.group(2))) if match else None


def analyze_elf_loader(payload: bytes) -> dict[str, object]:
    """Parse the loader identity without executing or loading the ELF object."""

    header = _elf_range(payload, 0, 64, "header")
    if header[:7] != b"\x7fELF\x02\x01\x01":
        raise ValueError("loader is not little-endian ELF64")
    elf_type, machine = struct.unpack_from("<HH", header, 16)
    if elf_type != 3 or machine != 62:
        raise ValueError("loader is not an ELF x86_64 ET_DYN shared object")
    phoff = struct.unpack_from("<Q", header, 32)[0]
    shoff = struct.unpack_from("<Q", header, 40)[0]
    phentsize, phnum, shentsize, shnum = struct.unpack_from("<HHHH", header, 54)
    if phentsize != 56:
        raise ValueError("unsupported ELF program-header size")
    program_headers = _elf_range(payload, phoff, phentsize * phnum, "program headers")
    loads: list[tuple[int, int, int]] = []
    dynamic: tuple[int, int] | None = None
    for index in range(phnum):
        entry = program_headers[index * phentsize : (index + 1) * phentsize]
        typ = struct.unpack_from("<I", entry, 0)[0]
        offset, vaddr, _paddr, filesz = struct.unpack_from("<QQQQ", entry, 8)
        if typ == 1:
            _elf_range(payload, offset, filesz, "load segment")
            loads.append((vaddr, offset, filesz))
        elif typ == 2:
            if dynamic is not None:
                raise ValueError("ELF has multiple dynamic segments")
            _elf_range(payload, offset, filesz, "dynamic segment")
            dynamic = (offset, filesz)
    if dynamic is None:
        raise ValueError("ELF has no dynamic segment")

    entries: list[tuple[int, int]] = []
    dynamic_data = _elf_range(payload, *dynamic, "dynamic segment")
    if len(dynamic_data) % 16:
        raise ValueError("truncated ELF dynamic entry")
    for index in range(0, len(dynamic_data), 16):
        tag, value = struct.unpack_from("<qQ", dynamic_data, index)
        entries.append((tag, value))
        if tag == 0:
            break
    dynstr_address = next((value for tag, value in entries if tag == 5), None)
    dynstr_size = next((value for tag, value in entries if tag == 10), None)
    if dynstr_address is None or dynstr_size is None:
        raise ValueError("ELF dynamic string table is missing")
    for vaddr, offset, filesz in loads:
        if vaddr <= dynstr_address and dynstr_address + dynstr_size <= vaddr + filesz:
            dynstr = _elf_range(payload, offset + dynstr_address - vaddr, dynstr_size, "dynamic string table")
            break
    else:
        raise ValueError("ELF dynamic string table is outside load segments")
    needed = sorted(_elf_string(dynstr, value, "DT_NEEDED") for tag, value in entries if tag == 1)

    if shentsize != 64 or not shnum:
        raise ValueError("ELF version-requirement sections are missing")
    sections = _elf_range(payload, shoff, shentsize * shnum, "section headers")
    glibc_versions: list[tuple[int, int]] = []
    for index in range(shnum):
        section = sections[index * shentsize : (index + 1) * shentsize]
        typ = struct.unpack_from("<I", section, 4)[0]
        if typ != 0x6FFFFFFE:  # SHT_GNU_verneed
            continue
        offset, size = struct.unpack_from("<QQ", section, 24)
        link = struct.unpack_from("<I", section, 40)[0]
        if link >= shnum:
            raise ValueError("ELF version-requirement string table is invalid")
        linked = sections[link * shentsize : (link + 1) * shentsize]
        str_offset, str_size = struct.unpack_from("<QQ", linked, 24)
        strings = _elf_range(payload, str_offset, str_size, "version string table")
        version_data = _elf_range(payload, offset, size, "version-requirement section")
        cursor = 0
        while cursor < len(version_data):
            record = _elf_range(version_data, cursor, 16, "version requirement")
            count = struct.unpack_from("<H", record, 2)[0]
            aux_offset = struct.unpack_from("<I", record, 8)[0]
            next_offset = struct.unpack_from("<I", record, 12)[0]
            aux_cursor = cursor + aux_offset
            for _ in range(count):
                aux = _elf_range(version_data, aux_cursor, 16, "version requirement auxiliary entry")
                version = _glibc_version(_elf_string(strings, struct.unpack_from("<I", aux, 8)[0], "version"))
                if version is not None:
                    glibc_versions.append(version)
                aux_next = struct.unpack_from("<I", aux, 12)[0]
                if not aux_next:
                    break
                aux_cursor += aux_next
            if not next_offset:
                break
            cursor += next_offset
    if not glibc_versions:
        raise ValueError("ELF has no GLIBC symbol-version requirements")
    maximum = max(glibc_versions)
    return {
        "elfClass": "ELF64",
        "elfMachine": "x86_64",
        "elfType": "ET_DYN",
        "maxGlibc": f"GLIBC_{maximum[0]}.{maximum[1]}",
        "neededLibraries": needed,
    }


def validate_portable_loader(path: pathlib.Path) -> dict[str, object]:
    identity = analyze_elf_loader(path.read_bytes())
    maximum = _glibc_version(identity["maxGlibc"])
    assert maximum is not None
    if maximum > PORTABLE_GLIBC_MAX:
        ceiling = f"GLIBC_{PORTABLE_GLIBC_MAX[0]}.{PORTABLE_GLIBC_MAX[1]}"
        raise ValueError(f"loader requires {identity['maxGlibc']}, above portable glibc ceiling {ceiling}")
    unexpected = sorted(set(identity["neededLibraries"]) - PORTABLE_NEEDED_LIBRARIES)
    if unexpected:
        raise ValueError("loader has non-portable DT_NEEDED dependency: " + ", ".join(unexpected))
    return identity


def validated_opencomposite_runtime(root: pathlib.Path) -> tuple[pathlib.Path, dict[str, object]]:
    """Return only the reviewed OpenComposite loader and checkout provenance."""

    supplied_root = root.expanduser()
    if supplied_root.is_symlink():
        raise ValueError(f"OpenComposite root must not be a symbolic link: {supplied_root}")
    root = supplied_root.resolve()
    runtime = root.joinpath(*OPENCOMPOSITE_RUNTIME_PATH.parts)
    if not root.is_dir():
        raise ValueError(f"OpenComposite root is not a regular directory: {root}")
    top_level = pathlib.Path(run_git(root, "rev-parse", "--show-toplevel").strip()).resolve()
    if top_level != root:
        raise ValueError(f"OpenComposite root must be the checkout top level: {root}")
    origin = run_git(root, "remote", "get-url", "origin").strip()
    if origin not in OPENCOMPOSITE_OFFICIAL_ORIGINS:
        raise ValueError(f"OpenComposite origin is not the official znixian/OpenOVR repository: {origin!r}")
    if run_git(root, "status", "--porcelain=v1", "--untracked-files=all").strip():
        raise ValueError("OpenComposite checkout must be clean")
    if runtime.is_symlink() or not runtime.is_file():
        raise FileNotFoundError(f"missing regular OpenComposite Linux runtime: {runtime}")
    revision = run_git(root, "rev-parse", "HEAD").strip()
    if revision != OPENCOMPOSITE_REVISION:
        raise ValueError(f"OpenComposite checkout must be pinned at {OPENCOMPOSITE_REVISION}, got {revision!r}")
    runtime_hash = sha256(runtime)
    if runtime_hash != OPENCOMPOSITE_RUNTIME_SHA256:
        raise ValueError("OpenComposite runtime SHA-256 is not the reviewed current binary")
    build_patch = pathlib.Path(__file__).with_name("opencomposite-bullseye.patch")
    if not build_patch.is_file() or build_patch.is_symlink():
        raise FileNotFoundError(f"missing regular OpenComposite build patch: {build_patch}")
    build_patch_hash = hashlib.sha256(build_patch.read_bytes()).hexdigest()
    if build_patch_hash != OPENCOMPOSITE_BUILD_PATCH_SHA256:
        raise ValueError("OpenComposite build patch SHA-256 is not the reviewed current patch")
    identity = validate_portable_loader(runtime)
    return runtime, {
        "path": "dependencies/opencomposite-runtime/bin/linux64/vrclient.so",
        "sha256": runtime_hash,
        "sourceRevision": revision,
        "sourceOrigin": origin,
        "checkoutTopLevel": True,
        "checkoutClean": True,
        "buildPatchStatus": OPENCOMPOSITE_BUILD_PATCH_STATUS,
        "buildPatchSha256": build_patch_hash,
        **identity,
    }


def validated_xrizer_runtime(root: pathlib.Path) -> tuple[pathlib.Path, dict[str, object]]:
    """Return the reviewed XRizer compatibility build and its provenance."""

    supplied_root = root.expanduser()
    if supplied_root.is_symlink():
        raise ValueError(f"XRizer root must not be a symbolic link: {supplied_root}")
    root = supplied_root.resolve()
    if not root.is_dir():
        raise ValueError(f"XRizer root is not a regular directory: {root}")
    top_level = pathlib.Path(run_git(root, "rev-parse", "--show-toplevel").strip()).resolve()
    if top_level != root:
        raise ValueError(f"XRizer root must be the checkout top level: {root}")
    origin = run_git(root, "remote", "get-url", "origin").strip()
    if origin not in XRIZER_OFFICIAL_ORIGINS:
        raise ValueError(f"XRizer origin is not the official Supreeeme/xrizer repository: {origin!r}")
    revision = run_git(root, "rev-parse", "HEAD").strip()
    if revision != XRIZER_BASE_REVISION:
        raise ValueError(f"XRizer checkout must be pinned at {XRIZER_BASE_REVISION}, got {revision!r}")
    if run_git(root, "diff", "--cached", "--binary", "HEAD"):
        raise ValueError("XRizer compatibility patch must not contain staged changes")
    if run_git(root, "ls-files", "--others", "--exclude-standard").strip():
        raise ValueError("XRizer checkout must not contain untracked files")
    patch = run_git(root, "diff", "--binary", "--no-ext-diff", "--full-index", "HEAD").encode("utf-8")
    patch_hash = hashlib.sha256(patch).hexdigest()
    if patch_hash != XRIZER_COMPATIBILITY_PATCH_SHA256:
        raise ValueError("XRizer compatibility patch SHA-256 is not the reviewed current patch")
    runtime = root.joinpath(*XRIZER_RUNTIME_PATH.parts)
    if runtime.is_symlink() or not runtime.is_file():
        raise FileNotFoundError(f"missing regular XRizer Linux runtime: {runtime}")
    runtime_hash = sha256(runtime)
    if runtime_hash != XRIZER_RUNTIME_SHA256:
        raise ValueError("XRizer runtime SHA-256 is not the reviewed current binary")
    identity = validate_portable_loader(runtime)
    return runtime, {
        "path": "dependencies/xrizer-runtime/bin/linux64/vrclient.so",
        "sha256": runtime_hash,
        "baseRevision": XRIZER_BASE_REVISION,
        "sourceRevision": revision,
        "sourceOrigin": origin,
        "compatibilityPatchStatus": XRIZER_COMPATIBILITY_PATCH_STATUS,
        "compatibilityPatchSha256": patch_hash,
        **identity,
    }


def main() -> int:
    args = parse_args()
    repo = pathlib.Path(run_git(args.repo.resolve(), "rev-parse", "--show-toplevel").strip())
    status = run_git(repo, "status", "--porcelain=v1", "--untracked-files=all").strip()
    if status:
        raise RuntimeError("commit or remove all repository changes before creating the local handoff")

    head = run_git(repo, "rev-parse", "HEAD").strip()
    epoch = int(run_git(repo, "show", "-s", "--format=%ct", head).strip())
    timestamp = datetime.fromtimestamp(epoch, timezone.utc).timetuple()[:6]
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%SZ")
    public_assets = args.public_assets or repo / "artifacts/SkyrimTogetherVR/prerelease/stvr-v0.1.0-alpha.1"
    output = args.output or repo / f"artifacts/SkyrimTogetherVR/review-handoff/SkyrimTogetherVR-local-agent-complete-handoff-{stamp}.zip"
    output.parent.mkdir(parents=True, exist_ok=True)
    root = output.stem

    public_runtime = public_assets / "SkyrimTogetherVR-stvr-v0.1.0-alpha.1-linux-monado-runtime.zip"
    public_handoff = public_assets / "SkyrimTogetherVR-stvr-v0.1.0-alpha.1-review-handoff.zip"
    gameplay_package = args.gameplay_package or newest(
        repo / "artifacts/SkyrimTogetherVR/packages", "SkyrimTogetherVR-gameplay-*.zip"
    )
    build_evidence = args.build_evidence or newest(
        repo / "artifacts/SkyrimTogetherVR/build-evidence", "SkyrimTogetherVR-build-evidence-gameplay-*.zip"
    )
    runtime_evidence = args.runtime_evidence
    for path in (public_runtime, public_handoff, gameplay_package, build_evidence, runtime_evidence):
        if not path.is_file():
            raise FileNotFoundError(path)
    with zipfile.ZipFile(build_evidence) as archive:
        try:
            evidence_build_manifest = json.loads(
                archive.read("package/SkyrimTogetherVR_BuildManifest.json").decode("utf-8-sig")
            )
        except (KeyError, UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ValueError(f"could not read build revision from {build_evidence}: {error}") from error
    build_revision = evidence_build_manifest.get("sourceRevision")
    if not isinstance(build_revision, str) or len(build_revision) != 40:
        raise ValueError("build evidence has no full source revision")
    if subprocess.run(
        ["git", "-C", str(repo), "merge-base", "--is-ancestor", build_revision, head],
        check=False,
    ).returncode:
        raise ValueError(f"build source revision {build_revision} is not an ancestor of handoff HEAD {head}")
    build_identity = validate_artifact_pair(gameplay_package, build_evidence, build_revision)
    runtime_evidence_identity = validate_runtime_evidence(runtime_evidence, build_identity)
    xrizer_runtime, xrizer_provenance = validated_xrizer_runtime(args.xrizer_root)
    opencomposite_runtime, opencomposite_provenance = validated_opencomposite_runtime(args.opencomposite_root)

    with tempfile.TemporaryDirectory(prefix="stvr-local-handoff-") as temp_dir:
        bundle = pathlib.Path(temp_dir) / "source.bundle"
        run_git(
            repo,
            "bundle",
            "create",
            str(bundle),
            "HEAD",
            "refs/heads/main",
            "refs/heads/original-skyrim-together",
            "refs/tags/stvr-v0.1.0-alpha.1",
        )

        with zipfile.ZipFile(output, "w", allowZip64=True) as archive:
            writer = Writer(archive, timestamp)
            writer.add(public_runtime, f"{root}/bundles/{public_runtime.name}")
            writer.add(public_handoff, f"{root}/bundles/{public_handoff.name}")
            writer.add(gameplay_package, f"{root}/build/{gameplay_package.name}")
            writer.add(build_evidence, f"{root}/build/{build_evidence.name}")
            writer.add(runtime_evidence, f"{root}/evidence/{runtime_evidence.name}")
            writer.add(bundle, f"{root}/source.bundle")
            writer.add(repo / "Docs/SkyrimVR/local-agent-complete-handoff.md", f"{root}/START-HERE.md")
            writer.add(
                repo / "Tools/SkyrimVR/install_local_agent_handoff.py",
                f"{root}/INSTALL-SECOND-CLIENT.py",
            )
            writer.add(
                repo / "Tools/SkyrimVR/install_local_agent_handoff_windows.ps1",
                f"{root}/INSTALL-SECOND-CLIENT-WINDOWS.ps1",
            )
            writer.add(
                repo / "Tools/SkyrimVR/install_local_agent_handoff_windows.bat",
                f"{root}/INSTALL-SECOND-CLIENT-WINDOWS.bat",
            )

            tracked = run_git(repo, "ls-files", "--cached", "--recurse-submodules", "-z")
            for rel in sorted(item for item in tracked.split("\0") if item):
                path = repo / rel
                if path.is_file() and not skip(path, repo):
                    writer.add(path, f"{root}/source/{rel}")

            for mod_name in MOD_NAMES:
                mod_dir = args.fus_root / "mods" / mod_name
                if not mod_dir.is_dir():
                    raise FileNotFoundError(f"missing FUS dependency: {mod_dir}")
                for path in iter_tree(mod_dir):
                    rel = path.relative_to(mod_dir).as_posix()
                    writer.add(path, f"{root}/dependencies/fus-mods/{mod_name}/{rel}")

            for path in iter_game_overlay(args.game_dir):
                if path.is_relative_to(args.game_dir / "Data"):
                    rel = pathlib.Path("Data") / path.relative_to(args.game_dir / "Data")
                else:
                    rel = path.relative_to(args.game_dir)
                writer.add(path, f"{root}/dependencies/current-game-overlay/{rel.as_posix()}")

            writer.add(xrizer_runtime, f"{root}/dependencies/xrizer-runtime/libxrizer.so", executable=True)
            writer.add(
                xrizer_runtime,
                f"{root}/dependencies/xrizer-runtime/bin/linux64/vrclient.so",
                executable=True,
            )
            writer.add(
                opencomposite_runtime,
                f"{root}/dependencies/opencomposite-runtime/bin/linux64/vrclient.so",
            )
            writer.add_bytes(
                (json.dumps(NEUTRAL_OPENVR_PATHS, sort_keys=True) + "\n").encode("utf-8"),
                f"{root}/dependencies/openvrpaths.vrpath",
            )
            for path in iter_tree(args.xrizer_root):
                if path.name == "openvrpaths.vrpath":
                    continue
                rel = path.relative_to(args.xrizer_root).as_posix()
                writer.add(path, f"{root}/dependencies/source-references/XRizer/{rel}")

            for path in iter_tree(args.opencomposite_root):
                rel = path.relative_to(args.opencomposite_root).as_posix()
                writer.add(path, f"{root}/dependencies/source-references/OpenComposite/{rel}")

            for label, rel_root in REFERENCE_SOURCES.items():
                source_root = args.reference_root / rel_root
                if not source_root.is_dir():
                    raise FileNotFoundError(source_root)
                for path in iter_tree(source_root, devbench=label == "devbench"):
                    rel = path.relative_to(source_root).as_posix()
                    writer.add(path, f"{root}/dependencies/source-references/{label}/{rel}")

            for name, rel_path in EXTERNAL_REVIEW_NOTES.items():
                path = pathlib.Path.home() / rel_path
                if not path.is_file():
                    raise FileNotFoundError(path)
                writer.add(path, f"{root}/review-notes/{name}")

            for pattern in DOWNLOAD_PATTERNS:
                matches = sorted(args.downloads.glob(pattern))
                if not matches:
                    raise FileNotFoundError(f"missing supplied dependency archive matching {pattern}")
                for path in matches:
                    writer.add(path, f"{root}/dependencies/download-archives/{path.name}")

            prefix = args.compatdata / "pfx/drive_c/users/steamuser"
            direct_profile = {
                "Plugins.txt": prefix / "AppData/Local/Skyrim VR/Plugins.txt",
                "loadorder.txt": prefix / "AppData/Local/Skyrim VR/loadorder.txt",
                "SkyrimPrefs.ini": prefix / "Documents/My Games/Skyrim VR/SkyrimPrefs.ini",
            }
            for name, path in direct_profile.items():
                writer.add(path, f"{root}/profiles/direct-proton/{name}")
            fus_profile = args.fus_root / "profiles/FUS (Basic)"
            for path in iter_tree(fus_profile):
                writer.add(path, f"{root}/profiles/FUS (Basic)/{path.relative_to(fus_profile).as_posix()}")

            manifest = {
                "schema": "skyrim_together_vr_local_agent_handoff_v1",
                "generatedAtUtc": datetime.now(timezone.utc).isoformat(),
                "sourceHead": head,
                "buildSourceRevision": build_revision,
                "localOnly": True,
                "serverEndpoint": "incidentalstoat.xyz:26099",
                "gameplayPackage": {
                    "name": gameplay_package.name,
                    "sha256": sha256(gameplay_package),
                    **build_identity,
                },
                "buildEvidence": {
                    "name": build_evidence.name,
                    "sha256": sha256(build_evidence),
                    **build_identity,
                },
                "runtimeEvidence": {
                    "name": runtime_evidence.name,
                    "path": f"evidence/{runtime_evidence.name}",
                    "sha256": sha256(runtime_evidence),
                    **runtime_evidence_identity,
                },
                "xrizerRuntime": xrizer_provenance,
                "openCompositeRuntime": opencomposite_provenance,
                "records": writer.records,
            }
            writer.add_bytes(
                (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8"),
                f"{root}/LOCAL-MANIFEST.json",
                record=False,
            )

    checksum = output.with_suffix(output.suffix + ".sha256.txt")
    checksum.write_text(f"{sha256(output)}  {output.name}\n", encoding="ascii")
    print(output)
    print(checksum)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
