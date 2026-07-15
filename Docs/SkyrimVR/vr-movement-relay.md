# VR Movement Relay

The current VR movement work has a dedicated connection-only network stream. It replicates a compact movement snapshot by authenticated server player id without enabling `CharacterService`, remote actor spawn, ownership views, normal actor movement packets, or actor movement mutation.

## Captured State

`VRMovementService` polls the local `PlayerCharacter` once per update and converts local cell/worldspace forms through `ModSystem::GetServerModId()` into `GameId` values.

The shared `VRMovementUpdate` payload contains:

- a local monotonically increasing sequence number
- current cell id
- current worldspace id
- player position
- player pitch/yaw rotation
- direction, currently sourced from yaw for the staged readout

The sequence only advances when the captured movement snapshot changes.

## Runtime Service

When connected, `VRMovementService` sends `RequestVRMovementUpdate` at 20 Hz if a local movement snapshot is available. The service also subscribes to `NotifyVRMovementUpdate`, ignores loopback updates for the local player id, and stores remote snapshots by server player id.

Remote movement state is cleared when the client disconnects. Individual remote entries are removed on `NotifyPlayerLeft`, duplicate or out-of-order sequence numbers are ignored, and stale entries are pruned after three seconds without an update.

The service does not:

- send `ClientReferencesMoveRequest`
- process `ServerReferencesMoveRequest`
- require `LocalComponent`, `RemoteComponent`, `OwnerComponent`, or `MovementComponent`
- look up remote actor entities
- call `MoveTo` or `SetPosition`
- mutate actor movement, physics, or animation

## Handoff File

The latest local and remote snapshots are written to:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.movement
```

The fields include:

- `ready`
- `online`
- `localPlayerId`
- `localMovementAvailable`
- `remoteMovementCount`
- `localMovement.sequence`
- `localMovement.cell.serverModId`
- `localMovement.cell.serverBaseId`
- `localMovement.worldSpace.serverModId`
- `localMovement.worldSpace.serverBaseId`
- `localMovement.position`
- `localMovement.rotation`
- `localMovement.direction`
- `remoteMovement.<playerId>.ageSeconds`
- `remoteMovement.<playerId>.position`
- `remoteMovement.<playerId>.rotation`

This file is a validation and handoff surface in the default target. It is intended for launcher, external overlay, or debug/proxy visual consumers.

The explicit avatar-sync and gameplay targets now use `VRAvatarService` for embodiment. It sends the local root and complete humanoid graph snapshot over the canonical `ClientReferencesMoveRequest` lane and consumes canonical `ServerReferencesMoveRequest` updates for CommonLib-owned actors, including retained-identity spatial transfer. The VR-only `VRMovementService` relay remains an observation and compatibility lane; it is not the authoritative actor-mutation input. HMD/hand/VRIK targets are not applied to remote skeletons.

## Server Relay

`VRMovementRelayService` subscribes to `PacketEvent<RequestVRMovementUpdate>`, copies the payload into `NotifyVRMovementUpdate`, stamps `PlayerId` from `Player::GetId()`, and broadcasts to all other connected players. It drops sequence `0`, non-increasing sequence numbers, and per-player packets that arrive faster than the intended 20 Hz movement lane. Per-player relay state is cleared on player leave.

The relay intentionally does not require a character entity. This is the key difference from the normal movement sync path, where requests target owned spawned character entities and remote application depends on actor lookup plus movement mutation.

## Next Steps

- Validate the local and remote fields in-game with two VR clients connected to a server.
- Runtime-validate the `SkyrimTogetherVRAvatarSync.exe` consumer that combines remote movement with remote pose, then validate the same path in `SkyrimTogetherVRGameplay.exe` before making it a default path.
- Replace the staged direction source with a validated VR movement or animation direction if needed.
- Extend the same sequence/rate guard pattern to staged non-movement gameplay relay lanes before any of them mutate actors or objects.
- Keep HIGGS hand, grab, collision, and physics callbacks behind a separate HIGGS bridge.
