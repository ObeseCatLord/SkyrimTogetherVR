# Senior Brief: Tick Bridge Startup Fallback

## Goal and scale

Decide whether SkyrimTogetherVR should add a native SKSEVR task bootstrap fallback so
connection processing can begin when the launcher and SKSEVR bridge are active, even
when the packaged startup quest has not yet dispatched its first Papyrus `OnInit`.

This is a solo-operator port and test deployment. The decision must not weaken the
existing game-thread, endpoint, or process-boundary validation. The immediate goal is
to connect the Linux/Proton VR client to the existing dedicated server; it is not to
enable unvalidated multiplayer gameplay services.

## Evidence

| Claim | Status | Evidence |
| --- | --- | --- |
| The VR launcher created a valid tick endpoint and activated it on the client startup thread. | [verified: runtime log] | `.../SkyrimVR/logs/tp_client.log`, latest run: endpoint prepared at 23:13:19.124 and ready on thread 336 at 23:13:20.209. |
| The SKSEVR bridge loaded and its Papyrus registration callback ran. | [verified: runtime log] | `.../Data/SKSE/Plugins/SkyrimTogetherVRTickBridge.log`, latest run: bridge registered then Papyrus callback completed. |
| No `papyrus_cadence_owner`, `papyrus_arm`, `papyrus_tick`, `task_enqueued`, or `task_run` log lines have appeared in the latest run. | [verified: runtime log] | Same bridge log; all latest-run entries end at registration callback. |
| The launcher has created a `connect` command file for `incidentalstoat.xyz:10578`, but it remains unconsumed. | [verified: runtime files] | `.../Data/SkyrimTogetherReborn/SkyrimTogetherVR.command`; `SkyrimTogetherVR.connection` only echoes endpoint/password. |
| The remote Docker server is healthy but has zero players. | [verified: remote runtime] | `foundry: docker logs skyrim-together-vr`, title reports `0 player`. |
| A clean direct test profile contains `SkyrimTogether.esp`, its SEQ, Realm of Lorkhan alternate-start ESP/scripts, and an auto-generated Realm SEQ. | [verified: filesystem/profile] | Skyrim VR `Data`; Proton `plugins.txt` has Realm before SkyrimTogether. |
| The SkyrimTogether startup and migration quests are Start Game Enabled/Run Once, with PEX scripts deployed. | [verified: existing package audit and binary inspection] | `Tools/SkyrimVR/audit_gamefiles.py`; prior deployment checks showed form IDs `0x02003DD0` and `0x02003DD1`, flags `0x0111`. |
| The current cadence is entirely Papyrus-driven. | [verified: source] | `GameFiles/SkyrimVR/scripts/source/SkyrimTogetherVerifyLaunchScript.psc` and `SkyrimTogetherVRMigrationXScript.psc` call `Tick()` from `OnUpdate`; `Code/vr_tick_bridge/main.cpp` queues the native task only in `PapyrusTick`. |
| The native task validates exact process, endpoint ABI/state, image base, executable callback page, and that `ReadyThreadId == GetCurrentThreadId()` before callback dispatch. | [verified: source] | `Code/vr_tick_bridge/main.cpp`, `ValidateReadyEndpoint`; `Code/client/VRTickBridge.cpp`, `Dispatch`. |
| The stage-0 VR target deliberately runs `connection_only`; multiplayer gameplay sync is not within this decision. | [verified: runtime/status source] | `SkyrimTogetherVR.compatibility` has `connectionOnly=1`, and `Code/client/TiltedOnlineApp.cpp` handles `TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY`. |

## Code topology

* Client process allocates/publishes the in-process mapping in
  `Code/client/VRTickBridge.cpp::Initialize`, and marks it ready in `Activate` from
  `TiltedOnlineApp::BeginMain`.
* `Code/vr_tick_bridge/main.cpp` is a separate SKSEVR plugin DLL. `SKSEPlugin_Load`
  obtains `SKSETaskInterface` and registers Papyrus functions. `PapyrusTick` queues
  `ClientUpdateTask`; `Run` dispatches to `TiltedOnlineApp::Update` only after all
  endpoint checks succeed.
* The two quest scripts use `RegisterForSingleUpdate(0.05)` to call native `Tick`.
  `ClaimCadence` is a one-owner guard for fresh-game vs. migrated-save paths.
* On current runtime evidence, no quest script event has reached the bridge. The game
  process remains live at a pre-world stage; the user must enter a New Game manually
  to exercise a quest start. We cannot automate the VR UI reliably.

## Open decisions

### 1. Add an autonomous native startup pump?

**Current lean:** Add a carefully bounded fallback in the SKSEVR bridge, enabled only
after the mapped endpoint reports `Ready`, that queues the existing `ClientUpdateTask`
until Papyrus explicitly claims cadence. It must run on the established ready thread,
must stop immediately after a Papyrus owner is recorded, and must never dispatch
before `g_appInstance` exists.

Options:

1. **Native pre-cadence fallback (lean).** Prevents a start-game quest delivery issue
   from indefinitely blocking an explicitly configured connection command. Requires a
   small bridge change, regenerated DLL, and Windows rebuild.
2. **Papyrus-only plus Realm of Lorkhan.** Minimal change. Rejected as the sole
   solution because static package validation cannot prove a quest event arrived and
   manual headset progression is a fragile dependency for connection bootstrap.
3. **Force the SkyrimTogether quest through game-form APIs or a hardcoded hook.**
   Rejected because its VR ABI/signature/runtime behavior would be less established
   than the validated SKSE task interface.
4. **Pump directly in the launcher main-loop hook.** Rejected provisionally because
   it crosses the current component boundary and would bypass the dedicated
   SKSE-task/game-thread validation path.

### 2. What may the fallback do before a world/player exists?

**Current lean:** Let it dispatch the normal `g_appInstance->Update()` only after the
endpoint is Ready. Existing VR services already defer player-derived work as shown by
the client log. The command connection service needs this update path, not an
alternate network implementation.

Alternative: gate on player availability. Rejected because it recreates the
pre-world startup deadlock and because the current design already has player-null
deferrals. This needs source verification: identify every early `World::Update` path
that could dereference a missing player on title/menu frames.

### 3. Cadence handoff/rate?

**Current lean:** The fallback should run at a conservative retry cadence and yield
forever to `ClaimCadence(1/2)` once called. It should not run concurrently with a
Papyrus-scheduled task; reuse `g_taskQueued` as the mutual-exclusion primitive.

Need a recommendation for the safest SKSEVR task scheduling mechanism. `AddTask` is
known to execute `ClientUpdateTask` on the ready thread in the intended design; it is
not yet runtime-proven in this run because no task was queued.

## Suspected overlap

The autonomous bootstrap and early-world safety are one decision: approving a fallback
without proving `World::Update` is title-safe would be incomplete.

## Depth budget and not-list

Audit these exact files and any direct dependencies needed to answer the above:

* `Code/vr_tick_bridge/main.cpp`
* `Code/client/VRTickBridge.cpp`
* `Code/client/TiltedOnlineApp.cpp`
* `Code/client/World.cpp`
* `Code/client/Services/VRConnectionService.*`
* the four listed VR Papyrus scripts

Do **not** review overall stage-0 multiplayer parity, HIGGS/PLANCK behavior, server
deployment, general address-library mapping, or rewrite unrelated services. Do not
edit files. Verify first and return a prioritized critique with a concrete recommended
design or a reason not to implement one.
