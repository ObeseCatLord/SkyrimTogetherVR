# Skyrim Together VR parity integration senior-review brief

## Goal and scope

Review the unbuilt Skyrim Together VR source candidate after the desktop-parity
implementation stage. The target is a distributable Skyrim VR port that retains
the original client's canonical gameplay authority while using a CommonLibSSE-NG
SKSEVR bridge for game access. This is a solo-maintained public alpha: require
fail-closed ABI and protocol behavior, but do not prescribe enterprise ceremony.

The reviewer is read-only. Do not build, run tests, launch Skyrim, edit files, or
re-review untouched PE loader/startup hardening. Verify claims in source before
critiquing them.

## Environment facts

| Fact | Evidence status |
| --- | --- |
| Repository is `SkyrimTogetherVR`, branch `main`, outer base `ac8ce1634331ce4b95710c0e0e503ea99fde080f` | `[verified: git rev-parse]` |
| Worktree contains the complete unbuilt parity candidate; no test or build has run for it | `[verified: session command history and git status]` |
| CommonLib submodule merged alandtse `ng` 6.3.4 (`7bdcb9efe`) into local branch at `586eac1f4`; quest API edits are uncommitted inside the submodule | `[verified: submodule log/status/diff]` |
| Gameplay wire revision is 15 and native bridge capability revision is 33 | `[verified: GameplayCapabilities.h and VRGameplayBridge.h]` |
| Matching fork server is required; stock Tilted Evolution protocol compatibility is not claimed | `[verified: exact protocol check in GameServer.cpp]` |
| Existing runtime evidence proves one older client connected/bootstrap-completed, not this candidate or two-client gameplay | `[verified: checked-in runtime report; candidate intentionally unbuilt]` |
| Optional HIGGS is a separate physical interaction extension; PLANCK has no public remote physical replay API | `[verified: bridge sources and prior API review]` |

## Implemented candidate, separated from runtime claims

1. `[verified: source inspection]` VR identity, gameplay intent, and native parity
   admission are separate protocol bits. Gameplay admission requires core,
   `VrClient`, `VrGameplayClient`, and `NativeGameplayParity`.
2. `[verified: source inspection]` gameplay target requests native parity only
   while the CommonLib bridge reports every mandatory capability active. Loss of
   that contract closes an authenticated gameplay transport once per transition.
3. `[verified: source inspection]` only direct relays with complete apply behavior
   are advertised (`VRPose`, `VRHiggs`, `VRAppearance`). Canonical desktop services
   own the remaining gameplay domains.
4. `[verified: source inspection]` native quest mutation is synchronous:
   `SetStage`, then `SetActive(true)` for start/stage behavior; stop clears active
   then calls `Stop`. Capture suppression is bounded and canceled on rejection.
5. `[verified: official address-map lookup recorded in source]` the VR quest-stage
   RVA is `0x03803D0` for Skyrim VR 1.4.15. The new CommonLib wrapper has not yet
   been compile- or runtime-verified.
6. `[verified: source inspection]` save/load and reconnect use a finite state
   machine with deadlines and retire the old session before reconnecting.
7. `[verified: source inspection]` diagnostics write one atomic
   `SkyrimTogetherVR.gameplay` snapshot. It uses connection rehydration readiness,
   bridge capability readiness, and per-domain counters; it labels those counters
   local-only and requires correlated paired peer/session evidence before they
   can support two-client proof. It logs transitions and only rate-limited
   error/drop summaries.
8. `[verified: source inspection]` HIGGS callbacks are the only physical mutation
   producer. A bounded 25 ms exact-key deduper protects callback duplication;
   consumed/stashed behavior remains on canonical inventory replication.
9. `[verified: desktop source inspection]` melee damage remains target-owner
   actor-value authoritative. The candidate removed a VR-only direct
   attacker-provided scalar damage mutation instead of advertising it as parity.
10. `[unverified]` all changes compile, satisfy source/readiness audits, admit the
    intended client variants, survive save/load, and exchange gameplay between
    two VR clients.

## Current decisions and rejected alternatives

| Decision | Current lean | Rejected alternative |
| --- | --- | --- |
| VR admission | Require explicit gameplay intent plus active native parity contract | Inferring VR from optional relay bits allowed partial clients to look gameplay-ready |
| Canonical vs direct paths | Preserve desktop authority; direct packets only for genuinely VR-only data | Reimplementing every desktop service as a second VR packet path creates duplicate authority |
| Quest mutation | Synchronous native call with postconditions and capture suppression | Async Papyrus acknowledged dispatch before completion and could half-commit |
| Melee damage | Target-owner actor-value replication, matching desktop behavior | Direct attacker scalar damage was noncanonical and duplication-prone |
| Rehydration | Finite staged state with deadlines and terminal failure | Indefinite raw-socket `online` made stale sessions appear ready |
| Readiness | One aggregate atomic status is authoritative; legacy observers are supplemental | Static compatibility or source-token audits cannot prove a live session |
| HIGGS | One callback producer plus bounded dedupe | Generic grab plus HIGGS callbacks emitted duplicate gestures |
| PLANCK | Report unsupported remote physical replay until an API/implementation exists | Advertising telemetry-only behavior as gameplay parity is false |

## Required review questions

Prioritize correctness and likely compile/runtime blockers. Report findings with
file and line evidence.

1. Are protocol identity/admission helpers correct for gameplay, avatar-sync,
   connection-only, observers, NPC ownership, and assignment rejection? Audit
   whether server `IsVrGameplayClient` call sites should instead use `IsVrClient`.
2. Can bridge mandatory readiness become true before local event sinks or other
   capture/apply components are actually usable? Is loss-of-readiness handling
   safe and non-reentrant?
3. Is the quest ABI, mutation ordering, suppression lifetime, idempotency, and
   postcondition behavior fail-closed? Identify half-commit cases that require a
   different design.
4. Is the rehydration state machine legal under actual dispatcher ordering?
   Check disconnect errors, lifecycle changes during connect/auth/bootstrap,
   assignment rejection, deadline progression, retained command behavior, and
   whether `Ready` can be reached early or become stuck.
5. Is aggregate diagnostics truthful and compile-safe? Check domain indexing,
   readiness reasons, session reset/counter semantics, movement/save-load
   counting, event result unions, atomic write path, and log rate limiting.
6. Is HIGGS now exactly-once enough given its public callback API? Check callback
   type mapping, dedupe false positives, optional capability negotiation, and
   inventory interaction overlap.
7. Does removing direct melee mutation preserve complete desktop combat
   authority? Identify any actual desktop consumer/producer omitted by the VR
   canonical path, but do not demand a second damage authority.
8. Find likely C++ API/signature/include/build-target errors introduced by the
   integration, including whether new tests are included automatically.
9. Find stale revision/capability/readiness expectations in tests, tools, CI, or
   current authority docs. Historical reports may remain historical.
10. Identify any implemented gameplay domain still silently disabled or falsely
    advertised. Separate core desktop parity from optional VR embodiment
    extensions.

## Output contract

Return at most 1,500 words:

- Findings first, ordered P0-P3, each with concrete file/line evidence and a
  specific repair.
- A compact disposition recommendation for each current decision.
- Explicit compile blockers versus runtime-only risks.
- Missing behavior tests that should run after code is complete.
- State clearly if a question cannot be verified from source.

Do not edit files, run builds/tests, launch the game, deploy, commit, push, or
broaden into untouched loader/startup code.
