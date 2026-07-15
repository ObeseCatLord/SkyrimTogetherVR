# Skyrim Together VR runtime crash-surface senior-review brief

## Goal and scope

Audit the default Skyrim Together VR connection-only client for every credible
native crash path before another Windows rebuild. The immediate failure is an
access violation at mapped Skyrim VR address `0x140131AAB`, but the review must
also cover launcher mapping, detour ABIs and targets, initialization/teardown,
callback ownership, bridge atomics, and connection-only services. This is a
solo-maintained compatibility port; require concrete safety, not enterprise
process.

The reviewer is read-only. Verify claims against the repository and return a
prioritized list with file/line evidence. Label every item as rebuild-blocking,
runtime-test-only, or future gameplay risk. Prefer removing an unnecessary hook
over retaining it for diagnostics.

## Environment facts

| Fact | Evidence |
| --- | --- |
| Repository/branch | `/home/obesecatlord/Documents/SkyrimModding/SkyrimTogetherVR`, `main` |
| Audited build revision | `b4556de68bf784300228706e8541a11cb67f1417` |
| Runtime | Skyrim VR 1.4.15 under GE-Proton 10-34, Monado/XRizer |
| Default build mode | Connection-only; unvalidated gameplay hooks and validated inline patches remain compile-time disabled |
| Required coexistence | SKSEVR 2.0.12, VR Address Library, VRIK, HIGGS, PLANCK |
| Latest package audit | Build succeeded; strict post-install audit found zero failures and all required mods |
| Latest failed mode | `STVR_VM_UPDATE_MODE=observe`; no client update dispatch is permitted |
| Latest dump | `SkyrimVR/crash_UTC_2026-07-15_02-53-43.dmp` (local runtime artifact, not committed) |
| Current source tests | All Python address, hook, package, bridge, crash-diagnostic, and runtime-fixture audits passed before the build |

## Verified runtime evidence

1. `[verified: current tp_client.log lines 14801-14848]` The new build resolved
   WinMain to RVA `0x5B4290`, `Main::Draw` to `0x5B9330`, and the VM virtual
   update to `0x12765B0`. All four delayed/immediate bring-up hooks committed.
2. `[verified: current tp_client.log]` `Main::Draw` entered twice on activation
   thread 320 with depth 1. Its original returned both times. The VM observer
   returned on worker thread 620 twice.
3. `[verified: current tp_client.log]` At 21:53:43 the VM observer returned on
   activation thread 320 and logged its first activation-thread observation.
   Immediately afterward the process raised `c0000005` at `0x140131AAB` on
   thread 320.
4. `[verified: dump through Proton's WineDbg]` Exception registers include
   `RIP=0x140131AAB`, `RSP=0x11F4B0`, `RBX=0`, and raw stack candidates
   `0x1402A0AE7` and `0x140D9F5FE`. The minimal dump contains no executable code
   pages and WineDbg cannot unwind beyond the faulting frame.
5. `[verified: on-disk SkyrimVR.exe inspection]` The Steam executable's mapped
   game section is encrypted on disk, so static disassembly at those RVAs is
   garbage and must not be treated as instruction evidence.
6. `[verified: historical tp_client.log]` The same `0x140131AAB` signature
   occurred repeatedly before the corrected VM target existed, including runs
   where only the old outer-loop/render hooks were installed. Several failures
   followed an SKSE task callback; one occurred after 102,000 forwarded VM calls.
   Therefore the latest adjacency does not prove the VM detour is the root cause.
7. `[verified: current DevBench output and log timestamp]` The latest failure
   happened after entering RaceSex while automation attempted to open its
   confirmation dialog. This is a competing trigger hypothesis because prior
   manual/synthetic RaceSex acceptance has also failed, but causality is
   unverified.
8. `[verified: Code/client/SkyrimVM64.cpp]` Observe mode calls each original
   first and records telemetry only. It cannot consume a permit or call
   `TiltedOnlineApp::Update()`.
9. `[verified: CommonLibSSE-NG IVirtualMachine.h]` The VM virtual signature is
   declared `void Update(float)` at slot 4. `[verified: project macros]`
   `TP_THIS_FUNCTION` becomes the Windows x64 fastcall ABI in this build.
10. `[verified: current package/address audit]` Curated address rows have
    precedence over AE-to-SE aliases, preventing the earlier numeric-ID
    collision from silently selecting a desktop translation.

## Current architecture

- The immersive launcher maps Skyrim VR and links the client into the launcher
  process. `Code/client/main.cpp` detours exact VR WinMain and uses a return guard
  to reach idempotent teardown.
- `VRTickBridge` accepts SKSE/Papyrus cadence from task threads, coalesces a
  pending sequence, and records a fixed activation-thread owner.
- `Main::Draw` is the sole active-mode client-update owner. It calls the original
  first, requires outermost depth and the activation thread, rechecks endpoint
  state, consumes one coalesced permit, and guards against reentrancy.
- The VM detour is telemetry only. It is no longer required to dispatch client
  updates.
- Connection-only services intentionally avoid gameplay hooks, but their
  constructors, event registrations, and per-update work still need review for
  unsafe game access and teardown ordering.

## Open decisions and current lean

### D1: Retain the VM detour

Current lean: remove it from the default VR hook set because it is no longer
needed and every detour adds ABI/patch risk. Keep address evidence for future
research, but do not execute it merely for telemetry.

Alternative considered: keep it because CommonLib identifies the ABI. Rejected
provisionally because observer telemetry has no product value once Main::Draw is
the update owner.

### D2: Interpret `0x140131AAB`

Current lean: treat the RaceSex synthetic-input path and old SKSE task-thread
dispatch architecture as competing historical causes. Do not claim a root cause
without a hook-off matrix or runtime decrypted code capture.

Alternative considered: blame the new VM detour from log adjacency. Rejected
because the same signature predates that detour and also occurred on worker
threads.

### D3: Minimize observer-mode mutation

Current lean: observer mode should install only WinMain, `Main::Draw`, render
breadcrumbs, and the minimum SKSE endpoint needed to prove cadence. No service
should access mutable game state, send network traffic, or run update work.

Alternative considered: initialize the full connection-only application in
observer mode. Rejected provisionally because that makes crash attribution
ambiguous.

### D4: Connection-only service safety

Current lean: audit every service enabled by the VR connection-only constructor
and every path reachable from `TiltedOnlineApp::Update()`. Require explicit
activation-thread assertions before game memory/API access and idempotent
teardown for callbacks that can outlive the app.

### D5: Hook commit and failure atomicity

Current lean: if any required hook cannot resolve or commit, refuse launch before
mapped game entry. Teardown must retire producers before destroying subscribers.
Audit partial-init paths, exception paths, and hooks installed before
`g_appInstance` is fully ready.

### D6: Address/ABI collision audit

Current lean: enumerate every hook actually enabled in the default VR binary,
not the dormant gameplay surface. For each, require exact VR provenance,
signature evidence, original-call ordering, return type, and reentrancy/thread
contract. Separately identify dormant hooks that become crash risks when gameplay
mode is enabled later.

## Requested review deliverable

1. Verify the brief first; correct any false framing.
2. Produce findings ordered P0-P3 with exact file/line evidence.
3. For each enabled native detour, state whether target, ABI, original call,
   return semantics, recursion behavior, and teardown are defensible.
4. Audit all default connection-only services for use-after-free, cross-thread
   game access, stale callbacks, races, null dereferences, and lifecycle gaps.
5. Audit VersionDb alias precedence and all default exact address rows for a
   similar wrong-offset class.
6. Audit launcher/SKSE bootstrap and bridge interactions with VRIK/HIGGS/PLANCK.
7. Recommend the smallest coherent patch set before the next rebuild and a
   runtime matrix that distinguishes the repeated `0x140131AAB` crash from new
   hook failures.
8. Explicitly list what is safe to defer until gameplay hooks are enabled.

## Depth budget and not-list

Spend depth on native correctness and crash prevention. Do not review server
protocol design, UI polish, FBT pose networking, broad code style, or disabled
desktop gameplay behavior except where it leaks into the default VR binary.
Do not edit files.
