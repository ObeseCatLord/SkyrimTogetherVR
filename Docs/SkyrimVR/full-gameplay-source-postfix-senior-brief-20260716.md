# Full Gameplay Source Post-Fix Senior Review Brief

Date: 2026-07-16

## Goal And Boundary

Decide whether the completed Skyrim Together VR source tranche is ready for one
final Windows/WinBoat compile-and-audit cycle. The target is Skyrim VR 1.4.15
with original Skyrim Together gameplay implemented natively through the mapped
client and a CommonLib SKSEVR plugin. This is a solo-maintained port: demand
strong ABI, authority, lifecycle, and crash safety without enterprise process.

Verify the brief against the real dirty worktree and the
`original-skyrim-together` branch before critiquing it. Do not edit files, build,
run tests or audits, launch Skyrim, deploy, or clean storage. The user requires
all source review and fixes before the single final build.

## Environment Facts

| Fact | Value |
| --- | --- |
| Repository | `/home/obesecatlord/Documents/SkyrimModding/SkyrimTogetherVR`, branch `main`, intentionally dirty source tranche |
| Original comparison | `original-skyrim-together` |
| Runtime | Skyrim VR 1.4.15.0, locked executable and Address Library hashes |
| SKSE | SKSEVR 2.0.12 |
| CommonLib | alandtse CommonLibVR `ng` v4.37.0, commit `d36bc08cae5d445804d4c22ccd49fec4e9dfbdc4` |
| Shared bridge | ABI 12, capability revision 20, 2048 event records, 1024 command records |
| Network protocol | Exact-match gameplay protocol revision 7 |
| Ownership boundary | Mapped client owns protocol/entity state; CommonLib plugin alone owns game pointers and engine mutation; server owns authority and interest routing |
| Build policy | No compile/test/audit has run for this source tranche |

## Verified Post-Fix Claims

1. `[verified: source trace]` client `TransportService::Send` serializes into a
   bounded 256-packet/8 MiB owner-thread queue. Packets carry connection-attempt
   and authenticated-generation identity, stale packets are dropped, and
   queue acceptance is returned to retrying producers. TiltedConnect's pinned
   void `Send` API is unchanged.
2. `[verified: source trace]` non-player ownership uses a five-second server
   grant bound to server entity, selected/current player pointers, connection
   IDs, connection generations, and session nonces. The exact selected session
   gets one token attempt; expiry, player leave, entity removal, replacement,
   or current-owner mismatch fail closed.
3. `[verified: source trace]` VR ownership ignores the legacy native mutation
   handler, waits for a complete mapped snapshot, sends the grant token, and
   promotes only after a zero-token completion acknowledgement. Current desktop
   clients echo the token through their original native handler and do not
   receive the VR-only completion acknowledgement.
4. `[verified: source trace]` VR worn equipment and magic selection are sent as
   one nonzero transaction with a complete final `Inventory`. The server checks
   owner, capability, session ledger, monotonic transaction ID, bounded unique
   worn entries, counts against authoritative inventory, and all fields before
   one `Inventory::UpdateEquipment` and one final notify.
5. `[verified: source trace]` final equipment begin/items/end commands are built
   in memory and published with one `TryPushBatch` reservation. Batch admission
   permits only retained-remote gameplay actions for one handle and domain.
   CommonLib stages without mutation until the end record, validates all forms
   and inventory counts before mutation, and clears staging on both lifecycle
   reset paths.
6. `[verified: explicit product boundary]` nonzero final equipment transactions
   are sent only to compatible VR recipients. The server does not synthesize
   partially accepted legacy deltas for desktop recipients. VR still consumes
   transaction-zero desktop notifies. Mixed desktop observation of VR equipment
   is therefore deliberately fail-closed, not claimed parity.
7. `[verified: source trace]` exact `ActorMediator::PerformAction` capture/replay
   remains compiled but unreachable. The plugin forces its optional capability
   false, the client cannot request the network capability, and command
   validation rejects the lane. It is blocked on `GameId` translation,
   generation-bound renegotiation, and verified `TESActionData` lifetime.
8. `[verified: executable call-site disassembly]` `Actor::SpeakSound` at VR RVA
   `0x5F0E20` has 14 arguments and returns duration as `float` in XMM0. The hook
   typedef and call sites now preserve that ABI. `Actor::RemoveSpell` is pinned
   to RVA `0x6385F0` and its exact prologue.
9. `[verified: source trace]` curated mandatory address rows now cover
   `MagicTarget::AddTarget` ID 33742/RVA `0x5579C0`, `Actor::SpeakSound` ID
   36541/RVA `0x5F0E20`, `Actor::RemoveSpell` ID 37772/RVA `0x6385F0`, and the
   actor mediator candidate. Generated artifacts have intentionally not yet
   been regenerated.
10. `[verified: source trace]` native lifecycle transition and explicit epoch
    retirement reset avatar, gameplay text, actor-action prototype, local
    capture, body pose, interaction, actor/world state, and equipment staging.
11. `[verified: source trace]` `RemoveSpellRequest` and the broader magic,
    inventory, actor value, combat, ownership, and equipment mutations require
    valid owned entities before server mutation or rebroadcast.
12. `[verified: source fixtures written, not run]` encoding round-trip fixtures
    cover ownership tokens and final equipment transactions; `Inventory`
    equality now includes serialized magic equipment.
13. `[verified: source trace]` ring contracts explicitly require one bound
    consumer per ring. Multi-record event and command producers reserve complete
    contiguous transactions and publish commit/end records last.
14. `[verified: scripts]` the WinBoat build wrapper requires a clean pushed
    revision, holds a build/cleanup lock, performs bounded pre/post cleanup,
    imports and validates the exact package/evidence pair, then recreates and
    audits the private handoff ZIP.
15. `[post-review source fix]` native capture is disarmed until the mapped
    client has authenticated and assigned its canonical local server entity.
    `ArmLocalCapture` is retried after lifecycle transitions; rejected native
    publications and mapped transport sends retain bounded retry state, and
    accepted baselines advance only after the relevant queue accepts them.
16. `[post-review source fix]` final equipment application is correlated with
    CommonLib result action IDs. The receiver commits its transaction ledger
    only after a successful end result and retains a bounded retry snapshot on
    application failure.
17. `[post-review source fix]` server equipment replay ledgers fail closed at
    capacity and clean up on player/entity lifecycle events. Cached appearance
    replay uses the same cell/range rule as live appearance relay.

## Deliberate Unsupported Boundaries

- full remote face tint masks, face morphs, texture overlays, and hair generation;
- native black respawn fade without a verified five-argument VR callable;
- remote VRIK finger/calibration application without a remote-actor API;
- direct PLANCK remote physical grab/ragdoll replay without a stable public API;
- Vivox voice;
- outbound scripted-object-animation capture, which upstream also compiles out;
- exact ActorMediator actions until the lifetime and translation blockers above
  are resolved.

Do not recommend guessed offsets, local-player APIs for remote actors, or
Papyrus-owned replication as substitutes.

## Open Decisions

### 1. Final Equipment Versus Mixed Desktop Clients

Current lean: retain the VR-only final-state notify for this build. Synthesizing
several legacy deltas reintroduces the partial network transaction the redesign
removes. Adapt only if a single native desktop final-state receiver can be added
without broadening the final build risk.

### 2. Engine Mutation Failure After Validation

Current lean: accept best-effort CommonLib equip calls after validating every
form/count before the first mutation. Skyrim has no verified transactional
equip API and rollback can itself fail. Require result telemetry and runtime
convergence rather than guessed rollback.

### 3. Transport Queue Thread Contract

Current lean: retain the owner-thread queue if all callers are demonstrably on
the mapped client owner thread. If any producer can call `Send` asynchronously,
require an explicit producer-safe mailbox or thread rejection before building.

### 4. Build Readiness

Current lean: fix every P0/P1 and economical P2 that can force a crash, corrupt
authority, break wire decoding, or require another rebuild. Defer runtime-only
behavior questions until evidence exists.

## Suspected Overlap

- Equipment ring batching, server transactions, staging reset, and transport
  queue acceptance are one end-to-end transaction contract.
- Ownership grants and equipment replay ledgers share the same connection
  generation/session identity and should be audited together.
- Exact-action capability negotiation and compiled unreachable hook code may
  still hide an accidental activation path.
- Native manager reset coverage may be incomplete for newly added static state.

## Requested Output

Return, without edits:

1. P0/P1/P2 findings ordered by severity, each with exact file:line evidence and
   a concrete minimal fix;
2. compile/link/API/ABI problems likely to force another Windows rebuild;
3. authority, replay, queue, lifecycle, and shutdown failures under reconnect,
   load, server restart, or pressure;
4. original-branch gameplay behavior lost unintentionally, excluding the
   explicit unsupported boundaries;
5. verdicts on the four open decisions;
6. a final build-readiness verdict;
7. runtime-only risks that should not trigger speculative source changes.

Spend depth on load-bearing correctness. Do not review formatting, historical
connection-only notes, release prose, or runtime test ergonomics.
