# Full Gameplay Source Integration Review Brief

Date: 2026-07-16

## Goal And Scope

Determine whether the source fixes made in response to the prior Sol review are
internally correct and ready for the one final static/build cycle. This is a
solo-maintained Skyrim Together port targeting Skyrim VR 1.4.15. Review only the
new arm, retry, equipment-result, replay-ledger, and appearance-interest paths.
Do not re-review unrelated historical launcher, overlay, UI, or runtime tooling.

Verify every claim against the dirty worktree. Do not edit, build, run tests or
audits, launch Skyrim, deploy, or clean storage.

## Environment Facts

| Fact | Value |
| --- | --- |
| Repository | `/home/obesecatlord/Documents/SkyrimModding/SkyrimTogetherVR`, `main`, intentionally dirty |
| Original comparison | `original-skyrim-together` |
| Runtime/toolchain | Skyrim VR 1.4.15, SKSEVR 2.0.12, alandtse CommonLibVR `ng` v4.37.0 |
| Shared bridge | ABI 12, capability revision 20, event ring 2048, command ring 1024 |
| Network | exact-match gameplay protocol revision 7 |
| Build policy | no build/test/audit has run for this source tranche |

## Verified Source Claims

1. `[verified: source trace]` `ArmLocalCapture` is an ActorState action with a
   strict zero-payload/local-player schema. The mapped client submits it only
   after nonzero authenticated session identity and canonical local server ID;
   plugin execution calls `LocalGameplayCapture::Arm()`.
2. `[verified: source trace]` native reset disarms capture. Periodic and event
   capture are gated by the atomic arm state. The native snapshot now starts
   from the last accepted snapshot and updates individual fields only after
   event-ring acceptance. Inventory retains per-item accepted counts and final
   equipment advances only after complete batch acceptance.
3. `[verified: source trace]` mapped local sends use bounded stateful and
   inventory queues with one monotonic global order, mark action/baseline state
   only after transport acceptance, retry FIFO, move coalesced last-value state
   to the newest order, merge only globally adjacent inventory deltas, and
   retire/rebase the native epoch on bounded retry exhaustion.
4. `[verified: source trace]` `TransportService` latches its construction thread
   and rejects off-thread sends before deque access. Packets remain bounded and
   session/generation tagged.
5. `[verified: source trace]` remote final equipment retains translated entries,
   tracks the shared bridge action ID, commits the transaction ledger only on a
   successful `EquipmentSnapshotEnd`, and retries result failure or a dropped
   result acknowledgement up to three times. Spawn-state result tracking uses
   the same bounded timeout/resync policy. Pending/action maps clear on
   lifecycle, disconnect, player leave, and entity removal.
6. `[verified: source trace]` server equipment transaction ledgers no longer
   evict arbitrary entries. Unknown keys fail closed at 512; player leave and
   character removal deterministically erase relevant keys.
7. `[verified: source trace]` cached appearance replay uses each source player's
   committed interest cell and the normal `CellIdComponent::IsInRange` dragon
   rule. A post-spawn `PlayerCellChangedEvent` replays relevant cached sources
   to the mover and the mover's cached appearance to relevant capable peers.
   Character-spawn replay also runs if the owner has not published appearance.
8. `[verified: source inspection]` native capture missing braces in accepted sex
   and weight updates and stale package-zero retention were found during main
   integration and fixed.
9. `[unverified]` all changed files compile with MSVC/CommonLib. Per user policy,
   compile and static checks wait until source review is complete.

## Open Decisions

1. Is queue acceptance plus native per-field ring acceptance sufficient to
   prevent duplicate delta mutations under pressure and lifecycle rebase?
   Current lean: yes; hard exhaustion retires the epoch instead of continuing.
2. Is committing final equipment only on successful end-result sufficient when
   earlier batch records can independently report failure? Current code retries
   on any tracked failure and commits only end success.
3. Does appearance replay exactly preserve established interest semantics at
   join/spawn, including missing characters and dragons? Current lean: yes.
4. Are any source-level compile/API errors visible that would force another
   Windows build? This is the highest-priority practical check.

## Requested Output

Return a prioritized review with exact file:line evidence:

1. P0/P1/P2 correctness, compile/API, deadlock, ordering, lifecycle, replay, or
   authority findings in the scoped files;
2. minimal concrete fix for each confirmed finding;
3. verdict on the four open decisions;
4. build-ready or not-build-ready verdict;
5. runtime-only risks that should not trigger speculative source changes.

Keep the result under 1,500 words. Do not edit anything.
