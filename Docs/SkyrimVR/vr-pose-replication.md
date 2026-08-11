# VR Pose Replication

The VR pose stream captures local Skyrim VR node transforms, sends them through
the matched gameplay protocol, and applies them to canonical managed remote
actors created by the normal character/ownership flow.

VRIK IK sync is mandatory for SkyrimTogetherVR. The pose packet carries core
world-space HMD/hand targets, a versioned post-VRIK/post-HIGGS body hierarchy,
sparse named finger rotations, and a VRIK diagnostics lane.

## Captured Nodes

`VRPlayerPose` reads Skyrim VR's `PlayerCharacter::VR_NODE_DATA` through the local `PlayerCharacter::GetVRNodeData()` accessor. That typed view is based on CommonLibSSE-NG's VR offsets and has compile-time assertions for the nodes used by the relay:

- `UprightHmdNode`
- left and right controller or wand nodes
- spell origin and destination
- arrow origin and destination
- bow aim and bow rotation
- left and right weapon offset nodes
- primary and secondary magic offset/aim nodes

For each valid node, `VRPlayerPoseSnapshot` reads the `NiAVObject::world` transform through the local `NiAVObject` layout:

- node address
- three rotation axes
- world position
- scale

## Runtime Service

`VRPoseService` publishes local samples and caches validated remote samples.
`VRActorReplicationService` translates those remote packets into bounded,
result-owned bridge transactions targeting actors managed by `VRAvatarService`.

When connected, it sends `RequestVRPoseUpdate` at 20 Hz. The packet contains a shared `VRPoseUpdate` payload with:

- a local monotonically increasing sequence number
- HMD transform
- left and right hand transforms
- spell origin and destination transforms
- arrow origin and destination transforms
- bow aim and bow rotation transforms
- left and right weapon offset transforms
- primary and secondary magic offset/aim transforms
- body format 3: pelvis/legs, spine0/1/2, neck, clavicles, upper arms, and forearms
- sparse unit-quaternion rotations for 30 named left/right finger joints
- independent body capture sequence and skeleton-root generation
- VRIK detection state
- left and right VRIK finger curl values when the VRIK/HIGGS bridge provides them
- VRIK camera offset, final camera offset, and final smoothing offset when the VRIK interface provides them

It also subscribes to `NotifyVRPoseUpdate` and stores the latest remote pose by server player id. Duplicate or out-of-order sequence numbers are ignored, and the client repeats the server's finite/bounds/orthogonality/body-format validation before caching a packet. Remote poses are cleared immediately on `DisconnectedEvent`, pruned when the client goes offline, removed when `NotifyPlayerLeft` is received, or removed when a pose has not been refreshed for three seconds.

`VRPoseUpdate` is a fixed-order protocol structure. Body `FormatVersion` is a nested body-lane validator, not mixed-version wire negotiation. Client and server binaries must come from the same matched build whenever the schema changes.

## SkyrimVR-FBT Body Lane

`SkyrimTogetherVRHiggsBridge` attaches to the process-local callback endpoint before registering its HIGGS callbacks. Its post-VRIK/post-HIGGS callback asks the launcher to capture the current local player body into a nonblocking SRW mailbox. `VRPoseService` reads that mailbox on its normal update, treats samples older than 250 ms as invalid, and sends the body alongside the normal pose.

Body format 3 reads the final callback state after VRIK, SkyrimVR-FBT, and HIGGS.
SkyrimVR-FBT may publish lower-spine correction through world transforms only,
so capture derives each spine node's effective rotation as parent-world inverse
times node-world. Pelvis carries local translation and rotation; all other body
nodes carry rotation only. Shared validation requires finite, near-unit,
orthogonal, right-handed bases, near-one scale, bounded pelvis translation,
near-zero limb translation, coherent capture/root generations, and safe sparse
finger quaternions. Formats 1 and 2 remain decodable inside protocol revision
13, but mixed protocol revisions fail before fixed-order packet decoding.

The CommonLib gameplay bridge applies remote body nodes parent-first and updates
the hierarchy before deriving head/hand locals from their world targets. It then
applies sparse named finger rotations. Writes require managed remote identity,
matching root generation, safe transforms, and non-ragdoll state.

## VRIK IK Data Lane

The VRIK interface reference inspected from `_refs/higgs/include/vrikinterface001.h` exposes build/settings access, finger curl reads, and camera-offset reads. It does not expose a remote avatar solver. For multiplayer visibility, SkyrimTogetherVR must therefore carry the IK targets itself:

- HMD, left-hand, and right-hand transforms are mandatory IK targets.
- Weapon, bow, arrow, and magic nodes provide the combat/interaction target context needed by a remote avatar.
- `VRVrikData` carries VRIK installation/API state, finger curl values, and camera/smoothing offsets.
- The post-VRIK/post-HIGGS callback fills the API-backed fields and captures the
  final body state.

The current implementation sets VRIK detection from `Data/SKSE/Plugins/vrik.dll` or `VRIK.dll`, serializes the VRIK lane in `VRPoseUpdate`, relays it through the server, writes it to `SkyrimTogetherVR.pose`, and exposes it in the VR Papyrus telemetry readout. `SkyrimTogetherVRVrikBridge` is built as a separate SKSEVR plugin, requests VRIK's interface with message `0xF2AFAEE6` after SKSE's `PostPostLoad` messaging, and writes API-backed values into:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.vrik
```

`VRPoseService` reads that file at the 20 Hz pose cadence and requires a coherent
`bridge.loaded`/`bridge.sequence`/`bridge.epoch` snapshot plus an observed newer
`vrik.snapshotSequence` before relaying API-backed values. The VRIK bridge
captures and rate-limits handoff publication from the validated
post-VRIK/post-HIGGS game-thread callback; it has no background writer. If the
bridge is absent, stale, partially written, or VRIK does not provide an
interface, detection remains visible while finger/camera payload validity is
cleared.

## Pose Handoff File

`VRPoseService` writes a status/readout file for launcher, external overlay, and the required remote-player avatar consumer:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.pose
```

The file is rewritten at 4 Hz and contains:

- connection state flags: `online`, `localPlayerId`
- local pose state: `localPoseAvailable`, `localSequence`
- remote pose count: `remotePoseCount`
- selected local nodes: `local.hmd`, `local.leftHand`, `local.rightHand`, `local.spellOrigin`, `local.arrowOrigin`, `local.bowAim`
- selected remote nodes by player id: `remote.<playerId>.hmd`, `remote.<playerId>.leftHand`, `remote.<playerId>.rightHand`, `remote.<playerId>.spellOrigin`, `remote.<playerId>.arrowOrigin`, `remote.<playerId>.bowAim`
- local and remote VRIK state: `.vrik.detected`, `.vrik.interfaceAvailable`, `.vrik.leftFingers`, `.vrik.rightFingers`, and `.vrik.cameraOffsetsValid`
- local and remote body state: `.body.formatVersion`, `.body.valid`,
  `.body.captureSequence`, `.body.rootGeneration`, joint metadata, and all
  pelvis/leg/spine/neck/clavicle/arm nodes

Each node records `.valid`, and valid nodes also record `.position`, `.axisX`,
`.axisY`, `.axisZ`, and `.scale`. The file is diagnostic; native remote actor
mutation occurs only through the CommonLib gameplay bridge.

This keeps HIGGS-owned hand physics and grab state untouched. The service reads node transforms and sends pose data only.

## Remote Avatar Target Staging

`VRPoseService` owns remote pose liveness and clears remote pose entries on disconnect, `NotifyPlayerLeft`, or three seconds without an update. `VRInventoryService` separately owns the remote equipment map. The previous `CharacterService` cache prototype (`RemoteVRPoseComponent` and `RemoteVREquipmentComponent`) remains in source for comparison but is not constructed by VR targets.

The canonical embodiment path is `VRAvatarService` plus
`SkyrimTogetherVRGameplayBridge.dll`. It owns character lifecycle, root
movement, graph state, body/HMD/hand/finger pose, equipment, appearance, combat,
magic, projectiles, interactions, world state, and death/respawn through typed
or verified native boundaries. VRIK camera offsets and PLANCK physical replay
remain diagnostics because their public APIs do not expose safe remote-actor
mutation contracts.

The explicit avatar-sync and gameplay targets have a readout-only proxy cache,
`VRRemotePlayerService`, which writes
`Data/SkyrimTogetherReborn/SkyrimTogetherVR.remoteplayers`. It joins server
player notifications with the enabled relay maps without creating actors,
marker objects, or scene nodes. The proxy is disabled in the default
connection-proof target.

The gameplay target loads the same bridge used by canonical avatar lifecycle and
applies physical pose only to those managed remote actor records. The older
connection-only/avatar-sync targets remain bounded diagnostics packages and are
not evidence of full gameplay parity.

`VRAvatarService` writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.avatar`
once per second with bridge readiness, lifecycle/root/animation state, pose
submission results, and remote actor diagnostics. Physical pose writes are
performed only by the managed-actor CommonLib bridge and are audited separately
from the readout file.

This still needs two-client runtime validation before release enablement.

## Server Relay

`VRPoseRelayService` subscribes to `PacketEvent<RequestVRPoseUpdate>`, stamps the sender's server player id into `NotifyVRPoseUpdate`, and broadcasts to all other connected players. The relay drops empty pose packets, sequence `0`, non-increasing sequence numbers per server player id, malformed transforms/body data, and packets that arrive faster than the intended 20 Hz pose lane. VRIK-only packets count as non-empty. Per-player relay state is cleared on player leave so reconnects do not inherit stale sequence or rate windows.

The relay intentionally does not:

- require a spawned character
- create or mutate actors
- transfer ownership
- inspect HIGGS state
- apply pose data to physics or animation

## Next Steps

- Build and install `SkyrimTogetherVRVrikBridge.dll` on Windows, then validate that SKSEVR loads it and that `SkyrimTogetherVR.vrik` updates while VRIK is installed.
- Validate FBT locally by proving `bodyCapture.successCount` increases and
  `local.body.valid=1`, then prove format-3 spine/arms/legs/fingers on the
  managed remote actor with two clients.
- Runtime-validate `SkyrimTogetherVRGameplay.exe` with matched pose/avatar and
  gameplay evidence, including tracker loss, ragdoll suppression, rollback, and
  root replacement.
- Extend similar server-side sequence/rate guards to the staged non-pose gameplay relay lanes before any of those leave observation mode.
- Keep HIGGS interactions behind a separate bridge for grabs, collisions, and held objects.
