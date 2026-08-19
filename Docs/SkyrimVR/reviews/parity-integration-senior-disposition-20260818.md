# Skyrim Together VR parity integration senior-review disposition

Review brief: `parity-integration-senior-brief-20260818.md`

The Sol-max review returned **NO-GO for build** until its integration findings
have source evidence. This document records source evidence only, not a test,
build, runtime, or repaired-release claim. A disposition is not repaired until
the cited source change exists and the required later verification passes.

| Finding | Disposition |
| --- | --- |
| Diagnostics header namespace compile blocker | Adopted. Public declarations now fully qualify bridge types. |
| Native-parity guard affected avatar-sync | Adopted. The loss guard is gameplay-target-only and schedules a generation-bound deferred close. |
| Connection-only could not finish rehydration | Adopted. Rehydration has explicit connection-only, avatar-sync, and gameplay profiles. |
| VR identity and gameplay intent were conflated | Adopted. VR payload shape uses `IsVrClient`; admission/sentinel behavior uses `IsVrGameplayClient`; every VR client must negotiate NPC ownership support. |
| Script sinks alone allowed premature readiness | Adopted. Capability revision 33 adds mandatory aggregate `LocalCaptureSinks`, active only with script and current-player animation sinks. |
| Quest partial commits looked retryable | Adopted. Quest partial application is terminal `Degraded`, residual suppression is canceled, and actual post-state is published for reconciliation. |
| Protocol-15 DevBench/tooling drift | Adopted. DevBench derives the revision from `vr_handoff.GAMEPLAY_PROTOCOL_REVISION`; current docs require revision 15. |
| Lifecycle retirement could await a nonexistent callback | Adopted. Queued connects are token-bound; pre-authentication retirement completes without a disconnect callback, while authenticated sessions still close before reconnect. |
| Diagnostics conflated availability with proof | Adopted. Status separates operational readiness from local observed counters and explicitly denies single-snapshot two-client proof; failed atomic writes remain dirty for bounded retry. |
| Full gameplay evidence accepted only requested relay domains | Source evidence recorded. `--require-gameplay` now requires paired bidirectional evidence for every mandatory canonical gameplay domain; direct HIGGS/grab is an explicit optional extension, not a substitute for canonical coverage. Tests, build, and runtime verification remain pending. |
| Counter totals alone could imply paired proof | Source evidence recorded. The archive audit now requires a second trusted gameplay archive, matching source/package identity, distinct player/process/launch identities, and matching per-client session status before it evaluates counters. Tests, build, and runtime verification remain pending. |
| Readiness was at risk of becoming a global identity requirement | Source evidence recorded. Fresh structural gameplay identity remains required for live admission, while `ready=1` is enforced only for gameplay and gameplay-bootstrap profiles. Tests, build, and runtime verification remain pending. |
| HIGGS was negotiated from build flags | Adopted. Authentication requests HIGGS only from an operational interface/callback/snapshot state, and sending also checks negotiated capability. |

The canonical desktop authority decision remains unchanged: target-owner actor
values own melee damage, so no second attacker-authored scalar damage path was
added. PLANCK remote physical replay remains explicitly unsupported because the
public integration does not provide that apply API; this does not disable any
original desktop Skyrim Together behavior.

## Remaining verification

No tests or builds have run for the current source candidate. The next gate is
source inspection of the recorded changes, then the complete local
source/readiness suite, followed by a clean Windows client and matching server
build. Runtime credit requires one-client lifecycle testing and then trusted
paired two-client evidence for every mandatory canonical gameplay domain; any
optional direct VR extension needs its own explicitly requested evidence.

The Skyrim VR native quest-stage RVA and calling convention remain runtime
unverified. The first VR quest test must therefore begin with a disposable save
and inspect both bridge results and the reconciled quest state.
