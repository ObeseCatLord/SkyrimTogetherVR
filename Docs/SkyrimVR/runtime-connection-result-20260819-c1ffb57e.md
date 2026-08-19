# Calibration-Gated Connection Result (`c1ffb57e`)

## Result

On 2026-08-19, the exact audited Windows gameplay package built from
`c1ffb57e0747c37f00af48ceba9a4a8880976b8a` was installed into the isolated
handoff-only Linux test tree and launched through GE-Proton 10-34, XRizer, and
Monado. The matching ARM64 Foundry server was running at
`incidentalstoat.xyz:26099`.

While Skyrim VR was in `CalibrationOptionMenu` and cell `VRPlayroom01`, the
client correctly reported `state=suspended`, `ready=0`, and
`reason=calibration_option_menu`. Transport remained offline and the server had
zero players. This proves multiplayer startup no longer races Skyrim VR's
startup calibration, which owns recentering and the black `Fader Menu`.

Loading the settled Sleeping Giant Inn save removed the calibration blocker.
The lifecycle became ready, autoconnect authenticated on the first attempt, and
the stale HUD/Fader-only stack was recovered automatically. The bridge logged a
generation-2 recovery candidate, issued one Fader hide after the safety delay,
and verified it closed. DevBench then reported only `HUD Menu` open.

The no-launch evidence collector captured the still-live process with the
`--gameplay-bootstrap` profile. The archive
`SkyrimTogetherVR-runtime-evidence-gameplay-bootstrap-c1ffb57e.zip` passed its
independent offline audit with 19 required checks, zero failures, and zero
warnings.

## Correlated Identity

- network version: `stvr-v0.1.0-alpha.1-94-gc1ffb57e`
- gameplay protocol revision: `15`
- requested/negotiated capabilities: `0x7f80e0f`
- player ID: `1`
- client session nonce: `220696837027532`
- server instance nonce: `9726195123562425886`
- connection attempt/generation: `1/1`
- lifecycle epoch: `2`
- lifecycle state: `ready`
- rehydration state/profile: `ready/gameplay`

Foundry recorded the same player, protocol, capability mask, session nonce,
server-instance nonce, attempt, and generation. The client and server versions
matched exactly.

## Scope

This run proves exact-build launch, calibration admission, deferred
autoconnect, authentication, save-load lifecycle recovery, current-cell
synchronization, and conservative stale-Fader recovery. It also disproves
network movement as the cause of the startup fling: no multiplayer transport
was online while the calibration room was recentering the player.

The run remains one-client bootstrap evidence. It does not prove remote
application semantics or two-client gameplay parity. Audible NPC dialogue also
still requires headset confirmation even though the exact dialogue hook and
trampoline installed successfully.

## Server Deployment

- image: `skyrim-together-vr-server:c1ffb57e-arm64`
- image ID: `sha256:5f390a1372a6b0fdb007cd14a023c7bd8b797521ab4c1d3f60489988b045d733`
- executable SHA-256: `d226c6beeab6f6714f8b2280e290e00f7e801f455fd53d9f00db2ac654a52bb0`
- core SHA-256: `7942cea1a86cbf464c8d58618a0869729111d99d8a8c3488ff533a223f8418e7`
- container count/restarts: `1/0`
- transport: UDP 26099, host networking

The deployment preserves the existing persistent config, data, and log mounts.
