# VR Magic Effect Relay

The current VR magic work has a dedicated connection-only telemetry stream. It reports player-centric magic-effect application events by authenticated server player id without enabling `MagicService`, spell-cast hooks, magic-target hooks, remote spell casting, or remote effect application.

## Captured State

`VRMagicService` listens to Bethesda's `TESMagicEffectApplyEvent` dispatcher. It captures an event only when the local `PlayerCharacter` is the caster or target. This keeps the stream player-centric and avoids relaying every ambient NPC or world magic effect in the loaded area.

The shared `VRMagicEffectEvent` payload contains:

- a local monotonically increasing sequence number
- magic effect id
- caster id, if available
- target id, if available
- caster position
- target position
- caster form type
- target form type
- whether the local player is the caster
- whether the local player is the target

The event is captured only when the game emits `TESMagicEffectApplyEvent`. The service does not install `HookSpellCast`, `HookInterruptCast`, or `HookAddTarget`.

## Runtime Service

When connected, `VRMagicService` sends `RequestVRMagicEffectEvent` for each captured local player-centric magic-effect event. The service also subscribes to `NotifyVRMagicEffectEvent`, ignores loopback updates for the local player id, and stores the latest remote magic-effect event by server player id.

Remote magic state is cleared when the client disconnects. Individual remote entries are removed on `NotifyPlayerLeft`, and stale entries are pruned after ten seconds without an update.

The service does not:

- instantiate `MagicService`
- send `SpellCastRequest`
- process `NotifySpellCast`
- send or process interrupt-cast packets
- send or process add-target packets
- send or process remove-spell packets
- require spawned character entities
- look up remote actor entities
- call `MagicCaster`
- call `MagicTarget`
- call `CastSpellImmediate`
- call `AddTarget`
- mutate spells, active effects, actor state, inventory, physics, or HIGGS state

## Handoff File

The latest local and remote magic-effect snapshots are written to:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.magic
```

The fields include:

- `online`
- `localPlayerId`
- `localMagicEffectAvailable`
- `remoteMagicEffectCount`
- `localMagicEffect.sequence`
- `localMagicEffect.effect.serverModId`
- `localMagicEffect.effect.serverBaseId`
- `localMagicEffect.caster.serverModId`
- `localMagicEffect.caster.serverBaseId`
- `localMagicEffect.target.serverModId`
- `localMagicEffect.target.serverBaseId`
- `localMagicEffect.casterPosition`
- `localMagicEffect.targetPosition`
- `localMagicEffect.casterFormType`
- `localMagicEffect.targetFormType`
- `localMagicEffect.casterIsPlayer`
- `localMagicEffect.targetIsPlayer`
- `remoteMagicEffect.<playerId>.ageSeconds`
- `remoteMagicEffect.<playerId>.effect.serverModId`
- `remoteMagicEffect.<playerId>.caster.serverModId`
- `remoteMagicEffect.<playerId>.target.serverModId`
- `remoteMagicEffect.<playerId>.casterIsPlayer`
- `remoteMagicEffect.<playerId>.targetIsPlayer`

This file is a validation and handoff surface only. It is intended for launcher, external overlay, or future debug/proxy visual consumers.

## Server Relay

`VRMagicRelayService` subscribes to `PacketEvent<RequestVRMagicEffectEvent>`, copies the payload into `NotifyVRMagicEffectEvent`, stamps `PlayerId` from `Player::GetId()`, and broadcasts to all other connected players. It drops sequence `0`, packets without an effect id plus player/caster/target context, non-increasing sequence numbers, and per-player magic-effect bursts faster than the staged event-lane cap. Per-player relay state is cleared on player leave.

The relay intentionally does not require a character entity. This is the key difference from the normal magic sync path, where requests contain entity ids and remote application uses actor lookup plus magic mutation.

## HIGGS Boundary

This stage is compatible with the current HIGGS policy because it observes game-emitted magic-effect events and does not force spell casts, hand state, collision state, grabbed object state, or physics state. Spell origin and aiming transforms remain covered by the VR pose relay.

## Next Steps

- Validate the local and remote fields in-game with two VR clients connected to a server.
- Confirm `SkyrimTogetherVR.magic` updates for self-cast effects, outgoing spell effects, and incoming hostile or healing effects.
- Add an in-game debug/proxy visual consumer for recent remote magic effects before applying anything to real actors.
- Keep normal spell casting and magic-effect application disabled until actor/entity lifecycle, caster identity, target identity, and VR magic layouts are validated.
