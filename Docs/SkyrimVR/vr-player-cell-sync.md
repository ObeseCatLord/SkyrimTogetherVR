# VR Player Cell Sync

The VR targets enable player-cell handoff telemetry behind `TP_SKYRIM_VR_ENABLE_PLAYER_CELL_SERVICE=1`.

This is not the normal full player service. In `TP_SKYRIM_VR && TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY`, the client connects only:

- `UpdateEvent`
- `GridCellChangeEvent`
- `CellChangeEvent`

The VR branch sends only:

- `ShiftGridCellRequest`
- `EnterExteriorCellRequest`
- `EnterInteriorCellRequest`
- `PlayerLevelRequest`

All VR package flavors also write a read-only status file for the companion panel, runtime audit, and evidence collector:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.playercell
```

The file is rewritten at most once per second, or immediately after a player cell/grid/level send changes the counters. It is telemetry only and does not itself enable actor spawn, movement application, object replay, or player mutation. All VR package flavors use this same network-only `PlayerService` branch; staged remote actor lifecycle/root work is owned separately by the CommonLib gameplay bridge.

## Handoff Fields

The current schema is key/value text:

- `ready`: `1` once the local player, base form, and parent cell are available.
- `online`: `1` when the transport is online enough to send staged requests.
- `localPlayerId`: server-assigned local player id, if known.
- `playerFormId`: local player form id.
- `currentLevel`: current local player level.
- `cachedLevel`: last level observed by the VR player-service level tracking path.
- `lastLevelSent`: last level sent through `PlayerLevelRequest`.
- `gridCellRequestCount`: count of `ShiftGridCellRequest` sends.
- `exteriorCellRequestCount`: count of `EnterExteriorCellRequest` sends.
- `interiorCellRequestCount`: count of `EnterInteriorCellRequest` sends.
- `levelRequestCount`: count of VR-tracked `PlayerLevelRequest` sends.
- `offlineSkippedRequestCount`: count of cell/grid events skipped because transport was offline.
- `worldSpaceTranslationFailureCount`: count of grid sends skipped because the local worldspace id could not be translated through the mod map.
- `lastGrid.valid`: `1` after at least one grid request was sent.
- `lastGrid.worldSpace.serverModId` and `lastGrid.worldSpace.serverBaseId`: translated server worldspace id for the last grid request.
- `lastGrid.playerCell.serverModId` and `lastGrid.playerCell.serverBaseId`: translated server player cell id for the last grid request.
- `lastGrid.center`: last grid center as `x,y`.
- `lastGrid.cellCount`: number of cells included in the last grid request.
- `lastCell.valid`: `1` after at least one cell request was sent.
- `lastCell.exterior`: `1` for the last exterior-cell request, `0` for interior.
- `lastCell.cell.serverModId` and `lastCell.cell.serverBaseId`: translated server cell id for the last cell request.
- `lastCell.worldSpace.serverModId` and `lastCell.worldSpace.serverBaseId`: translated server worldspace id for the last exterior-cell request, or `0,0` for interior.
- `lastCell.currentCoords`: last exterior cell coords as `x,y`, or the default coords for interior.

`Tools/SkyrimVR/audit_runtime_handoff.py` validates the schema structurally through the `player-cell schema` check. `Tools/SkyrimVR/collect_runtime_evidence.py` records the same validation as `player_cell_status` in `runtime_checklist.json`, and `Tools/SkyrimVR/audit_runtime_evidence_zip.py` requires that checklist id in collected evidence zips.

## Disabled Player Mutations

The following normal `PlayerService` paths are not connected in VR connection-only mode:

- server settings difficulty enforcement
- death system and respawn handling
- player gold loss
- godmode/knockdown post-death handling
- beast-form detection
- party join/leave global edits
- dialogue requests
- `NotifyPlayerRespawn`

This keeps the staged service limited to server-visible player location/level state.

## Server Adjustment

`server/Services/PlayerService::HandleExteriorCellEnter()` now updates `Player::CellComponent` even when the player has no spawned character entity. Character exterior-cell events and spawn fanout still require a character where appropriate.

This matters for VR connection-only clients because actor assignment/spawn remains disabled, but the server still needs the player cell for player list, cell notifications, and later staged systems.

`ShiftGridCellRequest` can still make an unmodified server send `CharacterSpawnRequest` packets for nearby actors. In the default staged VR mode those packets are decoded by `TransportService` and ignored because `CharacterService` is not instantiated.

The explicit `SkyrimTogetherVRClientAvatarSync` build is the exception. It keeps `TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY=1`, adds `TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC=1`, and instantiates `VRAvatarService`. That service uses the canonical assignment/spawn/movement protocol and delegates actor lifecycle, retained-identity root/spatial mutation, and named humanoid graph snapshots to `SkyrimTogetherVRGameplayBridge.dll`; it does not instantiate the legacy desktop `CharacterService` or the full object, inventory, magic, combat, projectile, overlay, and input service set.

## Remaining Gate

Validate server-side cell tracking with `SkyrimTogetherVR.discovery` before making these systems part of the default target:

- `CharacterService`
- player character assignment
- actor spawn/ownership
- movement replication
