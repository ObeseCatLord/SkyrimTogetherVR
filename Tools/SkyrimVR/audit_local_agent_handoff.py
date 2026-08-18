#!/usr/bin/env python3
"""Audit the private machine-local Skyrim Together VR agent handoff."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import shutil
import stat
import struct
import subprocess
import tempfile
import zipfile

from local_handoff_artifacts import validate_artifact_pair


REQUIRED_PATHS = (
    "START-HERE.md",
    "INSTALL-SECOND-CLIENT.py",
    "INSTALL-SECOND-CLIENT-WINDOWS.ps1",
    "INSTALL-SECOND-CLIENT-WINDOWS.bat",
    "source.bundle",
    "bundles/SkyrimTogetherVR-stvr-v0.1.0-alpha.1-linux-monado-runtime.zip",
    "bundles/SkyrimTogetherVR-stvr-v0.1.0-alpha.1-review-handoff.zip",
    "source/Docs/SkyrimVR/original-gameplay-parity-checklist.md",
    "source/Docs/SkyrimVR/local-agent-complete-handoff.md",
    "source/Tools/SkyrimVR/install_local_agent_handoff_windows.ps1",
    "source/Tools/SkyrimVR/install_local_agent_handoff_windows.bat",
    "source/Tools/SkyrimVR/linux/launch-skyrim-together-vr.sh",
    "source/Tools/SkyrimVR/linux/launch-skyrim-vr-offline.sh",
    "source/Tools/SkyrimVR/linux/stvr-xrizer-input-compat.sh",
    "source/Tools/SkyrimVR/build_portable_openvr_runtimes.sh",
    "source/Tools/SkyrimVR/finalize_local_agent_handoff.sh",
    "source/Tools/SkyrimVR/opencomposite-bullseye.patch",
    "dependencies/current-game-overlay/SkyrimVR.exe",
    "dependencies/current-game-overlay/openvr_api.dll",
    "dependencies/current-game-overlay/sksevr_loader.exe",
    "dependencies/current-game-overlay/Data/SKSE/Plugins/devbench.dll",
    "dependencies/current-game-overlay/Data/SKSE/Plugins/devbench/config.json",
    "dependencies/current-game-overlay/Data/SKSE/Plugins/devbench/recordings/GuardianStonesToWhiterun.json",
    "dependencies/current-game-overlay/Data/SKSE/Plugins/SkyrimVR-FBT.dll",
    "dependencies/current-game-overlay/Data/SKSE/Plugins/higgs_vr.dll",
    "dependencies/current-game-overlay/Data/SKSE/Plugins/activeragdoll.dll",
    "dependencies/current-game-overlay/Data/SKSE/Plugins/VRIK.dll",
    "dependencies/current-game-overlay/Data/SKSE/Plugins/version-1-4-15-0.csv",
    "dependencies/xrizer-runtime/libxrizer.so",
    "dependencies/xrizer-runtime/bin/linux64/vrclient.so",
    "dependencies/opencomposite-runtime/bin/linux64/vrclient.so",
    "dependencies/openvrpaths.vrpath",
    "dependencies/source-references/OpenComposite/LICENSE.txt",
    "profiles/direct-proton/Plugins.txt",
    "review-notes/REVIEW-AGENT-HANDOFF.md",
    "review-notes/REVIEW-FRIEND-SUMMARY.md",
    "review-notes/deep-research-report-1.md",
)

MACHINE_LOCAL_OVERLAY_PATHS = (
    "dependencies/current-game-overlay/launch-skyrim-together-vr.sh",
    "dependencies/current-game-overlay/launch-skyrim-vr-offline.sh",
    "dependencies/current-game-overlay/stvr-xrizer-input-compat.sh",
)

PORTABLE_LAUNCHER_PATHS = (
    "source/Tools/SkyrimVR/linux/launch-skyrim-together-vr.sh",
    "source/Tools/SkyrimVR/linux/launch-skyrim-vr-offline.sh",
    "source/Tools/SkyrimVR/linux/stvr-xrizer-input-compat.sh",
)

REQUIRED_PREFIXES = (
    "dependencies/source-references/devbench/src/",
    "dependencies/source-references/XRizer/src/",
    "dependencies/source-references/HIGGS/src/",
    "dependencies/source-references/PLANCK-activeragdoll/src/",
    "dependencies/source-references/CommonLibSSE-NG/include/",
    "dependencies/source-references/CommonLibSSE-sample-plugin/src/",
    "dependencies/source-references/SKSE-Menu-Framework-3/",
    "dependencies/source-references/PapyrusTweaks/",
    "dependencies/source-references/OpenComposite/",
    "dependencies/fus-mods/HIGGS - Hand Interaction and Gravity Gloves for Skyrim VR/",
    "dependencies/fus-mods/PLANCK - Physical Animation and Character Kinetics/",
    "dependencies/fus-mods/VRIK Player Avatar/",
    "dependencies/fus-mods/Controller Fix VR/",
    "dependencies/fus-mods/Realm of Lorkhan - Freeform Alternate Start/",
    "dependencies/download-archives/ClibDT (SOURCE)-",
    "dependencies/download-archives/ClibDT (EXE)-",
    "dependencies/download-archives/SkyrimVR FBT Source 185070",
    "dependencies/download-archives/SkyrimVRTools-27782-",
)

OPENCOMPOSITE_RUNTIME_PATH = "dependencies/opencomposite-runtime/bin/linux64/vrclient.so"
OPENCOMPOSITE_REVISION = "cff07db75c4823afe93ed7027b03d5f7bc86f164"
OPENCOMPOSITE_RUNTIME_SHA256 = "a703fdd1eaff092d28d91798b0ad1afb1611523da1456563dd51f2892b471751"
OPENCOMPOSITE_BUILD_PATCH_SHA256 = "529453480dc9ff838b9cabcb341109672fb961dc0676d4006d238c4662558a67"
OPENCOMPOSITE_BUILD_PATCH_STATUS = "applied-to-isolated-build-copy"
OPENCOMPOSITE_OFFICIAL_ORIGINS = {
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
XRIZER_ROOT_RUNTIME_PATH = "dependencies/xrizer-runtime/libxrizer.so"
XRIZER_RUNTIME_PATH = "dependencies/xrizer-runtime/bin/linux64/vrclient.so"
XRIZER_BASE_REVISION = "31319560c1bd0f1e5c16936a946bb1c7295dbfd9"
XRIZER_RUNTIME_SHA256 = "b278c4695f15bba7c554aaac5303520247cc8ab3bcae3f8b55e934e2b114ccaf"
XRIZER_COMPATIBILITY_PATCH_SHA256 = "64d837980afd29cc3d557f4326eee34a165f0bb49888c247fc2af36361990142"
XRIZER_COMPATIBILITY_PATCH_STATUS = "applied-worktree-patch"
XRIZER_OFFICIAL_ORIGINS = {
    "https://github.com/Supreeeme/xrizer.git",
    "https://github.com/Supreeeme/xrizer",
    "git@github.com:Supreeeme/xrizer.git",
    "git@github.com:Supreeeme/xrizer",
    "ssh://git@github.com/Supreeeme/xrizer.git",
    "ssh://git@github.com/Supreeeme/xrizer",
}
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


def hash_entry(archive: zipfile.ZipFile, name: str) -> tuple[str, int]:
    digest = hashlib.sha256()
    size = 0
    with archive.open(name) as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
            size += len(chunk)
    return digest.hexdigest(), size


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
    """Parse loader identity from bytes without executing or loading the ELF object."""

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
        if typ != 0x6FFFFFFE:
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


def portable_loader_failure(identity: dict[str, object]) -> str | None:
    maximum = _glibc_version(identity.get("maxGlibc", ""))
    if maximum is None:
        return "loader has invalid GLIBC symbol-version identity"
    if maximum > PORTABLE_GLIBC_MAX:
        ceiling = f"GLIBC_{PORTABLE_GLIBC_MAX[0]}.{PORTABLE_GLIBC_MAX[1]}"
        return f"loader requires {identity['maxGlibc']}, above portable glibc ceiling {ceiling}"
    unexpected = sorted(set(identity.get("neededLibraries", [])) - PORTABLE_NEEDED_LIBRARIES)
    if unexpected:
        return "loader has non-portable DT_NEEDED dependency: " + ", ".join(unexpected)
    return None


def runtime_provenance_failures(
    label: str,
    metadata: object,
    expected: dict[str, object],
    official_origins: set[str],
    identity: dict[str, object] | None,
) -> list[str]:
    if not isinstance(metadata, dict):
        return [f"{label} runtime provenance metadata is missing"]
    failures: list[str] = []
    if any(metadata.get(key) != value for key, value in expected.items()):
        failures.append(f"{label} runtime provenance metadata is not the reviewed current loader")
    if metadata.get("sourceOrigin") not in official_origins:
        failures.append(f"{label} runtime provenance has a non-official origin")
    if identity is not None:
        for key in ("elfClass", "elfMachine", "elfType", "maxGlibc", "neededLibraries"):
            if metadata.get(key) != identity.get(key):
                failures.append(f"{label} runtime provenance ELF identity does not match payload")
                break
        policy_failure = portable_loader_failure(identity)
        if policy_failure:
            failures.append(f"{label} runtime {policy_failure}")
    return failures


def xrizer_runtime_pair_failures(
    archive: zipfile.ZipFile,
    root: str,
    names: list[str],
    records_by_path: dict[object, dict[str, object]],
) -> list[str]:
    """Require the paired XRizer runtime files to be executable byte-for-byte copies."""

    root_name = f"{root}/{XRIZER_ROOT_RUNTIME_PATH}"
    loader_name = f"{root}/{XRIZER_RUNTIME_PATH}"
    failures: list[str] = []
    if records_by_path.get(root_name, {}).get("sha256") != XRIZER_RUNTIME_SHA256:
        failures.append("XRizer root runtime payload hash is not the reviewed current loader")
    if root_name not in names or loader_name not in names:
        return failures
    for name in (root_name, loader_name):
        mode = archive.getinfo(name).external_attr >> 16
        if stat.S_IFMT(mode) != stat.S_IFREG or mode & 0o111 != 0o111:
            failures.append(f"XRizer runtime payload is not a regular executable file: {name}")
    if archive.read(root_name) != archive.read(loader_name):
        failures.append("XRizer root runtime and OpenVR loader payloads differ")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=pathlib.Path)
    args = parser.parse_args()
    failures: list[str] = []

    try:
        archive = zipfile.ZipFile(args.archive)
    except (OSError, zipfile.BadZipFile) as error:
        print(f"audit failed\n - could not open handoff ZIP: {error}")
        return 1

    with archive:
        names = [name for name in archive.namelist() if not name.endswith("/")]
        for name in names:
            normalized = name.replace("\\", "/")
            canonical = pathlib.PurePosixPath(normalized).as_posix()
            if (
                normalized != name
                or name.startswith("/")
                or ".." in normalized.split("/")
                or canonical != normalized
            ):
                failures.append(f"unsafe archive path: {name}")
            if pathlib.PurePosixPath(name).suffix.lower() in {".pdb", ".log", ".dmp", ".lib", ".exp"}:
                failures.append(f"excluded developer/runtime artifact present: {name}")
        if archive.testzip() is not None:
            failures.append("ZIP CRC test failed")

        manifest_names = [name for name in names if name.endswith("/LOCAL-MANIFEST.json")]
        if len(manifest_names) != 1:
            failures.append("expected exactly one LOCAL-MANIFEST.json")
            manifest = {}
            root = ""
        else:
            root = manifest_names[0].removesuffix("/LOCAL-MANIFEST.json")
            try:
                manifest = json.loads(archive.read(manifest_names[0]).decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as error:
                failures.append(f"invalid local handoff manifest JSON: {error}")
                manifest = {}
            if not isinstance(manifest, dict):
                failures.append("local handoff manifest is not a JSON object")
                manifest = {}
        if root and any(not name.startswith(root + "/") for name in names):
            failures.append("archive payload is not contained under one root directory")
        if manifest.get("schema") != "skyrim_together_vr_local_agent_handoff_v1" or manifest.get("localOnly") is not True:
            failures.append("invalid local handoff manifest")

        records = manifest.get("records", [])
        if not isinstance(records, list) or any(not isinstance(record, dict) for record in records):
            failures.append("manifest records field is not a list of objects")
            records = []
        record_paths = [record.get("path") for record in records]
        if len(set(record_paths)) != len(record_paths):
            failures.append("manifest contains duplicate record paths")
        records_by_path = {record.get("path"): record for record in records}
        payload_names = set(names) - set(manifest_names)
        if set(record_paths) != payload_names:
            failures.append("manifest record set does not match archive payload")
        for record in records:
            path = record.get("path", "")
            if path not in payload_names:
                continue
            digest, size = hash_entry(archive, path)
            if digest != record.get("sha256") or size != record.get("size"):
                failures.append(f"manifest mismatch: {path}")

        relative_names = {name.removeprefix(root + "/") for name in names} if root else set()
        for required in REQUIRED_PATHS:
            if required not in relative_names:
                failures.append(f"missing required payload: {required}")
        opencomposite_patch_name = f"{root}/source/Tools/SkyrimVR/opencomposite-bullseye.patch"
        if records_by_path.get(opencomposite_patch_name, {}).get("sha256") != OPENCOMPOSITE_BUILD_PATCH_SHA256:
            failures.append("OpenComposite build patch payload hash is not the reviewed current patch")
        stale_runtime_metadata = sorted(
            name for name in relative_names if name.endswith("openvrpaths.vrpath") and name != "dependencies/openvrpaths.vrpath"
        )
        if stale_runtime_metadata:
            failures.append(
                "generated openvrpaths.vrpath is producer-machine metadata, not portable payload: "
                + ", ".join(stale_runtime_metadata)
            )
        if "machinePaths" in manifest:
            failures.append("manifest contains producer-machine path metadata")
        pathreg_name = f"{root}/dependencies/openvrpaths.vrpath"
        if pathreg_name in names:
            try:
                pathreg = json.loads(archive.read(pathreg_name).decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as error:
                failures.append(f"portable openvrpaths.vrpath is invalid JSON: {error}")
            else:
                if pathreg != NEUTRAL_OPENVR_PATHS:
                    failures.append("portable openvrpaths.vrpath must contain only version 1 and an empty runtime list")
        runtime_specs = (
            (
                "XRizer",
                "xrizerRuntime",
                XRIZER_RUNTIME_PATH,
                XRIZER_RUNTIME_SHA256,
                {
                    "path": XRIZER_RUNTIME_PATH,
                    "sha256": XRIZER_RUNTIME_SHA256,
                    "baseRevision": XRIZER_BASE_REVISION,
                    "sourceRevision": XRIZER_BASE_REVISION,
                    "compatibilityPatchStatus": XRIZER_COMPATIBILITY_PATCH_STATUS,
                    "compatibilityPatchSha256": XRIZER_COMPATIBILITY_PATCH_SHA256,
                    "elfClass": "ELF64",
                    "elfMachine": "x86_64",
                    "elfType": "ET_DYN",
                    "maxGlibc": "GLIBC_2.29",
                    "neededLibraries": [
                        "ld-linux-x86-64.so.2",
                        "libc.so.6",
                        "libdl.so.2",
                        "libgcc_s.so.1",
                        "libm.so.6",
                        "libpthread.so.0",
                        "libstdc++.so.6",
                    ],
                },
                XRIZER_OFFICIAL_ORIGINS,
            ),
            (
                "OpenComposite",
                "openCompositeRuntime",
                OPENCOMPOSITE_RUNTIME_PATH,
                OPENCOMPOSITE_RUNTIME_SHA256,
                {
                    "path": OPENCOMPOSITE_RUNTIME_PATH,
                    "sha256": OPENCOMPOSITE_RUNTIME_SHA256,
                    "sourceRevision": OPENCOMPOSITE_REVISION,
                    "checkoutTopLevel": True,
                    "checkoutClean": True,
                    "buildPatchStatus": OPENCOMPOSITE_BUILD_PATCH_STATUS,
                    "buildPatchSha256": OPENCOMPOSITE_BUILD_PATCH_SHA256,
                    "elfClass": "ELF64",
                    "elfMachine": "x86_64",
                    "elfType": "ET_DYN",
                    "maxGlibc": "GLIBC_2.14",
                    "neededLibraries": [
                        "ld-linux-x86-64.so.2",
                        "libGL.so.1",
                        "libX11.so.6",
                        "libc.so.6",
                        "libdl.so.2",
                        "libgcc_s.so.1",
                        "libm.so.6",
                        "libstdc++.so.6",
                        "libvulkan.so.1",
                    ],
                },
                OPENCOMPOSITE_OFFICIAL_ORIGINS,
            ),
        )
        for label, metadata_key, runtime_path, expected, expected_provenance, origins in runtime_specs:
            runtime_name = f"{root}/{runtime_path}"
            if records_by_path.get(runtime_name, {}).get("sha256") != expected:
                failures.append(f"{label} runtime payload hash is not the reviewed current loader")
            identity = None
            if runtime_name in names:
                try:
                    identity = analyze_elf_loader(archive.read(runtime_name))
                except (UnicodeDecodeError, ValueError, zipfile.BadZipFile) as error:
                    failures.append(f"{label} runtime payload is not a valid ELF loader: {error}")
            failures.extend(
                runtime_provenance_failures(
                    label,
                    manifest.get(metadata_key),
                    expected_provenance,
                    origins,
                    identity,
                )
            )
        failures.extend(xrizer_runtime_pair_failures(archive, root, names, records_by_path))
        opencomposite_source_prefix = "dependencies/source-references/OpenComposite/"
        invalid_opencomposite_snapshot = sorted(
            name for name in relative_names
            if name.startswith(opencomposite_source_prefix)
            and any(part in {".git", "build"} for part in pathlib.PurePosixPath(name).parts)
        )
        if invalid_opencomposite_snapshot:
            failures.append("OpenComposite source snapshot contains excluded .git/build content")
        for prefix in REQUIRED_PREFIXES:
            if not any(name.startswith(prefix) for name in relative_names):
                failures.append(f"missing required payload prefix: {prefix}")
        for forbidden in MACHINE_LOCAL_OVERLAY_PATHS:
            if forbidden in relative_names:
                failures.append(f"machine-local launcher present in compatibility overlay: {forbidden}")
        for launcher in PORTABLE_LAUNCHER_PATHS:
            archive_name = f"{root}/{launcher}"
            if launcher not in relative_names:
                continue
            try:
                launcher_text = archive.read(archive_name).decode("utf-8")
            except UnicodeDecodeError:
                failures.append(f"portable launcher is not UTF-8 text: {launcher}")
                continue
            if "/home/" in launcher_text:
                failures.append(f"portable launcher contains producer-machine home path: {launcher}")
        stale_handoff = sorted(
            name
            for name in relative_names
            if name.startswith("dependencies/current-game-overlay/Data/SkyrimTogetherReborn/SkyrimTogetherVR.")
        )
        if stale_handoff:
            failures.append(
                "current-game-overlay contains stale SkyrimTogetherReborn runtime readout/control file(s): "
                + ", ".join(stale_handoff[:5])
            )

        artifact_paths: dict[str, str] = {}
        for metadata_key in ("gameplayPackage", "buildEvidence"):
            metadata = manifest.get(metadata_key, {})
            if not isinstance(metadata, dict):
                failures.append(f"{metadata_key} metadata is not an object")
                continue
            artifact_name = metadata.get("name")
            artifact_hash = metadata.get("sha256")
            relative_path = f"build/{artifact_name}" if isinstance(artifact_name, str) else ""
            matching = f"{root}/{relative_path}" if root and relative_path in relative_names else ""
            if not artifact_name or not matching:
                failures.append(f"invalid {metadata_key} artifact reference")
                continue
            artifact_paths[metadata_key] = matching
            if records_by_path.get(matching, {}).get("sha256") != artifact_hash:
                failures.append(f"{metadata_key} hash does not match its payload record")

        build_revision = manifest.get("buildSourceRevision")
        source_head = manifest.get("sourceHead")
        if not isinstance(build_revision, str) or not isinstance(source_head, str):
            failures.append("manifest source/build revisions are missing")
        elif len(artifact_paths) == 2:
            try:
                with tempfile.TemporaryDirectory(
                    prefix=".stvr-handoff-audit-", dir=args.archive.resolve().parent
                ) as temp_dir:
                    temp = pathlib.Path(temp_dir)
                    package_path = temp / pathlib.PurePosixPath(artifact_paths["gameplayPackage"]).name
                    evidence_path = temp / pathlib.PurePosixPath(artifact_paths["buildEvidence"]).name
                    bundle_path = temp / "source.bundle"
                    for archive_name, output_path in (
                        (artifact_paths["gameplayPackage"], package_path),
                        (artifact_paths["buildEvidence"], evidence_path),
                        (f"{root}/source.bundle", bundle_path),
                    ):
                        with archive.open(archive_name) as source, output_path.open("wb") as output:
                            shutil.copyfileobj(source, output, length=1024 * 1024)
                    try:
                        identity = validate_artifact_pair(package_path, evidence_path, build_revision)
                        for metadata_key in ("gameplayPackage", "buildEvidence"):
                            metadata = manifest[metadata_key]
                            for key, value in identity.items():
                                if metadata.get(key) != value:
                                    failures.append(f"{metadata_key} {key} does not match nested artifacts")
                    except (OSError, ValueError, zipfile.BadZipFile) as error:
                        failures.append(f"nested gameplay artifact audit failed: {error}")

                    bare_repo = temp / "bundle.git"
                    init = subprocess.run(
                        ["git", "init", "--bare", str(bare_repo)], capture_output=True, text=True, check=False
                    )
                    unbundle = subprocess.run(
                        ["git", "-C", str(bare_repo), "bundle", "unbundle", str(bundle_path)],
                        capture_output=True,
                        text=True,
                        check=False,
                    ) if init.returncode == 0 else init
                    relation = subprocess.run(
                        ["git", "-C", str(bare_repo), "merge-base", "--is-ancestor", build_revision, source_head],
                        capture_output=True,
                        text=True,
                        check=False,
                    ) if unbundle.returncode == 0 else unbundle
                    if init.returncode or unbundle.returncode or relation.returncode:
                        failures.append("source bundle does not contain the declared build-to-handoff revision ancestry")
            except OSError as error:
                failures.append(f"could not stage embedded artifacts for audit: {error}")

    if failures:
        print("audit failed")
        for failure in failures:
            print(f" - {failure}")
        return 1
    print(f"audit passed: {len(records)} payload files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
