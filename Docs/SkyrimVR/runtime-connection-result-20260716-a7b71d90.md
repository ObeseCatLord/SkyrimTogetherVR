# Runtime Connection Result: a7b71d90

Date: 2026-07-16

## Build And Environment

- Source: `a7b71d900a72c44e8e31436a245fe448b97d0daa`.
- Network version: `stvr-v0.1.0-alpha.1-54-ga7b71d90`.
- Client: WinBoat/MSVC gameplay package, 503 files, zero package-audit
  failures, zero build-evidence failures, and zero evidence warnings.
- Server: Linux ARM64 image `skyrim-together-vr-server:a7b71d90`, image ID
  `sha256:2d5757bb014826ed2cfc360ed036804f8edda1d9006a6cde8f09d6c4c6f8650f`.
- Runtime: Linux, GE-Proton 10-34, XRizer, Monado Qwerty HMD/controllers,
  Skyrim VR 1.4.15, Realm of Lorkhan, DevBench, VRIK, HIGGS, and PLANCK.
- Endpoint: `incidentalstoat.xyz:26099/udp`.

The strict installed-package readiness audit passed with zero warnings and zero
failures before launch. It included the exact gameplay package, SKSEVR, VR
Address Library, VRIK, HIGGS, PLANCK, address/prologue guards, CommonLib layout,
FUS native-DLL compatibility, package self-tests, and evidence self-tests.

## Smoke Test

The automated fresh New Game path passed:

1. Main Menu and Realm of Lorkhan confirmation opened normally.
2. RaceSex confirmation and XRizer naming completed through controller input.
3. The character finalized as `Shezarrine`; `SkyrimTogether.esp` was active.
4. Realm of Lorkhan loaded and its allowlisted introduction was accepted.
5. The client authenticated as player 1 and emitted a valid interior-cell
   request with no worldspace translation failure.
6. The server admitted `Skyrim VR Player` with 10 plugins and changed to one
   connected player. The server and client reported the same network version.

This proves fresh startup, character finalization, authentication, server
admission, and current-cell synchronization for one client. It does not prove
remote actors or any two-client gameplay domain.

## Findings

- Several native gameplay event subscriptions logged critical fail-closed
  `BSTEventSource::AddEventSink` rejections because their VR targets remain
  unvalidated. Affected producer lanes cannot be treated as working until each
  subscription is identified, mapped, and exercised.
- `qqq` returned Skyrim's WinMain and reached `endmain.done`; transport closed
  and the server logged the disconnect. During teardown, the client warned that
  the CommonLib gameplay session could not be retired.
- The outer Proton/immersive-launcher process remained alive for more than 30
  seconds after Skyrim and Skyrim Together teardown completed. It was terminated
  after the server disconnect. Clean process exit therefore remains failed.
- The final handoff status file remained `online=1` after teardown even though
  the server had disconnected the player. Lifecycle correctly changed to
  `teardown`; connection status cleanup needs validation.
- No crash or vectored-exception failure occurred in this run. GStreamer warned
  about optional host codecs, but startup, rendering, input automation, and the
  connection test continued.

## Remaining Acceptance

Run two clients for actors, movement, animation, appearance, equipment,
inventory, actor state, death/respawn, objects, combat, projectiles, magic,
quests, dialogue, party/world state, VRIK/FBT, HIGGS, PLANCK, reconnect,
save/load, controller navigation, and clean shutdown. The authoritative matrix
is `original-gameplay-parity-checklist.md`.
