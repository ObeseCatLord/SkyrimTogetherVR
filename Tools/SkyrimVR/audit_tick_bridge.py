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
        "InterlockedExchange64(&s_pendingDispatchSequence, sequence)",
        "bool TryConsumeUpdatePermit(std::uint64_t& arSequence) noexcept",
        "InterlockedExchange64(&s_pendingDispatchSequence, 0)",
        "Diagnostics GetDiagnostics() noexcept",
        "void RecordOwnerHeartbeat() noexcept",
        "void RecordOwnerUpdateCompleted(std::uint64_t aSequence) noexcept",
        "SkyrimTogetherVR update owner starved:",
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
    "Code/client/TiltedOnlineApp.cpp": (
        "SkyrimTogetherVR::TickBridge::Retire();",
        "World::Get().ctx().at<VRLifecycleService>().BeginTeardown();",
    ),
    "Code/client/main.cpp": (
        "static int __stdcall HookVrWinMain",
        "~ShutdownGuard() { RunTiltedEnd(); }",
        "TP_HOOK(&s_vrWinMain, HookVrWinMain);",
        "void RunTiltedEnd() noexcept",
        "g_appInstance->EndMain();",
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

    teardown_path = root / "Code" / "client" / "TiltedOnlineApp.cpp"
    teardown_text = teardown_path.read_text(encoding="utf-8", errors="replace") if teardown_path.exists() else ""
    retire_index = teardown_text.find("SkyrimTogetherVR::TickBridge::Retire();")
    lifecycle_index = teardown_text.find("World::Get().ctx().at<VRLifecycleService>().BeginTeardown();")
    if retire_index < 0 or lifecycle_index < 0 or retire_index >= lifecycle_index:
        failures.append("Code/client/TiltedOnlineApp.cpp: tick bridge must retire before VR lifecycle teardown")

    client_main = (root / "Code" / "client" / "main.cpp").read_text(encoding="utf-8", errors="replace")
    hook_start = client_main.find("static int __stdcall HookVrWinMain")
    hook_end = client_main.find("static bool InstallVrWinMainLifecycleHook", hook_start)
    hook_body = client_main[hook_start:hook_end] if hook_start >= 0 and hook_end >= 0 else ""
    end_start = client_main.find("void RunTiltedEnd() noexcept")
    end_body = client_main[end_start:] if end_start >= 0 else ""
    if "~ShutdownGuard() { RunTiltedEnd(); }" not in hook_body or "g_appInstance->EndMain();" not in end_body:
        failures.append("Code/client/main.cpp: mapped VR WinMain must reach idempotent client teardown")

    print(f"Audited SKSEVR tick bridge files: {len(REQUIRED_TOKENS)}")
    print(f"SKSEVR tick bridge audit failures: {len(failures)}")
    for failure in failures:
        print(f"- {failure}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
