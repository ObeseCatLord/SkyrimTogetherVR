# Skyrim Together VR Shutdown 0xAB4F26 Senior Disposition

## Decision

The next build will correct crash-handler semantics and instrument the existing
shutdown path. It will not speculate that MinHook removal, world destruction,
or the Havok routine at `SkyrimVR.exe+0xAB4F26` is the root cause without a
phase boundary or a true unhandled-exception context proving that claim.

The previous build already proved startup, character finalization, lifecycle
readiness, server admission, and current-cell synchronization. This patch is
therefore limited to terminal crash capture and normal-exit observability.

## Review Disposition

| Senior recommendation | Disposition | Implementation |
| --- | --- | --- |
| Remove terminal work from the first-chance vectored handler. | Adopted | `CrashHandler` now uses `SetUnhandledExceptionFilter`; no client VEH or `spdlog::shutdown()` remains. |
| Install after SKSEVR plugins establish their filters. | Adopted | Installation moved from `TiltedOnlineApp` construction to `BeginMain`, the first verified callback after launcher-side SKSEVR bootstrap. |
| Preserve the previous filter and its disposition. | Adopted | The saved filter runs first. `CONTINUE_EXECUTION` returns immediately and rearms STVR; `EXECUTE_HANDLER` gets context-only STVR diagnostics; `CONTINUE_SEARCH` gets the full fallback dump attempt. |
| Prevent recursive or concurrent duplicate captures. | Adopted | An atomic guard remains latched for terminal dispositions and clears only when the prior filter resumes execution. |
| Capture register/access context without directly dereferencing a damaged stack. | Adopted | `CrashContextSnapshot` records RIP/RSP/RCX/RDX, AV operation/address, and reads the stack top through guarded `ReadProcessMemory`. |
| Make phase diagnostics no-throw and durable. | Adopted | All WinMain, hook, transport, service, locator, and launcher markers use one `noexcept` helper that catches sink failures and flushes immediately. |
| Add Windows crash-policy coverage. | Adopted | `TPTests` compiles the real `CrashHandler.cpp`. A dedicated subprocess probe exercises a real frame-handled AV without Catch2's fatal-exception handler; parent tests cover outermost installation, all prior dispositions, guard latching/rearming, recursive entry, context extraction, and unreadable RSP. |
| Move dumps out of process. | Deferred | Microsoft documents deadlock risk for in-process `MiniDumpWriteDump`. The inherited dump remains best effort for this diagnostic build; replacing it requires a separate crash-reporter protocol and is not needed to distinguish this shutdown fault. |
| Skip unhook or retain the world for process lifetime. | Rejected pending evidence | The same RVA predates the current unhook/world changes, and the offline XRizer control also fails to exit cleanly. Phase evidence must identify the failing boundary first. |

## Static Gates

`audit_crash_diagnostics.py` now rejects the old terminal VEH, logging shutdown,
filter mutation during dispatch, early constructor installation, a rearmed
terminal capture guard, direct throwable phase logging, and missing Windows
policy tests. Existing tick-bridge teardown ordering remains audited.

## Runtime Gate

1. Build the exact clean revision on Windows and run `TPTests`.
2. Install the default package and pass strict package/Papyrus audits.
3. Reach Realm of Lorkhan, connect, and verify server admission and current-cell
   synchronization.
4. Exit through normal `Alt+F4` and record the last completed shutdown phase.
5. Require one server-side disconnect, no duplicate client, and no new terminal
   fault capture. If Linux XRizer still hangs after
   `launcher.game_main.returned`, classify that separately from STVR teardown.

## Residual Risk

`SetUnhandledExceptionFilter` is process-global and a component installing a
new filter after `BeginMain` can still replace STVR. Reasserting STVR repeatedly
is not safe because it can create a filter cycle. Runtime logs must therefore
confirm the expected handler on the tested mod stack; a future crash-reporter
process is the durable cross-mod solution.
