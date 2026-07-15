#!/usr/bin/env python3
"""Static policy audit for the SKSEVR task-backed connection tick bridge."""

from __future__ import annotations

import pathlib
import sys


REQUIRED_TOKENS = {
    "Code/vr_common/VRTickBridge.h": (
        "kMappingHandleEnvironment",
        "kEndpointMagic",
        "kEndpointAbiVersion = 3",
        "enum class EndpointState",
        "struct alignas(8) Endpoint",
        "DispatchCallback",
        "BodyCaptureCallback",
        "BodyCaptureCallbackRva",
        "std::uint64_t, std::uint64_t, std::uint32_t",
        "std::is_trivially_copyable_v<Endpoint>",
    ),
    "Code/client/VRTickBridge.cpp": (
        "CreateFileMappingW",
        "SetEnvironmentVariableW(kMappingHandleEnvironment",
        "EndpointState::Prepared",
        "EndpointState::Ready",
        "EndpointState::Retired",
        "s_endpoint->ActivationThreadId = GetCurrentThreadId()",
        "std::uint32_t GetActivationThreadId() noexcept",
        "return s_endpoint->ActivationThreadId",
        "SkyrimTogetherVR SKSE task callback accepted",
        "aExecutorThreadId != currentThreadId",
        "InterlockedCompareExchange64(&s_lastDispatchSequence",
        "DispatchResult::InvalidSequence",
        "InterlockedExchange(&s_updatePermit, 1)",
        "bool ConsumeUpdatePermit() noexcept",
        "InterlockedExchange(&s_updatePermit, 0)",
        "TP_SKYRIM_VR_ENABLE_BODY_POSE_CAPTURE",
        "BodyPoseCapture::Activate()",
        "BodyPoseCapture::CaptureFromPostHiggs",
        "BodyPoseCapture::Retire()",
    ),
    "Code/vr_tick_bridge/main.cpp": (
        "SKSETaskInterface",
        "SKSEPapyrusInterface",
        "NativeFunction0<StaticFunctionTag, bool>",
        "NativeFunction1<StaticFunctionTag, bool, SInt32>",
        "PackValue<bool>",
        "GetTypeID<bool>",
        "UnpackValue<SInt32>",
        "GetTypeID<SInt32>",
        "StringCache::Ref::Ref()",
        "void* UnpackHandle(VMValue*, UInt32)",
        "RUNTIME_VR_VERSION_1_4_15",
        "SKSE_VERSION_RELEASEIDX",
        "IsSupportedRuntime",
        "apInfo->version = 2",
        "#undef max",
        "SkyrimTogetherVRTickBridge",
        "QueueClientUpdate",
        "g_taskInterface->AddTask(task)",
        "MapViewOfFile",
        "const volatile LONG",
        "MemoryBarrier()",
        "InterlockedIncrement64(&g_dispatchSequence)",
        "callback(g_endpoint->Epoch, sequence, executorThreadId)",
        "task_run_entry=",
        "task_dispose=",
        "VirtualQuery",
        "PAGE_EXECUTE_READ",
        "AllocationBase != reinterpret_cast<void*>(acEndpoint.ImageBase)",
        "acEndpoint.Reserved0 != 0",
    ),
    "Code/higgs_bridge/main.cpp": (
        "AddPostVrikPostHiggsCallback",
        "PublishPostHiggsBodyPose",
        "BodyCaptureCallback",
        "BodyCaptureCallbackRva",
        "MapEndpoint();",
        "bodyCapture.successCount=",
    ),
    "Code/client/Games/Skyrim/VR/VRBodyPoseCapture.cpp": (
        "TryAcquireSRWLockExclusive",
        "TryAcquireSRWLockShared",
        "GetTickCount64",
        "NPC Pelvis [Pelv]",
        "NPC L Thigh [LThg]",
        "NPC R Foot [Rft ]",
    ),
    "GameFiles/SkyrimVR/Scripts/source/SkyrimTogetherVerifyLaunchScript.psc": (
        "RearmCadence",
        "UnregisterForUpdate()",
        "RegisterForSingleUpdate(0.05)",
        "Event OnUpdate()",
        "SkyrimTogetherVRTickBridge.Tick()",
    ),
    "GameFiles/SkyrimVR/Scripts/source/SkyrimTogetherPlayerAliasScript.psc": (
        "Event OnCellAttach()",
        "Event OnLocationChange(Location akOldLocation, Location akNewLocation)",
        'RearmCadence("OnCellAttach")',
        'RearmCadence("OnLocationChange")',
    ),
    "GameFiles/SkyrimVR/Scripts/source/SkyrimTogetherVRMigrationScript.psc": (
        "Event OnCellAttach()",
        "Event OnLocationChange(Location akOldLocation, Location akNewLocation)",
        'RearmCadence("OnCellAttach")',
        'RearmCadence("OnLocationChange")',
    ),
    "GameFiles/SkyrimVR/Scripts/source/SkyrimTogetherVRTickBridge.psc": (
        "ScriptName SkyrimTogetherVRTickBridge Native Hidden",
        "Bool Function Tick() Global Native",
    ),
    "Tools/SkyrimVR/compile_papyrus.py": (
        '"SkyrimTogetherVRTickBridge.psc"',
    ),
    "Code/vr_tick_bridge/xmake.lua": (
        "LegacySksePrefix.h",
        '"/permissive"',
        'add_defines("RUNTIME", "IS_VR", "RUNTIME_VERSION=0x010400F1")',
        '"GameAPI.cpp"',
        '"Relocation.cpp"',
    ),
    "Code/vr_tick_bridge/LegacySksePrefix.h": (
        "#undef NOMINMAX",
        "#include <common/IPrefix.h>",
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
    "Code/client/VRTickBridge.cpp": (
        "g_appInstance",
        "World::Update",
        "TiltedOnlineApp.h",
    ),
    "Code/vr_tick_bridge/main.cpp": (
        "std::thread",
        "CreateThread",
        "AddPostVrik",
        "HiggsPluginAPI",
        "PlanckPluginAPI",
        "RegisterForSingleUpdate",
        "InterlockedCompareExchange(const_cast",
        "ready_thread_mismatch",
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
