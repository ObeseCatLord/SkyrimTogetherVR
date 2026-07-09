#!/usr/bin/env python3
"""Audit that this working copy exposes only Skyrim VR client/launcher targets."""

from __future__ import annotations

import pathlib
import sys


REQUIRED_TOKENS = {
    "Code/client/xmake.lua": (
        'build_vr_client("SkyrimTogetherVRClient")',
        'build_vr_client("SkyrimTogetherVRGameplayClient"',
        'add_defines("TP_SKYRIM_VR=1")',
    ),
    "Build.bat": (
        "BuildAndAuditSkyrimTogetherVR-Windows.bat",
        "Skyrim VR-only compatibility wrapper",
        "never builds",
        "Skyrim SE targets",
    ),
    "Code/immersive_launcher/xmake.lua": (
        'target("SkyrimVRImmersiveLauncher")',
        'target("SkyrimVRImmersiveLauncherGameplay")',
        'set_basename("SkyrimTogetherVR")',
        'set_basename("SkyrimTogetherVRGameplay")',
        'add_defines("TP_SKYRIM_VR=1")',
        'add_deps("SkyrimTogetherVRClient")',
        'add_deps("SkyrimTogetherVRGameplayClient")',
    ),
    "Code/immersive_launcher/TargetConfig.h": (
        '#error "SkyrimTogetherVR launcher must be built with TP_SKYRIM_VR=1"',
        "#define CLIENT_DLL 0",
        "nullptr",
        'L"Skyrim VR"',
        '611670',
        '#define TARGET_NAME L"SkyrimVR"',
        '#define PRODUCT_NAME L"Skyrim Together VR"',
    ),
    "Code/client/ScriptExtender.cpp": (
        '#error "SkyrimTogetherVR client must be built with TP_SKYRIM_VR=1"',
        'L"sksevr"',
        "SKSEVR 2.0.12 or newer is required",
        "SKSEVR is active; StartSKSE export is not expected for this runtime",
    ),
    "Code/client/Services/Generic/TransportService.cpp": (
        'request.Username = "Skyrim VR Player";',
    ),
    "Code/immersive_launcher/stubs/FileMapping.cpp": (
        'GetModuleHandle("SkyrimVR.exe")',
        "SKSEVR's runtime lookup can ask for the executable module by name",
    ),
    "Code/immersive_launcher/stubs/DllBlocklist.cpp": (
        "SKYRIM TOGETHER VR marker",
        "Skyrim Together VR",
    ),
    "Docs/SkyrimVR/windows-build.md": (
        "VR-only",
        "normal Skyrim SE `SkyrimTogetherClient` and `SkyrimImmersiveLauncher` targets are intentionally absent",
    ),
    "Docs/SkyrimVR/porting-status.md": (
        "VR-only xmake target graph",
        "normal Skyrim SE `SkyrimTogetherClient` and `SkyrimImmersiveLauncher` targets are intentionally absent",
    ),
}

FORBIDDEN_TOKENS = {
    "Code/client/xmake.lua": (
        'build_client("SkyrimTogetherClient"',
        'add_defines("TP_SKYRIM_VR=0")',
    ),
    "Build.bat": (
        "xmake project",
        "xmake install",
        "distrib",
        "pnpm deploy:production",
        "SkyrimTogetherClient",
        "SkyrimImmersiveLauncher",
    ),
    "Code/immersive_launcher/xmake.lua": (
        'target("SkyrimImmersiveLauncher")',
        'set_basename("SkyrimTogether")',
        'add_deps("SkyrimTogetherClient")',
        'add_defines("TP_SKYRIM_VR=0")',
    ),
    "Code/immersive_launcher/TargetConfig.h": (
        'L"SkyrimTogether.dll"',
        'L"SkyrimTogetherVR.dll"',
        'L"Skyrim Special Edition"',
        "489830",
        'L"SkyrimSE"',
        '"SkyrimSE"',
    ),
    "Code/client/ScriptExtender.cpp": (
        'L"skse64"',
        "Pre anniversary Script Extender is unsupported",
        "kScriptExtenderStartExportOptional",
    ),
    "Code/client/Services/Generic/TransportService.cpp": (
        "Some dragon boi",
        "TP_SKYRIM_VR ?",
    ),
    "Code/immersive_launcher/stubs/FileMapping.cpp": (
        "SkyrimSE.exe",
        "skse64",
    ),
    "Code/immersive_launcher/stubs/DllBlocklist.cpp": (
        "SKYRIM TOGETHER REBORN marker",
        "Skyrim Together Reborn",
    ),
}


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def read_text(root: pathlib.Path, relative_path: str) -> str:
    return (root / relative_path).read_text(encoding="utf-8", errors="replace")


def main() -> int:
    root = repo_root()
    failures: list[str] = []

    for relative_path, tokens in REQUIRED_TOKENS.items():
        text = read_text(root, relative_path)
        missing = [token for token in tokens if token not in text]
        if missing:
            failures.append(f"{relative_path}: missing required VR-only tokens: {', '.join(missing)}")

    for relative_path, tokens in FORBIDDEN_TOKENS.items():
        text = read_text(root, relative_path)
        present = [token for token in tokens if token in text]
        if present:
            failures.append(f"{relative_path}: forbidden non-VR tokens present: {', '.join(present)}")

    print(f"Audited VR-only files: {len(set(REQUIRED_TOKENS) | set(FORBIDDEN_TOKENS))}")
    print(f"VR-only audit failures: {len(failures)}")
    for failure in failures:
        print(f"- {failure}")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
