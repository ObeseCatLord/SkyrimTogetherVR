# VR Pose Replication

The current VR pose work has a dedicated network stream for connection-only bring-up. It captures local Skyrim VR node transforms, sends them to the server through a VR-only pose packet, and relays them to other connected clients without enabling actor spawn, ownership, animation, or gameplay sync.

VRIK IK sync is mandatory for SkyrimTogetherVR. The pose packet therefore carries both the core IK driver targets and an explicit VRIK data lane. The current client publishes the lane through the relay and handoff surfaces; `SkyrimTogetherVRVrikBridge` fills the VRIK API-backed fields through a small SKSEVR plugin handoff file. The default build remains non-mutating, the explicit `SkyrimTogetherVRClientAvatarSync` and `SkyrimVRImmersiveLauncherAvatarSync` targets build an in-game remote-avatar validation path that applies replicated HMD/hand targets and validates remote VRIK payloads against spawned remote actor scene nodes, and the explicit gameplay targets reuse that path with the normal gameplay service set enabled.

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

`VRPoseService` is instantiated in `TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY` mode for bring-up, and is also constructed before `CharacterService` in the staged avatar-sync and gameplay branches so remote player actors can receive pose targets and VRIK context. The avatar-sync validation build keeps `VRConnectionService` active and does not enable the full object/inventory/magic/combat gameplay service set, so it can use the same `STVR_AUTOCONNECT` and `SkyrimTogetherVR.command` file handoff as the safer connection-only build. The gameplay build keeps the same VR relay services active while also constructing the normal gameplay services. `VRMovementService` remains active in those VR branches too, so the in-engine avatar consumer can use replicated movement for the actor root, replicated pose for HMD/hand targets, and replicated VRIK metadata for strict avatar-readiness evidence. `VRPoseService` subscribes to `UpdateEvent`, captures the latest local player pose snapshot, and logs a compact summary once per second.

When connected, it sends `RequestVRPoseUpdate` at 20 Hz. The packet contains a shared `VRPoseUpdate` payload with:

- a local monotonically increasing sequence number
- HMD transform
- left and right hand transforms
- spell origin and destination transforms
- arrow origin and destination transforms
- bow aim and bow rotation transforms
- left and right weapon offset transforms
- primary and secondary magic offset/aim transforms
- VRIK detection state
- left and right VRIK finger curl values when the VRIK/HIGGS bridge provides them
- VRIK camera offset, final camera offset, and final smoothing offset when the VRIK interface provides them

It also subscribes to `NotifyVRPoseUpdate` and stores the latest remote pose by server player id. Duplicate or out-of-order sequence numbers are ignored. Remote poses are cleared immediately on `DisconnectedEvent`, pruned when the client goes offline, removed when `NotifyPlayerLeft` is received, or removed when a pose has not been refreshed for three seconds.

## VRIK IK Data Lane

The VRIK interface reference inspected from `_refs/higgs/include/vrikinterface001.h` exposes build/settings access, finger curl reads, and camera-offset reads. It does not expose a remote avatar solver. For multiplayer visibility, SkyrimTogetherVR must therefore carry the IK targets itself:

- HMD, left-hand, and right-hand transforms are mandatory IK targets.
- Weapon, bow, arrow, and magic nodes provide the combat/interaction target context needed by a remote avatar.
- `VRVrikData` carries VRIK installation/API state, finger curl values, and camera/smoothing offsets.
- HIGGS pre/post VRIK callback phases remain the safest future timing point for filling API-backed fields.

The current implementation sets VRIK detection from `Data/SKSE/Plugins/vrik.dll` or `VRIK.dll`, serializes the VRIK lane in `VRPoseUpdate`, relays it through the server, writes it to `SkyrimTogetherVR.pose`, and exposes it in the VR Papyrus telemetry readout. `SkyrimTogetherVRVrikBridge` is built as a separate SKSEVR plugin, requests VRIK's interface with message `0xF2AFAEE6` after SKSE's post-load messaging, and writes API-backed values into:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.vrik
```

`VRPoseService` reads that file at the 20 Hz pose cadence and requires a coherent fresh `bridge.loaded`/`bridge.sequence`/`bridge.epoch` snapshot before relaying API-backed values. If the bridge is absent, stale, partially written, or VRIK does not provide an interface, the lane still reports VRIK installation detection while finger curl and camera-offset values remain invalid. The VRIK bridge no longer polls VRIK getters from its writer thread; it serializes cached snapshot data until a validated main-thread/update-phase VRIK snapshot hook is added for live finger/camera updates.

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

Each node records `.valid`, and valid nodes also record `.position`, `.axisX`, `.axisY`, `.axisZ`, and `.scale`. This is a read-only consumer of the replicated pose stream; it does not create actors, move nodes, or apply animation/physics.

This keeps HIGGS-owned hand physics and grab state untouched. The service reads node transforms and sends pose data only.

## Remote Avatar Target Staging

The first in-engine consumer is staged through `RemoteVRPoseComponent` and `RemoteVREquipmentComponent`. In the avatar-sync branch, `CharacterService` matches `NotifyVRPoseUpdate` and `NotifyVREquipmentUpdate` entries by server player id to remote player entities carrying `PlayerComponent`, then stores the latest `VRPoseUpdate` and `VREquipmentUpdate` snapshots on that entity. It also reads the matching `VRMovementService` remote movement snapshot when available. This gives the avatar/IK layer direct access to movement, HMD, hand, weapon, projectile, magic, VRIK finger, camera-offset, weapon-draw, equipped-weapon, equipped-ammo, equipped-spell, and power/shout targets without looking through the file handoff.

The avatar pose component is a cache copy. `VRPoseService` owns remote pose liveness and clears remote pose entries on disconnect, `NotifyPlayerLeft`, or three seconds without an update. `VRInventoryService` owns the remote equipment map and clears it on player leave or disconnect. When those service maps no longer contain a player id, `CharacterService` removes the corresponding avatar cache component immediately instead of running a second component-local stale timer.

The default connection-only target has a separate readout-only proxy cache, `VRRemotePlayerService`, which writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.remoteplayers`. It tracks `NotifyPlayerJoined`, `NotifyPlayerCellChanged`, and `NotifyPlayerLeft`, then joins those player ids with the remote pose, movement, equipment, action-lane, and HIGGS relay maps. This gives the companion panel and Papyrus telemetry a stable default remote-player row with HMD/left-hand/right-hand pose target availability, pose/movement/equipment/HIGGS availability, same-cell/same-worldspace hints, VRIK avatar readiness, and a separate HIGGS-aware avatar readiness value without creating actors, marker objects, or scene nodes.

The component path is deliberately non-mutating in the default target. `TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS` is set to `0` for `SkyrimTogetherVRClient`, so the staged component does not move actors or apply animation during safe bring-up. The explicit `SkyrimTogetherVRClientAvatarSync` target keeps `TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY=1`, adds `TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC=1`, and sets `TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS=1`; the explicit `SkyrimTogetherVRGameplayClient` target uses the same remote-avatar gates with connection-only mode off. In those builds, the guarded branch first runs a same-space guard using the local and remote `VRMovementUpdate` cell/worldspace ids. Direct actor root movement authority is disabled, and direct HMD/hand skeleton writes are gated off by `TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SKELETON_WRITES=0`, because writing `NiAVObject::world` from the VM update path is overwritten by the animation/render pipeline. The actor-target pass now records same-space readiness plus remote VRIK detection/API, finger-curl, and camera-offset payload availability without applying unsafe finger-bone transforms. Invalid remote pose or VRIK payload values are counted in the avatar handoff instead of being written into actor roots or scene nodes. The remote equipment lane applies only the drawn/sheath visual cue through the existing delayed weapon-draw helper; equipped forms remain cached context until inventory/equipment mutation is validated.

`CharacterService` writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.avatar` once per second in the avatar-sync branch. The file records remote player/pose match counts, pose component upserts, actor-target attempts, skeleton-write and movement-authority policy flags, same-space guard counts, `actorTargetSkippedDifferentCellCount`, `actorTargetSkippedDifferentWorldSpaceCount`, missing local/remote movement counts, missing form/actor/root counts, remote VRIK detection/API/finger/camera payload counts, movement/fallback movement counts, rejected invalid transform, VRIK, and movement counts, spell origin/destination, arrow origin/destination, bow aim/rotation, left/right weapon-offset, and primary/secondary magic-offset/aim validity counts, stale pose component removals, remote equipment match counts, equipment component upserts, queued weapon-draw visual counts, stale equipment component removals, the latest player/form/pose sequence target result, the latest `last.sameSpaceForApply` guard result, the latest pose/VRIK-lane validity booleans, and the latest equipment player/form/sequence/drawn flags. The companion helper reads this as the `avatar` readout and attaches the latest target/cache plus pose-context and equipment-cache summary to the `Remote Players` table. The VR Papyrus status and telemetry message boxes also include the same avatar target summary. The strict avatar-sync runtime audit now accepts cached same-space/VRIK readiness when `actorSkeletonWritesEnabled=0`, and only requires HMD plus left/right hand node copies when skeleton writes are explicitly enabled.

This still needs two-client runtime validation and likely a proper VRIK/animation/equipment integration before it should be enabled in normal builds.

## Server Relay

`VRPoseRelayService` subscribes to `PacketEvent<RequestVRPoseUpdate>`, stamps the sender's server player id into `NotifyVRPoseUpdate`, and broadcasts to all other connected players. The relay drops empty pose packets, sequence `0`, non-increasing sequence numbers per server player id, and packets that arrive faster than the intended 20 Hz pose lane. Per-player relay state is cleared on player leave so reconnects do not inherit stale sequence or rate windows.

The relay intentionally does not:

- require a spawned character
- create or mutate actors
- transfer ownership
- inspect HIGGS state
- apply pose data to physics or animation

## Next Steps

- Build and install `SkyrimTogetherVRVrikBridge.dll` on Windows, then validate that SKSEVR loads it and that `SkyrimTogetherVR.vrik` updates while VRIK is installed.
- Runtime-validate `SkyrimTogetherVRAvatarSync.exe` with two clients, then validate `SkyrimTogetherVRGameplay.exe` with the same pose/avatar evidence plus gameplay relay evidence before adding ghost-hand/ghost-avatar rendering or a proper VRIK/animation integration for remote avatars.
- Extend similar server-side sequence/rate guards to the staged non-pose gameplay relay lanes before any of those leave observation mode.
- Keep HIGGS interactions behind a separate bridge for grabs, collisions, and held objects.
