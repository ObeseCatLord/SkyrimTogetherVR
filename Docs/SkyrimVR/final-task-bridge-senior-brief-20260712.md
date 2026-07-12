# Final SKSEVR Task Bridge Review Brief

## Goal

Approve or correct the default SkyrimTogetherVR scheduler before the next
Windows build. This is a solo-operator VR bring-up package, not a claim that
full Skyrim Together gameplay sync is ready.

The required outcome is a connection-only client update path that does not
reuse unvalidated Skyrim SE VM, main-loop, BSScript, HIGGS, PLANCK, renderer,
or input hooks.

## Verified Facts

| Claim | Evidence |
| --- | --- |
| The prior VR VM destructor hook caused an access violation after its original call. | Runtime log and `Docs/SkyrimVR/vm-destructor-crash-senior-brief-20260712.md`. |
| The VR main-loop address is now observation-only. | `Code/client/SkyrimVM64.cpp`; no VR `g_appInstance->Update()` call remains there. |
| The flat-client BSScript custom-native constructor ABI is unvalidated in VR. | `Code/client/Games/Misc/BSScript.cpp` previously bypassed custom registration; now its VR branch installs no BSScript detours. |
| Official SKSEVR 2.0.12 exposes `SKSETaskInterface::AddTask` and normal Papyrus registration. | Pinned source archive is fetched by `BuildSkyrimTogetherVR-Windows.ps1`; the task bridge uses its `PluginAPI.h`, `PapyrusNativeFunctions.h`, and `gamethreads.h`. |
| HIGGS post-update callbacks are unsafe as a general client scheduler. | Prior Sol review recorded in `Docs/SkyrimVR/higgs-tick-bridge-senior-brief-20260712.md`: callback occurs inside player update and could reenter engine/HIGGS/PLANCK work. |
| `World::Update()` is broad. | It drains runner work and updates service subscribers, including transport/file handoff. |

## Implemented Candidate

1. `Code/client/VRTickBridge.cpp` creates an unnamed in-process mapping before
   SKSEVR bootstrap and publishes its handle through `STVR_TICK_BRIDGE_HANDLE`.
   The POD endpoint has magic/version/size/PID/epoch/launcher-image-base/callback
   RVA/ready game-thread id/state and zeroed reserved fields.
2. At the end of `TiltedOnlineApp::BeginMain`, it marks the endpoint ready on
   the client start thread; `EndMain` retires it before teardown.
3. `SkyrimTogetherVRTickBridge.dll` loads through normal SKSEVR, registers only
   `SkyrimTogetherVRTickBridge.Tick()` using the legacy official SKSEVR SDK, and
   receives a 50 ms one-shot Papyrus timer request from the quest script.
4. The native coalesces requests into one `SKSETaskInterface::AddTask` task. It
   maps the process-local endpoint, checks immutable endpoint fields, reserved
   fields, state, PID, epoch, image base, expected thread, overflow, committed
   allocation base, and read-only executable callback page before dispatch.
5. The callback checks state/epoch/thread/reentrancy/client existence and calls
   `g_appInstance->Update()` once. It logs first success and warns at >10 ms.
6. The default VR client no longer installs VM update/destructor, BSScript,
   HIGGS, PLANCK, renderer-scheduling, or input scheduling hooks. The old
   menu spell is deliberately not granted, because its flat-client native
   functions are not registered.

## Open Decision

Current lean: keep this task bridge as the default scheduler and build it.

Alternatives considered and rejected:

- Direct HIGGS callback: rejected because it nests within `PlayerCharacter`
  update and can reenter broad client work.
- Main-loop/VM destructor/VM update hook: rejected due to the observed crash or
  unvalidated VR address/function ABI.
- Worker thread: rejected because it cannot safely mutate game/client state.
- Task self-requeue: rejected in favor of a Papyrus single-update timer plus
  coalesced one-shot task to reduce lifecycle pressure.

## Review Scope

Read only. Verify the candidate in these files:

- `Code/vr_common/VRTickBridge.h`
- `Code/client/VRTickBridge.{h,cpp}`
- `Code/vr_tick_bridge/main.cpp`, `Code/vr_tick_bridge/xmake.lua`
- `Code/client/main.cpp`, `Code/client/TiltedOnlineApp.cpp`,
  `Code/client/SkyrimVM64.cpp`, `Code/client/Games/Misc/BSScript.cpp`
- `GameFiles/SkyrimVR/scripts/source/SkyrimTogetherVerifyLaunchScript.psc`,
  `GameFiles/SkyrimVR/scripts/source/SkyrimTogetherPlayerAliasScript.psc`,
  `GameFiles/SkyrimVR/scripts/source/SkyrimTogetherVRTickBridge.psc`
- `BuildSkyrimTogetherVR-Windows.ps1`

Do not re-review full gameplay sync, avatar implementation, HIGGS/PLANCK
observation bridges, the server protocol, or unrelated dirty artifacts. Do not
edit files. Report at most 10 prioritized findings with file/line evidence,
then an explicit go/no-go for a Windows compile. Focus on ABI, lifecycle,
threading/reentrancy, SKSE interface correctness, build/package completeness,
and regressions caused by disabling the old BSScript hook.

## Senior Review Disposition

| Finding | Disposition |
| --- | --- |
| Missing legacy SDK prefix include. | Adopted: the xmake target now force-includes `common/IPrefix.h`, adds the SDK source root, and mirrors the required VR preprocessor definitions. |
| Missing primitive Papyrus argument definitions. | Adopted: the bridge defines only the required `bool` `PackValue`/`GetTypeID` specializations instead of linking the broad `PapyrusArgs.cpp` unit and unrelated globals. |
| Missing runtime gate. | Adopted: query/load now require Skyrim VR 1.4.15, SKSEVR 2.0.12-or-later version/release index, and task/Papyrus interface versions. |
| Unverified supplied SDK root. | Adopted: the build validates bridge-critical SDK source hashes for explicit, environment, cache, and downloaded roots. |
| Skippable matching PEX. | Adopted: package creation rejects `-SkipPapyrusCompile` when the tick bridge is selected. |
| 20 Hz retry after a permanent bridge fault. | Adopted: Papyrus falls back to a one-second retry after `Tick()` returns false. |
