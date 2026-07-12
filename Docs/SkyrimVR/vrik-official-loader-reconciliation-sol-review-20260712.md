# Senior Review Brief: Reconcile VRIK Stall With Official SKSEVR Loader

## Decision

Determine whether any launch-timing source change is justified for the VRIK
process-attach stall. Do not accept a post-game-CRT SKSE deferment unless it is
compatible with the actual official SKSEVR 2.0.12 control flow.

## Verified contradiction

The immediately preceding senior review attributed VRIK's attach stall to
loading SKSE before the mapped game CRT, and suggested delaying SKSE until
`VCRUNTIME140!__telemetry_main_invoke_trigger`. Local official SKSEVR source
does not support that premise as stated:

| Fact | Evidence |
| --- | --- |
| Official loader creates `SkyrimVR.exe` with `CREATE_SUSPENDED`. | `sksevr_2_00_12/src/sksevr/skse64_loader/main.cpp:214-229` |
| It then calls `InjectDLLThread(..., true, ...)`, which uses `CreateRemoteThread(... LoadLibraryA ...)` and waits for completion. | `sksevr_2_00_12/src/sksevr/skse64_loader/main.cpp:265-304`; `skse64_loader_common/Inject.cpp:42-81` |
| Only after that injected thread returns does it resume the primary game thread. | same loader `main.cpp:291-304` |
| SKSEVR's `DllMain(DLL_PROCESS_ATTACH)` directly calls `SKSE64_Initialize`. | `sksevr_2_00_12/src/sksevr/skse64/skse64.cpp:130-137` |
| `SKSE64_Initialize` directly invokes `g_pluginManager.Init()`. | same `skse64.cpp:42-120`, specifically line 90 |
| `PluginManager::Init` directly invokes `InstallPlugins()`, which calls each native plugin's `LoadLibrary`. | `sksevr_2_00_12/src/sksevr/skse64/PluginManager.cpp:124-145`, `264+` |

Thus official SKSEVR also loads VRIK from a helper loader thread before the
primary game thread is resumed. A generic claim that VRIK cannot process-attach
before the Skyrim CRT has run is incompatible with this source unless a real
difference is identified.

## Current direct runtime evidence

* In the mapped launcher, Wine logs VRIK `MODULE_InitDLL(PROCESS_ATTACH) - CALL`
  but no `RETURN`; VRIK's `LoadLibrary` remains pending for about 59 seconds.
* Wine logs helper thread `0190` waiting on a zero-owner critical section at
  `0x1436F2F10` within the mapped Skyrim image. Other threads wait on Wine's
  loader section, blocked by `0190`.
* VRIK has an 8-byte TLS template and an empty TLS callback table; its user
  `DllMain` is trivial. Its PE entrypoint is `_DllMainCRTStartup` with 191
  `.CRT$XCU` slots.
* The current target includes full VRIK 0.8.5, HIGGS, PLANCK, all bridge DLLs,
  SKSEVR 2.0.12, and VR Address Library. A bridge-exclusion control run produced
  the same VRIK stop.
* The attempted local WineDbg GDB proxy attached but GE-Proton's proxy reported
  `qSupported` packet errors and did not provide a usable stopped-thread prompt.
  It is not usable evidence.

## Current mapper facts

* `ExeLoader` overwrites the launcher's image at SkyrimVR's normal base and
  enters the mapped `gameMain` only after `BootstrapScriptExtenderOnLoaderThread`.
* Module-name/file-name APIs are selectively hooked. `LdrGetDllFullName` is
  disabled because the implementation is contract-invalid. `LdrGetDllHandleEx`
  compares a byte-valued `UNICODE_STRING.Length` as a wchar count.
* The loaded image has a normal base address but is not a conventional Wine LDR
  main-module entry for `SkyrimVR.exe`.

## Required review outcome

1. Reconcile the official loader source with the mapped-launcher failure.
2. State whether the evidence supports any game-CRT timing change. If not,
   explicitly reject it.
3. Pick one next action that discriminates the remaining actual differences:
   manual image mapping/section initialization, PEB-LDR identity, API hooks,
   helper-thread setup, or VRIK-version behavior.
4. If a source change is warranted, provide exact narrow scope and explain why
   it remains valid under official pre-resume SKSEVR loading. Otherwise specify
   a no-source diagnostic/control.
5. Review is read-only: do not edit, build, launch, deploy, or modify the
   server. Do not propose removing VRIK or a broad mapper rewrite.

## Review Disposition

| Recommendation | Disposition | Rationale |
| --- | --- | --- |
| Reject game-CRT timing because the official loader creates a pre-resume remote loader thread. | Rejected | The Steam executable uses the separate `kProcType_Steam` branch, which injects `sksevr_steam_loader.dll`, not the SKSEVR core. |
| Use the official Steam loader shim before mapped game entry, then enter `gameMain`. | Adopted | The shim patches the exact VCRUNTIME IAT boundary used by official SKSEVR and defers core/plugin loading to the resumed game thread. |
| Keep helper-thread/TLS preparation and the pre-game mapper barrier. | Adopted | The shim itself is still loaded before game entry and requires the mapped process view; only the core/plugin chain is deferred. |
| Repair PEB/LDR identity, section layout, or MinHook behavior in this build. | Deferred | Those are credible residual risks but not needed to test the phase-correct official shim first. |
