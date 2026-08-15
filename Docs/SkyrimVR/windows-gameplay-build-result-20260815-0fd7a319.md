# Windows Gameplay Build Result (2026-08-15, `0fd7a319`)

## Result

Commit `0fd7a319a9f2b74588551e0c13dbcac462f91f0e` passed the
disposable WinBoat candidate build and the subsequent clean detached WinBoat
gameplay build. This is build, package, and static evidence only. Skyrim was
not launched, no package was installed, and no one-client or two-client runtime
claim is established by this result.

The build used project CommonLib commit
`e74c63b8dd9cebb84a3dc1386cfaf40059ec3d65`, published on
`stvr-exclusive-vr-6.1.1`. That commit merges alandtse `CommonLibVR` `ng`
6.3.1 lineage at `108836139ee612651f6c6c4dc4c41e673dcde623`.

## Verified Gates

- Disposable candidate revision `c41464e5a954b39de36137b8f4c396e2ec50bb7d`
  completed with `STVR_CANDIDATE_BUILD_SUCCESS=true`.
- Windows `TPTests` passed 3,996 assertions in 58 test cases in both the
  candidate and clean builds.
- The gameplay client, immersive launcher, CommonLib gameplay bridge, VRIK,
  HIGGS, PLANCK, tick bridges, and `TPProcess` compiled and linked.
- Eight Papyrus scripts compiled with Caprica.
- The 503-file gameplay package audit reported zero failures.
- The 65-entry build-evidence audit reported zero failed commands, zero
  warnings, and zero failures.
- The complete local source-readiness suite passed all 30 gates; focused Linux
  tests passed 3,816 assertions in 39 cases before the Windows candidate.
- The generated private Linux/Windows handoff passed its manifest audit for
  11,306 payload files and `unzip -t` reported no errors.

## Retained Artifacts

| Artifact | Size | SHA-256 |
| --- | ---: | --- |
| `SkyrimTogetherVR-gameplay-0fd7a319-20260815222459Z.zip` | 279,645,051 bytes | `b083a50848437e84ee6afe469ab0a1a139a836680f213a380dc90fd6527fac04` |
| `SkyrimTogetherVR-build-evidence-gameplay-20260815-223554Z.zip` | 164,993 bytes | `af625a57b4ba885e89820c2b9fba75339f42355e36713837575dc9e7d577cacf` |
| `SkyrimTogetherVR-local-agent-complete-handoff-0fd7a319-20260815222459Z.zip` | 1,023,590,268 bytes | `50dd710ef330ff600187806c3f67ccc60ccbc67ccc687ed87faee14a81d96bde` |

The handoff is a private local testing archive, not a redistribution asset. It
contains dry-run-by-default Linux and Windows installers, source and dependency
history, the exact gameplay package and evidence, test/server tooling,
documentation, and the locally authorized mod/test references required by its
manifest. Windows users should start with the root `.bat`; Linux users should
run the root Python installer first in its default validation-only mode and
pass `--install` only after reviewing the plan.

## Remaining Gates

1. Install the exact package on target Linux and Windows Skyrim VR 1.4.15
   systems and pass strict prerequisite/readiness checks.
2. Deploy the matching server revision and retain one-client lifecycle evidence
   for connect, load/new game, disconnect/reconnect, and clean shutdown.
3. Run the two-client gameplay and compatibility matrix. Quest synchronization
   and `SetCombatTarget` remain deliberately unsupported and must not be
   advertised as parity.

