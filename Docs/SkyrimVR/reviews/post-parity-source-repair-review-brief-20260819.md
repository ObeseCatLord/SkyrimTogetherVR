# Skyrim Together VR Post-Parity Repair Review Brief

Date: 2026-08-19

## Goal And Review Boundary

Perform one focused, read-only Sol review of the repairs made after the full
post-parity source review. This is the final source gate before the first build
of this dirty candidate. Verify the source directly; do not infer build or
runtime success. Do not edit, build, test, deploy, commit, package, or launch.

The accepted deployment remains `c1ffb57e`. The current branch base is
`14ea8df7`; all reviewed changes are still an unbuilt worktree candidate.

## Prior Findings And Repairs

| Prior finding | Implemented repair | Primary evidence |
|---|---|---|
| Queued Protocol 17 events could reserve the same ledger revision and invalidate later work. | Shared FIFO admission order now spans reliable gameplay and inventory queues. Only the head refreshes an unadmitted token after an earlier commit. Exact duplicates are suppressed across both queues. | `VRActorReplicationService.cpp`: `IsAdmissionHead`, `TrySubmitReliableGameplayWork`, `TrySubmitInventoryTransaction`; `vr_actor_replication_recovery.cpp`. |
| Actor/server ledger identities and canonical resync state could outlive actor retirement. | `ForgetPlayer` and `ForgetServer` retire both server and mapped-player identities plus actor/equipment resync state. | `VRActorReplicationService.cpp`: `ForgetPlayer`, `ForgetServer`. |
| Canonical resync exhausted permanently and actor snapshot staging cleared quarantine before allocation-fallible work completed. | Exhausted requests rotate through bounded exponential-backoff rounds; actor snapshot state is copied/staged before swaps and quarantine release. | `VRActorReplicationService.cpp`: `OnCharacterSpawn`, `OnActorResync`, `retryCanonicalResyncs`; recovery policy tests. |
| Respawn gold and legacy health could mutate before obtaining a nonzero event identity. | `InventoryService` owns bounded event-ID reservation; optional respawn gold loss is skipped if reservation fails while actual respawn continues; legacy health reserves before mutation. | `InventoryService.{h,cpp}`, `PlayerService.cpp`, `ActorValueService.cpp`, `ServerAuthorityPolicy.h`. |
| Activation/lock accepted sender-supplied authority and cell information. | The server now requires authenticated sender ownership and canonical target, cell, activator, observability, and range before mutation/fanout. | `ObjectService.cpp`, `ServerAuthorityPolicy.h`, `vr_health_change_request.cpp`. |
| Equipment success meant dispatch succeeded rather than final native state matched. | Full and immediate equipment paths capture observed inventory/hand/spell/shout state inside replay scope and return `EngineRejected` on mismatch. | `AnimationAppearanceManager.cpp`, `EquipmentPostconditionPolicy.h`, `vr_equipment_postcondition_policy.cpp`. |
| Drop vtable contract was documented but not checked live. | Install verifies module/rdata/page/range/overflow and requires vtable slot `0xCD` at RVA `0x16E2898` to point to RVA `0x6C00F0` before enabling the body detour. | `DropHooks.{h,cpp}`, `vr_drop_hook_policy.cpp`. |
| Exceptions, including logger failures, could escape native callbacks or `noexcept` teardown paths. | `VrNoThrow::BestEffort` protects diagnostics and endpoint faulting; actor authority, magic, activation, calendar, invisibility, progression, waypoint, projectile, actor-action, dialogue, and drop boundaries are catch-all contained. | `VrNoThrow.h` and the named hook modules. |

## Verified Constraints

- [verified: source] Target remains Skyrim VR 1.4.15.0, SKSEVR 2.0.12,
  mapping ABI 23, bridge capability revision 34, gameplay protocol revision 17.
- [verified: process] No build or test has run after this coding pass.
- [verified: source] Runtime logging on hot detours is aggregate/power-of-two or
  bounded rather than per-frame info logging.
- [verified: diff check] `git diff --check` passes apart from the repository's
  existing CRLF conversion warning for the address override CSV.
- [verified: executable/static RE] Exact native targets and known false VR IDs
  remain recorded in `desktop-vr-equivalence-re-disposition-20260819.md` and
  `vr_address_contract.py`.

## Questions To Resolve

1. Does changing `OnCharacterSpawn` to return `bool` remain compatible with the
   dispatcher connection, and is its staged-map swap/retirement ordering truly
   transactional under allocation failure and ID replacement?
2. Can FIFO admission deadlock, reorder, erase, or prematurely timeout work
   across gameplay and inventory queues, including sequence-zero traffic, pose
   coalescing, admitted retries, and admission-order wrap?
3. Can resync request rotation accept a late response for an old request, lose
   a newer canonical revision, spin/log excessively, or retain unbounded state?
4. Are server event IDs reserved exactly once and before every corresponding
   mutation, without turning optional respawn-gold failure into failed respawn?
5. Are activation/lock canonicalization checks based on the right identifiers
   and entity lifecycle, and do they avoid spoofed sender/cell/target fanout?
6. Do equipment postconditions compare the same representation used to stage
   requests, including left/right flags, duplicate stacks, spells, and shouts?
7. Does every game-called detour preserve the correct original-call behavior on
   an exception, and can any logger/fault call still terminate a `noexcept`
   callback or teardown path?
8. Is the live Drop vtable validation ABI-correct and free of invalid reads or
   overflow before dereferencing the slot?

Return only prioritized P0-P3 findings with exact file/line evidence, followed
by a concise disposition for each repaired finding: verified, needs change, or
runtime-only proof. Distinguish compile-risk from behavioral risk. No broad
desktop-parity re-review and no deployment/package review.
