# VR Grab/Release Relay

`VRGrabService` is a conservative grab/release telemetry bridge for Skyrim VR connection-only mode.

## Current Behavior

- Registers a Bethesda `TESGrabReleaseEvent` sink through the CommonLibSSE-NG `ScriptEventSourceHolder` offset `0x580`.
- Captures the grabbed reference id, parent cell id, worldspace id, position, base-form type, and grabbed/released state.
- Sends `RequestVRGrabEvent` only as player-id telemetry when connected.
- Receives `NotifyVRGrabEvent` snapshots from other players and writes them to `Data/SkyrimTogetherReborn/SkyrimTogetherVR.grab`.
- `VRGrabRelayService` stamps the authenticated server player id, drops sequence `0`, packets without an object id, non-increasing sequence numbers, and per-player grab/release bursts faster than the staged event-lane cap, clears relay state on player leave, and rebroadcasts accepted events to other clients.

## HIGGS Boundary

This service is observation-only. It does not call HIGGS APIs, force a grab/drop, alter hand collision, mutate object physics, replay remote object interaction, or call normal Skyrim Together object sync.

`SkyrimTogetherVRHiggsBridge` is the dedicated HIGGS API-side bridge. It requests `IHiggsInterface001` through SKSEVR messaging, observes HIGGS pull/grab/drop/stash/consume/collision/two-handing callbacks, and writes compact local state to `Data/SkyrimTogetherReborn/SkyrimTogetherVR.higgs` without calling mutating HIGGS functions. `VRHiggsService` reads that file and writes networked observation state to `Data/SkyrimTogetherReborn/SkyrimTogetherVR.higgsnet`. The grab relay remains separate from both HIGGS paths and still does not replay remote grabs or drops.

## Readout

The handoff file uses key/value lines:

```text
online=1
localPlayerId=7
localGrabAvailable=1
remoteGrabCount=1
localGrab.sequence=12
localGrab.object.serverModId=0
localGrab.object.serverBaseId=12345
localGrab.cell.serverModId=0
localGrab.cell.serverBaseId=67890
localGrab.worldSpace.serverModId=0
localGrab.worldSpace.serverBaseId=60
localGrab.position=1,2,3
localGrab.formType=29
localGrab.grabbed=1
```

Remote entries use the `remoteGrab.<playerId>.` prefix and include `ageSeconds`.
