# VR Equipment Relay

The current VR equipment work has a dedicated connection-only network stream. It replicates a compact equipped-form snapshot by authenticated server player id without requiring `CharacterService`, remote actor spawn, normal inventory sync, or `EquipManager`.

## Captured State

`VRInventoryService` polls the local `PlayerCharacter` once per update and converts equipped forms through `ModSystem::GetServerModId()` into `GameId` values.

The shared `VREquipmentUpdate` payload contains:

- a local monotonically increasing sequence number
- weapon drawn state
- weapon fully drawn state
- left and right equipped weapon ids
- equipped ammo id
- left and right equipped spell ids
- equipped shout or power id

The sequence only advances when the captured equipment snapshot changes.

## Runtime Service

When connected, `VRInventoryService` sends `RequestVREquipmentUpdate` at 1 Hz if a local equipment snapshot is available. The service also subscribes to `NotifyVREquipmentUpdate`, ignores loopback updates for the local player id, ignores duplicate or out-of-order sequence numbers, and stores remote snapshots by server player id.

Remote equipment state is cleared when the client disconnects. Individual remote entries are removed on `NotifyPlayerLeft`.

In the explicit `SkyrimTogetherVRClientAvatarSync` build and the explicit `SkyrimTogetherVRGameplayClient` build, `CharacterService` also reads the `VRInventoryService` remote map and stores matching snapshots on remote player entities as `RemoteVREquipmentComponent`. The first in-engine consumer is deliberately narrow: when a new remote equipment sequence changes the drawn/sheath state, `CharacterService` queues the existing delayed remote-actor weapon draw helper for the matched actor. It does not equip forms, alter inventory, or call normal equipment sync. The same pass writes equipment match/upsert/stale counts, `equipmentWeaponDrawQueuedCount`, missing form/actor counters, and latest equipment sequence/drawn flags into `SkyrimTogetherVR.avatar`, so the companion panel and Papyrus telemetry can confirm the remote avatar cache has pose and held-item context together.

The service does not:

- install equip hooks
- listen for inventory change hooks
- send `RequestEquipmentChanges`
- process `NotifyEquipmentChanges`
- call `EquipManager`
- mutate inventory or actor equipment

## Handoff File

The latest local and remote snapshots are written to:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.inventory
```

The relay-specific fields include:

- `policy=equipment_snapshot_only`
- `fullInventoryTraversal=0`
- `inventoryMutation=0`
- `remoteEquipmentMutation=0`
- `normalInventoryPackets=0`
- `localEquipmentAvailable`
- `remoteEquipmentCount`
- `localEquipment.sequence`
- `localEquipment.weaponDrawn`
- `localEquipment.weaponFullyDrawn`
- `localEquipment.leftWeapon.serverModId`
- `localEquipment.leftWeapon.serverBaseId`
- `remoteEquipment.N.playerId`
- `remoteEquipment.N.sequence`
- `remoteEquipment.N.leftWeapon.serverModId`
- `remoteEquipment.N.leftWeapon.serverBaseId`

This file is a validation and handoff surface only. It is intended for launcher, external overlay, or future debug/proxy visual consumers. The policy fields are intentionally explicit: the equipment relay is not full inventory sync, and normal inventory packets/equipment mutation must remain disabled until separately validated.

## Server Relay

`VREquipmentRelayService` subscribes to `PacketEvent<RequestVREquipmentUpdate>`, copies the payload into `NotifyVREquipmentUpdate`, stamps `PlayerId` from `Player::GetId()`, and broadcasts to all other connected players. It drops sequence `0`, non-increasing sequence numbers, and per-player packets that arrive faster than the intended 1 Hz equipment lane. Per-player relay state is cleared on player leave.

The relay intentionally does not require a character entity. This is the key difference from the normal equipment sync path, where requests target spawned character entities and remote application depends on actor lookup plus equip mutation.

## Next Steps

- Validate the local and remote fields in-game with two VR clients connected to a server.
- Validate the staged remote weapon draw/sheath cue before applying equipped forms to real actors.
- Keep HIGGS-held object, grab, drop, collision, and physics callbacks behind a separate HIGGS bridge.
