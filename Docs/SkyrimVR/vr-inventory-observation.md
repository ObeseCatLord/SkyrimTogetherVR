# VR Inventory Observation

The VR target now has a staged inventory/equipment observer behind `TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE=1`.

This is not the full `InventoryService`. It does not install equip hooks, listen for inventory change hooks, send normal inventory/equipment packets, apply remote inventory messages, or call `EquipManager`. It polls local player state that is already exposed through the current RE shims, writes a compact handoff file for validation, and optionally sends the same equipped-form snapshot through the VR-only player-id relay.

The VR-only relay uses `RequestVREquipmentUpdate` and `NotifyVREquipmentUpdate`. Those messages identify remote equipment by authenticated server player id, not by character actor entity id. This keeps the stage compatible with connection-only mode, where `CharacterService`, remote actor spawn, and `LocalComponent`/`RemoteComponent` ownership are still disabled.

## Handoff File

The service writes:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.inventory
```

Current fields:

```ini
ready=1
policy=equipment_snapshot_only
fullInventoryTraversal=0
inventoryMutation=0
remoteEquipmentMutation=0
normalInventoryPackets=0
online=0
localPlayerId=0
playerFormId=20
localEquipmentAvailable=1
remoteEquipmentCount=1
weaponDrawn=0
weaponFullyDrawn=0
leftWeapon.formId=0
leftWeapon.serverModId=0
leftWeapon.serverBaseId=0
rightWeapon.formId=0
rightWeapon.serverModId=0
rightWeapon.serverBaseId=0
ammo.formId=0
ammo.serverModId=0
ammo.serverBaseId=0
leftSpell.formId=0
leftSpell.serverModId=0
leftSpell.serverBaseId=0
rightSpell.formId=0
rightSpell.serverModId=0
rightSpell.serverBaseId=0
powerOrShout.formId=0
powerOrShout.serverModId=0
powerOrShout.serverBaseId=0
localEquipment.sequence=1
localEquipment.weaponDrawn=0
localEquipment.weaponFullyDrawn=0
localEquipment.leftWeapon.serverModId=0
localEquipment.leftWeapon.serverBaseId=0
localEquipment.rightWeapon.serverModId=0
localEquipment.rightWeapon.serverBaseId=0
localEquipment.ammo.serverModId=0
localEquipment.ammo.serverBaseId=0
localEquipment.leftSpell.serverModId=0
localEquipment.leftSpell.serverBaseId=0
localEquipment.rightSpell.serverModId=0
localEquipment.rightSpell.serverBaseId=0
localEquipment.powerOrShout.serverModId=0
localEquipment.powerOrShout.serverBaseId=0
remoteEquipment.0.playerId=2
remoteEquipment.0.sequence=4
remoteEquipment.0.weaponDrawn=1
remoteEquipment.0.weaponFullyDrawn=1
remoteEquipment.0.leftWeapon.serverModId=0
remoteEquipment.0.leftWeapon.serverBaseId=0
```

`ready=1` requires `PlayerCharacter`, its base form, and its parent cell to exist. Server ids are best-effort translations through `ModSystem`; zero values mean no equipped form or no useful translation yet.

The policy fields are part of the runtime acceptance surface. `policy=equipment_snapshot_only` means this file is proving the staged equipped-form observer, not full inventory sync. The `fullInventoryTraversal=0`, `inventoryMutation=0`, `remoteEquipmentMutation=0`, and `normalInventoryPackets=0` fields must stay zero until the normal inventory path is deliberately validated for Skyrim VR.

`localEquipment.*` is the last snapshot sent to the server. `remoteEquipment.N.*` entries are the latest relayed snapshots received from other authenticated players. They are observation data only; the service does not equip items on actors or mutate inventory.

## Why This Stage Exists

The normal `InventoryService` depends on the full character/entity model:

- `FormIdComponent` plus `LocalComponent`/`RemoteComponent`
- equip/inventory hook events
- remote actor lookup and mutation
- `EquipManager` calls for remote equipment updates
- full inventory reads through container-change structures

Those assumptions are still unsafe for the default VR target because `CharacterService` and remote actor spawn remain disabled. This observer gives a runtime validation surface for local equipment state before networking inventory/equipment changes.

The VR equipment relay exists to test the narrow network path without depending on those assumptions. It is explicitly separate from `RequestEquipmentChanges`/`NotifyEquipmentChanges`, which still require spawned character entities and the normal inventory service.

The explicit `SkyrimTogetherVRClientAvatarSync` build is the first staged exception to the no-`CharacterService` default, and the explicit `SkyrimTogetherVRGameplayClient` build reuses the same remote-avatar equipment cache with connection-only mode off. Both receive the same VR-only equipment stream by server player id. `CharacterService` copies matching snapshots into `RemoteVREquipmentComponent` on remote player entities and uses only the relayed drawn/sheath state to queue the existing delayed `SetWeaponDrawnEx()` visual helper for the matched actor. It still does not equip items, alter inventory, or call `EquipManager`. `VRInventoryService` owns remote equipment liveness; when the service removes a player entry on leave or disconnect, `CharacterService` drops the avatar equipment cache instead of keeping a separate component timeout. The avatar validation handoff now records `remoteEquipmentMatchCount`, `equipmentComponentUpsertCount`, `equipmentWeaponDrawQueuedCount`, `staleEquipmentRemovedCount`, latest equipment sequence/drawn flags, and missing form/actor counters so two-client tests can verify held-item context reached the remote avatar cache before any real actor equipment mutation is attempted.

## Layout Gate

The local inventory prerequisites now have CommonLib-informed static assertions:

- `ExtraContainerChanges::Entry` matches CommonLib's `InventoryEntryData` field offsets and size.
- `ExtraContainerChanges::Data` matches CommonLib's `InventoryChanges` first fields and size.
- `TESContainer::Entry` matches CommonLib's `ContainerObject` field offsets and size.
- `InventoryEntry`, `ExtraDataList`, and `MiddleProcess` assert the offsets used by equipped-form observation.
- `TESObjectREFR::GetInventory()` and `GetItemCountInInventory()` now use the local CommonLib-style `VisitInventory()` wrapper instead of raw `GetContainerChanges()->entries` traversal.

This only validates the narrow observer path. Full inventory sync still needs VR runtime validation of the wrapped traversal, inventory packet flow, spawned actor ownership, and remote equipment mutation before it is safe to send normal inventory/equipment packets. Runtime evidence now checks the policy fields above so a run cannot accidentally present the equipment snapshot lane as completed full inventory sync.

## Next Steps

- Validate the handoff file in-game while equipping weapons, spells, ammo, and powers.
- Validate `TESObjectREFR::GetInventory()` and `GetItemCountInInventory()` in-game before adding full inventory counts.
- Validate the staged remote weapon draw/sheath visual cue before attempting real actor equipment application.
- Keep `RequestEquipmentChanges`, `NotifyEquipmentChanges`, and `EquipManager` disabled until spawned VR actors and inventory layouts are validated.
