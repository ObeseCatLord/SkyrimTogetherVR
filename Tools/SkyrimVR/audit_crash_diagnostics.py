#!/usr/bin/env python3
"""Keep the VR crash diagnostics and hook-failure guard from regressing."""

from __future__ import annotations

import pathlib
import re

def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def require_tokens(root: pathlib.Path, path: pathlib.Path, tokens: tuple[str, ...]) -> list[str]:
    text = path.read_text(encoding="utf-8")
    return [f"{path.relative_to(root)}: missing {token}" for token in tokens if token not in text]


def main() -> int:
    root = repo_root()
    failures = []
    failures.extend(
        require_tokens(
            root,
            root / "Code" / "client" / "CrashHandler.cpp",
            (
                "PruneCrashDumps",
                "kRetainedCrashDumpCount = 5",
                "MiniDumpWithUnloadedModules",
                "MiniDumpWithFullMemoryInfo",
                "MiniDumpWithCodeSegs",
                "STVR_FULL_MEMORY_DUMP",
                "MiniDumpWithFullMemory",
                "dumpWritten = MiniDumpWriteDump",
                "coredump write failed",
                "(IS_MASTER) && (!defined(TP_SKYRIM_VR) || TP_SKYRIM_VR != 1)",
            ),
        )
    )
    failures.extend(
        require_tokens(
            root,
            root / "Code" / "client" / "SkyrimVM64.cpp",
            (
                "POINTER_SKYRIMSE(TMainDraw, cMainDraw, 35560)",
                "TiltedPhoques::ThisCall(MainDraw, apThis, aUnk, aMainMenuOpen);",
                "s_clientUpdateInProgress.test_and_set(std::memory_order_acquire)",
                "beforeConsume.Ready",
                "beforeUpdate.Ready",
                "TryConsumeUpdatePermit(sequence)",
                "RecordOwnerUpdateCompleted(sequence)",
            ),
        )
    )
    failures.extend(
        require_tokens(
            root,
            root / "Code" / "client" / "VRTickBridge.cpp",
            (
                "EndpointState::Retired",
                "s_pendingDispatchSequence",
                "SkyrimTogetherVR update owner starved:",
                "aExecutorThreadId != currentThreadId",
                "DispatchResult::InvalidSequence",
            ),
        )
    )
    failures.extend(
        require_tokens(
            root,
            root / "Docs" / "SkyrimVR" / "post-character-owner-starvation-senior-disposition-20260714.md",
            ("0xC53E1B", "0xAB4F26", "0x133B008", "0xF43AD1", "0x131AAB"),
        )
    )

    vm_text = (root / "Code" / "client" / "SkyrimVM64.cpp").read_text(encoding="utf-8")
    vr_match = re.search(r"#if TP_SKYRIM_VR(?P<body>.*?)#else", vm_text, re.DOTALL)
    vr_block = vr_match.group("body") if vr_match else ""
    for forbidden in (
        "POINTER_SKYRIMSE(TMainLoop",
        "HookVMDestructor",
        "TP_HOOK(&MainLoop",
        "HookVrVmUpdate",
        "TVrVmUpdate",
        "POINTER_SKYRIMSE(TVrVmUpdate",
    ):
        if forbidden in vr_block:
            failures.append(f"Code/client/SkyrimVM64.cpp: VR block contains retired hook `{forbidden}`")

    client_main = (root / "Code" / "client" / "main.cpp").read_text(encoding="utf-8")
    retire_index = client_main.find("SkyrimTogetherVR::TickBridge::Retire();", client_main.find("void RunTiltedEnd()"))
    end_main_index = client_main.find("g_appInstance->EndMain();", client_main.find("void RunTiltedEnd()"))
    if retire_index < 0 or end_main_index < 0 or retire_index >= end_main_index:
        failures.append("Code/client/main.cpp: endpoint retirement must precede client teardown")
    for token in (
        "static int __stdcall HookVrWinMain",
        "~ShutdownGuard() { RunTiltedEnd(); }",
        "TP_HOOK(&s_vrWinMain, HookVrWinMain);",
        "void RunTiltedEnd() noexcept",
        "g_appInstance->EndMain();",
        "if (!g_appInstance)",
    ):
        if token not in client_main:
            failures.append(f"Code/client/main.cpp: missing reachable WinMain teardown token `{token}`")
    failures.extend(
        require_tokens(
            root,
            root / "Libraries" / "TiltedReverse" / "Code" / "reverse" / "src" / "FunctionHook.cpp",
            (
                "HookInstallSummary FunctionHookManager::InstallDelayedHooks",
                "m_ownsHook",
                "if (createStatus != MH_OK)",
                "if (enableStatus != MH_OK)",
                "MH_RemoveHook(hook.m_pSystemFunction)",
                "for (auto it = m_installedHooks.rbegin(); it != m_installedHooks.rend(); ++it)",
                "rolling back all hooks after a delayed MinHook failure",
            ),
        )
    )
    failures.extend(
        require_tokens(
            root,
            root / "Code" / "client" / "Games" / "Skyrim" / "Events" / "EventDispacther.cpp",
            (
                "blocked an unvalidated VR BSTEventSource::AddEventSink request",
                "blocked an unvalidated VR BSTEventSource::RemoveEventSink request",
            ),
        )
    )
    failures.extend(
        require_tokens(
            root,
            root / "Code" / "immersive_launcher" / "stubs" / "FileMapping.cpp",
            (
                "IsExactBareModuleNameMatch",
                "WideCharToMultiByte(CP_ACP",
                "TP_GetModuleFileNameW(aModule, pBuffer, aBufferSize)",
            ),
        )
    )
    failures.extend(
        require_tokens(
            root,
            root / "Code" / "client" / "main.cpp",
            (
                "hookSummary.Failures != 0",
                "refused to enter Skyrim VR",
                "hook install verified",
            ),
        )
    )
    failures.extend(
        require_tokens(
            root,
            root / "Code" / "client" / "Games" / "Misc" / "BSScript.cpp",
            (
                "#if defined(TP_SKYRIM_VR) && TP_SKYRIM_VR",
                "The flat-client",
                "install no BSScript",
                "#if !defined(TP_SKYRIM_VR) || !TP_SKYRIM_VR",
            ),
        )
    )
    failures.extend(
        require_tokens(
            root,
            root / "Code" / "immersive_launcher" / "Launcher.cpp",
            (
        "if (!RunTiltedInit(LC->gamePath, runtimeVersion.Major, runtimeVersion.Minor, runtimeVersion.Revision, runtimeVersion.Build))",
                "could not install its VR hook set",
            ),
        )
    )

    print(f"Crash diagnostic audit failures: {len(failures)}")
    for failure in failures:
        print(f"- {failure}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
