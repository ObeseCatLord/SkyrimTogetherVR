# Final Review Brief: SKSEVR Steam-Shim Startup

## Goal and boundary

Decide whether the current narrow change is the correct last source change before
a Windows build and VR test. The goal is to avoid the VRIK process-attach stall
caused by loading the SKSEVR core before the mapped Skyrim VR executable reaches
its Steam/VCRUNTIME startup callback. This is a solo operator port: retain the
official SKSEVR loader sequence and avoid a mapper, loader, or CommonLib rewrite.

## Verified evidence

| Claim | Evidence |
| --- | --- |
| Skyrim VR is classified as Steam by official SKSEVR. | Local `sksevr_2_00_12/src/sksevr/IdentifyEXE.cpp` and the target `.bind` file. |
| Official Steam launch injects `sksevr_steam_loader.dll` before resume. | Local `sksevr_2_00_12/src/sksevr/skse64_steam_loader/main.cpp`. |
| The shim patches `VCRUNTIME140!__telemetry_main_invoke_trigger`, then `HookMain` loads the core. | Same local official source. |
| The previous direct-core launch stalls inside VRIK process attach. | Target SKSE log and Wine module trace: VRIK `DLL_PROCESS_ATTACH` entered with no return; loader critical section blocks. |
| The pre-entry bootstrap in `Launcher.cpp` runs before `LC->gameMain()`. | `Code/immersive_launcher/Launcher.cpp`. |
| Existing code directly loaded `sksevr_1_4_15.dll` in that pre-entry helper. | Parent version of `Code/client/ScriptExtender.cpp`. |
| Current code loads only the official Steam shim before `gameMain`, dynamically observes the core later, and retains the existing helper/TLS barrier. | Current `Code/client/ScriptExtender.cpp`, `Code/immersive_launcher/Launcher.cpp`. |
| Authentication can query SKSE state after deferred core loading. | Current `Code/client/Services/Generic/TransportService.cpp`. |

## Current implementation

1. `LoadScriptExender()` short-circuits only if the matching core is already
   loaded from the normalized expected game-root path and has a verified
   supported version resource.
2. Otherwise it validates core plus `sksevr_steam_loader.dll`, validates both
   DLL version resources match, and loads only the shim.
3. The mapped game enters through `LC->gameMain()`; the shim is expected to load
   the core at the official VCRUNTIME callback.
4. `IsScriptExtenderLoaded()` resolves the core with `GetModuleHandleW` on every
   first observation rather than treating shim presence as core presence.
5. The first VR authentication request retries core observation for at most
   250 ms only after a successful bootstrap result, then truthfully reports the
   resulting active state.
6. Package audits require both official SKSEVR files and the VR policy audit
   asserts the shim behavior token set.

## Open decisions and current lean

### 1. Adopt official shim timing (current lean: adopt)

Options: (a) current shim-only pre-entry path, (b) retain direct core load, (c)
rewrite the mapped executable/PEB loader behavior. Reject (b) because it has
measured VRIK attach failure. Reject (c) now because it is high-risk and the
official shim is the exact Steam bootstrap mechanism.

### 2. Authentication readiness (current lean: bounded 250 ms retry)

Options: (a) current bounded retry, (b) report inactive immediately, (c) defer
the entire connection state machine. Reject (b) because it creates a timing
false-negative after successful shim install. Reject (c) because it broadens a
startup fix into transport-state refactoring without evidence that 250 ms is
insufficient.

### 3. Preloaded core fallback (current lean: accept it)

The code recognizes an already loaded matching core before requiring the shim.
This retains a valid path for environments where the official loader has already
performed its work. It does not load the core itself.

## Suspected overlap

The shim timing and the 250 ms authentication observation are one lifecycle
contract, but only the former changes actual loading. Do not conflate a delayed
observation with permission to claim the core is active before it is loaded.

## Review request

Verify the facts against the named current files and local official SKSEVR source.
Prioritize correctness/ABI/lifecycle risks that could force another Windows
rebuild. Recommend only changes necessary before that rebuild. Do not review
unrelated VR features, CommonLib, bridges, server code, mapper overhaul, or full
game behavior. Return a concise prioritized disposition, with file/line evidence.
