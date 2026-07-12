# Skyrim Together VR Verified Tick Bridge: Senior Review Brief

> Superseded for default deployment by the SKSEVR task-backed bridge. The
> Papyrus timer remains the cadence producer, but it queues a one-shot SKSE
> task rather than calling the client endpoint directly from the Papyrus VM.

## Goal and scope

Choose an implementation that supplies `World::Update()` on a known game-thread
callback without relying on flat-Skyrim VM/main-loop offsets. The immediate
goal is a stable **default connection-only** Skyrim VR client that can consume
`SkyrimTogetherVR.command`, write status, and join the configured server.

This is a solo-operator port. The design must work with the supported SKSEVR
2.0.12 stack on Windows and Proton, and must coexist with HIGGS and PLANCK.
It must not enable gameplay synchronization, native client Papyrus bindings,
or flat overlay code.

Do not re-review full gameplay replication, remote avatars, the server, or
controller bindings.

## Verified facts

| Fact | Evidence |
|---|---|
| The inherited VM destructor hook is a live crash path. | `tp_client.log` at 17:28:22 records `VM destructor hook reached` immediately before `0xc0000005`; see `vm-destructor-crash-senior-brief-20260712.md`. |
| Default VR code now excludes VM update/destructor hooks and retains only a forwarding main-loop observer. | `Code/client/SkyrimVM64.cpp` VR branch. The source patch is not built yet. |
| The observer is intentionally not a client tick. | Same file: it logs cadence then forwards only. |
| `VRConnectionService` command polling and all status/transport update processing require `UpdateEvent`. | `Code/client/Services/Generic/VRConnectionService.cpp:86-113`; `World::Update()` triggers this at `Code/client/World.cpp:256-269`. |
| The current VR client has no verified way to call `World::Update`. | `SkyrimVM64.cpp` was the only client update caller in this branch. |
| Existing VRIK/HIGGS/PLANCK SKSEVR bridges are independent shared DLLs loaded by SKSEVR. | `Code/{vrik_bridge,higgs_bridge,planck_bridge}/main.cpp` export `SKSEPlugin_Query`/`SKSEPlugin_Load`; package scripts stage them in `Data/SKSE/Plugins`. |
| Those bridges currently use SKSE messaging and background writer threads, not a client tick callback. | Same bridge sources; their writer loops use `sleep_for`. |
| The client executable is mapped into the launcher process and its PE headers are overwritten with Skyrim VR headers. | `Code/immersive_launcher/loader/ExeLoader.cpp:311-365`; `TargetConfig.h` reserves a 1GB game segment. A normal bridge cannot rely on `GetProcAddress` from the mapped client image. |
| SKSE exposes a task interface with one-shot `AddTask`/`AddUITask`. | Local CommonLibSSE-NG `include/SKSE/Interfaces.h:184-199`, `src/SKSE/Interfaces.cpp:166-198`; official SKSE API header documents the same one-shot task delegate ABI. |
| The local sources do not prove SKSEVR task recurrence/requeue semantics. | No periodic interface exists in supplied headers. A generic self-requeue would be an unverified scheduling assumption. |
| The staged VR ESP already runs `SkyrimTogetherVerifyLaunchScript`, which extends `Quest` and has `OnInit`; the player alias calls it again after load. | `GameFiles/SkyrimVR/scripts/source/SkyrimTogetherVerifyLaunchScript.psc`; `SkyrimTogetherPlayerAliasScript.psc`. |
| Existing client-side custom Papyrus binding is intentionally bypassed in VR after a confirmed constructor crash. | `Code/client/Games/Misc/BSScript.cpp` VR branch and `tp_client.log` original binder records. |

## Candidate design: dedicated SKSEVR Papyrus tick bridge

Create a new generic shared target, `SkyrimTogetherVRTickBridge.dll`, packaged
under `Data/SKSE/Plugins`. It has no HIGGS/PLANCK/VRIK dependency.

1. The mapped client creates a process-local named file mapping before SKSEVR
   plugin load. The mapping contains a versioned, fixed-size endpoint with a
   magic value, process id, epoch, active flag, and an explicitly exported
   `void __cdecl()` callback pointer. The callback only guards against
   reentrancy then calls `g_appInstance->Update()`.
2. The standalone SKSEVR bridge obtains the normal SKSE Papyrus interface and
   registers a `SkyrimTogetherVRTickBridge.Tick()` native function. This is a
   normal SKSE plugin registration; it does **not** construct or register
   BSScript native functions in the mapped client.
3. Extend the already-attached `SkyrimTogetherVerifyLaunchScript` PEX/source:
   schedule `RegisterForSingleUpdate(0.05)` from `OnInit` and after player
   load. `OnUpdate` calls the bridge's `Tick()` and schedules the next update.
4. The bridge opens the process-local mapping, validates magic/version/pid,
   validates that the callback has executable committed memory, calls it, and
   records diagnostic counters. It never dereferences Skyrim game objects.
5. The tick interval is 50ms (20Hz) for the initial connection-only surface.
   This matches existing VR pose service cadence and is not claimed to support
   full movement/combat replication.

## Design constraints and guardrails

- The client callback must never be invoked from a bridge worker thread.
- The bridge must treat missing/invalid endpoint state as a no-op, not crash.
- The shared endpoint must be created before `BootstrapScriptExtenderOnLoaderThread()` and retired before application teardown.
- All callback updates must be serialized with an atomic reentrancy guard.
- The bridge must use only the legacy SKSEVR ABI actually supplied by the
  loaded extender; do not introduce NG runtime-version assumptions.
- `SkyrimTogetherVerifyLaunchScript.pex` must be deployed with this change.
  It is a deliberate exception to the prior install rule that preserved
  user-built PEX files, because the changed lifecycle code cannot work from
  the old bytecode.
- The bridge must coexist with HIGGS and PLANCK by not installing game hooks,
  intercepting their callbacks, or calling either API.

## Open decisions

### 1. Papyrus-timer bridge versus SKSE task self-requeue

**Current lean: Papyrus timer bridge.** It relies on an existing Quest lifecycle
and normal SKSE Papyrus registration, while task recurrence/requeue semantics
are not established. It only schedules a 20Hz connection-only tick.

**Rejected alternative:** a recurring `SKSETaskInterface::AddTask` callback.
The one-shot API is known, but requeue semantics could run immediately or
create unbounded work; a background producer would add a second unverified
thread-affinity assumption.

### 2. Shared callback endpoint versus direct module export

**Current lean: named, versioned shared endpoint.** The mapped executable's
headers are replaced by Skyrim VR headers, so normal PE exports cannot be
relied on after mapping.

**Rejected alternative:** `GetModuleHandle`/`GetProcAddress` from the bridge.
No stable mapped-client export surface exists.

### 3. Existing verification quest versus a new ESP record

**Current lean: extend the existing attached verification quest script.** It
already receives `OnInit` and a player-load callback, avoiding new ESP record
creation. The changed PEX must be explicitly installed.

**Rejected alternative:** attach a new startup quest/script. It broadens ESP
editing and increases package/test surface without improving the callback
contract.

### 4. 20Hz initial cadence

**Current lean: 20Hz.** It limits new bridge work and is sufficient to validate
connection/state/telemetry plumbing. Full gameplay sync remains disabled.

**Rejected alternative:** frame-rate tick. It is premature and risks Papyrus
queue pressure before the connection-only path is validated.

## Request to reviewer

Verify these facts against the cited code. Then return:

1. A ranked critique of the four decisions and a minimum patch architecture.
2. Any ABI, mapped-image lifetime, Papyrus registration, PEX deployment, or
   Proton-specific failure mode that invalidates the plan.
3. Exact safety gates and runtime breadcrumbs required before the one Windows
   build.
4. The minimum user-run validation sequence for this bridge and server join.

Do not edit files. Keep the review under 1,500 words; distinguish verified
facts from inferences and name any required human decision.
