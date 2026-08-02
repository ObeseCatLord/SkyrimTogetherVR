# Parity-Safety Stage: 2026-08-02

## Scope And Evidence

This is the authoritative status for the current unbuilt parity-safety source
stage. Baseline `82c7999a` has a clean WinBoat audited build. The changes after
that baseline are dirty source only: no candidate build, clean build, package
audit, deployment, or runtime test has been run for them.

No handoff artifact or handoff file update is authorized in this stage.

## Implemented Source Changes Pending Proof

- Mapping ABI is 20, raised from 19 for `CommandStatus::Degraded`.
- Tint-bearing appearance validates and applies proven race, sex, weight, name,
  headparts, morphs, hair, face texture, and body skin tone. It omits unsafe
  FaceGen texture-mask composition and returns `Degraded` rather than claiming
  full face-generation success.
- Only `Appearance` `CommitAppearance` treats `Degraded` as executed and
  accepted. Every other command treats it as rejection.
- The first mixed VR final-equipment legacy baseline is captured before the
  owner inventory mutation, is capability-gated, and is bound to session and
  entity generation. Pure legacy-equipment diff tests are present but unrun.
- Exact `ActorMediator` actions are deliberately disabled end to end: no
  `PerformAction` detour/capture, no advertised capability, and no remote
  replay. The public VR rows for upstream `ForceAction` helpers point to
  incompatible code or data. Generic animation graph/event fallback remains.
- Respawn fade uses safe Papyrus-equivalent/admission behavior only. No native
  fade call is guessed.

## Sol Disposition And Residual Risk

Sol review disposition: the above changes close the identified safety defects
by preferring degraded or fail-closed behavior to unverified VR ABI calls.
They are not build or runtime proof.

Residual risks:

- Mapping ABI 20, command-result classification, and the new equipment diff
  helper have not compiled or run.
- The mixed VR/legacy first-equipment fanout needs a two-client convergence
  test, including reconnect and generation rollover.
- Safe appearance must be checked on real remote actors for visual convergence
  and no FaceGen instability.
- Exact action remains unavailable until a VR-safe ABI, argument construction,
  lifetime, form translation, and capability contract are proven.
- Vivox, remote VRIK finger/calibration, and direct PLANCK physical replay
  remain blocked by absent, proprietary, or insufficient public APIs.

## Required Next Stage

1. Run the full WinBoat candidate build using the disposable candidate helper.
2. Commit and push only if that candidate passes.
3. Run a clean committed-revision build with `--skip-handoff`.
4. Deploy that exact client package and matching server revision.
5. Prove one Monado client connection.
6. Run the Windows/Linux two-client matrix, including appearance, mixed
   equipment, reconnect/generation rollover, and compatibility coverage.

No runtime checkbox is complete until the matching deployed revision passes its
documented runtime evidence gate.
