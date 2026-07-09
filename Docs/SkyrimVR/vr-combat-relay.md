# VR Combat Hit Relay

The current VR combat work has a dedicated connection-only telemetry stream. It reports player-centric hit events by authenticated server player id without enabling `CombatService`, projectile launch hooks, remote combat target mutation, spawned actor lookup, or normal projectile replay.

## Captured State

`VRCombatService` listens to Bethesda's `TESHitEvent` dispatcher. It captures an event only when the local `PlayerCharacter` is the hitter or the hittee. This keeps the stream player-centric and avoids relaying every ambient NPC hit in the loaded area.

The shared `VRCombatHitEvent` payload contains:

- a local monotonically increasing sequence number
- hitter id, if available
- hittee id, if available
- source form id, if available
- projectile form id, if available
- hitter position
- hittee position
- raw 32-bit hit flag word reconstructed from the CommonLib-shaped flag byte and padding at offset `0x18`
- whether the hit matches PLANCK's documented hit-event magic
- hitter form type
- hittee form type
- whether the local player is the hitter
- whether the local player is the hittee

The event is captured only when the game emits `TESHitEvent`. The service does not install projectile hooks, launch hooks, or the normal Skyrim Together combat sync path.

## Runtime Service

When connected, `VRCombatService` sends `RequestVRCombatHitEvent` for each captured local player-centric hit event. The service also subscribes to `NotifyVRCombatHitEvent`, ignores loopback updates for the local player id, and stores the latest remote combat hit by server player id.

Remote combat state is cleared when the client disconnects. Individual remote entries are removed on `NotifyPlayerLeft`, and stale entries are pruned after ten seconds without an update.

The service does not:

- instantiate `CombatService`
- send `ProjectileLaunchRequest`
- process `NotifyProjectileLaunch`
- replay projectiles with `Projectile::Launch`
- require spawned character entities
- look up remote actor entities
- call `SetCombatTarget` or `SetCombatTargetEx`
- run normal target update logic
- install `HookLaunch`
- mutate actor state, combat targets, weapon state, inventory, physics, or HIGGS state
- call PLANCK APIs, read `GetLastHitData()`, or replay active-ragdoll hits

## Handoff File

The latest local and remote combat hit snapshots are written to:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.combat
```

The fields include:

- `online`
- `localPlayerId`
- `localCombatHitAvailable`
- `remoteCombatHitCount`
- `localCombatHit.sequence`
- `localCombatHit.hitter.serverModId`
- `localCombatHit.hitter.serverBaseId`
- `localCombatHit.hittee.serverModId`
- `localCombatHit.hittee.serverBaseId`
- `localCombatHit.source.serverModId`
- `localCombatHit.source.serverBaseId`
- `localCombatHit.projectile.serverModId`
- `localCombatHit.projectile.serverBaseId`
- `localCombatHit.hitterPosition`
- `localCombatHit.hitteePosition`
- `localCombatHit.rawHitFlags`
- `localCombatHit.planckHit`
- `localCombatHit.hitterFormType`
- `localCombatHit.hitteeFormType`
- `localCombatHit.hitterIsPlayer`
- `localCombatHit.hitteeIsPlayer`
- `remoteCombatHit.<playerId>.ageSeconds`
- `remoteCombatHit.<playerId>.hitter.serverModId`
- `remoteCombatHit.<playerId>.hittee.serverModId`
- `remoteCombatHit.<playerId>.source.serverModId`
- `remoteCombatHit.<playerId>.projectile.serverModId`
- `remoteCombatHit.<playerId>.rawHitFlags`
- `remoteCombatHit.<playerId>.planckHit`
- `remoteCombatHit.<playerId>.hitterIsPlayer`
- `remoteCombatHit.<playerId>.hitteeIsPlayer`

This file is a validation and handoff surface only. It is intended for launcher, external overlay, or future debug/proxy visual consumers.

## Server Relay

`VRCombatRelayService` subscribes to `PacketEvent<RequestVRCombatHitEvent>`, copies the payload into `NotifyVRCombatHitEvent`, stamps `PlayerId` from `Player::GetId()`, and broadcasts to all other connected players. It drops sequence `0`, packets that do not include a player-side hit with a hitter or hittee id, non-increasing sequence numbers, and per-player combat-hit bursts faster than the staged event-lane cap. Per-player relay state is cleared on player leave.

The relay intentionally does not require a character entity. This is the key difference from the normal combat and projectile sync paths, where requests target spawned character entities and remote application depends on actor lookup plus projectile or combat mutation.

## HIGGS Boundary

This stage is compatible with the current HIGGS/PLANCK policy because it observes game-emitted hit events and does not force weapon collision, projectile creation, combat targets, grabbed object state, hand state, or physics state. PLANCK hit classification is limited to the documented 32-bit `TESHitEvent` magic word reconstructed from the existing hit-event flags offset; it does not request `IPlanckInterface001`, does not poll `GetLastHitData()`, and does not mutate PLANCK settings or actor ignore lists. Weapon, hand, and spell transforms remain covered by the VR pose relay.

## Next Steps

- Validate the local and remote fields in-game with two VR clients connected to a server.
- Confirm `SkyrimTogetherVR.combat` updates when the local player hits an actor and when the local player is hit.
- With PLANCK installed, confirm `localCombatHit.planckHit` or a `remoteCombatHit.<playerId>.planckHit` row appears for PLANCK-provided physical hits before designing any extended hit-data relay.
- Add an in-game debug/proxy visual consumer for recent remote combat hits before applying anything to real actors.
- Keep normal projectile replay and combat target mutation disabled until actor/entity lifecycle, projectile layout, weapon collision, and HIGGS interactions are validated.
