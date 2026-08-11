# Literal-Parity Source Stage: 2026-08-11

## Scope And Evidence

This is the authoritative status for the current unbuilt literal-parity source
stage. Baseline `82c7999a` has a clean WinBoat audited build. The changes after
that baseline are dirty source only: no candidate build, clean build, package
audit, deployment, or runtime test has been run for them.

No handoff artifact or handoff file update is authorized in this stage.

## Implemented Source Changes Pending Proof

- Mapping ABI is 22. ABI 20 introduced explicit degraded-result semantics;
  later assignment/bootstrap contracts raised it again so mixed bridge builds
  fail closed instead of decoding changed fixed records.
- alandtse CommonLibVR `ng` is current at upstream 6.1.1 commit `5ae93ea`,
  with only the project's VR-only source-build commit `612394bda` on top.
- Tint-bearing appearance validates and applies race, sex, weight, name,
  headparts, morphs, hair, face texture, body skin tone, and full FaceGen
  tint-mask composition through Skyrim VR 1.4.15 targets verified against the
  official SKSEVR 2.0.12 implementation.
- The first mixed VR final-equipment legacy baseline is captured before the
  owner inventory mutation, is capability-gated, and is bound to session and
  entity generation. Pure legacy-equipment diff tests are present but unrun.
- Exact `ActorMediator` action capture and ForceAction-equivalent replay use a
  complete verified Skyrim VR target set and advertise capability only after all
  target, prologue, call-sequence, singleton, context, and vtable checks pass.
- Respawn uses a verified native VR fade target and preserves/restores
  essential and no-bleedout state. The exact decrypted VR 1.4.15 respawn calls
  are `DispelAllSpells` RVA `0x0557070` and `GetCOCPlacementInfo` RVA
  `0x027A4C0`; curated overrides replace the disproven generated rows.
- Gameplay protocol revision 14 includes body format 3: pelvis/legs, spine, neck,
  clavicles, arms, and the sparse 30 finger-joint quaternion extension. Capture
  occurs after VRIK and HIGGS, converts SkyrimVR-FBT world-only spine output to
  effective parent-local rotations, and applies remote body nodes parent-first
  before resolving head/hand world endpoints.
- Format-2/3 body/joint sequence and root generation must agree. Pose delivery
  keeps one admitted and one replaceable pending frame per player, never replays
  admitted visual work, and reserves half the result-owner table plus 64 pending
  work slots for state-changing gameplay. A maximum format-3 frame is 112 bridge
  commands.
- PLANCK interface revision 1 acquisition occurs only at PostPostLoad. Handoff
  telemetry is refreshed through the VRIK/HIGGS game-thread callback with no
  unload-unsafe worker; canonical engine hit messages remain the only damage
  path.
- HIGGS mutation events use an ordered sequence independent from observation
  telemetry. A bounded 32-event replay window provides at-least-once transport;
  the server commits its ledger only after successful fanout and the receiver
  commits its tombstone only after bridge admission, suppressing retransmits.
- Discord and server-list background work is owned and joined. The server-list
  worker receives immutable snapshots and publishes rejection back to the owner
  thread rather than retaining World/GameServer references.
- Body/endpoint and sparse joint skeleton commits preserve original locals and
  roll back plus propagate the hierarchy on failed commit paths.
- Explicit transient FaceGen commit failures retry a complete appearance
  snapshot under a fresh bridge transaction sequence; permanent failures and
  ambiguous result timeouts remain terminal.

## Sol Disposition And Residual Risk

Sol review disposition: the above changes close the identified safety defects
by preferring degraded or fail-closed behavior to unverified VR ABI calls.
They are not build or runtime proof.

Residual risks:

- Mapping ABI 22, protocol revision 14, exact-action calls, FaceGen calls, native
  fade, PLANCK acquisition, joint-pose transport, and the equipment diff helper
  have not compiled or run.
- The mixed VR/legacy first-equipment fanout needs a two-client convergence
  test, including reconnect and generation rollover.
- Full appearance must be checked on real remote actors for visual convergence,
  renderer ownership, retry behavior, and no FaceGen instability.
- Exact actions need runtime proof for every supported action type and remote
  replay suppression.
- Pose queue behavior under high player count/bridge latency and quaternion
  orientation on real skeletons still need profiling and runtime evidence.
- The original branch's optional Vivox source/SDK is proprietary, gitignored,
  absent from both trees, and disabled in normal builds. VRIK calibration and
  direct PLANCK remote physics remain outside available public APIs; neither is
  substituted with unsafe local-player calls.

## Required Next Stage

1. Run and disposition a Sol max/xhigh review of original-branch parity, raw VR
   ABI, protocol, ownership, sequencing, lifecycle, concurrency, and crash risk.
2. Run the full WinBoat candidate build using the disposable candidate helper.
3. Commit and push only if that candidate passes.
4. Run a clean committed-revision build with `--skip-handoff`.
5. Deploy that exact client package and matching server revision.
6. Prove one Monado client connection.
7. Run the Windows/Linux two-client matrix, including appearance, mixed
   equipment, reconnect/generation rollover, and compatibility coverage.

No runtime checkbox is complete until the matching deployed revision passes its
documented runtime evidence gate.
