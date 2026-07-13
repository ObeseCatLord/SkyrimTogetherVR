# Live SKSEVR Tick Bridge Review Brief

## Goal

Make the installed default SkyrimTogetherVR package dispatch its safe
connection-only `World::Update()` cadence on Linux/Proton, then connect to the
already-running dedicated server. This is a solo-operator port; the decision
must favor a narrow, crash-resistant runtime path over a broad redesign.

## Verified Runtime Evidence

| Claim | Evidence |
| --- | --- |
| Linux launch is healthy and focused in a real VR runtime. | `logs/stvr-launch-20260713T011603Z.log` shows Bigscreen Beyond, both Valve Knuckles controllers, their Index interaction profiles, and an OpenXR `FOCUSED` state. |
| The remote server is healthy. | `foundry` has `skyrim-together-vr` Up with a host-network UDP listener on `10578`; the console reports zero players. |
| The installed package is the verified default package. | Game-root manifest has `sourceRevision=d5f4ad5024858c615f6a4b36ae9380d72d8831b6`, `packageFlavor=default`, and `papyrusCompiled=true`. |
| The old VM destructor crash path is absent in this launch. | New `tp_client.log` launch records only `Installing SkyrimTogetherVR main-loop observer`; it does not record `VM destructor hook reached`, and no new dump was created. |
| The tick endpoint inside the mapped client is ready. | `tp_client.log` has `tick bridge endpoint prepared` and `tick bridge endpoint ready on thread 344`. `TiltedOnlineApp::BeginMain` activates the endpoint at `Code/client/TiltedOnlineApp.cpp:119-121`. |
| SKSEVR loaded the standalone tick plugin. | Current `sksevr.log` says `SkyrimTogetherVRTickBridge.dll ... loaded correctly (handle 5)`. The DLL exports `SKSEPlugin_Query` and `SKSEPlugin_Load`. |
| The ESP is enabled. | Current Proton-prefix `plugins.txt` and `loadorder.txt` include `SkyrimTogether.esp`. |
| HIGGS, PLANCK, VRIK, and their bridge DLLs are live. | Current handoff reports `higgs.loaded=1`, `planck.loaded=1`, VRIK/HIGGS/PLANCK bridge sequences increment continuously. |
| No tick-driven client update has happened. | After several focused minutes, no `SkyrimTogetherVR.status`, pose, player-cell, discovery, or connection readout exists; the fresh connect file remains unconsumed, and server remains at zero players. |

## Current Design

1. The client maps a process-local endpoint before SKSEVR bootstrap, activates it
   after `World::Create()`, and retires it before teardown.
2. `SkyrimTogetherVRTickBridge.dll` registers the normal SKSEVR Papyrus native
   `SkyrimTogetherVRTickBridge.Tick()` in
   `Code/vr_tick_bridge/main.cpp:217-249`.
3. `SkyrimTogetherVerifyLaunchScript` is already attached to the VR ESP quest.
   Its `OnInit` calls `RegisterForSingleUpdate(0.05)` and `OnUpdate` invokes
   the native then schedules the next tick; player-load alias re-arms it.
4. `Tick()` coalesces a single `SKSETaskInterface::AddTask`, validates the
   mapped endpoint/image/thread/page, then calls `TiltedOnlineApp::Update()`.
5. The old VM update/destructor hooks, flat overlay, native binder detours,
   HIGGS callbacks, and worker-thread updates are intentionally absent.

## Verified Failure Boundary

- `[verified]` The native bridge DLL loads, but SKSEVR does not log each native
  registration or call. The bridge only uses `OutputDebugStringA`, not a
  durable per-run log.
- `[verified]` The `SkyrimTogetherVRTickBridge.pex` is present and was compiled
  by the Windows package build.
- `[unknown]` Whether the ESP quest starts before/load game in Skyrim VR.
  The runtime never demonstrates its `OnInit` or `OnUpdate` timer.
- `[unknown]` Whether VR's `RegisterForSingleUpdate` timer works for this
  existing quest instance in the current plugin lifecycle.
- `[unknown]` Whether the normal SKSE Papyrus native registration silently
  rejects the static native or whether the native runs but `AddTask` never
  dispatches under VR.

## Decisions

### 1. Establish a reliable safe cadence

**Current lean:** preserve the proven process-local endpoint and single quest
timer, then add durable diagnostics before considering any new cadence source.

Options:

- A. Fix only the existing quest/ESP lifecycle so `OnInit`/timer is guaranteed.
  This needs correct VR ESP flags/quest lifecycle evidence and potentially an
  ESP edit, but retains the no-new-hook scheduler.
- B. Have the tick plugin subscribe to a normal SKSE message (for example
  `kMessage_DataLoaded`) and use `SKSETaskInterface` for an initial/recurring
  tick. This avoids reliance on the ESP timer, but self-requeue semantics and
  cadence must not be assumed without verification.
- C. Restore old VM/main-loop/destructor or HIGGS callback scheduling. Rejected:
  those paths caused a real access violation or create reentrant update risk.
- D. Call the client update from a worker thread. Rejected: it mutates
  client/world state and violates game-thread ownership.

### 2. What must be proven before re-enabling any cadence

**Current lean:** require explicit durable evidence for: plugin registration,
Papyrus native entry, task enqueue, task execution, callback acceptance/reject,
and a post-tick status write. Fail closed on endpoint mismatch; never restore
the destructor hook as a diagnostic shortcut.

### 3. Scope boundary

Do not re-review or re-enable full gameplay synchronization, remote avatar
application, VRIK skeleton writes, HIGGS/PLANCK mutation, flat CEF/D3D overlay,
or unvalidated Skyrim hooks. The goal is default-package connection plus a
stable Linux VR launch.

## Review Request

Verify the facts against the cited source/runtime files before judging them.
Rank the smallest safe changes that can turn this live failure into an
observable, repeatable connection path. Identify any ABI/lifecycle hazards in
the existing static Papyrus native and `SKSETaskInterface` use. Choose whether
the ESP quest fix, an SKSE lifecycle fallback, or another narrow mechanism is
the right next implementation, and specify testable acceptance evidence.

## Sol Max Review Disposition

Reviewed with `gpt-5.6-sol` at maximum reasoning on 2026-07-12.

| Recommendation | Disposition | Implementation |
| --- | --- | --- |
| Keep one cadence owner: the Start Game Enabled quest timer. | Adopted | No new worker, self-requeue, VM, renderer, or HIGGS scheduler was added. |
| Remove the quest-script `Native` marker. | Adopted | `SkyrimTogetherVerifyLaunchScript` is now a normal hidden quest script; it has no native declarations. |
| Add durable bridge lifecycle evidence. | Adopted | The standalone DLL writes a rate-limited log beside itself for registration, arm markers, Papyrus ticks, task enqueue/run, wrong-thread rejection, and dispatch results. |
| Distinguish quest init/load lifecycle from native failure. | Adopted | Added zero-argument `ArmOnInit` and `ArmOnPlayerLoadGame` SKSE natives, called before timer arm/rearm. |
| Gate generated PEX symbols. | Adopted | `audit_gamefiles.py` now requires both arm markers in source and tick-bridge PEX, and rejects the obsolete quest `Native` declaration. |
| Add an SKSE self-requeue loop. | Rejected | Task queue drain/requeue behavior is not proven for this VR path. |
| Restore VM/destructor/main-loop/renderer/HIGGS cadence or worker-thread updates. | Rejected | Those alternatives carry known crash, reentrancy, or game-thread ownership risk. |

The next package must demonstrate this sequence in
`Data\\SKSE\\Plugins\\SkyrimTogetherVRTickBridge.log`: plugin load,
Papyrus registration callback, `papyrus_arm=OnInit` or
`papyrus_arm=OnPlayerLoadGame`, `papyrus_tick`, task enqueue, task run on the
ready thread, and a successful dispatch. Runtime evidence must then show the
connection command consumed, a `SkyrimTogetherVR.status` file, and one server
player without an access violation.
