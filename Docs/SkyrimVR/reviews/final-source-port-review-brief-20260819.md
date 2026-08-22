# Skyrim Together VR Final Source Port Review Brief

Date: 2026-08-19

## Goal And Scope

Review the complete unbuilt source candidate for a native Skyrim Together port
to Skyrim VR 1.4.15.0. The target is literal behavior parity with the tracked
`original-skyrim-together` branch, followed by VR embodiment through VRIK/FBT,
HIGGS, and PLANCK. This is a solo-maintained mod project, but crashes, corrupt
physics state, protocol divergence, or falsely advertised capabilities are
release blockers.

This is a read-only final source review. Do not edit files, build, run tests,
deploy, launch Skyrim, update a handoff, or touch the server.

## Verified Environment Facts

| Fact | Status and verification |
|---|---|
| Repository | `[verified: git]` `/home/obesecatlord/Documents/SkyrimModding/SkyrimTogetherVR`, branch `main`, base HEAD `bcfdd366` |
| Candidate state | `[verified: git status]` large dirty worktree, 151 tracked root files changed plus new protocol/build/test files and a patched PLANCK submodule; none of this candidate has been built |
| Last historical proof | `[verified: checked-in checklist]` commit `9eba3bb5` built and connected one client; it does not prove the current worktree or two-client gameplay |
| Runtime | `[verified: source/constants]` Skyrim VR 1.4.15.0 and SKSEVR 2.0.12 |
| CommonLib | `[verified: submodule]` project commit `586eac1f4`, merging alandtse CommonLibVR/NG `7bdcb9efe` (6.3.4) |
| Addressing | `[verified: source/docs]` VR Address Library plus collision-aware generated aliases and curated exact-RVA/prologue overrides |
| Mapping ABI | `[verified: source]` fixed gameplay bridge ABI 24 |
| Wire protocol | `[verified: source]` exact-match gameplay protocol revision 21; revision 20 introduced PLANCK interface002 and revision 21 owner-scopes and revision-orders quest updates/recovery |
| Build policy | `[verified: user instruction]` no compile or tests until source coding and this senior review are complete; final MSVC build will use WinBoat |
| Deployment policy | `[verified: user instruction]` do not modify local Skyrim, Foundry server, or handoff during this review |
| CI | `[verified: repo/user instruction]` automatic GitHub workflows disabled and not part of acceptance |
| Root pressure | `[verified: df]` root is 96% used; bounded cleanup ran and removed only declared reproducible temporary output |

## Candidate Architecture And Evidence

1. `[verified: source trace]` The mapped client owns transport, protocol,
   canonical entities, original gameplay services, retries, and session state.
   `SkyrimTogetherVRGameplayBridge.dll` owns CommonLib/SKSEVR engine pointers,
   hooks, retained handles, and native mutation.
2. `[verified: source trace]` Full gameplay admission requires the complete
   native bridge contract and exact animation actions. Optional VR relays are
   negotiated only when their runtime bridge reports operational.
3. `[verified: source trace]` Object, owner-scoped quest, actor/equipment, and
   mount durable failures now have revisioned server-authored canonical
   recovery. Quest producers bind the authenticated local player ID, the server
   authorizes the requested canonical owner against party membership, and
   deltas/snapshots share one per-owner revision sequence. Mount responses are
   request/revision/session/lifecycle scoped; ordinary ordered notifications
   retire older recovery, while a canonical revision commits only after the
   matching native action succeeds for the same entity/work generation.
4. `[verified: official HIGGS source plus implementation]` The 41-slot HIGGS
   interface is mirrored with ABI assertions. Pull/grab/drop/stash/consume,
   held object transform, node identity, two-hand state, and post-HIGGS pose are
   captured. Remote object motion solves the grabbed-node target back to the
   reference root. It deliberately does not call HIGGS `GrabObject` for remote
   players because that API owns the local player's hand.
5. `[verified: source trace]` HIGGS mutation sequencing is separate from sampled
   held transforms; terminal mutations cancel stale sampled work. Server relay
   validates identity, range, ordering, and object lease. Producer-epoch or
   replay-window gaps emit a baseline-only rebase marker until transport accepts
   it; the server authenticates that marker and releases predecessor leases in
   the same admitted transaction. Network-vector packing bounds finite values,
   defines nonfinite encoding, and keeps sign bits independent from magnitudes.
6. `[verified: source trace]` The PLANCK submodule is patched with a data-only
   interface revision 2 for remote hit impulses, ragdoll enter/exit, bounded
   impulse-driven actor grips, session clear, and local physical-event dequeue.
   The local producer emits actor-only hit, ragdoll, and grip lifecycle events;
   ordinary object grabs remain HIGGS-owned.
7. `[verified: source trace]` PLANCK commands cross a fixed POD bridge, an
   authenticated exact-revision wire message, server sender/range/PVP/lease/
   order/rate checks, bounded receiver retry FIFO, and game-side replay
   suppression. Damage remains exclusively on the canonical health channel.
8. `[verified: source trace]` PLANCK local and remote queues are bounded,
   terminal GripEnd/RagdollExit events are protected from ordinary queue
   eviction, node names reject control characters and overlong aliases, event
   IDs avoid zero, and logs use power-of-two aggregate reporting. The plugin
   binds each `sourceSession + GripId` to one target at API admission, rejects
   target-changing update/end traffic before queueing, and retires that identity
   on end, ragdoll exit, session clear, target retirement, or reset.
9. `[verified: source trace]` PLANCK uses one server-visible producer epoch per
   authenticated server-nonce/connection-generation pair. Gameplay-bridge
   lifecycle changes are local discard fences and cannot rotate that wire
   identity. ClearRemoteSession is immediate rather than queued; copied commands
   recheck bounded cancellation tombstones before mutation, failed client clears
   remain in a bounded retry queue, and capacity exhaustion fails admission
   closed.
10. `[verified: archive inspection]` The user-supplied
   `hk2010_2_0_r1.7z` is Havok 2010.2, SHA-256
   `7349946401a820784fc86aa13bc667def6c409ed938865b01c8e6c3d86692555`.
   Required Common/Physics/Animation headers are present. Official SKSEVR
   2.0.12 archive hash is
   `f03df5d8663f2c9a781f830fb0809c63a9a0e3b626d6d1a96e38493f81a3c9ad`.
    The staged official SKSEVR source contains 403 files and has canonical tree
    hash `edbb4945544718054279c9f949ac689e735b13c8efcd3272b6f74e2398dd5d53`.
11. `[verified: source trace]` New PowerShell/batch tooling hash-verifies and
    freshly extracts those external dependencies outside git, hashes the full
    consumed Havok Common/Physics/Animation closure and full SKSEVR source tree,
    race-checks both trees around the forced PLANCK `Rebuild`, binds the
    DLL/source/dependency provenance into the package manifest, removes
    author-specific paths from PLANCK's vcxproj, discovers MSBuild with vswhere,
    and deploys only when an explicit Skyrim path is set.
12. `[verified: source trace]` The server grants at most one PLANCK grip lease
    per canonical target actor, binds producer identity to the authenticated
    connection, validates target/PVP/cell/range/physical distance, and commits
    ordering and rate state only after successful fanout. HIGGS held-state relay
    likewise scrubs authority-bearing held fields when its object lease cannot
    be renewed and rejects actor references so PLANCK remains the actor-physics
    owner.
13. `[verified: static checks]` `git diff --check` and PLANCK submodule
    `git diff --check` pass. No compile, unit test, runtime test, or PowerShell
   execution has been performed for this candidate. Shell syntax checks pass for
   the WinBoat candidate, clean-build, and private dependency provisioners; the
   source-tree hasher reproduced the 403-file SKSEVR digest above. Python source
   compile checks pass for the changed audits/hashers, all 51 root C++ test
   translation units are covered by the xmake wildcard, and message factories,
   opcodes, and active protocol assertions consistently use revision 21.
14. `[verified: source trace]` Post-auth loss of a negotiated HIGGS or PLANCK
    interface is detected, logged once, removed from active routing, and causes a
    session-generation-bound fail-closed disconnect rather than continuing with
    a capability the endpoint no longer implements.

## Open Decisions And Current Lean

### A. PLANCK physics implementation safety

Current lean: retain the interface002 design only if its game-thread/Havok
thread, lock ordering, pointer lifetime, unit conversion, queue ordering, and
replay suppression are internally sound. Fail closed or narrow advertised
features wherever source cannot support the promise.

Rejected alternative: invoking existing local-player HIGGS/PLANCK entry points
for remote actors, because those APIs can seize the receiver's local hands or
mix damage authority.

### B. PLANCK ragdoll-exit semantics

Current lean: `RagdollExit` may terminate remote grip ownership and allow the
engine's normal get-up transition, but it must not be advertised as an active
remote get-up primitive if no such mutation occurs. Determine whether this is
correct convergence or a missing implementation.

Rejected alternative: directly writing actor knock-state bits without a proven
engine transition contract.

### C. Optional capability truthfulness

Current lean: advertise PLANCK interface002 only when all five exact feature
bits and all bridge exports are live; HIGGS only after official API capture is
operational. The server should reject incomplete exact-revision peers.

Rejected alternative: capability inference from DLL presence.

### D. Repaired recovery and physics invariants

Current lean: the prior P1 findings are repaired, but each repair needs focused
adversarial verification: quest recovery must never snapshot the requester when
the failed mutation originated from another party owner; mount recovery must not
commit on queue admission; PLANCK local lifecycle clear must not erase wire
replay high-water; `GripId` target identity must agree at server, bridge, API,
and game-thread boundaries; and authenticated HIGGS baseline rebases must be
accepted without admitting retained mutation edges. Verify the implementation,
not merely the new policy tests.

Rejected alternative: declaring the five prior P1s closed from token-search
audits alone.

### E. Final source-completeness claim

Current lean: after P0/P1 corrections, call source implementation complete but
retain connection/bootstrap-alpha release classification until exact-HEAD build
and paired runtime evidence exist. Do not convert unproved runtime rows to
complete.

Rejected alternative: equating protocol/services/static audits with two-client
behavior.

## Suspected Overlap

- PLANCK grip replay, HIGGS object replay, canonical movement, and VRIK/FBT pose
  can all touch physical presentation. Verify ownership boundaries prevent
  competing writes, especially during ragdoll, cell transfer, and disconnect.
- Canonical damage, PLANCK hit impulse, spell/projectile replay, and combat
  events share one collision episode. Verify exactly one damage mutation and
  one intended physical impulse.
- Canonical mount recovery and actor spawn/recovery share acceptance ledgers and
  retained handles. Verify one cannot commit against a replaced entity.
- Capability negotiation and runtime bridge availability are one safety
  boundary; check both authentication-time and post-auth bridge loss.
- Client PLANCK plugin tokens and wire replay identity deliberately exclude the
  local gameplay lifecycle generation. Verify local queue discard, old-token
  clear, replacement admission, and authenticated connection replacement remain
  correctly ordered without coupling either identity to that local lifecycle.
- PLANCK cancellation tombstones expire only after a game-thread cancellation
  sweep and a retention interval. Verify overflow fail-closed behavior clears
  all active controllers and cannot be reset by an untrusted producer.

## Required Review Output

Verify before critiquing. Read the real dirty source and original branch where
needed. Return a prioritized list of P0/P1/P2 findings with exact file/line
evidence and concrete repairs. Explicitly assess:

1. crashes, UAF/data races, deadlocks, ABI/layout mismatches, and unsafe Havok
   or SKSE thread use;
2. ordering, replay, queue overflow, terminal events, retries, reconnect, cell
   changes, lifecycle reset, and entity replacement;
3. server authority, capability claims, protocol encode/decode symmetry, and
   mixed optional-mod behavior;
4. whether every original desktop gameplay service still has a complete VR
   producer, server path, receiver, and failure recovery;
5. HIGGS, PLANCK, VRIK, FBT ownership conflicts and missing API semantics;
6. build-script correctness sufficient to attempt one final WinBoat build.

Treat these five previously reported P1s as explicit go/no-go regression targets:
owner-correct quest recovery, native-complete mount recovery, PLANCK replay
high-water retention, producer-plus-grip target identity, and HIGGS epoch rebase.
Also inspect the new PLANCK API admission map for exception/queue transaction
ordering and same-target duplicate-Begin semantics.

Separate verified defects from runtime-only risks. Re-rank by leverage and
surface missed failure modes. If there is no source blocker, state that
unambiguously and list only the exact build/runtime gates that remain.

## Not In Scope

- Do not review handoff packaging, public release prose, GitHub CI, Foundry
  deployment, Monado/OpenComposite/XRizer launch behavior, or FUS-wide DLL
  compatibility in this pass.
- Do not require Papyrus replacements for behavior already implemented natively.
- Do not treat absent two-client evidence as a source defect.
- Do not re-open settled fail-closed raw addresses without contradictory source
  or binary evidence.
