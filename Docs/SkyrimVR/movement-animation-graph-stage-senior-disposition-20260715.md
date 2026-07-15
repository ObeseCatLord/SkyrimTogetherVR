# Movement And Animation Graph Stage Senior Disposition

Date: 2026-07-15

## Scope

This disposition covers gameplay bridge ABI v2, server/client movement ordering,
remote spatial transfer, pending visibility spawns, and bidirectional humanoid
animation graph snapshots. Exact `ActorMediator` action replay remains disabled
because its Skyrim VR call targets are not semantically verified.

## Disposition

| Senior recommendation | Disposition | Result |
|---|---|---|
| Pass a null target handle to coordinate `MoveTo_Impl`, then verify retained identity and destination. | Adopted | Spatial transfer now matches the original coordinate move contract, re-resolves the adapter handle, and commits cell/worldspace state only after exact post-move checks. |
| Stop graph writes on failure and restore the preimage. | Adopted | The adapter captures all old named values, pins the graph-manager identity through the update, stops at the first failed setter, and performs best-effort rollback before quarantining animation. |
| Require actual spatial transfer and stale-order evidence. | Adopted | Dedicated submitted/succeeded/rejected transfer counters and stale-tick rejection are part of schema v2; the strict acceptance audit requires one successful transfer, one deliberate stale-tick rejection, and no transfer rejection. |
| Expose animation event/command ring loss and stop a snapshot after first push failure. | Adopted | Bridge diagnostics export both ring drop counters, local snapshot publication aborts on the first failed chunk, and strict evidence requires zero drops. |
| Distinguish static graph capability from runtime readiness. | Adopted | `animationGraphEnabled` reports negotiated implementation support; `localAnimationGraphReady` requires one fully assembled local snapshot. Remote apply emits an explicit applied/faulted acknowledgement after graph construction and named-variable validation. |

## Additional Corrections Found During Integration

1. Server movement forwarding now includes `CellId` and `WorldSpaceId`; before
   this correction the VR client rejected every outbound movement as locationless.
2. Invalid/unowned movement logging no longer dereferences an end iterator.
3. Client and server reject duplicate/older movement ticks, while accepting an
   initial zero. Tick acceptance resets on both ownership-transfer paths.
4. A bounded pending-spawn map retains visibility messages that arrive before
   the local CommonLib snapshot or bridge capability becomes ready.
5. Graph chunks share one tested assembly implementation. It rejects malformed
   metadata, non-finite values, stale snapshots, and mismatched direction before
   applying a complete five-chunk snapshot.
6. A complete remote graph snapshot waits for the actor graph to be constructed
   instead of permanently faulting immediately after actor creation.
7. A lost spatial-transfer acknowledgement cannot freeze an avatar indefinitely:
   the client pins the submitted destination, blocks later root commands until
   the matching acknowledgement, and retires the lifecycle after a bounded
   timeout so queue loss recovers fail-closed.
8. Movement ticks use serial-number ordering. Duplicate/older values and an
   implausible half-range future jump are rejected, while natural 64-bit wrap is
   accepted and ownership transfer still resets the ordering epoch.

## Deferred Exact Action Replay

The original action lane depends on `ActorMediator::PerformAction`/`ForceAction`
and globals represented by IDs 38949, 38953, 39004, 403566, and 403567. Address
correspondence exists, and `TESActionData` layout/vtable is corroborated, but the
VR functions and global pointer shapes do not yet have sufficient semantic and
calling-convention proof. The capability remains absent. `NotifyAnimationGraph`
may later be offered as a clearly labeled visual fallback, but it is not full
Skyrim Together action parity.

## Completed Build Gates

- clean WinBoat gameplay build at `d201a3f8`;
- 571 `TPTests` assertions in 33 test cases;
- 503-file gameplay package plus paired evidence audits with zero failures and
  zero evidence warnings;
- independent Linux package/evidence validation and exact client deployment;
- matching ARM64 `d201a3f8` server binary deployed as the only server container,
  with zero restarts and UDP 26099 listening.

## Remaining Gates

- two-client runtime proof of retained-handle exterior/interior transfer,
  locomotion graph application, deliberate stale-tick rejection, and zero ring
  drops/rejections;
- dedicated binary/runtime probe before implementing exact action replay.
