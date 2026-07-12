#!/usr/bin/env python3
"""Static policy audit for the SKSEVR task-backed connection tick bridge."""

from __future__ import annotations

import pathlib
import sys


REQUIRED_TOKENS = {
    "Code/vr_common/VRTickBridge.h": (
        "kMappingHandleEnvironment",
        "kEndpointMagic",
        "kEndpointAbiVersion",
        "enum class EndpointState",
        "struct alignas(8) Endpoint",
        "DispatchCallback",
        "std::is_trivially_copyable_v<Endpoint>",
    ),
    "Code/client/VRTickBridge.cpp": (
        "CreateFileMappingW",
        "SetEnvironmentVariableW(kMappingHandleEnvironment",
        "EndpointState::Prepared",
        "EndpointState::Ready",
        "EndpointState::Retired",
        "s_endpoint->ReadyThreadId = GetCurrentThreadId()",
        "SkyrimTogetherVR SKSE task tick dispatched World::Update",
        "g_appInstance->Update()",
        "InterlockedCompareExchange(&s_dispatchInProgress, 1, 0)",
    ),
    "Code/vr_tick_bridge/main.cpp": (
        "SKSETaskInterface",
        "SKSEPapyrusInterface",
        "NativeFunction0<StaticFunctionTag, bool>",
        "PackValue<bool>",
        "GetTypeID<bool>",
        "RUNTIME_VR_VERSION_1_4_15",
        "SKSE_VERSION_RELEASEIDX",
        "IsSupportedRuntime",
        "SkyrimTogetherVRTickBridge",
        "QueueClientUpdate",
        "g_taskInterface->AddTask(task)",
        "MapViewOfFile",
        "VirtualQuery",
        "PAGE_EXECUTE_READ",
        "AllocationBase != reinterpret_cast<void*>(acEndpoint.ImageBase)",
        "acEndpoint.Reserved0 != 0",
        "callback(g_endpoint->Epoch)",
    ),
    "GameFiles/SkyrimVR/scripts/source/SkyrimTogetherVerifyLaunchScript.psc": (
        "StartTickBridge",
        "RegisterForSingleUpdate(0.05)",
        "Event OnUpdate()",
        "SkyrimTogetherVRTickBridge.Tick()",
    ),
    "GameFiles/SkyrimVR/scripts/source/SkyrimTogetherVRTickBridge.psc": (
        "ScriptName SkyrimTogetherVRTickBridge Hidden",
        "Bool Function Tick() Global Native",
    ),
    "Tools/SkyrimVR/compile_papyrus.py": (
        '"SkyrimTogetherVRTickBridge.psc"',
    ),
    "Code/vr_tick_bridge/xmake.lua": (
        "add_cxflags(\"/FIcommon/IPrefix.h\"",
        'add_defines("RUNTIME", "IS_VR", "RUNTIME_VERSION=0x010400F1")',
        '"GameAPI.cpp"',
        '"Relocation.cpp"',
    ),
    "BuildSkyrimTogetherVR-Windows.ps1": (
        "SkyrimTogetherVRTickBridge",
        "Resolve-SkseVrSdk",
        "Test-SkseVrSdkRoot",
        "SKSEVR_SDK_ROOT",
        "f03df5d8663f2c9a781f830fb0809c63a9a0e3b626d6d1a96e38493f81a3c9ad",
        "SkipPapyrusCompile",
    ),
}

FORBIDDEN_TOKENS = {
    "Code/vr_tick_bridge/main.cpp": (
        "std::thread",
        "CreateThread",
        "AddPostVrik",
        "HiggsPluginAPI",
        "PlanckPluginAPI",
        "RegisterForSingleUpdate",
        "g_appInstance",
        "World::Update",
    ),
    "Code/higgs_bridge/main.cpp": (
        "SkyrimTogetherVRTickBridge",
        "g_taskInterface->AddTask",
    ),
}


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def main() -> int:
    root = repo_root()
    failures: list[str] = []

    for relative_path, tokens in REQUIRED_TOKENS.items():
        path = root / relative_path
        text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
        for token in tokens:
            if token not in text:
                failures.append(f"{relative_path}: missing `{token}`")

    for relative_path, tokens in FORBIDDEN_TOKENS.items():
        path = root / relative_path
        text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
        for token in tokens:
            if token in text:
                failures.append(f"{relative_path}: forbidden `{token}`")

    print(f"Audited SKSEVR tick bridge files: {len(REQUIRED_TOKENS)}")
    print(f"SKSEVR tick bridge audit failures: {len(failures)}")
    for failure in failures:
        print(f"- {failure}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
