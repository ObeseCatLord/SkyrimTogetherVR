# VR Projectile Intent Relay

The current VR projectile work has a dedicated connection-only telemetry stream. It reports player-centric bow-shot and spell-cast intent by authenticated server player id without enabling `CombatService`, `ProjectileLaunchedEvent`, `ProjectileLaunchRequest`, `NotifyProjectileLaunch`, `HookLaunch`, `Projectile::Launch`, remote projectile creation, or actor mutation.

## Captured State

`VRProjectileService` listens to Bethesda's `TESPlayerBowShotEvent` and `TESSpellCastEvent` dispatchers. Bow-shot events are already player-specific. Spell-cast events are captured only when the local `PlayerCharacter` is the casting object.

The shared `VRProjectileEvent` payload contains:

- a local monotonically increasing sequence number
- event kind: bow shot or spell cast
- source player id
- weapon id, for bow shots
- ammo id, for bow shots
- spell id, for spell casts
- VR origin position
- VR destination position
- origin and destination validity flags
- bow shot power
- sun-gazing flag from `TESPlayerBowShotEvent`

The origin and destination positions are taken from the existing VR pose nodes:

- bow shots: `ArrowOrigin` and `ArrowDestination`
- spell casts: `SpellOrigin` and `SpellDestination`

The service does not install projectile launch hooks and does not read or write `Projectile::LaunchData`.

## Runtime Service

When connected, `VRProjectileService` sends `RequestVRProjectileEvent` for each captured local player-centric projectile intent event. The service also subscribes to `NotifyVRProjectileEvent`, ignores loopback updates for the local player id, and stores the latest remote projectile event by server player id.

Remote projectile state is cleared when the client disconnects. Individual remote entries are removed on `NotifyPlayerLeft`, and stale entries are pruned after ten seconds without an update.

The service does not:

- instantiate `CombatService`
- send `ProjectileLaunchRequest`
- process `NotifyProjectileLaunch`
- subscribe to `ProjectileLaunchedEvent`
- install `HookLaunch`
- use `Projectile::LaunchData`
- call `Projectile::Launch`
- require spawned character entities
- look up remote actor entities
- mutate actor state, combat targets, weapon state, inventory, physics, or HIGGS state

## Handoff File

The latest local and remote projectile intent snapshots are written to:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.projectile
```

The fields include:

- `online`
- `localPlayerId`
- `localProjectileEventAvailable`
- `remoteProjectileEventCount`
- `localProjectileEvent.sequence`
- `localProjectileEvent.kind`
- `localProjectileEvent.source.serverModId`
- `localProjectileEvent.source.serverBaseId`
- `localProjectileEvent.weapon.serverModId`
- `localProjectileEvent.ammo.serverModId`
- `localProjectileEvent.spell.serverModId`
- `localProjectileEvent.originValid`
- `localProjectileEvent.destinationValid`
- `localProjectileEvent.origin`
- `localProjectileEvent.destination`
- `localProjectileEvent.power`
- `localProjectileEvent.isSunGazing`
- `remoteProjectileEvent.<playerId>.ageSeconds`
- `remoteProjectileEvent.<playerId>.kind`
- `remoteProjectileEvent.<playerId>.weapon.serverModId`
- `remoteProjectileEvent.<playerId>.ammo.serverModId`
- `remoteProjectileEvent.<playerId>.spell.serverModId`

This file is a validation and handoff surface only. It is intended for launcher, external overlay, or future debug/proxy visual consumers.

## Server Relay

`VRProjectileRelayService` subscribes to `PacketEvent<RequestVRProjectileEvent>`, copies the payload into `NotifyVRProjectileEvent`, stamps `PlayerId` from `Player::GetId()`, and broadcasts to all other connected players. It drops sequence `0`, packets without a source id plus projectile intent context, non-increasing sequence numbers, and per-player projectile-intent bursts faster than the staged event-lane cap. Per-player relay state is cleared on player leave.

The relay intentionally does not require a character entity. This is the key difference from the normal projectile sync path, where requests target spawned character entities and remote application depends on actor lookup plus projectile construction.

## HIGGS Boundary

This stage is compatible with the current HIGGS policy because it observes game-emitted bow/spell events and VR node transforms without forcing projectile creation, weapon collision, hand state, grabbed object state, or physics state.

## Next Steps

- Validate the local and remote fields in-game with two VR clients connected to a server.
- Confirm `SkyrimTogetherVR.projectile` updates for bow releases and projectile spell casts.
- Add an in-game debug/proxy visual consumer for recent remote projectile intent before creating any real projectiles.
- Keep normal projectile replay disabled until actor/entity lifecycle, projectile layout, weapon collision, and HIGGS interactions are validated.
