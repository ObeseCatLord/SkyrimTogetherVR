# VR Save/Load Observation

The current VR save/load work is a local lifecycle observer for connection-only mode. It records Bethesda `TESLoadGameEvent` notifications and local player readiness after a load without invoking `BGSSaveLoadManager`, forcing reconnects, mutating actors, or enabling the normal gameplay sync services.

## Captured State

`VRSaveLoadService` listens to the `TESLoadGameEvent` dispatcher and polls a minimal local readiness condition on update:

- `PlayerCharacter::Get()` is available
- the player has a base form
- the player has a parent cell

The service records:

- whether a load-game event has been observed
- the number of load-game events observed this process
- whether the player was ready when the last load event was seen
- whether the service is still waiting for player readiness after the last load event
- seconds since the last observed load event
- connection state mirrored from transport events
- local player id, player form id, cell form id, and worldspace form id
- best-effort server-mapped `GameId` rows for the player, parent cell, and worldspace

CommonLibSSE-NG exposes `TESLoadGameEvent` as a marker event. No matching save-game event source is currently wired in this codebase, so this stage intentionally does not claim save-event coverage.

## Runtime Service

`VRSaveLoadService` is enabled only by `TP_SKYRIM_VR_ENABLE_SAVELOAD_OBSERVATION_SERVICE` in the VR connection-only target.

The service does not:

- call `BGSSaveLoadManager::Save`
- call `BGSSaveLoadManager::Load`
- disconnect or reconnect the transport
- install save/load hooks
- enable `CharacterService`, `ObjectService`, `InventoryService`, `MagicService`, `CombatService`, or projectile replay
- mutate local or remote actor state

This makes it a validation surface for load lifecycle timing before any full save/load recovery behavior is added.

## Handoff File

The service writes:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.saveload
```

Current fields:

- `ready`
- `online`
- `localPlayerId`
- `connectionState`
- `loadGameObserved`
- `loadGameCount`
- `readyAfterLastLoad`
- `waitingForReadyAfterLoad`
- `secondsSinceLastLoad`
- `playerFormId`
- `playerCellFormId`
- `playerWorldSpaceFormId`
- `player.serverModId`
- `player.serverBaseId`
- `playerCell.serverModId`
- `playerCell.serverBaseId`
- `playerWorldSpace.serverModId`
- `playerWorldSpace.serverBaseId`
- `lastConnectionError`, only when a connection error has been observed

The raw form IDs are kept for local VR inspection. The `*.serverModId` and `*.serverBaseId` pairs are produced through the current `ModSystem` mapping so later recovery code can compare stable server identities instead of depending only on client load-order bits. They are best-effort fields and may be zero while offline, before mod mappings have arrived, or when a form cannot be translated.

This file is intended for launcher, external overlay, and manual validation workflows.

## Next Steps

- Validate that `loadGameCount` increments after loading a save in Skyrim VR.
- Confirm `readyAfterLastLoad` becomes `1` only after the local player and parent cell are available.
- Compare load timing while offline, connecting, and already connected.
- Decide the later recovery policy for connected clients loading a different save before adding any reconnect, resync, actor cleanup, or gameplay mutation behavior.
