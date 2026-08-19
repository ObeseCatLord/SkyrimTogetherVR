# Exact Gameplay-Package Connection Result (`b8305b3b`)

## Result

On 2026-08-18, the exact audited Windows gameplay package built from
`b8305b3bb87c35d404d18fcd1b88d084880fad82` was installed into the isolated
handoff-only Linux test tree and launched through GE-Proton 10-34, XRizer, and
Monado. It connected to the matching ARM64 Foundry server at
`incidentalstoat.xyz:26099` on the first attempt.

The no-launch evidence collector captured the still-live process with the
`--gameplay-bootstrap` profile. The resulting archive is
`SkyrimTogetherVR-evidence-20260819-011635Z.zip`. Its independent offline audit
passed 19 required checks, failed zero checks, and emitted zero warnings.

## Correlated Identity

- network version: `stvr-v0.1.0-alpha.1-91-gb8305b3b`
- gameplay protocol revision: `15`
- requested/negotiated capabilities: `0x7f80e0f`
- player ID: `1`
- client session nonce: `210159333889828`
- server instance nonce: `9726216499650066432`
- connection attempt/generation: `1/1`
- lifecycle epoch: `1`
- lifecycle state: `ready`
- rehydration state/profile: `ready/gameplay`

Foundry recorded the same protocol, capability mask, player, session,
server-instance nonce, attempt, and generation. The client gameplay snapshot
reported the CommonLib bridge initialized and ready, aggregate local event and
capture sinks active, the native parity contract active, zero rejected or stale
commands, and zero event- or command-ring drops.

## Runtime Scope

All mandatory canonical domains reported `availability=active` and
`state=active`: animation, appearance, equipment, inventory, actor state,
objects, combat, projectiles, magic, quests, dialogue, party, world state,
VR body pose, NPC ownership, movement, and save/load lifecycle rehydration.
VRIK and HIGGS interfaces were live, Index controllers were detected through
XRizer, and PLANCK 0.8.0 reported a valid revision-1 interface.

This proves exact-build startup, native bridge admission, connection,
authentication, local assignment, current-cell synchronization, and one-client
operational readiness. It does not prove remote application semantics. The
strict paired `--gameplay` gate remains open until two clients deliberately
exercise both directions of every advertised domain, including reconnect,
save/load, and server restart. PLANCK remote physical replay remains explicitly
unsupported because PLANCK's public interface does not expose a stable replay
API; coexistence and canonical combat deduplication are the supported scope.

## Server Deployment

- image: `skyrim-together-vr-server:b8305b3b-arm64`
- image ID: `sha256:ed4391b577f9e0428fe46ae98a71f169ff9352187d8b530762e8eb46e11133cd`
- executable SHA-256: `daecb901001cc61063d9920e43b0f87dd8ccc2154399925f512d139704c35162`
- core SHA-256: `3b64ea8c4905e6c0bc64159f65a342a74a8696a304bd5ffe148274c2f7f10c55`
- container count/restarts: `1/0`
- transport: UDP 26099, host networking

The deployment preserves the existing persistent config, data, and log mounts.
