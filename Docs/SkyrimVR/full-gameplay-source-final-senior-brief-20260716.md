# Full Gameplay Source Final Senior Review Brief

> Historical pre-integration brief. The active ABI is now 12 with capability
> revision 20; see `full-gameplay-source-postfix-senior-disposition-20260716.md`.

Date: 2026-07-16

## Goal And Review Boundary

Review the completed source tranche for porting Skyrim Together to Skyrim VR
1.4.15. The immediate decision is whether this source is ready for its one final
Windows/WinBoat compile-and-audit cycle, not whether runtime gameplay has already
been proven. This is a solo-maintained mod port: require strong crash, ABI,
concurrency, lifecycle, authority, and protocol correctness without adding
enterprise process.

Do not build, run tests, launch Skyrim, deploy, clean storage, edit files, or
review cosmetic style. The user required all source to be written before any
compile/test pass. This review is read-only and should identify every source fix
worth making before that build.

## Environment Facts

| Fact | Evidence |
| --- | --- |
| Repository | `/home/obesecatlord/Documents/SkyrimModding/SkyrimTogetherVR`, branch `main`, intentionally dirty source tranche. |
| Baseline | `original-skyrim-together` contains the original TiltedEvolution code for comparison. |
| Runtime | Skyrim VR `1.4.15.0`, executable SHA-256 locked in `Dependencies/SkyrimVR.lock.json`. |
| SKSE | SKSEVR `2.0.12`, plugin interface/release checks are separate from executable version. |
| CommonLib | alandtse `CommonLibVR`, branch `ng`, tag `v4.37.0`, commit `d36bc08cae5d445804d4c22ccd49fec4e9dfbdc4`. |
| Address data | VR Address Library hash is locked; generated aliases are collision-aware; curated overrides are in `Tools/SkyrimVR/vr_address_contract.py`. |
| Native boundary | The mapped client owns network/protocol state. `Code/vr_gameplay_bridge` is the CommonLib SKSEVR plugin and alone owns game pointers/mutation. |
| Bridge | Fixed POD ABI v11, capability revision 19, a 2048-record event ring and 1024-record command ring with one bound consumer each. |
| Build policy | No build/test has run for this source tranche. `Tools/SkyrimVR/build_winboat_gameplay.sh` requires a clean pushed commit and automates build, package/evidence import, and handoff refresh. |
| Runtime policy | Do not run Skyrim during this review. The human performs final gameplay verification. |

## Verified Evidence

1. `[verified: source trace]` `World.cpp` constructs only VR-safe gameplay
   services for the VR gameplay target; desktop mutation services remain in the
   non-VR branch.
2. `[verified: source trace]` `BridgeEndpoint`, `VRGameplayBridge`, and
   `CommandExecutor` enforce owner-thread execution, nonce/generation/epoch,
   entity generation, action/sequence ordering, fixed payload sizes, capability
   gates, and reserved-byte validation.
3. `[verified: source trace]` remote actor create/move/transfer/destroy uses
   retained CommonLib handles and result correlation rather than flat desktop
   actor layout.
4. `[verified: original/current comparison]` original protocol messages remain
   canonical. VR-only hit/magic/projectile snapshots are diagnostic when they
   lack the complete original mutation payload.
5. `[verified: source trace]` original gameplay paths now cover actor movement,
   exact actions/graph snapshots, appearance, equipment/inventory, actor state,
   death/respawn, NPC ownership, objects, projectiles, magic, quests, dialogue,
   packages, party, and world state.
6. `[verified: source trace]` local `Actor::RemoveSpell` capture uses desktop ID
   37772 mapped to verified VR RVA `0x6385F0` with a 16-byte prologue guard and
   remote-application echo suppression.
7. `[verified: executable disassembly]` desktop IDs 56205/56206 used by the
   dormant scripted-object-animation hooks resolve to Papyrus registration code
   in VR. Both original and current branches compile outbound publication out
   with `OBJECT_ANIM_SYNC=0`; inbound replay is implemented through Skyrim VM.
8. `[verified: source trace]` local-player packages now poll the typed
   `GetCurrentPackage`, refresh every five seconds, translate to
   `NewPackageRequest`, and retain only the latest unsent package per session.
9. `[verified: source trace]` death/respawn separates one local resurrection
   from retried server transport, chunks gold loss, scopes state to lifecycle,
   restores selection, clears bounties, centers the player, and applies delayed
   original-style knockdown/protection.
10. `[verified: source trace]` generic `TryPushBatch` reserves a contiguous ring
    range with one enqueue CAS. Command batches and every multi-record native
    event producer now use batch submission; commit records are ordered last.
11. `[verified: source trace]` transient incoming text retries are bounded and
    session-scoped. Spawn/action results, suppression windows, ownership, and
    pose state also reset on lifecycle/session boundaries.
12. `[verified: source trace]` HMD, hands, pelvis, thighs, calves, and feet apply
    to the canonical remote skeleton with basis validation and ragdoll guards.
    HIGGS durable inventory mutation stays canonical; PLANCK raw hits do not
    apply duplicate damage.
13. `[verified: API/source trace]` public VRIK has no remote-actor finger or
    calibration application API. No stable PLANCK remote physical grab/ragdoll
    API is present. These paths return explicit unsupported status.
14. `[verified: source/disassembly mismatch]` candidate FadeOutGame address rows
    do not identify a safely callable five-argument VR function. Respawn fade is
    intentionally omitted rather than guessed.
15. `[verified: scripts]` build and cleanup share an activity lock. Build output
    cleanup is bounded to reproducible paths. A successful build imports and
    validates the exact package/evidence revision and refreshes the handoff ZIP.

## Unverified Or Unknown

- `[unverified]` The current large source tranche compiles under MSVC/xmake.
- `[unverified]` `TryPushBatch` is correct under all MPMC wrap, producer
  preemption, and consumer interleavings.
- `[unverified]` Every original request/notify field is semantically preserved
  by the new VR translators, including retries and partial network enqueue.
- `[unverified]` Every manually pinned function signature/prologue is sufficient
  to prove its calling convention and semantic target.
- `[unverified]` `TESActionData::Create` cleanup and exact-action replay are
  lifetime-correct for all engine rejection paths.
- `[unverified]` equipment multi-message network submission cannot leave a
  harmful partially accepted baseline if one transport enqueue fails.
- `[unverified]` direct remote skeleton node writes remain stable with VRIK,
  SkyrimVR-FBT, HIGGS, and PLANCK update ordering in real gameplay.
- `[unknown]` Whether any original gameplay path still depends on an event that
  exists only in the disabled desktop service branch.

## Open Decisions And Current Lean

### 1. Ring Batch Algorithm

Current lean: retain the contiguous reservation algorithm if its sequence/CAS
reasoning is sound. It guarantees no queue-full partial publication and blocks
later producers behind any preempted reservation. Rejected alternative: a
global producer mutex would be simpler but adds process-shared synchronization
and does not solve consumer-visible partial assembly by itself.

### 2. Multi-Message Transport Transactions

Current lean: require retry or incremental baseline advancement anywhere a
logical mutation emits several original protocol messages and `Send` can fail.
Equipment is the highest-risk example. Rejected alternative: assume transport
enqueue never fails while online; queue pressure and disconnect races make that
an undocumented correctness dependency.

### 3. Exact Actor Action Lifetime

Current lean: keep the guarded native lane only if `TESActionData` construction,
owned fields, and cleanup are demonstrably correct; capability remains absent
until live semantic validation. Rejected alternative: replace exact action
replay with graph events, which is behaviorally incomplete.

### 4. Unsupported VR Mod Features

Current lean: keep remote VRIK fingers/calibration and direct PLANCK physical
replay unsupported until a verified actor-specific API exists. Rejected
alternative: call local-player APIs or guess offsets, which can mutate the wrong
actor or fight physics.

### 5. Scripted Object Animation

Current lean: inbound replay only, matching the fact that upstream outbound is
compiled out. Rejected alternative: hook IDs 56205/56206 in VR; disassembly
shows registration code, not the callable wrappers assumed by desktop code.

### 6. Respawn Fade

Current lean: ship the gameplay transaction without fade until a semantic VR
target is proven. Rejected alternative: call the conflicting Address Library
rows by guessed ABI.

### 7. Build Readiness

Current lean: fix every P0/P1 and economical P2 source issue from this review,
then perform one final static/unit/Windows package build. Rejected alternative:
build immediately and discover predictable cross-module failures one at a time.

## Suspected Overlap

- Ring batching, text retries, equipment baselines, and transport retry may be
  one transaction-consistency problem across two process boundaries.
- Lifecycle epoch reset and action/sequence dedup may conceal the same stale
  state bug under different services.
- HIGGS/PLANCK dedup and canonical combat/inventory ownership should be reviewed
  as authority design, not as independent mod integrations.

## Requested Review Output

Verify the brief against source and `original-skyrim-together` before accepting
its framing. Return, without edits:

1. prioritized P0/P1/P2 findings with file:line evidence and a concrete fix;
2. original-branch gameplay paths missing or incorrectly translated in VR;
3. ABI/address/lifetime hazards likely to crash before or during gameplay;
4. concurrency/transaction/lifecycle failures likely under queue pressure,
   reconnect, load, or shutdown;
5. a verdict for each open decision;
6. whether source is ready for the final build after the recommended fixes;
7. a concise list of runtime-only risks that should not trigger more source
   changes before evidence exists.

Use depth on correctness and future crash prevention. Do not spend the review on
formatting, historical docs, already documented product limitations, or runtime
test design beyond identifying the minimum evidence for a source-risk boundary.
