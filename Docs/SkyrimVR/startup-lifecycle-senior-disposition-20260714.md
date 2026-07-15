# Senior Review Disposition: Startup Lifecycle and Upstream Parity

## Verified upstream flow

Original Skyrim Together installs the VM-update detour at address-library ID
`53926`. `HookVMUpdate` calls `TiltedOnlineApp::Update()` only when
`VMContext::inactive == 0`, then forwards to Skyrim. `World::Update()` runs
`PreUpdateEvent`, queued runner work, and `UpdateEvent` in that order.

The overlay does not connect directly. It queues `TransportService::Connect()`
on `RunnerService`, so a request made while Skyrim's VM is paused remains
pending until an active VM update. Once transport connects, upstream immediately
reads the player, base NPC, parent cell/worldspace, level, calendar, and nearby
forms. It has no RaceSex automation and no separate character-readiness state.
Its safety boundary is therefore: **the user finishes normal character
creation or loads a valid save, then requests connection on the active
VM-update path**.

## Disposition

| Senior recommendation | Disposition | Reason / resulting rule |
| --- | --- | --- |
| Treat Papyrus cadence stopping during RaceSex as expected. | Adopted | `RaceSexMenu` pauses the game. Manual native ticks bypass upstream's active-gameplay boundary and exposed transient game objects. |
| Remove generic RaceSex `kHide`, finalized-RaceSex fallback, and manual tick pumping. | Adopted immediately | `kHide` reproduced the same access violation twice. A named player and cell while RaceSex remains open do not prove that Skyrim completed its character-finalization transaction. |
| Do not automate finalization with `RaceSexMenu::ChangeName` alone. | Adopted | The mapped method supplies a name, but available source does not prove it performs confirmation, name event, normal menu close, and finalization. |
| Use a valid post-character save for unattended XRizer tests. | Adopted | The fixture must be authored once through vanilla finalization with the packaged load order. Manual New Game remains a separate release gate. |
| Replace rotating SKSE-task execution with one verified update owner. | Adopted as the target architecture | Current `VRTickBridge::Dispatch` calls `World::Update()` from rotating task thread IDs. `TiltedConnect::Client` has no owner enforcement. Upstream keeps both transport pumping and engine work on one active VM-update owner. |
| Re-enable VR address ID `53926` directly. | Adapted: corrected target, observer first, active only after proof | The former hand-authored override `0x9869d0` was disproved. CommonLibSSE-NG identifies `BSScript::Internal::VirtualMachine::Update(float)` as vtable slot 4; SkyrimVR.exe's readable vtable at RVA `0x18E2148` points that slot to RVA `0x12765B0`, with a `void (this, float)` ABI. The project-local ID now maps exactly to that target and a static audit locks the address, provenance, and signature. Compile `off/observe/active` modes together; observe cadence, stable TID, forwarding, and clean RaceSex/load/exit before selecting active without rebuilding. Never read the unverified VR `VMContext` layout. |
| Add a lifecycle service with epochs and stable readiness. | Adopted after update-owner proof | `Boot -> Suspended -> Stabilizing -> Ready(epoch) -> Teardown`. Readiness requires RaceSex/loading/fader menus absent and stable player/base/cell identities over consecutive owner ticks plus elapsed time. `VirtualQuery` readability alone is insufficient. |
| Keep command, connection, discovery, and packet work epoch-aware. | Adopted | A connect command remains pending until `Ready`. Load/player invalidation increments the epoch, resets discovery, rejects stale queued work, disconnects on the owner, then reconnects only after the next stable epoch. Explicit disconnect clears the retained endpoint. |
| Put transport on a new worker thread now. | Rejected for parity implementation | The current source has no such owner despite an earlier brief implying otherwise. A worker would still require a separate verified game-thread mailbox. The original single-owner VM path is smaller once its VR target is proven. No worker may touch engine objects. |

## Required runtime flow

1. Launch with the VR VM target in `observe` mode. The detour only records
   cadence and thread ID and forwards; Papyrus may publish atomic permits but may
   not call `World::Update()`.
2. Complete RaceSex through Skyrim's real confirm/name transaction, or load the
   deterministic post-character save. No Skyrim Together game-state work runs
   while RaceSex or loading UI is active.
3. Prove one useful VM-update cadence, one stable owner thread, correct
   forwarding, and clean launch/RaceSex/load/exit. Switch the same build to
   `active` mode.
4. On the owner, stabilize player, base, and cell identities and enter
   `Ready(epoch)`. Only then consume a pending connect command and capture the
   immutable authentication snapshot.
5. Run `Client::{Connect, Update, Send, Close}`, world dispatch, discovery, and
   packet application on that owner. Assert the owner at low-level transport
   entry points. Mutable engine work additionally requires the current ready
   epoch.
6. On load, RaceSex reopen, player invalidation, or teardown, suspend first,
   advance the epoch, discard stale work, reset discovery, close/drain transport,
   and only reconnect after a new stable ready epoch.

## One-build validation order

The next DLL should contain all three runtime modes so observer and active runs
do not require separate compilation. Static audits must prove that Papyrus/SKSE
task callbacks cannot invoke `World::Update`, active VR code never reads
`VMContext::inactive`, discovery/status require `Ready(epoch)`, and unsafe
RaceSex automation is absent.

Runtime validation order is observer launch and exit, observer New Game/load,
active deterministic-save connect and first cell sync, online load/reconnect,
then clean teardown. Active mode must remain opt-in until the observer gates
pass.

## Corrected VM target evidence

The installed Skyrim VR 1.4.15 executable used for validation has SHA-256
`6961efb4f4775a307b0fc9a3d637542c1e090be207d3b09467eab216b7f87971`.
`objdump -h` reports `.rdata` at RVA `0x157F000` and file offset `0x157DA00`.
CommonLibSSE-NG's primary VR VM vtable RVA `0x18E2148` therefore maps to file
offset `0x18E0B48`; an eight-entry `od -tx8` read produced:

```text
1412911f0 141297270 141276ec0 141276e60
1412765b0 141276c30 141276dd0 141276de0
```

Slot 4 is exactly VA `0x1412765B0`, proving RVA `0x12765B0` for this executable.
This closes the senior review's binary-evidence gate for an observer run. The
runtime must still prove the detour's cadence, forwarding, and stable owner
before the active connection result is accepted.

The first observer run disproved first-caller ownership: startup/RaceSex calls
arrived on Windows worker TIDs `588` and `592`, while the tick bridge activation
thread was `320`. Five consecutive post-character GDB breakpoints at
`0x1412765B0` then ran on the process main thread with caller `0x14095D1AA`.
The Sol-max follow-up therefore selected `TickBridge::ActivationThreadId` as the
semantic owner. The hook forwards every VM call, but only the published ready
activation thread may consume a permit and update the client; worker calls can
neither steal a permit nor permanently disable dispatch.
