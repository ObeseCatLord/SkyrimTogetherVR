#!/usr/bin/env python3
"""Audit SkyrimVR-FBT capture integration and an optional installed profile."""

from __future__ import annotations

import argparse
import configparser
import pathlib
import struct
import sys


REQUIRED_TOKENS = {
    "Code/vr_common/VRTickBridge.h": (
        "kEndpointAbiVersion = 3",
        "BodyCaptureCallback",
        "BodyCaptureCallbackRva",
    ),
    "Code/client/Games/Skyrim/VR/VRBodyPoseCapture.cpp": (
        "NPC Pelvis [Pelv]",
        "NPC L Thigh [LThg]",
        "NPC R Foot [Rft ]",
        "TryAcquireSRWLockExclusive",
        "TryAcquireSRWLockShared",
    ),
    "Code/client/Games/Skyrim/VR/VRBodyPoseCapture.h": (
        "aMaxAgeMilliseconds = 250",
    ),
    "Code/higgs_bridge/main.cpp": (
        "AddPostVrikPostHiggsCallback",
        "PublishPostHiggsBodyPose",
        "BodyCaptureCallbackRva",
        "bodyCapture.successCount",
    ),
    "Code/encoding/Structs/VRPoseUpdate.h": (
        "struct VRBodyPoseData",
        "CaptureSequence",
        "RootGeneration",
        "IsVRBodyPoseDataSafe",
    ),
    "Code/server/Services/VRPoseRelayService.cpp": (
        "IsVRPoseUpdateSafe(pose)",
        "HasAnyVRPosePayload(pose)",
    ),
    "Code/client/Services/Generic/VRPoseService.cpp": (
        "BodyPoseCapture::CopyLatestFresh",
        "local.body",
        "remote.",
    ),
    "Code/client/Services/Generic/CharacterService.cpp": (
        "bodyPoseValidCount",
        "bodyPoseUnsafeCount",
        "last.bodyCaptureSequence",
        "IsVRBodyPoseDataSafe",
    ),
    "Code/client/VRCompatibilityStatus.cpp": (
        "SkyrimVR-FBT.dll",
        "fbt.installed",
        "fbt.loaded",
        "fbtPolicy=local_post_higgs_capture_and_network_cache_only",
    ),
    "Docs/SkyrimVR/fbt-compatibility.md": (
        "AllowTimestampMismatch=1",
        "bodyCapture.successCount",
        "local.body.valid=1",
        "not redistributed",
        "actor-specific post-animation",
    ),
    "Docs/SkyrimVR/fbt-networking-senior-disposition-20260714.md": (
        "Sol Max",
        "nonblocking mailbox",
        "remote skeleton writes",
        "matched client/server",
    ),
}


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def read_pe(path: pathlib.Path) -> tuple[int, list[tuple[int, int, int, int]], bytes]:
    data = path.read_bytes()
    if len(data) < 0x100 or data[:2] != b"MZ":
        raise ValueError("not a PE image")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if pe_offset + 24 > len(data) or data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ValueError("invalid PE header")

    section_count = struct.unpack_from("<H", data, pe_offset + 6)[0]
    timestamp = struct.unpack_from("<I", data, pe_offset + 8)[0]
    optional_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
    section_offset = pe_offset + 24 + optional_size
    sections: list[tuple[int, int, int, int]] = []
    for index in range(section_count):
        offset = section_offset + index * 40
        if offset + 40 > len(data):
            raise ValueError("truncated PE section table")
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", data, offset + 8)
        sections.append((virtual_address, max(virtual_size, raw_size), raw_offset, raw_size))
    return timestamp, sections, data


def rva_bytes(data: bytes, sections: list[tuple[int, int, int, int]], rva: int, size: int) -> bytes:
    for virtual_address, mapped_size, raw_offset, raw_size in sections:
        if virtual_address <= rva < virtual_address + mapped_size:
            delta = rva - virtual_address
            if delta + size > raw_size or raw_offset + delta + size > len(data):
                raise ValueError("RVA extends beyond section data")
            return data[raw_offset + delta : raw_offset + delta + size]
    raise ValueError("RVA is not mapped by a PE section")


def parse_int(value: str) -> int:
    return int(value.strip(), 0)


def audit_installed(game_path: pathlib.Path, require_installed: bool) -> tuple[list[str], list[str]]:
    failures: list[str] = []
    warnings: list[str] = []
    plugins = game_path / "Data" / "SKSE" / "Plugins"
    fbt_dll = plugins / "SkyrimVR-FBT.dll"
    fbt_ini = plugins / "fbt.ini"
    vrik_dll = plugins / "vrik.dll"
    if not vrik_dll.exists():
        vrik_dll = plugins / "VRIK.dll"

    missing = [path for path in (fbt_dll, fbt_ini, vrik_dll) if not path.exists()]
    if missing:
        message = "missing installed file(s): " + ", ".join(str(path) for path in missing)
        (failures if require_installed else warnings).append(message)
        return failures, warnings

    parser = configparser.ConfigParser()
    parser.read(fbt_ini, encoding="utf-8")
    if not parser.has_section("VrikHook"):
        failures.append(f"{fbt_ini}: missing [VrikHook]")
        return failures, warnings

    try:
        configured_timestamp = parse_int(parser.get("VrikHook", "VrikTimestamp"))
        wrapper_rva = parse_int(parser.get("VrikHook", "RvaUpdateBodyWrapper"))
        allow_mismatch = parser.getboolean("VrikHook", "AllowTimestampMismatch")
    except (ValueError, configparser.Error) as exc:
        failures.append(f"{fbt_ini}: invalid VrikHook profile: {exc}")
        return failures, warnings

    if allow_mismatch:
        failures.append(f"{fbt_ini}: AllowTimestampMismatch must remain disabled")

    try:
        actual_timestamp, sections, data = read_pe(vrik_dll)
        wrapper = rva_bytes(data, sections, wrapper_rva, 8)
    except (OSError, ValueError, struct.error) as exc:
        failures.append(f"{vrik_dll}: could not verify configured wrapper: {exc}")
        return failures, warnings

    if configured_timestamp != actual_timestamp:
        failures.append(
            f"{fbt_ini}: VrikTimestamp 0x{configured_timestamp:08X} does not match {vrik_dll.name} 0x{actual_timestamp:08X}"
        )
    if not (wrapper[:3] == b"\x48\x83\x3D" and wrapper[7] == 0):
        failures.append(
            f"{fbt_ini}: RvaUpdateBodyWrapper 0x{wrapper_rva:X} does not point to the expected guarded wrapper"
        )
    return failures, warnings


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--game-path", type=pathlib.Path)
    parser.add_argument("--require-installed", action="store_true")
    args = parser.parse_args()

    root = repo_root()
    failures: list[str] = []
    warnings: list[str] = []
    for relative_path, tokens in REQUIRED_TOKENS.items():
        path = root / relative_path
        text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
        for token in tokens:
            if token not in text:
                failures.append(f"{relative_path}: missing `{token}`")

    if args.require_installed and args.game_path is None:
        failures.append("--require-installed requires --game-path")
    if args.game_path is not None:
        installed_failures, installed_warnings = audit_installed(args.game_path, args.require_installed)
        failures.extend(installed_failures)
        warnings.extend(installed_warnings)

    print(f"SkyrimVR-FBT audit warnings: {len(warnings)}")
    for warning in warnings:
        print(f"- warning: {warning}")
    print(f"SkyrimVR-FBT audit failures: {len(failures)}")
    for failure in failures:
        print(f"- {failure}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
