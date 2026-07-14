# Skyrim Together VR FBT Networking Senior Review Brief

## Goal and scale

Choose the smallest technically sound architecture that networks SkyrimVR-FBT
lower-body motion together with the existing HMD/hand pose lane. This is a
single mod port maintained by a solo operator. Avoid enterprise machinery, but
do not ship scene-graph writes at a phase where Skyrim immediately overwrites
them.

The decision must work on Windows and Proton and remain optional when FBT is not
installed. HIGGS, PLANCK, and VRIK compatibility must not regress.

## Verified evidence

| Claim | Verification |
|---|---|
| FBT runs its body solve after VRIK. | `[verified: source]` `fbt_vrik_hook.cpp:29-40` calls VRIK, tracker/calibration work, then `OnPostVrikUpdate`. |
| The final pose is present in normal player skeleton world transforms. | `[verified: source]` `fbt_body.cpp:44-73,130-145,281-360,365-575` resolves pelvis/thigh/calf/foot nodes and writes local and world transforms. |
| The final lower-body nodes are pelvis, both thighs, calves, and feet; spine writes are conditional. | `[verified: source]` `fbt_body.cpp:44-73,417-510,544-573`. |
| The supplied FBT binary exports no pose API. | `[verified: source]` `main.cpp:27-66` exposes only SKSE load/query; archive contains no public API header. |
| The supplied FBT source has no license or build project. | `[verified: archive listing]` source ZIP contains 13 `.cpp/.h` files and no LICENSE, README, CMake, xmake, solution, or vcxproj. Do not vendor or publish it without permission. |
| STVR already relays fixed-field HMD/hand transforms at about 20 Hz. | `[verified: source]` `VRPoseService.cpp`, `VRPoseUpdate.{h,cpp}`, and `VRPoseRelayService.cpp`; server minimum interval is 45 ms. |
| The wire payload has no schema field and matched client/server binaries are already enforced elsewhere by build commit. | `[verified: source]` `VRPoseUpdate.cpp` fixed-order serialization; existing authentication uses build compatibility. Adding fields is a deliberate matched-build protocol change. |
| Existing remote actor skeleton writes are disabled. | `[verified: source/docs]` `CharacterService.cpp` defaults `TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SKELETON_WRITES=0`; `vr-pose-replication.md` records that task-time world writes are overwritten by animation/render updates. |
| HIGGS exposes a post-VRIK/post-HIGGS callback. | `[verified: source]` `higgs_bridge/main.cpp:150-154,420-439`. The bridge already registers it and snapshots HIGGS state there. |
| STVR’s normal update is dispatched through an SKSE task callback. | `[verified: source]` `vr_tick_bridge/main.cpp` queues `ClientUpdateTask`; `VRTickBridge.cpp:130+` calls `World::Update`; `World.cpp:264-277` emits `UpdateEvent`. |
| Exact relative ordering between the SKSE task callback and FBT/HIGGS render callbacks is documented. | `[unknown]` The code establishes each path independently, but not their total order in a frame. |
| Directly resolving player lower-body nodes during the STVR update will observe the final FBT solve every frame. | `[unverified]` Likely when the task follows actor update, but total ordering is not proven. |
| Calling a narrow launcher callback from HIGGS post-update can write remote bones after animation without races. | `[unverified]` It is the strongest available phase signal, but callback reentrancy, client lifetime, and actor update ordering need design scrutiny. |

## Current proposal

1. Extend `VRPlayerPose`, `VRPlayerPoseSnapshot`, and `VRPoseUpdate` with pelvis,
   left/right thigh, calf, and foot transforms. Resolve the local nodes by their
   stable vanilla skeleton names. This captures FBT, VRIK, and vanilla lower-body
   output without calling private FBT offsets.
2. Extend serialization, equality, server validation, status telemetry, and
   encoding tests in the existing pose lane. Invalid body transforms are dropped
   by the same bounded orthonormal checks as hands/HMD.
3. Keep normal `UpdateEvent` responsible for networking and for building a
   plain, validated cache of remote actor IDs plus body transforms.
4. Extend the existing HIGGS bridge process-local endpoint pattern with a
   versioned, lifetime-validated callback invoked by
   `AddPostVrikPostHiggsCallback`. The callback does only bounded cache copy,
   actor/node resolution, and world-transform application. No network, EnTT
   mutation, allocation-heavy work, or transport pumping occurs there.
5. Make post-phase writes an explicit build feature used only by AvatarSync and
   Gameplay targets. Connection-only remains non-mutating. If HIGGS or the
   endpoint is unavailable, retain networking/telemetry but do not perform
   task-time fallback writes.
6. Do not modify or redistribute the unlicensed FBT source. Optional detection
   reports whether `SkyrimVR-FBT.dll` is installed, but integration depends only
   on public skeleton state and HIGGS’s existing API.

## Open decisions

### D1: local capture phase

**Lean:** capture named player bones in `VRPoseService::OnUpdate` and record
sequence/staleness telemetry. This avoids modifying FBT and should see the most
recent complete pose, while the next frame will refresh it.

Options: capture in normal STVR update; capture inside the HIGGS callback into a
separate shared snapshot; patch FBT with a public API. The FBT API option was
rejected for the shipping path because its source has no redistributable license
or build metadata. A HIGGS-callback local snapshot is more phase-precise but adds
an additional cross-module data channel.

### D2: remote application phase and ownership

**Lean:** post-HIGGS callback into the launcher, backed by an immutable/bounded
cache prepared during normal update. The callback must fail closed on epoch,
thread, endpoint, generation, stale sequence, actor, cell/worldspace, or node
validation failures.

Options: existing task-time writes; post-HIGGS callback; actor-update hooks;
ghost render props. Task-time writes are rejected because project evidence says
animation overwrites them. Actor hooks add version-sensitive addresses. Ghost
props require a separate rendering/avatar implementation and are too large for
this increment.

### D3: transform representation

**Lean:** retain existing world-space matrix+position+scale data for consistency,
but before application derive local transforms relative to each target node’s
parent and update both local and world fields. Review whether sending local bone
transforms relative to the skeleton is safer for actors whose root placement
differs slightly between clients.

### D4: feature gating

**Lean:** add `remote_avatar_post_phase_writes` and enable it for AvatarSync and
Gameplay only. Do not enable the old task-time skeleton-write macro. Require a
healthy HIGGS post callback and expose status counters.

### D5: protocol compatibility

**Lean:** fixed matched-build extension is acceptable because STVR already
rejects mismatched builds. Consider adding a `BodyPoseValid` bit/format version
inside `VRPoseUpdate` to make absence explicit and future append-only revisions
auditable.

## Suspected overlaps

D1 and D2 share one phase/lifetime question. Merge them if one process-local
post-phase bridge can safely publish local pose and consume remote pose without
calling network code from the callback.

## Reviewer instructions and depth budget

Verify the load-bearing code claims before critiquing. Re-rank the decisions,
identify callback lifetime/reentrancy, scene-graph, coordinate-space, and remote
actor ownership failures that would cause crashes or invisible poses. Recommend
a concrete minimal architecture and its invariants. A design that merely enables
the existing task-time writes is not sufficient without disproving the overwrite
evidence.

Do not re-review address-library aliases, connection authentication, controller
bindings, server deployment, or the Realm modal fix. Do not edit files or run a
build. Return a prioritized review with file/line evidence and a short acceptance
checklist suitable for a disposition document.
