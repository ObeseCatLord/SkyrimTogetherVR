# Skyrim Together VR Shutdown 0xAB4F26 Senior Brief

## Goal And Scope

Choose the smallest correct fix for the repeatable Skyrim VR shutdown access
violation at `SkyrimVR.exe+0xAB4F26` after the default connection-only client
has successfully connected. This is a distributable VR port, so clean process
teardown is a release gate, but process-exit cleanup should not be made more
complex than necessary. Review the VR WinMain detour, hook uninstall, client
world/transport teardown, and mapped-launcher exit path. Do not re-review menu
lifecycle, server protocol, gameplay hooks, avatar/FBT relays, packaging, or
controller automation except where they constrain shutdown.

The reviewer is read-only. Verify the brief against source and local artifacts
before critiquing. Return a prioritized decision and one coherent pre-build
patch/proof set; do not edit files.

## Environment Facts

| Item | Value |
| --- | --- |
| Repository | `/home/obesecatlord/Documents/SkyrimModding/SkyrimTogetherVR` |
| Reviewed revision | `de0b69ff81cbb09d2aa1fccfdddafabb84d3b663` |
| Original comparison | local branch `original-skyrim-together` |
| Game/runtime | Skyrim VR 1.4.15, SKSEVR 2.0.12, GE-Proton 10-34, XRizer/Monado |
| Client mode | default `releasedbg`, connection-only, active verified draw owner |
| Compatibility mods | VRIK, HIGGS, PLANCK; their gameplay surfaces remain gated |
| Server | exactly one remote Docker container on UDP 26099 |
| Test exit action | normal `Alt+F4` window close after connection proof |
| Fault | `c0000005`, thread 320, `0x140AB4F26` |
| Address context | RVA lies in Havok code, `0xADA` before VR ID 60551 (`hkpWorld::CastRay`) |

## Verified Runtime Evidence

| Claim | Status | Evidence |
| --- | --- | --- |
| Exact clean source built on Windows. | `[verified: WinBoat worktree, manifest, log]` | HEAD/manifest are `de0b69ff`; source dirty is false; wrapper exited zero. |
| Native tests passed. | `[verified: Windows TPTests]` | 262 assertions in 15 test cases, including the new tri-state VR menu-map probe. |
| Package and installed target passed strict audits. | `[verified: Linux package audit]` | Zero failures; SKSEVR, Address Library, VRIK, HIGGS, PLANCK, package hashes, and Papyrus hashes passed. |
| Character creation and connection succeeded. | `[verified: deterministic DevBench, handoff, server log]` | RaceSex finalized, Realm of Lorkhan loaded, lifecycle became ready, client became online as player 2, current interior cell synchronized, and server admitted the ten-mod client. |
| No crash occurred during startup, RaceSex, lifecycle transition, or connection. | `[verified: log and dump timestamps]` | The first fault follows explicit shutdown; no new `.dmp` was written because `MiniDumpWriteDump` returned access denied under Proton. |
| Shutdown began on the activation owner. | `[verified: client log]` | Thread 320 retired TickBridge and logged `WinMain lifecycle shutdown hook reached`. |
| Transport close completed far enough to publish disconnect. | `[verified: client log]` | `Disconnected from server 4` was logged 38 ms after shutdown entry. |
| The access violation followed 30 ms later. | `[verified: client log]` | Fault at `0x140AB4F26`, thread 320, then dump write failed. |
| The process remained stuck until explicitly terminated. | `[verified: process list]` | Proton, Steam shim, and mapped launcher remained after the exception workaround. |
| The signature is historical, not novel. | `[verified: rotated client log]` | Older runs faulted at the same RVA on threads 304 and 320. |
| The repository declares this signature a release gate. | `[verified: docs/audit]` | `post-character-owner-starvation-senior-disposition-20260714.md` says no new dump may match `0xAB4F26`; `audit_crash_diagnostics.py` preserves that token but does not inspect current runtime logs. |

## Verified Source Evidence

1. `[verified: Code/client/main.cpp:51-59]` `HookVrWinMain` creates a stack
   `ShutdownGuard`, calls the original through the MinHook trampoline, then the
   guard calls `RunTiltedEnd()` while the WinMain detour itself is still active.
2. `[verified: Code/client/main.cpp:329-346]` `RunTiltedEnd()` retires TickBridge
   and calls `g_appInstance->EndMain()` on that same thread.
3. `[verified: Code/client/TiltedOnlineApp.cpp:137-145]` `EndMain()` begins
   lifecycle teardown, calls `UninstallHooks()`, then destroys `World`.
4. `[verified: Libraries/TiltedReverse/.../FunctionHook.cpp:110-134]`
   `UninstallHooks()` disables and removes all MinHook targets in reverse order,
   restores trampoline pointer variables, clears the installed-hook vector,
   then restores IAT hooks.
5. `[verified: install order]` The installed delayed hooks are WinMain first and
   Main::Draw second, so reverse teardown removes Main::Draw then removes the
   currently executing WinMain hook from inside its own detour.
6. `[verified: Code/client/World.cpp:271-390]` `World::Shutdown()` marks teardown,
   calls `m_transport.Close()` while subscribers exist, then erases enabled
   services in explicit order. In the default package, disconnected handlers
   are limited to the connection/string-cache/transport lane; cell-only
   `PlayerService` does not subscribe to `DisconnectedEvent`.
7. `[verified: original branch]` Original Skyrim Together leaves
   `TiltedOnlineApp::UninstallHooks()` empty; its main bootstrap does not contain
   this VR WinMain return guard.
8. `[verified: submodule history]` Commit `569633b` changed hook teardown from
   ownership clearing to actual `MH_DisableHook`/`MH_RemoveHook`; main commit
   `aa0d7801` made VR `EndMain()` call it.
9. `[verified: conflicting prior dispositions]`
   `post-character-owner-starvation-senior-disposition-20260714.md` says live
   MinHook detours should remain resident until process exit because partial
   detach risks an in-flight callback. The later
   `runtime-crash-surface-senior-disposition-20260714.md` says normal teardown
   should remove hooks in reverse order. Current code implements the latter.
10. `[unknown]` The current fault is not proven to occur inside MinHook removal
    versus `World::Destroy()` or mapped CRT continuation. Logging has no phase
    markers between shutdown entry, disconnect publication, and the fault.
11. `[verified: exact offline control after brief creation]` The otherwise
    identical Skyrim VR/SKSEVR/VRIK/HIGGS/PLANCK process was launched through
    `sksevr_loader.exe` with every `SkyrimTogetherVR*` SKSE file moved out and
    `SkyrimTogether.esp` removed. It reached the main menu, but the same Alt+F4
    action was not a clean exit: XRizer panicked in
    `VulkanData::drop`/`IVRClientCore::Cleanup` after
    `ERROR_DEVICE_LOST`, and the process remained stuck. With the STVR mapper
    absent there was no STVR VEH to identify a game RVA. This proves an
    independent Linux XR shutdown defect, but neither proves nor excludes the
    STVR teardown as the source of `0xAB4F26`.

## Open Decisions

### D1: Detour Lifetime During VR WinMain Return

**Current lean:** never remove the currently executing WinMain detour from its
own return guard. Retire the TickBridge and make late detours forwarding-only;
leave MinHook targets resident until the detour has returned/process exit.

Options:

1. Skip normal MinHook uninstall for VR process-exit teardown.
2. Move uninstall to a later mapped-CRT callback after HookVrWinMain returns.
3. Keep current in-detour removal and add only logging.

Rejected for now: option 3 because it conflicts with the prior in-flight hook
safety disposition and a release-gated signature just recurred.

### D2: Whether To Destroy World During Process Exit

**Current lean:** preserve explicit `World::Shutdown()` if it can be shown safe,
because it cleanly disconnects and tears down dispatcher subscriptions. If the
fault remains ambiguous, add exact phase breadcrumbs around hook removal,
transport close, service erase, locator reset, and detour return in the same
patch rather than guessing a Havok fix.

Options:

1. Retain full world teardown but skip self-unhook.
2. Close transport and intentionally retain process-lifetime world/services.
3. Keep full teardown and add phase probes only.

Rejected for now: directly patching Havok or swallowing the exception; there is
no proven malformed game object on the successful connection path.

### D3: Static FunctionHookManager Destruction

**Current lean:** if VR skips `EndMain()` uninstall, decide explicitly whether
the manager destructor may unhook after WinMain detour return or should avoid
normal-exit MinHook mutation entirely for the mapped VR process. Startup-failure
rollback must remain transactional.

### D4: Proof Obligations For One More Build

**Current lean:** one build should contain the actual lifetime correction plus
enough phase logging/audits to distinguish each teardown boundary. The runtime
gate is: connected server admission, normal Alt+F4, shutdown completion marker,
process exits without intervention, server sees disconnect, no new historical
fault signature.

## Reviewer Request

Verify every load-bearing claim. Determine whether self-removal of the WinMain
MinHook is a sufficient high-confidence root cause or only one suspect. Rank
the teardown options by correctness for a mapped PE under Proton and ordinary
Windows. Identify any destructor, callback, dispatcher, or static-lifetime path
that would invalidate a process-lifetime-detour strategy. Recommend the
smallest coherent code change and exact static/unit/runtime checks for one final
build. Separate rebuild-blocking findings from diagnostic improvements.

Do not edit files. Do not broaden into gameplay synchronization or general
address translation.
