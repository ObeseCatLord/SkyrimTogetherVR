# Full Gameplay Source Integration Disposition

Date: 2026-07-16

## Review Provenance

The completed Sol max post-fix review is dispositioned in
`full-gameplay-source-postfix-senior-disposition-20260716.md`. Main integration
then verified its load-bearing findings against the worktree and fixed the two
remaining concerns surfaced during that review: global retry order and
appearance replay on interest transitions.

Two narrower follow-up Sol sessions were requested against the integrated
source. Neither returned a terminal report and both were cancelled after
bounded waits. They are not represented as completed reviews here. The final
build decision therefore rests on the completed Sol review, its adopted fixes,
and the main integration spot-checks below.

## Disposition

| Finding | Disposition | Source response |
| --- | --- | --- |
| Stateful and inventory retry queues could reorder traffic | Adopted | Each retained send now receives one monotonic global order. Retry chooses the oldest front entry across both queues, coalesced last-value state moves to the newest order, and inventory merges only when globally adjacent. |
| Appearance replay did not cover every interest-entry path | Adopted | `PlayerCellChangedEvent` fires after normal spawn routing for grid, interior, and exterior transitions. Cached relevant appearances replay to the mover and the mover's cached appearance replays to capable peers using the normal cell/dragon range rule. |
| A dropped final-equipment result event could leave application permanently awaiting acknowledgement | Adopted | Awaiting equipment results have a two-second timeout inside the existing three-attempt budget. Timeout removes stale action correlation and resubmits the complete idempotent final-state transaction. |
| Spawn-state result events had the same dropped-acknowledgement failure mode | Adopted | Spawn action tracking now has a bounded timeout. A timed-out server actor clears stale result IDs and requeues its retained complete spawn snapshot through the existing three-attempt resync budget. |
| Transport owner-thread rule was documentary | Adopted in prior tranche | Construction latches the owner thread and `Send` rejects off-thread callers before queue access. Serialized packets remain bounded and tagged by connection attempt and authenticated generation. |
| Native capture could advance unauthenticated or rejected baselines | Adopted in prior tranche | ABI 12 adds authenticated `ArmLocalCapture`; reset disarms capture and accepted native/event plus mapped/transport boundaries own baseline advancement. |

## Build Decision

No remaining source-known P0, native ABI, wire-layout, authority, lifecycle, or
ordering blocker is accepted for the gameplay package. The exact worktree still
requires the final static/address checks and one clean MSVC/CommonLib build.
Only successful artifacts from that exact revision may be deployed.

Runtime-only risks remain engine equip/magic outcomes, Address Library prologue
behavior in Skyrim VR 1.4.15, queue convergence under deliberate pressure,
cell transition replay with two clients, reconnect/server restart, and
HIGGS/PLANCK/VRIK coexistence. These require user-run Skyrim VR evidence and do
not justify speculative source mutation before the build.
