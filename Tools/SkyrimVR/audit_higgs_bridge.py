#!/usr/bin/env python3
"""Audit the observation-only SkyrimTogetherVR HIGGS bridge."""

from __future__ import annotations

import pathlib
import re
import sys


REQUIRED_TOKENS = {
    "Code/higgs_bridge/main.cpp": (
        "SKSEPlugin_Query",
        "SKSEPlugin_Load",
        "kHiggsMessageGetInterface = 0xF9279A57",
        "kSkseMessagePostLoad = 0",
        "kSkseMessagePostPostLoad",
        "apMessage->type == kSkseMessagePostLoad || apMessage->type == kSkseMessagePostPostLoad",
        '"HIGGS"',
        "SkyrimTogetherVR.higgs",
        "std::atomic<HiggsPluginAPI::IHiggsInterface001*>",
        "AddPulledCallback",
        "AddGrabbedCallback",
        "AddDroppedCallback",
        "AddStashedCallback",
        "AddConsumedCallback",
        "AddCollisionCallback",
        "AddStartTwoHandingCallback",
        "AddStopTwoHandingCallback",
        "AddPostVrikPostHiggsCallback",
        "OnPostVrikPostHiggs",
        "CaptureHiggsSnapshot",
        "g_snapshotLock",
        "GetGrabbedObject",
        "IsHoldingObject",
        "IsTwoHanding",
        "GetFingerValues",
        "GetGrabTransform",
        "recentEventCount",
        "higgs.interfaceAvailable",
        "higgs.snapshotAvailable",
        "higgs.snapshotSequence",
    ),
    "Code/higgs_bridge/xmake.lua": (
        'target("SkyrimTogetherVRHiggsBridge")',
        'set_kind("shared")',
        'set_basename("SkyrimTogetherVRHiggsBridge")',
    ),
    "Code/xmake.lua": (
        'includes("higgs_bridge")',
        '"SkyrimTogetherVRHiggsBridge"',
    ),
    "BuildSkyrimTogetherVR-Windows.ps1": (
        "SkyrimTogetherVRHiggsBridge",
        "Data\\SKSE\\Plugins",
    ),
    "Tools/SkyrimVR/vr_handoff.py": (
        '"higgs": "SkyrimTogetherVR.higgs"',
        '"higgs": ("bridge.loaded", "bridge.sequence", "higgs.detected", "higgs.interfaceAvailable", "higgs.callbacksRegistered"',
        "higgsEventCount",
    ),
    "Tools/SkyrimVR/install_vr_prereqs.py": (
        "HIGGS 1.10.10",
        "SKSE/Plugins/higgs_vr.dll",
        "Scripts/HiggsVR.pex",
        "higgs_vr.esp",
        "--enable-plugins",
    ),
    "Code/client/Games/PapyrusFunctions.cpp": (
        'GetHandoffPath("SkyrimTogetherVR.higgs")',
        "AppendHiggsSummary",
        "AppendHiggsTelemetry",
        "higgs.interfaceAvailable",
        "higgs.callbacksRegistered",
        "higgs.snapshotAvailable",
        "left.holdingObject",
        "right.holdingObject",
    ),
    "Docs/SkyrimVR/higgs-compatibility.md": (
        "SkyrimTogetherVRHiggsBridge",
        "SkyrimTogetherVR.higgs",
        "observation-only",
        "PostLoad",
        "audit_higgs_bridge.py",
        "virtual method order",
        "do not query live HIGGS state",
    ),
    "Docs/SkyrimVR/windows-build.md": (
        "SkyrimTogetherVRHiggsBridge",
        "Data\\SKSE\\Plugins",
    ),
    "Docs/SkyrimVR/porting-status.md": (
        "SkyrimTogetherVRHiggsBridge",
        "SkyrimTogetherVR.higgs",
        "audit_higgs_bridge.py",
    ),
    "Docs/SkyrimVR/connection-only-mode.md": (
        "SkyrimTogetherVR.higgs",
        "HIGGS bridge",
    ),
}

FORBIDDEN_CALL_TOKENS = {
    "Code/higgs_bridge/main.cpp": (
        "->GrabObject",
        "->DisableHand",
        "->EnableHand",
        "->DisableWeaponCollision",
        "->EnableWeaponCollision",
        "->ForceWeaponCollisionEnabled",
        "->SetGrabTransform",
        "->SetSettingDouble",
        "->SetHiggsLayerBitfield",
        "->AddCollisionFilterComparisonCallback",
        "->AddPrePhysicsStepCallback",
    ),
}

UPSTREAM_HIGGS_INTERFACE = pathlib.Path("../_refs/higgs/include/higgsinterface001.h")
BRIDGE_HIGGS_INTERFACE = pathlib.Path("Code/higgs_bridge/main.cpp")


def extract_interface_block(text: str, interface_name: str) -> str:
    match = re.search(rf"struct\s+{re.escape(interface_name)}\b", text)
    if not match:
        return ""

    brace_start = text.find("{", match.end())
    if brace_start == -1:
        return ""

    depth = 0
    for index in range(brace_start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace_start + 1 : index]

    return ""


def extract_virtual_methods(text: str, interface_name: str) -> list[str]:
    block = extract_interface_block(text, interface_name)
    methods: list[str] = []
    for line in block.splitlines():
        stripped = line.strip()
        if not stripped.startswith("virtual "):
            continue

        match = re.search(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", stripped)
        if match:
            methods.append(match.group(1))

    return methods


def extract_function_block(text: str, function_name: str) -> str:
    match = re.search(rf"\b{re.escape(function_name)}\s*\(", text)
    if not match:
        return ""

    brace_start = text.find("{", match.end())
    if brace_start == -1:
        return ""

    depth = 0
    for index in range(brace_start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace_start + 1 : index]

    return ""


def audit_higgs_snapshot_lifecycle(root: pathlib.Path) -> list[str]:
    bridge_path = root / BRIDGE_HIGGS_INTERFACE
    if not bridge_path.exists():
        return [f"HIGGS bridge source is missing: {bridge_path}"]

    register_callbacks = extract_function_block(bridge_path.read_text(encoding="utf-8", errors="replace"), "RegisterCallbacks")
    if not register_callbacks:
        return ["could not extract HIGGS RegisterCallbacks body"]
    if "CaptureHiggsSnapshot();" in register_callbacks:
        return ["HIGGS RegisterCallbacks must not query live HIGGS state before PostVrikPostHiggs"]

    return []


def audit_higgs_interface_abi(root: pathlib.Path) -> tuple[list[str], int]:
    failures: list[str] = []
    upstream_path = (root / UPSTREAM_HIGGS_INTERFACE).resolve()
    bridge_path = root / BRIDGE_HIGGS_INTERFACE

    if not upstream_path.exists():
        return [f"upstream HIGGS reference header is missing: {upstream_path}"], 0
    if not bridge_path.exists():
        return [f"HIGGS bridge source is missing: {bridge_path}"], 0

    upstream_methods = extract_virtual_methods(
        upstream_path.read_text(encoding="utf-8", errors="replace"),
        "IHiggsInterface001",
    )
    bridge_methods = extract_virtual_methods(
        bridge_path.read_text(encoding="utf-8", errors="replace"),
        "IHiggsInterface001",
    )

    if not upstream_methods:
        failures.append("could not extract upstream IHiggsInterface001 virtual method list")
    if not bridge_methods:
        failures.append("could not extract bridge IHiggsInterface001 virtual method list")
    if upstream_methods and bridge_methods and upstream_methods != bridge_methods:
        failures.append("bridge IHiggsInterface001 virtual method order differs from upstream HIGGS")
        failures.append(f"  upstream: {', '.join(upstream_methods)}")
        failures.append(f"  bridge:   {', '.join(bridge_methods)}")

    return failures, len(upstream_methods)


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
            failures.append(f"{relative_path}: missing HIGGS bridge tokens: {', '.join(missing)}")

    for relative_path, tokens in FORBIDDEN_CALL_TOKENS.items():
        text = read_text(root, relative_path)
        present = [token for token in tokens if token in text]
        if present:
            failures.append(f"{relative_path}: forbidden mutating HIGGS call tokens present: {', '.join(present)}")

    abi_failures, abi_method_count = audit_higgs_interface_abi(root)
    failures.extend(abi_failures)
    failures.extend(audit_higgs_snapshot_lifecycle(root))

    print(f"Audited HIGGS bridge files: {len(set(REQUIRED_TOKENS) | set(FORBIDDEN_CALL_TOKENS))}")
    print(f"Audited IHiggsInterface001 virtual methods: {abi_method_count}")
    print(f"HIGGS bridge audit failures: {len(failures)}")
    for failure in failures:
        print(f"- {failure}")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
