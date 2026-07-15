# Senior Review Disposition: Post-Character Update Ownership

## Decision

The VR client will use `Main::Draw` at Skyrim VR address-library ID `35560`
(RVA `0x5B9330`) as its only candidate owner for `TiltedOnlineApp::Update()`.
The candidate is compiled in all runtime modes but remains forwarding-only in
`observe` mode. It may consume a coalesced SKSE-task permit in `active` mode
only after runtime telemetry proves stable activation-thread affinity, recurring
post-character cadence, non-reentrancy, and clean teardown.

The VM update target at project-local ID `53926` remains installed only as a
forwarding observer. The outer-loop target at ID `36564` is removed from the VR
bring-up path because runtime evidence showed only two entries and rotating
thread ownership. Neither path may call `TiltedOnlineApp::Update()`.

## Disposition

| Senior recommendation | Disposition | Resulting rule |
| --- | --- | --- |
| Reject the outer `MainLoop` hook as a frame owner. | Adopted | VR no longer installs ID `36564`. Static audits reject it in the VR branch. |
| Keep the VM hook observation-only. | Adopted | ID `53926` always forwards and can report cadence, but cannot consume a permit or call the client update. |
| Probe `Main::Draw(Main*, uint32_t, bool)` at ID `35560`. | Adopted | The hook calls the original first, records thread/cadence/depth/duration, and dispatches only in active mode. |
| Require activation-thread affinity. | Adopted | A draw callback on any other thread forwards and records a mismatch; ownership never migrates automatically. |
| Prevent overlapping client updates. | Adopted | A single-entry atomic guard is acquired before consuming a permit and is released with RAII after the update. Nested draws cannot dispatch. |
| Recheck lifecycle after original draw and before update. | Adopted | The endpoint must still be `Ready`, its activation thread must still match, and a permit must be available immediately before dereferencing the app. |
| Retire before world teardown. | Adopted | `TickBridge::Retire()` publishes `Retired` and clears pending work before lifecycle/world teardown starts. Late detours only forward. |
| Make teardown reachable from the immersive launcher. | Adopted after final review | Exact VR WinMain ID `35545` at RVA `0x5B4290` is wrapped. Its return guard calls idempotent `RunTiltedEnd()`, which reaches `EndMain()` before mapped CRT termination. |
| Report starvation without changing owner. | Adopted | Permit production, consumption, pending age, owner heartbeat age, and sequence numbers are exposed as atomic diagnostics. A rate-limited producer warning makes a silent stall visible. |
| Rebase or clamp time after starvation. | Adopted | VR world updates clamp elapsed time so the first recovered update cannot inject a multi-minute simulation delta. |
| Keep historical crash signatures as release gates. | Adopted | No new dump may match RVAs `0xC53E1B`, `0xAB4F26`, `0x133B008`, or the fixed `0xF43AD1` singleton fault. RVA `0x131AAB` is a RaceSex invalid-hide sentinel: the current automation avoids its trigger, but the engine path is not claimed universally fixed. |

## Activation Proof

An observer run must prove all of the following before an active connection run:

1. `Main::Draw` resolves to RVA `0x5B9330` from curated address evidence.
2. It recurs after character finalization on the published activation thread.
3. Maximum observed hook depth is one on the dispatching path and the original
   call returns normally.
4. VM and SKSE task workers continue forwarding without calling the client.
5. Teardown retires the endpoint before lifecycle/world destruction and creates
   no new dump.

The active run must then show monotonically increasing produced and consumed
permit sequences, lifecycle transition to `Ready`, one transport connection,
server admission, stable owner age, and no reentrant update or historical crash
signature. Failure of any gate leaves the default mode at `observe`.

## Similar-Crash Audit

The follow-up native audit covered address selection, direct update call sites,
permit publication, lifecycle teardown, hook residency, and bridge mapping
lifetime. It found and corrected the teardown ordering bug: endpoint retirement
now precedes lifecycle/world teardown. It also replaced the unsynchronized
first-callback log flag and explicitly aligns all 64-bit Interlocked counters.

The final Sol-max review found that the immersive launcher did not use
TiltedReverse's separate `HookedWinMain`, making the original `EndMain()` path
unreachable. This was a real P1 teardown gap and invalidated the first lexical
ordering-only audit. The fix wraps the actual VR WinMain at exact database ID
`35545`; an RAII return guard calls idempotent `RunTiltedEnd()`, which reaches
`EndMain()` before control returns to the mapped CRT. The hook also calls the
idempotent startup function on entry, so it remains correct if the existing CRT
startup IAT callback changes.

Live MinHook detours and the process-local endpoint mapping deliberately remain
resident until process exit. `FunctionHookManager::UninstallHooks()` currently
clears ownership without disabling MinHook targets; adding a partial detach or
unmapping the callback endpoint during `EndMain()` would create a more direct
use-after-free path for an in-flight callback. The safety contract is therefore
reachable atomic retirement followed by forwarding-only late detours.

`audit_crash_diagnostics.py`, `audit_bringup_hooks.py`, and
`audit_tick_bridge.py` now reject a VR VM observer that calls the client, any
restored VR main-loop/destructor hook, missing owner/reentrancy/recheck gates,
an unreachable WinMain shutdown path, or teardown before endpoint retirement.
The built-package audit independently requires exact curated `35545`, `35560`,
and `53926` rows, preventing stale generated address data from bypassing the
source checks.
