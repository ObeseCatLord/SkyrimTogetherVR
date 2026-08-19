# Skyrim Together VR Post-Parity Source Senior Brief

Date: 2026-08-19

## Completion Record

The exact-equivalence source pass is now complete for activation, calendar,
equipment authority, summon authority, remote temporary-save exclusion, local
drop capture, and weather access. Their Skyrim VR 1.4.15.0 contracts are direct
RVA/direct-ID contracts with executable, range, page, and entry-fingerprint
validation; no generated alias was added for them. `VersionDb` denies the
known false VR IDs for summon, flee, dialogue response, drop, and detection.

This is static source evidence only. No current dirty-worktree build, test,
single-client, or two-client result is implied. Residual runtime gates are:

- final-equipment queued/retry/timeout/resync paths;
- `DropObject` to `TESContainerChangedEvent` synchronicity and ambiguity;
- weather force/release and temporary-save target behavior across lifecycle;
- remote AI detection, queued response, and flee behavior without unproven
  native hooks; and
- PLANCK/non-PLANCK damage deduplication and ragdoll/pose non-interference.
  Direct remote PLANCK physical replay remains an optional extension outside
  tracked desktop parity.

## Goal And Scope

Review the complete unbuilt source pass that ports tracked desktop Skyrim
Together behavior to Skyrim VR 1.4.15.0. This is a correctness gate before the
first Windows build of this revision. The project is a solo-operated native
mod, but it crosses hostile ABI, shared-memory, networking, and game-engine
boundaries; fail-closed behavior is required.

Do not edit files, build, run tests, deploy, commit, package, or launch Skyrim.
Return findings first, ordered P0-P3 with exact file/line evidence. Verify claims
against source rather than accepting this brief.

## Environment Facts

| Fact | Status | Evidence |
|---|---|---|
| Target runtime is Skyrim VR 1.4.15.0 with SKSEVR 2.0.12. | [verified: source/config] | `Dependencies/SkyrimVR.lock.json`, bridge target guards. |
| Current branch is `main` at committed base `14ea8df7`; the worktree is intentionally dirty and unbuilt. | [verified: git] | `git status --short`, `git rev-parse --short HEAD`. |
| Accepted deployment remains commit `c1ffb57e`; this dirty candidate has not touched it. | [verified: operator record] | Current session deployment boundary. |
| CommonLib is pinned to project commit `586eac1f4`, based on alandtse CommonLibVR/NG 6.3.4. | [verified: lock/submodule] | `Dependencies/SkyrimVR.lock.json`, `Libraries/CommonLibSSE-NG`. |
| Mapping ABI is 23, bridge capability revision is 34, gameplay protocol revision is 17. | [verified: source/lock] | `Code/vr_common/VRGameplayBridge.h`, `Code/encoding/Structs/GameplayCapabilities.h`, `Dependencies/SkyrimVR.lock.json`. |
| No build or test result may be inferred for the current dirty revision. | [verified: process] | Compilation was deliberately deferred until all coding and this review finish. |

## Implemented Repair Set

1. [verified: diff] CRT startup is forwarding-only for VR; verified active
   `Main::Draw` owns client initialization.
2. [verified: diff] Quest mutation uses synchronous native stage application,
   desktop stage-to-active ordering, suppression lifetime, and postconditions.
3. [verified: diff] PE mapping validates exact executable size/hash and bounded
   PE ranges; malformed loader fixtures exist.
4. [verified: diff] Evidence tooling centralizes collector arguments, records
   artifact/source identity, supports action-correlated two-client evidence,
   and attributes bridge loss.
5. [verified: diff] Event publication uses a fixed local backlog; command
   results reserve capacity before mutation and retain FIFO under ring pressure.
6. [verified: diff] Spawn/equipment failure and timeout now terminalize the
   uncertain request. Only explicit pre-final-mutation equipment failures may
   retry; lifecycle retirement requires a fresh canonical spawn snapshot.
7. [verified: diff] `FinalEquipmentTransactions` is distinct from the unused
   direct VR equipment relay capability.
8. [verified: diff] Exact VR hooks were added for dialogue choice, calendar,
   progression, actor actions, damage/death loot, positive ActiveEffect health,
   RestoreActorValue health, generic/actor SetPosition, MoveTo, and the
   root-motion/controller processor. Every raw target pins runtime, RVA,
   executable range, and prologue and installs transactionally.
9. [verified: executable RE] Desktop AE ID 37356 maps to VR RVA `0x005E0E20`;
   the prior `0x006226A0` override was unrelated crime code and is replaced.
10. [verified: diff] Remote actors disable AI, retain authoritative root/health,
    restore preexisting-reference AI/commanding-actor state, and bind player
    summons. Authoritative root replay has a thread-local narrow bypass.
11. [verified: diff] Local-player physical damage to a managed remote NPC is
    applied once and emitted as its observed signed health delta. Protocol 16
    accepts it only with a sender-owned player attacker, range, finite negative
    delta, and monotonic session-bound action nonce. Ordinary VR owner health
    state remains a separate zero-attacker lane restricted to the owned target.
12. [verified: diff] Detour membership uses a fixed atomic managed-actor
    registry with reader leases and two-phase retirement; callbacks never read
    the mutable avatar map.
13. [verified: diff] Multi-record bridge publication is all-or-none by one
    session/entity identity, and retained batches flush without splitting.
14. [verified: executable RE] Calendar ID 35402/RVA `0x005AD8F0` is the VR
    time-advance body. The generated desktop-ID correlation at `0x005DB010` is
    an unrelated form-property thunk and is not used.
15. [verified: executable RE and diff] TESQuest::SetStage RVA `0x003803D0`
    is synchronous and now validates exact runtime/RVA/text/page/prologue
    before readiness. Completed-stage replay is idempotent.
16. [verified: executable RE] Root processor `0x005E0E20` is reached
    synchronously from full Actor::Process; Actor::SetPosition controller
    synchronization is inline and does not queue a later hook re-entry.
17. [verified: diff] Logging is aggregate/once for hot hook suppression and
    detailed in periodic diagnostics rather than per-frame info logs.
18. [verified: diff] Protocol 17 gives inventory, health, projectile, spell,
    and magic-effect edges stable server event IDs. Spawn and final-equipment
    terminal recovery request bounded canonical snapshots instead of waiting
    for unsolicited updates; actor-keyed server ledgers retire with actor/object
    destruction and report capacity exhaustion at first/128-event intervals.
19. [verified: diff] Managed remote speech/subtitles are suppressed before
    native presentation outside explicit network replay. Generated actor-AI
    aliases for desktop IDs 37577, 39643, and 42704 are proven unrelated and
    never admitted as native targets.
20. [verified: diff] Remote avatar publication now requires observed
    `EnableAI(false)`. Restoration or retirement failure faults the endpoint,
    keeps the registry reader gate closed, and retains the actor/record rather
    than returning an altered caller-owned reference to the engine.
21. [verified: diff] Strict paired gameplay evidence rejects operator-supplied
    `engine_correlated` claims and cannot certify a scenario until archived
    native/server traces corroborate action identity and payload digest.

## Exact RE Findings To Audit

- `TESObjectREFR::SetPosition`: VR RVA `0x002A8010`, ABI
  `void(TESObjectREFR*, const NiPoint3&)`.
- Actor SetPosition override: VR RVA `0x005DC380`, ABI
  `void(Actor*, const NiPoint3&, bool)`.
- `MoveTo_Impl`: VR RVA `0x009E90E0`, ABI already used by
  `AvatarManager::MoveActorToLocation`.
- Root-motion/controller processor: desktop AE ID 37356 maps through SE/VR ID
  36365 to RVA `0x005E0E20`, ABI
  `void(Actor*, float)`.
- `Actor::DoDamage`: direct VR ID 36345, RVA `0x005DE930`, returns the post-call
  death predicate. Suppressed remote-player calls use the pure adjustment helper
  at RVA `0x005ECEA0` to preserve desktop predicted-lethal semantics.
- Positive `ValueModifierEffect` health path: RVA `0x0056E070`; actor-value
  override `UINT32_MAX` uses the typed `ValueModifierEffect::actorValue` field.
- `RestoreActorValue`: direct VR ID 37513, RVA `0x006296B0`; desktop semantics
  block all remote health restoration through this entry.
- `MenuTopicManager::PlayDialogueOption`: RVA `0x00574BF0`, ABI
  `bool(MenuTopicManager*, int32_t)`.

## Open Decisions And Current Lean

| Decision | Current lean | Rejected alternative |
|---|---|---|
| Are the four root authority hooks coherent and sufficient? | Keep all four: outer processor/MoveTo plus actor and generic position gates cover different side effects. Retain owner-tick reconciliation for bypasses. | Hooking only generic SetPosition misses actor controller/3D and MoveTo transaction work. |
| Can hook callbacks classify managed actors safely? | Fixed atomic registry with reader leases and two-phase retirement; no mutable avatar map access. Audit lifetime and memory-order correctness. | Adding a mutex inside hot engine/physics hooks risks deadlock and reentrancy. |
| Is attacker-originated NPC damage authorization adequate? | Sender-owned player attacker, target range, NPC-only target, finite negative delta, and monotonic session-bound action nonce. Audit the separate owned-player state lane and mutation/fanout ordering. | Range alone permits forged or replayed mutations. |
| Is server edge dedup/recovery coherent? | Keep stable server event IDs plus bounded explicit actor/equipment canonical resync. Audit revision monotonicity, request binding, lifecycle cleanup, and retry terminalization. | Payload-equality dedup drops legitimate repeated edges; passive recovery can remain quarantined forever. |
| Can managed remote actors retain native behavior? | Fail avatar admission unless AI disable is observed, then suppress verified speech/subtitle entries outside replay. Leave unproven ProcessResponse/detection/fleeing candidates unhooked. | Generated desktop-to-VR aliases resolve to unrelated code and are not an ABI contract. |
| Is event backlog transaction-safe? | Keep all-or-none batch admission and whole-batch flush. Audit lifecycle retirement and command-result reservation cleanup. | Returning to direct ring drops reintroduces acknowledged-before-result and partial-transaction failures. |
| Are recovery paths idempotent? | Uncertain spawn/equipment operations terminalize and require fresh canonical state; only explicit pre-mutation equipment failure retries. | Replaying stale snapshots or post-commit transactions duplicates gameplay. |
| What counts as literal desktop parity? | Match behavior enabled in tracked `original-skyrim-together`; do not claim disabled `#if 0` combat-target code or unavailable proprietary Vivox as shipping parity. | Advertising source-disabled or absent components is not literal branch parity. |
| Release classification | Remain connection/bootstrap alpha until two current-revision clients prove every advertised gameplay domain. | Source/build/single-client proof cannot establish multiplayer behavior. |

## Review Questions

1. Find any ABI mismatch, wrong target, unsafe cast, partial hook-install state,
   detour recursion, unload hazard, or fail-open path in all exact hooks.
2. Find races/reentrancy across `AvatarManager`, hook callbacks, mapped rings,
   event backlogs, command-result reservations, lifecycle retirement, and fault
   handling.
3. Prove or disprove spawn/equipment retry idempotency, owner identity binding,
   and bounded-state cleanup.
4. Audit server sender authorization and range/ownership checks for all newly
   enabled state-changing paths, especially health, inventory, equipment,
   ownership, quest, teleport, and time.
5. Compare the tracked `original-skyrim-together` branch to the current source
   by behavior, not filenames. Identify any enabled desktop producer, server
   mutation, receiver, lifecycle behavior, or result semantics still missing.
6. Identify Skyrim SE behavior for which the current VR replacement is not
   demonstrably equivalent and specify whether more SkyrimVR.exe RE is needed.
7. Check that diagnostics are sufficient for two clients without per-frame log
   spam and cannot make stale evidence look current.
8. Identify missing behavior tests that are release-blocking before runtime.

## Not In Scope

- Do not require direct PLANCK remote physical-grab/ragdoll replay unless a
  stable public API or a separately justified native RE design exists. Canonical
  damage and ragdoll/pose non-interference remain required.
- Do not demand FUS compatibility or every third-party DLL before isolated
  handoff proof.
- Do not redesign the launcher UI, packaging layout, or server deployment unless
  a source defect directly blocks the parity build.
- Do not treat runtime-unproven behavior as a source defect by itself; distinguish
  required runtime evidence from missing implementation.

## Required Output

1. Findings P0-P3 with exact file/line evidence and concrete fixes.
2. A literal desktop-parity matrix: complete, intentionally out of tracked
   scope, or source-missing.
3. A VR-equivalence/RE matrix: verified equivalent, acceptable replacement,
   runtime-only proof needed, or more RE needed.
4. A minimal pre-build fix list and a post-build/two-client test matrix.
5. No edits.
