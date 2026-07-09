# VR HIGGS Observation Relay

`VRHiggsService` is a conservative HIGGS state relay for Skyrim VR connection-only mode.

## Current Behavior

- Reads `Data/SkyrimTogetherReborn/SkyrimTogetherVR.higgs`, which is written by the separate `SkyrimTogetherVRHiggsBridge` SKSEVR plugin.
- Parses coherent fresh bridge/API availability, callback registration, HIGGS update-phase snapshot availability, two-handing state, left/right hand state, finger values, optional grab transform, and the latest HIGGS event snapshot.
- Sends `RequestVRHiggsState` only as player-id telemetry when connected.
- Receives `NotifyVRHiggsState` snapshots from other players and writes them to `Data/SkyrimTogetherReborn/SkyrimTogetherVR.higgsnet`.
- `VRHiggsRelayService` stamps the authenticated server player id, drops sequence `0`, packets without observed bridge/API/hand/event content, non-increasing sequence numbers, and per-player HIGGS state packets faster than the intended 4 Hz lane, clears relay state on player leave, and rebroadcasts accepted snapshots to other clients.

## Boundary

This service is observation-only. It does not request `IHiggsInterface001`, register HIGGS callbacks, force a grab/drop, set grab transforms, disable hands, alter weapon collision, modify HIGGS layer masks, mutate object physics, replay remote object interaction, or call normal Skyrim Together object sync.

The only code that talks to the HIGGS API is `SkyrimTogetherVRHiggsBridge`, and that bridge also stays observation-only. Shared object ownership, conflict resolution, and remote visual consumers must be designed and validated before any mutating HIGGS API calls are considered.

## Readout

The network handoff file uses key/value lines:

```text
ready=1
online=1
localPlayerId=7
localHiggsAvailable=1
remoteHiggsCount=1
localHiggs.sequence=120
localHiggs.interfaceAvailable=1
localHiggs.callbacksRegistered=1
localHiggs.snapshotAvailable=1
localHiggs.snapshotSequence=3
localHiggs.twoHanding=0
localHiggs.left.holdingObject=1
localHiggs.left.grabbedObject.serverModId=0
localHiggs.left.grabbedObject.serverBaseId=12345
localHiggs.lastEvent.valid=1
localHiggs.lastEvent.kind=grabbed
```

`ready=1` means `VRHiggsService` has parsed a current coherent local bridge snapshot. A stale, missing, or partially written `SkyrimTogetherVR.higgs` file leaves the network readout not ready instead of reusing old HIGGS state indefinitely.

Remote entries use the `remoteHiggs.<playerId>.` prefix and include `ageSeconds`. The desktop companion merges those entries into the `Remote Players` table under the `higgs` column.
