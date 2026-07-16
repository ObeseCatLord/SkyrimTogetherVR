# Full Gameplay Source Post-Fix Senior Review Disposition

Date: 2026-07-16

Review basis: Sol max review of
`full-gameplay-source-postfix-senior-brief-20260716.md`, verified against the
dirty `main` worktree and `original-skyrim-together`. No build, test, audit, or
game launch had been run when these dispositions were written.

## Verdict

The review found no P0 and no confirmed compile, linker, API, wire, or native
ABI blocker. It did find three P1 reliability gaps and five P2 lifecycle,
replay, result, and interest-routing gaps. All source-actionable P1 findings and
the economical P2 findings were adopted before the final build cycle.

| Finding | Disposition | Source response |
| --- | --- | --- |
| Native capture committed initial baselines before authentication/local server identity | Adopted | Bridge ABI 12/capability revision 20 adds `ArmLocalCapture`. Reset disarms capture; the authenticated mapped client submits and retries the native arm command after canonical local entity assignment and after each lifecycle boundary. |
| Native event-ring rejection advanced periodic/nonperiodic baselines | Adopted | Local capture retains rejected publications in a bounded native retry design and advances accepted state only when the publication is retained. Multi-record equipment, object, NPC, and text transactions remain grouped and ordered. Exhaustion fails closed instead of silently committing a false baseline. |
| Mapped local gameplay consumed bridge state when transport admission failed | Adopted | Stateful requests, inventory deltas, appearance, actor state, projectiles, packages, mounts, and final equipment retain bounded retry state. Baselines/action ledgers advance only after `TransportService::Send` accepts the serialized packet. Retries are lifecycle scoped and FIFO; exhaustion forces a native epoch rebase. |
| Transport queue owner-thread contract was documentary only | Adopted | `TransportService` latches its construction thread and rejects off-thread `Send` calls before touching its bounded deque. |
| Server could not validate magic `GameId` form types in final equipment | Adapted | The server validates complete nonzero IDs, ownership, negotiated capability, bounded unique entries, counts, and authoritative inventory. Skyrim form type is client-runtime data unavailable to the dedicated server; the CommonLib receiver resolves and type-validates every translated spell/shout before mutation. Rejecting all nonzero magic IDs would remove intended parity. Runtime evidence must cover invalid/missing forms. |
| Final equipment command results were uncorrelated and not retried | Adopted | The mapped receiver retains each translated final snapshot, tracks the bridge batch action ID, commits its transaction ledger only after a successful `EquipmentSnapshotEnd` result, and retries failed applications up to a bounded limit. Lifecycle and entity removal clear pending/result state. |
| Equipment replay ledger evicted an arbitrary entry at capacity | Adopted | The ledger now fails closed for an unknown key at capacity. Entries are removed deterministically on player leave and character removal; session nonce and connection generation still reset a surviving key's replay domain. |
| Appearance join replay sent every cached appearance without interest filtering | Adopted | Join/spawn replay now resolves each source character and applies the same `CellIdComponent::IsInRange` and dragon range rule used by normal capability-filtered relay traffic. Missing/unroutable characters are skipped. |
| Exact `ActorMediator` action lane was compiled but unsafe | Retained boundary | Capability publication remains forced off and command/network negotiation fails closed. It stays unavailable until `GameId` translation, generation-bound renegotiation, and `TESActionData` lifetime are verified. |

## Product Decisions

- Final equipment remains a protocol-7 VR-only complete-state transaction.
  Desktop clients continue to interoperate through transaction-zero messages,
  but do not observe VR-owned final equipment until a native desktop
  complete-state receiver exists.
- CommonLib validates every final equipment form and count before the first
  equip call. Skyrim has no verified transactional equip API, so the plugin
  reports application results and retries instead of attempting speculative
  rollback that can itself fail.
- Native/CommonLib remains the game-state owner. Papyrus is limited to thin
  connection and UI adapters and does not own replicated gameplay state.

## Runtime-Only Risks

The following require the final Windows build and in-game evidence rather than
more speculative source edits: MSVC/CommonLib compile compatibility, mandatory
prologue/address audit results, actual equip/magic engine outcomes, mod/form
availability, bounded-queue convergence under pressure, mixed HIGGS/PLANCK/VRIK
behavior, reconnect/server restart, and the two-client gameplay matrix.
