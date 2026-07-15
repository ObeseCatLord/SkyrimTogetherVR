# VR Pose Replication

The current VR pose work has a dedicated network stream for connection-only bring-up. It captures local Skyrim VR node transforms, sends them to the server through a VR-only pose packet, and relays them to other connected clients without enabling actor spawn, ownership, animation, or gameplay sync.

VRIK IK sync is mandatory for SkyrimTogetherVR. The pose packet therefore carries both the core IK driver targets and an explicit VRIK data lane. The current client publishes the lane through the relay and handoff surfaces; `SkyrimTogetherVRVrikBridge` fills the VRIK API-backed fields through a small SKSEVR plugin handoff file. The default build remains non-mutating. The explicit avatar-sync and gameplay targets validate remote VRIK payload readiness, but the first CommonLib embodiment slice applies only same-cell actor lifecycle and root movement. Direct HMD/hand scene-node and VRIK skeleton writes remain disabled until a validated integration is added to the gameplay bridge.

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

`VRPoseService` is disabled in the default connection-proof target. It is
enabled as an observation/relay service in the explicit avatar-sync and
gameplay branches. `VRAvatarService` separately consumes canonical character
and movement messages and sends root-transform commands to the CommonLib
gameplay bridge; it does not consume pose/VRIK packets yet. `VRMovementService`
remains active in those broader branches for relay and readiness evidence.

When connected, it sends `RequestVRPoseUpdate` at 20 Hz. The packet contains a shared `VRPoseUpdate` payload with:

- a local monotonically increasing sequence number
- HMD transform
- left and right hand transforms
- spell origin and destination transforms
- arrow origin and destination transforms
- bow aim and bow rotation transforms
- left and right weapon offset transforms
- primary and secondary magic offset/aim transforms
- body format 1: pelvis local translation/rotation and left/right thigh, calf, and foot local rotations
- independent body capture sequence and skeleton-root generation
- VRIK detection state
- left and right VRIK finger curl values when the VRIK/HIGGS bridge provides them
- VRIK camera offset, final camera offset, and final smoothing offset when the VRIK interface provides them

It also subscribes to `NotifyVRPoseUpdate` and stores the latest remote pose by server player id. Duplicate or out-of-order sequence numbers are ignored, and the client repeats the server's finite/bounds/orthogonality/body-format validation before caching a packet. Remote poses are cleared immediately on `DisconnectedEvent`, pruned when the client goes offline, removed when `NotifyPlayerLeft` is received, or removed when a pose has not been refreshed for three seconds.

`VRPoseUpdate` is a fixed-order protocol structure. Body `FormatVersion` is a nested body-lane validator, not mixed-version wire negotiation. Client and server binaries must come from the same matched build whenever the schema changes.

## SkyrimVR-FBT Body Lane

`SkyrimTogetherVRHiggsBridge` attaches to the process-local callback endpoint before registering its HIGGS callbacks. Its post-VRIK/post-HIGGS callback asks the launcher to capture the current local player body into a nonblocking SRW mailbox. `VRPoseService` reads that mailbox on its normal update, treats samples older than 250 ms as invalid, and sends the body alongside the normal pose.

Body format 1 intentionally omits spine nodes. SkyrimVR-FBT defaults to a world-only lower-spine correction, so the local spine transforms do not faithfully encode the rendered correction. The pelvis carries local translation and rotation; thigh, calf, and foot carry rotation only. Shared server/client validation requires finite, near-unit, orthogonal, right-handed rotation bases, near-one scale, bounded pelvis translation, and near-zero limb translation.

Remote body samples are relay/cache/telemetry only. HIGGS's local-player callback is not a safe per-remote-actor post-animation phase, so no remote body node writes occur in this version. See `fbt-compatibility.md` and `fbt-networking-senior-disposition-20260714.md`.

## VRIK IK Data Lane

The VRIK interface reference inspected from `_refs/higgs/include/vrikinterface001.h` exposes build/settings access, finger curl reads, and camera-offset reads. It does not expose a remote avatar solver. For multiplayer visibility, SkyrimTogetherVR must therefore carry the IK targets itself:

- HMD, left-hand, and right-hand transforms are mandatory IK targets.
- Weapon, bow, arrow, and magic nodes provide the combat/interaction target context needed by a remote avatar.
- `VRVrikData` carries VRIK installation/API state, finger curl values, and camera/smoothing offsets.
- HIGGS pre/post VRIK callback phases remain the safest future timing point for filling API-backed fields.

The current implementation sets VRIK detection from `Data/SKSE/Plugins/vrik.dll` or `VRIK.dll`, serializes the VRIK lane in `VRPoseUpdate`, relays it through the server, writes it to `SkyrimTogetherVR.pose`, and exposes it in the VR Papyrus telemetry readout. `SkyrimTogetherVRVrikBridge` is built as a separate SKSEVR plugin, requests VRIK's interface with message `0xF2AFAEE6` after SKSE's `PostPostLoad` messaging, and writes API-backed values into:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.vrik
```

`VRPoseService` reads that file at the 20 Hz pose cadence and requires a coherent `bridge.loaded`/`bridge.sequence`/`bridge.epoch` snapshot plus an observed newer `vrik.snapshotSequence` before relaying API-backed values. The bridge now emits `vrik.snapshotAvailable` and `vrik.snapshotSequence` with each heartbeat (`vrik.snapshotAgeMs` for diagnostics), so unchanged snapshots clear finger/camera payload validity while preserving VRIK detection/API capability flags. If the bridge is absent, stale, partially written, or VRIK does not provide an interface, the lane still reports VRIK detection capability while finger curl and camera-offset values remain invalid. The VRIK bridge no longer polls VRIK getters from its writer thread; it serializes cached snapshot data until a validated main-thread/update-phase VRIK snapshot hook is added for live finger/camera updates.

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
- local and remote body state: `.body.formatVersion`, `.body.valid`, `.body.captureSequence`, `.body.rootGeneration`, and pelvis/leg nodes

Each node records `.valid`, and valid nodes also record `.position`, `.axisX`, `.axisY`, `.axisZ`, and `.scale`. This is a read-only consumer of the replicated pose stream; it does not create actors, move nodes, or apply animation/physics.

This keeps HIGGS-owned hand physics and grab state untouched. The service reads node transforms and sends pose data only.

## Remote Avatar Target Staging

`VRPoseService` owns remote pose liveness and clears remote pose entries on disconnect, `NotifyPlayerLeft`, or three seconds without an update. `VRInventoryService` separately owns the remote equipment map. The previous `CharacterService` cache prototype (`RemoteVRPoseComponent` and `RemoteVREquipmentComponent`) remains in source for comparison but is not constructed by VR targets.

The canonical embodiment path is now `VRAvatarService` plus `SkyrimTogetherVRGameplayBridge.dll`. It intentionally consumes only character lifecycle and root movement. Pose, finger, camera-offset, weapon, magic, projectile, HIGGS, PLANCK, and FBT data remains relayed/observable but is not written into CommonLib-owned remote actors yet.

The explicit avatar-sync and gameplay targets have a readout-only proxy cache,
`VRRemotePlayerService`, which writes
`Data/SkyrimTogetherReborn/SkyrimTogetherVR.remoteplayers`. It joins server
player notifications with the enabled relay maps without creating actors,
marker objects, or scene nodes. The proxy is disabled in the default
connection-proof target.

The default `SkyrimTogetherVRClient` target does not construct `VRAvatarService`. The explicit `SkyrimTogetherVRClientAvatarSync` target keeps connection-only mode, enables remote-avatar sync, and loads the gameplay bridge; the gameplay target uses the same bridge with the wider VR-safe observer graph. These targets apply validated same-cell actor lifecycle and root movement through CommonLib. Direct HMD/hand scene-node and VRIK skeleton writes remain disabled because the bridge has no validated actor-specific animation integration.

`VRAvatarService` writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.avatar` once per second using schema `commonlib_bridge_v1`. It reports bridge readiness and epoch, local assignment/snapshot state, tracked and active avatar counts, lifecycle/root command counts, accepted canonical remote movement, same-space observations, rejected commands, and invalid transforms. The strict audit requires a positive epoch and local server id, at least one successful actor create, an accepted `ServerReferencesMoveRequest`, and a submitted root update, with zero rejected commands or invalid transforms. `actorSkeletonWritesEnabled` must remain `0`; pose-relay telemetry is validated separately.

This still needs two-client runtime validation and likely a proper VRIK/animation/equipment integration before it should be enabled in normal builds.

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
- Validate FBT locally by proving `bodyCapture.successCount` increases and `local.body.valid=1`, then validate the matched remote body cache with two clients. Keep remote body writes disabled until an actor-specific post-animation hook is proven.
- Runtime-validate `SkyrimTogetherVRAvatarSync.exe` with two clients, then validate `SkyrimTogetherVRGameplay.exe` with the same pose/avatar evidence plus gameplay relay evidence before adding ghost-hand/ghost-avatar rendering or a proper VRIK/animation integration for remote avatars.
- Extend similar server-side sequence/rate guards to the staged non-pose gameplay relay lanes before any of those leave observation mode.
- Keep HIGGS interactions behind a separate bridge for grabs, collisions, and held objects.
