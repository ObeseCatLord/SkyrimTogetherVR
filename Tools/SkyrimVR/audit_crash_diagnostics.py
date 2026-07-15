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
                "CrashHandler::TopLevelCrashHandler",
                "SetUnhandledExceptionFilter(&CrashHandler::TopLevelCrashHandler)",
                "s_pPreviousUnhandledFilter(apExceptionInfo)",
                "CaptureCrashContext(apExceptionInfo)",
                "TopLevelCrashHandler: access type=",
                "TopLevelCrashHandler: stack return=",
                "FlushCrashLogs()",
                "disposition == EXCEPTION_CONTINUE_EXECUTION",
                "if (disposition == EXCEPTION_CONTINUE_SEARCH)",
                "A terminal disposition should produce at most one capture",
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
            root / "Code" / "client" / "CrashContext.h",
            (
                "struct CrashContextSnapshot",
                "ReadProcessMemory(",
                "record.ExceptionInformation[0]",
                "record.ExceptionInformation[1]",
                "context.Rip",
                "context.Rsp",
                "context.Rcx",
                "context.Rdx",
            ),
        )
    )
    failures.extend(
        require_tokens(
            root,
            root / "Code" / "tests" / "crash_context.cpp",
            (
                'TEST_CASE("Crash context captures x64 registers and access details")',
                'TEST_CASE("Crash context rejects unreadable stack memory without faulting")',
                "REQUIRE_FALSE(snapshot.HasStackReturn)",
            ),
        )
    )
    failures.extend(
        require_tokens(
            root,
            root / "Code" / "tests" / "crash_handler_windows.cpp",
            (
                'TEST_CASE("Frame-handled access violations do not reach the STVR top-level filter")',
                'TEST_CASE("Crash handler installs outermost and preserves the existing filter")',
                'TEST_CASE("Crash handler propagates prior filter dispositions")',
                'TEST_CASE("Recursive crash-handler entry does not recurse or deadlock")',
                "EXCEPTION_CONTINUE_EXECUTION",
                "EXCEPTION_EXECUTE_HANDLER",
                "EXCEPTION_CONTINUE_SEARCH",
                "GetInvocationCountForTesting() == 0",
                "s_priorCalls.load(std::memory_order_relaxed) == 1",
            ),
        )
    )
    failures.extend(
        require_tokens(
            root,
            root / "Code" / "tests" / "xmake.lua",
            (
                '"TP_CRASH_HANDLER_TESTING=1"',
                'add_includedirs("../../build")',
                '"../client/CrashHandler.cpp"',
                'add_packages("spdlog")',
                '"dbghelp"',
            ),
        )
    )

    crash_handler_text = (root / "Code" / "client" / "CrashHandler.cpp").read_text(encoding="utf-8")
    for forbidden in (
        "VectoredExceptionHandler",
        "AddVectoredExceptionHandler",
        "spdlog::shutdown()",
        "SetUnhandledExceptionFilter(CrashHandler::",
    ):
        if forbidden in crash_handler_text:
            failures.append(f"Code/client/CrashHandler.cpp: forbidden terminal first-chance handler pattern `{forbidden}`")

    filter_start = crash_handler_text.find("LONG WINAPI CrashHandler::TopLevelCrashHandler")
    filter_end = crash_handler_text.find("LONG CrashHandler::InvokeForTesting", filter_start)
    filter_body = crash_handler_text[filter_start:filter_end] if filter_start >= 0 and filter_end >= 0 else ""
    previous_filter_index = filter_body.find("s_pPreviousUnhandledFilter(apExceptionInfo)")
    dump_index = filter_body.find("WriteCrashDump(apExceptionInfo)")
    if previous_filter_index < 0 or dump_index < 0 or previous_filter_index >= dump_index:
        failures.append("Code/client/CrashHandler.cpp: prior top-level filter must get first refusal before STVR writes a dump")

    if "SetUnhandledExceptionFilter(" in filter_body:
        failures.append("Code/client/CrashHandler.cpp: top-level callback must not mutate the process-wide filter during dispatch")
    if filter_body.count("s_handlingCrash.clear(std::memory_order_release)") != 1:
        failures.append("Code/client/CrashHandler.cpp: terminal capture guard must only re-arm on prior CONTINUE_EXECUTION")

    app_text = (root / "Code" / "client" / "TiltedOnlineApp.cpp").read_text(encoding="utf-8")
    logger_index = app_text.find("set_default_logger(logger);")
    crash_install_index = app_text.find("m_crashHandler.Install();")
    if logger_index < 0 or crash_install_index < 0 or logger_index >= crash_install_index:
        failures.append("Code/client/TiltedOnlineApp.cpp: install the top-level crash handler only after the logger exists")
    constructor_start = app_text.find("TiltedOnlineApp::TiltedOnlineApp()")
    constructor_end = app_text.find("TiltedOnlineApp::~TiltedOnlineApp()", constructor_start)
    begin_main_start = app_text.find("bool TiltedOnlineApp::BeginMain()")
    begin_main_end = app_text.find("bool TiltedOnlineApp::EndMain()", begin_main_start)
    constructor_body = app_text[constructor_start:constructor_end]
    begin_main_body = app_text[begin_main_start:begin_main_end]
    if "m_crashHandler.Install();" in constructor_body or "m_crashHandler.Install();" not in begin_main_body:
        failures.append("Code/client/TiltedOnlineApp.cpp: install the crash handler in BeginMain after SKSEVR plugin loading")
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
        'LogShutdownPhase("winmain.original.returned")',
        'LogShutdownPhase("winmain.detour.returning")',
        'LogShutdownPhase("run_tilted_end.done")',
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
            root / "Code" / "client" / "ShutdownDiagnostics.h",
            (
                "inline void LogShutdownPhase(const char* apPhase) noexcept",
                'spdlog::info("SkyrimTogetherVR shutdown phase={}", apPhase)',
                "pLogger->flush();",
                "catch (...)",
            ),
        )
    )
    failures.extend(
        require_tokens(
            root,
            root / "Code" / "client" / "World.cpp",
            (
                'LogShutdownPhase("transport.close.begin")',
                'LogShutdownPhase("transport.close.done")',
                'LogShutdownPhase("service.vr_connection.erase.begin")',
                'LogShutdownPhase("service.vr_connection.erase.done")',
                'LogShutdownPhase("world.locator.reset.begin")',
                'LogShutdownPhase("world.locator.reset.done")',
            ),
        )
    )
    failures.extend(
        require_tokens(
            root,
            root / "Code" / "immersive_launcher" / "Launcher.cpp",
            ('LogShutdownPhase("launcher.game_main.returned")',),
        )
    )

    for relative_path in (
        "Code/client/main.cpp",
        "Code/client/TiltedOnlineApp.cpp",
        "Code/client/World.cpp",
        "Code/immersive_launcher/Launcher.cpp",
    ):
        phase_text = (root / relative_path).read_text(encoding="utf-8")
        if 'spdlog::info("SkyrimTogetherVR shutdown phase=' in phase_text:
            failures.append(f"{relative_path}: shutdown diagnostics must use the shared noexcept phase logger")
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
