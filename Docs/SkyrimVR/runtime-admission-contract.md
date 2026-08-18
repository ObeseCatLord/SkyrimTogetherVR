# Runtime Admission Contract

Skyrim Together VR runtime acceptance is an end-to-end state transition. A
running process, an `online=1` file, or a server log entry is not sufficient on
its own. Release tooling must prove every state below for the same game root,
process, launch, package, connection, and presentation.

## Admission States

1. `PACKAGE_VERIFIED`
   - The installed build manifest and required artifacts pass their hash,
     source-revision, target, and package-flavor checks.
   - The Linux launcher structurally admits only a clean Windows x64 package
     whose `networkVersion` equals its `buildVersion`; a custom launcher path
     does not bypass this package gate.
   - The selected OpenVR runtime passes ELF, dependency, symbol, provenance,
     and compatibility checks.
2. `PROFILE_VERIFIED`
   - All required handoff plugins exist.
   - Skyrim's engine-reported active plugin order matches the isolated release
     profile. A file merely existing under `Data` is not activation proof.
3. `XR_READY`
   - The selected Monado socket has a live `monado-service` listener.
   - An OpenXR canary can create an instance, obtain the HMD system, and
     enumerate at least one view configuration.
4. `LAUNCH_BOUND`
   - One exclusive writer owns the canonical selected game root. Alternate
     executable paths do not create a separate ownership identity.
   - Required readouts agree on the exact 128-bit `launchNonce`, process ID,
     and game root for this invocation.
   - Readouts and required log records are newer than the invocation start.
5. `FINALIZED`
   - Character creation completed through Skyrim's normal confirm/name path.
   - Player, base, cell, lifecycle epoch, and task owner remain stable.
   - Main, RaceSex, loading, Fader, and modal message-box states are absent.
6. `CONNECTED_BOUND`
   - The status session matches this launch and the connection generation
     advanced after the submitted command.
   - Client and authenticated server build versions match.
   - Gameplay protocol revision, server instance nonce, client session nonce,
     connection generation, and capability negotiation are valid and nonzero.
7. `USABLE_STABLE`
   - Current-cell synchronization and local-avatar assignment complete.
   - Two later task checkpoints retain the same identities without bridge
     rejection or ring drops.
   - The HUD remains present for at least one second of repeated probes while
     Fader, loading, RaceSex, main-menu, and modal states remain absent. Any
     transition or HUD disappearance restarts the interval.
8. `EVIDENCE_SEALED`
   - The evidence archive contains all identities and required readouts from
     the same admitted run.
   - Mixed nonces, processes, roots, builds, protocols, stale files, partial
     readouts, or failed admission states make the archive ineligible for
     runtime-acceptance claims.

## Failure Classification

Diagnostics must report the first failed state and preserve the lower-level
cause. In particular:

- `XR_READY` failures are launch/runtime failures, not networking failures.
- RaceSex or plugin-profile failures are game-readiness failures, not server
  failures.
- A rejected build/protocol identity is a deployment mismatch, not a timeout.
- `CONNECTED_BOUND` without `USABLE_STABLE` proves transport bootstrap only;
  it does not prove a usable headset presentation.
- Generic crash-evidence collection may preserve stale files, but it must mark
  them as historical and must not promote them to live acceptance.

## Release Rule

The canonical automated run must reach `EVIDENCE_SEALED` from a fresh process
using the exact packaged client and its matching server. CI unit and fixture
tests cover individual transitions, but they do not replace the target-machine
Proton/Monado run or the later two-client gameplay matrix.
