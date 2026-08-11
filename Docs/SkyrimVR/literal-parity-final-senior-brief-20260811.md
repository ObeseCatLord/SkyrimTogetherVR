# Skyrim Together VR Literal-Parity Final Senior Review Brief

Date: 2026-08-11

## Goal And Review Bar

Review the complete unbuilt source stage that ports the tracked
`original-skyrim-together` desktop behavior to Skyrim VR 1.4.15, then adds VR
embodiment and compatibility with VRIK/SkyrimVR-FBT, HIGGS, and PLANCK.

This is a high-risk native plugin review. Verify before critiquing. A passing
review must establish that the source is ready for its first Windows candidate
build, not that it is runtime-proven. Build, unit tests, package audits,
deployment, and Skyrim execution are intentionally deferred until this review is
dispositioned.

Repository: `/home/obesecatlord/Documents/SkyrimModding/SkyrimTogetherVR`

Current branch/parent: `main` at `e6b1af732965c11324844a3d0926d9a973e7d172`
plus the dirty source listed by `git status`.

Desktop comparison branch: `original-skyrim-together` at
`9d81ef07d68e4bb2bd94fca246e798a564b7fb92`.

## Verified Environment Facts

| Claim | Evidence |
|---|---|
| Runtime is Skyrim VR 1.4.15.0 and SKSEVR is 2.0.12. | [verified: source constants] `Code/vr_common/VRGameplayBridge.h`; package/runtime audits already bind executable and Address Library hashes. |
| CommonLib is current with alandtse `ng` 6.1.1. | [verified: fetched 2026-08-11] upstream `5ae93ea9059aae23990ad7f2cbf3a2624d85c117`; local VR-only source-build commit `612394bda3e2674da585831702308d571cf991b6` on top. |
| Fixed bridge mapping ABI is 22 and gameplay protocol revision is 13. | [verified: source] `Code/vr_common/VRGameplayBridge.h`, `Code/encoding/Structs/GameplayCapabilities.h`. |
| No current dirty source has compiled or run. | [verified: workflow] only static inspection and `git diff --check` have run. |
| Corrected respawn offsets are exact, not generated translations. | [verified: SteamStub-decrypted executable] `DispelAllSpells` is RVA `0x0557070`; `GetCOCPlacementInfo` is RVA `0x027A4C0`; both are curated overrides and prologue-gated. |
| The two previously generated respawn rows were wrong. | [verified: decrypted disassembly] `0x0579DF0` consumes RDX as a pointer; `0x0294070` is unrelated allocation code. Neither remains callable as the desktop ID. |
| Handoff artifacts must not be updated in this stage. | [verified: user instruction] no handoff script has run. |
| Desktop object-animation capture is not an omitted VR producer. | [verified: branch comparison] both branches compile `OBJECT_ANIM_SYNC=0` in `TESObjectREFR.cpp`; incoming VM replay remains. |
| Vivox is not present in the reviewable desktop source. | [verified: branch/history/artifact search] both branches contain only an optional `Services/Vivox` xmake hook, default `TP_VIVOX=0`; proprietary gitignored source, SDK, target, and runtime are absent. |

## Dirty Source Under Review

Core modified files:

- `Code/vr_gameplay_bridge/ActorActionHooks.cpp`
- `Code/vr_gameplay_bridge/VerifiedVrActorAction.{h,cpp}`
- `Code/vr_gameplay_bridge/AnimationAppearanceManager.cpp`
- `Code/vr_gameplay_bridge/VRFaceGen.{h,cpp}`
- `Code/vr_gameplay_bridge/ActorWorldManager.cpp`
- `Code/vr_gameplay_bridge/VerifiedVrDeath.{h,cpp}`
- `Code/vr_gameplay_bridge/VRInteractionManager.cpp`
- `Code/vr_gameplay_bridge/VRBodyPoseManager.cpp`
- `Code/client/Games/Skyrim/VR/VRBodyPoseCapture.cpp`
- `Code/client/Services/Generic/CharacterService.cpp`
- `Code/client/Services/Generic/VRDeathRespawnService.cpp`
- `Code/client/Services/Generic/VRPoseService.cpp`
- `Code/client/Services/Generic/VRActorReplicationService.cpp`
- `Code/client/Services/VRDeathRespawnService.h`
- `Code/client/Services/VRActorReplicationService.h`
- `Code/encoding/Structs/VRPoseUpdate.{h,cpp}`
- `Code/encoding/Structs/GameplayCapabilities.h`
- `Code/vr_common/VRGameplayBridge.h`
- `Code/planck_bridge/main.cpp`
- `Code/higgs_bridge/main.cpp`
- `Code/vrik_bridge/main.cpp`
- `Code/client/Services/Generic/DiscordService.cpp`
- `Code/client/Services/DiscordService.h`
- `Code/server/Services/ServerListService.cpp`
- `Code/server/Services/ServerListService.h`
- `Code/server/Services/VRHiggsRelayService.{h,cpp}`
- `Tools/SkyrimVR/vr_address_contract.py`
- `GameFiles/SkyrimVR/Data/SKSE/Plugins/SkyrimTogetherVR_AddressOverrides.csv`
- `Code/tests/encoding.cpp`
- `Code/tests/vr_gameplay_bridge_abi.cpp`

Status/checklist documents changed in the same tree are not implementation
evidence: `Docs/SkyrimVR/original-gameplay-parity-checklist.md` and
`Docs/SkyrimVR/parity-safety-stage-20260802.md`.

## Implemented Behavior And Evidence

### Literal Desktop Behavior

1. Exact actor actions
   - [verified: source and local executable/SKSEVR analysis] pins VR
     `PerformAction` `0x0643F20`, complex action `0x0644160`, animation-variable
     application `0x0646160`, `TESActionData` constructor `0x01FE070`, vtables,
     mediator singleton, and context.
   - Capability is advertised only after complete RVA/prologue/call-sequence and
     live-layout validation. Generic graph/event fallback remains.

2. Full remote appearance
   - [verified: official SKSEVR 2.0.12 source plus local executable] pins tint
     factory `0x0CAEF60`, mask application `0x03EADA0`, `TESTexture` ctor/dtor,
     render-texture creation, renderer global, dimensions, and vtables.
   - A transaction applies dynamic base/head/morph/tint state, rebuilds 3D, then
     composes masks. It is marked complete only after composition succeeds.

3. Death, respawn, and connected settings
   - [verified: source/local executable] native `FadeOutGame` is prologue-gated at
     `0x0903080`; essential and no-bleedout state are captured/restored.
   - [verified: original branch/source] first bleedout, actor-value forcing,
     ten-faction bounty clearing, five-second delay, COC placement, native move,
     spell/power restoration, knockdown, god mode, and fade ordering reproduce
     the desktop flow.
   - Kill-move frequency is saved, forced to zero for the connected session, and
     restored independently from death-system configuration.
   - [verified: decrypted Skyrim VR 1.4.15] the complete respawn target set is
     resolved atomically and prologue-gated: no-bleedout `0x062C950`, dispel-all
     `0x0557070`, COC placement `0x027A4C0`, and move `0x09E90E0`.

4. Remaining desktop domains
   - [verified: maintained checklist and source tracing] canonical entity
     assignment, movement/cells, graph state, equipment/inventory, actor values,
     combat/projectiles/magic, objects, quests/dialogue/party, world state,
     ownership, package state, death/respawn, and server authority remain native
     through the mapped client/CommonLib bridge/server paths.
   - Reviewer must independently compare these claims to
     `original-skyrim-together`; the checklist is not proof.

### VR Embodiment And Compatibility

1. Body/finger pose
   - Protocol revision 14 includes body format 3: pelvis/legs, spine0/1/2, neck,
     clavicles, upper arms, forearms, and a sparse 30-finger-joint unit
     quaternion payload captured after VRIK and HIGGS.
   - SkyrimVR-FBT world-only spine corrections are converted from final world
     transforms to effective parent-local rotations. The receiver applies the
     body parent-first, propagates it, then resolves HMD/hand world endpoints.
   - Joint sequence/root generation must equal the enclosing body frame.
   - Body formats 1 and 2 remain decoded and validated within the exact-match
     revision-14 schema.
   - Remote application accepts only managed actors, re-resolves the current 3D
     root, validates matrices, bounds pending state, and clears/suppresses during
     ragdoll.

2. Pose queue policy
   - At most one admitted plus one replaceable pre-admission pose batch exists
     per remote player. Admitted visual work is never replayed.
   - A maximum format-3 frame is 112 commands. Pose work may use at most 256 of
     512 result-owner records and 128 of 192 pending-work records, preserving
     state-changing gameplay capacity.
   - A waiting latest frame refreshes its uncommitted acceptance token after the
     prior admitted frame advances the ledger.
   - Body and finger skeleton commits retain original locals and roll back plus
     recompute hierarchy/world bounds when endpoint resolution, propagation, or
     bookkeeping fails.

3. PLANCK/HIGGS authority
   - PLANCK interface revision 1 is acquired only at PostPostLoad. Transient hit
     pointers/polling APIs are not called; canonical engine hit and actor-value
     messages own damage.
   - Periodic PLANCK/VRIK diagnostics are written from the existing HIGGS/VRIK
     game-thread callback. No background thread can outlive either SKSE DLL.
   - HIGGS callbacks still provide final post-VRIK/post-HIGGS capture ordering.
     Durable stash/consume changes remain canonical inventory traffic.
   - HIGGS handoff publishing is callback-driven and rate-limited on the game
     thread. A bounded ordered mutation window is transported at least once,
     independently of observation throttling. Server and receiver ledgers commit
     after fanout and bridge admission respectively, suppressing retransmits.

4. Worker lifetime
   - Discord callback and server-list HTTP workers are owned and joined; event
     subscriptions disconnect before owner teardown. Server-list workers use
     immutable snapshots and return rejection state to the owner thread.
   - Server-list HTTP uses libcurl with c-ares, a five-second total timeout, a
     stop-aware progress callback, and RAII multi/easy membership teardown so
     DNS cannot make the owned join unbounded.

5. Explicit public-API boundary
   - VRIK exposes local-player state but no remote solver API, so calibration
     diagnostics are not applied through local-player calls.
   - PLANCK exposes no stable remote physical grab/ragdoll application API, so
     direct remote physics replay is not fabricated. Pose writes stop in ragdoll.

## Load-Bearing Risks To Verify

1. **Actor action ABI and lifetime:** verify calling conventions, complex-path
   semantics, stack object alignment, constructor/vtable handling, manual member
   release, exception boundaries, hook recursion, action type flags, and whether
   every path matches desktop `ForceAction` closely enough to advertise exact
   capability.

2. **FaceGen renderer ownership:** verify all function signatures, object sizes,
   texture/refcount ownership, `TESTexture` construction/destruction, render
   target ownership, tint-array element stride, material lookup, thread affinity,
   and failure behavior after base/3D mutation. Decide whether the unrollbackable
   post-rebuild failure requires quarantine instead of automatic resend.

3. **Death state:** verify actor-base flag mutation is confined to dynamic remote
   bases or the local replicated flow as intended, no shared template/base is
   corrupted, native fade signature is exact, and reset ordering cannot leave
   essential/no-bleedout or kill-move state altered.

4. **Pose protocol and math:** verify fixed-order deserialize behavior, malformed
   version handling, all bounds, body/joint coherence, quaternion convention,
   named-node ordering, local/world spaces, root replacement, wraparound sequence
   comparisons, and ragdoll/lifecycle cleanup.

5. **Pose queue correctness:** verify iterator/reference safety around retirement,
   result callbacks, timeout, disconnect, player removal, epoch reset, token
   refresh, semantic tombstones, and command-batch failure. Challenge the 256/128
   budgets and prove gameplay cannot be starved.

6. **PLANCK ABI/lifecycle:** verify interface message size and revision against
   local `activeragdoll`, listener registration ordering, pointer retirement,
   writer-thread teardown, and that no secondary damage path exists.

7. **Literal branch parity:** compare every tracked desktop producer, message,
   server handler, receiver, mutation, lifecycle transition, `.esm`/Papyrus
   responsibility, and packaging prerequisite to main. Identify any behavior
   currently represented only by diagnostics, comments, or a capability bit.

8. **Crash/concurrency surface:** inspect all raw RVAs/prologues, relocation IDs,
   atomics, retained handles, fixed buffers, bridge rings, result-owner maps,
   native allocations, exception boundaries, and teardown callbacks for UAF,
   double free, stale actor/root, partial transaction, overflow, or wrong-thread
   engine access.

9. **Owned workers and synchronous I/O:** verify Discord teardown order,
   server-list join behavior and result publication, and the cost/failure mode of
   callback-thread HIGGS handoff writes.

## First Sol Review Disposition

The earlier Sol review was advisory input to this source stage, not the final
gate. Its findings were independently checked and dispositioned as follows:

| Finding | Disposition | Evidence/current state |
|---|---|---|
| PLANCK/VRIK writer-thread teardown can execute or join under loader lock. | Adopted. | Writer threads and static joiners were removed; rate-limited handoff writes now use existing game-thread callbacks. |
| Death/respawn ordering was not literal desktop behavior. | Adopted. | `ActorWorldManager` and `VerifiedVrDeath` now reproduce the original first-bleedout and delayed respawn sequence. |
| Desktop death policy also changes faction rank. | Rejected after verification. | Original `Actor.cpp` `SetPlayerRespawnMode` for form `0x14` calls essential/base-essential and no-bleedout controls; it does not change faction rank. |
| Connected greeting/world-encounter/kill-move settings were absent. | Rejected after verification. | `VRInteractionManager` captures, enforces, and independently restores all three session settings. |
| FaceGen failure had no convergence retry. | Adopted. | Explicit transient commit failures now retry the full snapshot with a new local transaction sequence and bounded attempts. |
| PLANCK acquisition was too early. | Adopted. | API dispatch occurs only on PostPostLoad. |
| Body wire tests lacked malformed-version coverage. | Adopted within available reader API. | Encoding tests cover v1/v2/v3, unsupported versions, masks, NaN/quaternion bounds, and stale extension state; the shared reader exposes no cursor/error state for a true truncated-buffer assertion. |
| FBT replication stopped at pelvis and legs. | Adopted. | Format 3 carries the complete upper/lower body and finger hierarchy with world-only spine recovery. |
| HIGGS writer/static joiner could outlive its DLL. | Adopted. | Handoff writes now run from the validated game-thread callback; no HIGGS writer or static joiner remains. |
| Retained HIGGS LastEvent replayed with each observation. | Adopted and strengthened. | Protocol 14 carries a bounded ordered mutation batch independently of observation telemetry. Server state advances after fanout; receiver state advances after bridge admission. |
| Discord and server-list detached workers could outlive owners. | Adopted. | Both workers are owned and joined; server-list engine/config access is snapshotted before launch. |
| Pose application could leave partial skeleton writes. | Adopted. | Body/endpoint and joint commits restore original locals and propagate rollback on failure. |
| Death-policy restore discarded retry state. | Adopted. | Stable form IDs and prior flags remain retained until a complete restore succeeds; periodic/reset paths retry. |
| Three respawn calls had only executable-page validation. | Adopted and strengthened. | All raw targets now require exact runtime and 16-byte prologues; wrong generated dispel/COC rows were replaced with independently proven RVAs. |

### Final Split Sol Review Disposition

The final source gate was split by native ABI, protocol/pose, and lifecycle so
each Sol xhigh review had a bounded evidence set. All verified findings were
adopted before the candidate build:

| Finding family | Final disposition |
|---|---|
| Pose helper declaration order and partial skeleton writes | Helper is declared before use; body, endpoint, and finger commits restore locals and propagate rollback. |
| Respawn facing and essential-policy ordering | Respawn preserves actor rotation; actor flag, base flag, and no-bleedout transitions match desktop order. |
| FaceGen preflight and raw signatures | Vtables are validated before mutation; SKSEVR base-return and const-reference signatures are used exactly. |
| Retained single HIGGS event and observation throttling | Protocol 14 carries a bounded ordered replay window independently of observation telemetry. |
| HIGGS authority, backpressure, and conflict stalls | Authority is staged transactionally; accepted/skipped terminals are bounded; accepted replay is retained; receiver unseen tails publish atomically. |
| HIGGS reconnect replay and unmappable objects | Offline events establish a same-epoch replay floor; unresolved receiver heads are bounded and explicitly skipped instead of blocking later edges. |
| Direct VRGrab throttling and payload bounds | Every newer discrete edge is considered; shared finite and 1,000,000-magnitude checks run before authority and receiver admission. |
| Detached or unbounded worker teardown | Discord remains owned/joined; server-list networking is owned, cancellable, DNS-bounded, and joined. |

Residual protocol limits are explicit: HIGGS is ordered at least once rather
than network exactly once, has no receiver ACK, and cannot recover mutations
older than its 32-event producer window. A bridge-only producer epoch reset in
the same multiplayer session remains fail-closed; normal disconnect/reconnect
rebases the retained handoff stream.

## Current Leans And Rejected Alternatives

| Decision | Current lean | Rejected alternative |
|---|---|---|
| Exact actions | Advertise only after complete native validation. | Generic events alone are not literal action parity. |
| FaceGen | Keep verified native compositor and fail closed. | Degraded base-only appearance is not literal visual parity. |
| Pose delivery | Latest-frame reliable bridge work with strict bounds. | Raising global limits or bypassing result ownership can starve gameplay or lose lifecycle proof. |
| PLANCK damage | Canonical engine messages only. | Polling PLANCK hit pointers duplicates authority and uses transient data. |
| Remote physics/calibration | Keep public-API boundary explicit. | Calling local-player-only or reverse-engineered mutation APIs without a proven contract is not shippable parity. |
| Vivox | Treat as absent optional proprietary product input. | Inventing a replacement is new functionality, not a port of available source. |

## Reviewer Output Contract

Return findings first, ordered P0-P3, with exact file:line evidence and an
actionable correction. Verify each load-bearing claim before accepting it.
Separate:

- source blockers that must be fixed before build;
- build/static-test gaps that can be checked by the candidate pipeline;
- runtime-only evidence requirements;
- external API/licensing limitations that source cannot solve safely.

Then provide a literal desktop parity matrix by domain, a VR compatibility
matrix, missing tests, and a clear `ready for candidate build: yes/no` verdict.
Do not edit files, build, run tests, deploy, launch Skyrim, update handoff
artifacts, or broaden into release marketing.
