# VR Activation Relay

The current VR activation work has a dedicated connection-only telemetry stream. It reports local activation events by authenticated server player id without enabling `ObjectService`, object assignment, remote actor spawn, normal activation sync, lock sync, inventory sync, or remote `Activate()` replay.

## Captured State

`VRActivationService` listens to Bethesda's `TESActivateEvent` dispatcher and converts the activated reference, parent cell, and worldspace through `ModSystem::GetServerModId()` into `GameId` values.

The shared `VRActivationEvent` payload contains:

- a local monotonically increasing sequence number
- activated object id
- parent cell id
- worldspace id, if available
- object position
- object form type
- object open state

The event is captured only when the game emits `TESActivateEvent`. The service does not install the flat Skyrim Together `HookActivate` path.

## Runtime Service

When connected, `VRActivationService` sends `RequestVRActivationEvent` for each captured local activation event. The service also subscribes to `NotifyVRActivationEvent`, ignores loopback updates for the local player id, and stores the latest remote activation by server player id.

Remote activation state is cleared when the client disconnects. Individual remote entries are removed on `NotifyPlayerLeft`, and stale entries are pruned after ten seconds without an update.

The service does not:

- instantiate `ObjectService`
- send `ActivateRequest`
- process `NotifyActivate`
- send or process object assignment packets
- send or process lock-change packets
- require spawned character entities
- look up remote actor or object entities
- call `TESObjectREFR::Activate`
- call `LockChange`
- mutate object state, inventory, physics, or actor state

## Handoff File

The latest local and remote activation snapshots are written to:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.activation
```

The fields include:

- `online`
- `localPlayerId`
- `localActivationAvailable`
- `remoteActivationCount`
- `localActivation.sequence`
- `localActivation.object.serverModId`
- `localActivation.object.serverBaseId`
- `localActivation.cell.serverModId`
- `localActivation.cell.serverBaseId`
- `localActivation.worldSpace.serverModId`
- `localActivation.worldSpace.serverBaseId`
- `localActivation.position`
- `localActivation.formType`
- `localActivation.openState`
- `remoteActivation.<playerId>.ageSeconds`
- `remoteActivation.<playerId>.object.serverModId`
- `remoteActivation.<playerId>.position`
- `remoteActivation.<playerId>.formType`
- `remoteActivation.<playerId>.openState`

This file is a validation and handoff surface only. It is intended for launcher, external overlay, or future debug/proxy visual consumers.

## Server Relay

`VRActivationRelayService` subscribes to `PacketEvent<RequestVRActivationEvent>`, copies the payload into `NotifyVRActivationEvent`, stamps `PlayerId` from `Player::GetId()`, and broadcasts to all other connected players. It drops sequence `0`, packets without an activated object id, non-increasing sequence numbers, and per-player activation bursts faster than the staged event-lane cap. Per-player relay state is cleared on player leave.

The relay intentionally does not require a character entity or object entity. This is the key difference from the normal activation sync path, where requests contain an activator entity id and remote application calls `TESObjectREFR::Activate`.

## HIGGS Boundary

This stage is compatible with the current HIGGS policy because it observes activation events after the game emits them and does not force grabs, drops, pickup, collision, hand, or physics state. HIGGS-owned object pull/grab/drop/collision behavior remains behind a future dedicated HIGGS bridge.

## Next Steps

- Validate the local and remote fields in-game with two VR clients connected to a server.
- Confirm `SkyrimTogetherVR.activation` updates for doors, containers, harvestables, books, and HIGGS-grabbable objects without disrupting HIGGS behavior.
- Add an in-game debug/proxy visual consumer for recent remote activations before applying anything to real objects.
- Keep normal activation replay disabled until object/entity lifecycle, activator identity, HIGGS interactions, and VR object layouts are validated.
