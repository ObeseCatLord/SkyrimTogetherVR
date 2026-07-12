# Senior Brief: HIGGS Tick Bridge

## Goal

Restore the default Skyrim Together VR connection-only client to a usable,
low-risk game-thread update path. This must not reintroduce the delayed VM
destructor crash, interfere with HIGGS or PLANCK, or require a new Papyrus
native/PEX control path. This is a solo-operator VR port, not an enterprise
integration layer.

## Scope and non-goals

- Scope: one process-local, fail-closed update callback from the existing
  `SkyrimTogetherVRHiggsBridge` SKSEVR plugin into the already-loaded client.
- Scope: default connection-only `World::Update()` at a conservative rate,
  enough for the transport/autoconnect path to operate.
- Non-goal: enable the existing gameplay hooks, remote actor synchronization,
  flat overlay, DInput hooks, renderer hooks, or VM hooks.
- Non-goal: modify HIGGS, PLANCK, VRIK, SKSEVR, the server, or Papyrus source.
- Non-goal: prove remote-avatar/gameplay synchronization. That remains a later
  validation lane after connection-only stability is established.

## Verified facts

| Claim | Evidence |
| --- | --- |
| The previous default VR VM destructor detour is unsafe. | A live fresh run reached the client startup and renderer breadcrumbs, then logged `SkyrimTogetherVR VM destructor hook reached` before an access violation at `0x140709f88`. The crash no longer occurred at native Papyrus registration after the original binder passthrough was added. |
| The current source removes VR VM-update and VM-destructor hooks. | `Code/client/SkyrimVM64.cpp` has a `TP_SKYRIM_VR` branch that installs only an observer for address ID 36564 and returns directly to the original function. `audit_bringup_hooks.py` rejects VM hook symbols and `g_appInstance->Update()` in that branch. |
| The default VR client has no tick after those VM hooks are removed. | `Code/client/TiltedOnlineApp.cpp:129-134` calls `World::Get().Update()` only through `TiltedOnlineApp::Update()`. `connection-only-mode.md` records that transport is constructed but cannot poll/authenticate until a verified tick dispatches it. |
| HIGGS bridge already obtains HIGGS API v1 and registers a post-update callback. | `Code/higgs_bridge/main.cpp:613-616` obtains API v1, `:425-440` registers `OnPostVrikPostHiggs`, and `:420-423` currently captures its snapshot from that callback. |
| HIGGS calls that callback after its normal VRIK/HIGGS update work. | Local reference `/home/obesecatlord/Documents/SkyrimModding/_refs/higgs/src/hooks.cpp:527-547`: `PostVRIKPCUpdateHook` triggers post-VRIK/pre-HIGGS, performs finger work, calls the normal `NiAVObject_UpdateNode`, invokes hand post-update, then triggers post-VRIK/post-HIGGS callbacks. |
| HIGGS bridge is an existing, packaged SKSEVR prerequisite for this port. | `Code/higgs_bridge/xmake.lua` defines `SkyrimTogetherVRHiggsBridge`; `Tools/SkyrimVR/audit_built_package.py` requires `Data/SKSE/Plugins/SkyrimTogetherVRHiggsBridge.dll` and checks installed HIGGS prerequisites. The target test environment already intentionally includes HIGGS and PLANCK. |
| Client lifecycle can prepare the endpoint before SKSE and activate it after `World` exists. | Launcher calls `RunTiltedInit` before `BootstrapScriptExtenderOnLoaderThread`; `RunTiltedApp` calls `BeginMain`; `TiltedOnlineApp::BeginMain` first calls `World::Create()` at `TiltedOnlineApp.cpp:69-72`. |

## Unverified assumptions to audit

- The HIGGS post-VRIK/post-HIGGS callback always runs on the same game thread
  that is valid for `World::Update()`.
- `World::Update()` does not reenter an engine/HIGGS operation that makes this
  phase unsafe. The default connection-only implementation should be limited to
  20 Hz at first and must have a single-entry guard.
- An inherited duplicated process handle remains valid to the later-loaded
  SKSEVR bridge DLL. This is Windows process-local inheritance, not cross-process
  IPC and must fail closed if parsing/mapping/validation fails.

## Proposed design

### Client endpoint

Add a small Windows-only `VRTickBridge` client module.

1. During `RunTiltedInit`, after the client is created but before SKSE bootstrap,
   allocate one anonymous pagefile mapping with `CreateFileMappingW` and map it
   locally. Publish its numeric process-local handle through
   `STVR_TICK_BRIDGE_HANDLE` with `SetEnvironmentVariableW`.
2. Write a fixed-layout `VRTickBridgeEndpoint` shared header, without shared
   `std::atomic` objects. Fields include magic, ABI version, size, process ID,
   epoch, main-image base, callback pointer, expected thread ID, active flag,
   and fixed-width `volatile LONG64` counters read/written with `Interlocked*`.
3. After `World::Create()` succeeds in `TiltedOnlineApp::BeginMain`, capture
   `GetCurrentThreadId`, publish a callback `VRTickBridge::Tick`, and atomically
   set `active=1`. The callback confirms active/world-ready, confirms its thread
   ID, uses an interlocked single-entry guard, invokes `g_appInstance->Update()`,
   records a counter, then clears the guard. It never calls HIGGS/PLANCK APIs.
4. The mapping and function target remain alive for process lifetime. No unload
   or teardown callback is assumed; failure leaves the endpoint inactive.

### HIGGS bridge dispatch

Extend only `Code/higgs_bridge/main.cpp`.

1. On its existing `OnPostVrikPostHiggs` callback, first retain the current
   `CaptureHiggsSnapshot()` behavior.
2. Map and strictly validate the endpoint once. Reject magic/version/size/PID,
   non-current image base, zero/inactive callback, mismatched current thread,
   non-executable callback memory, guarded/no-access pages, or callback memory
   whose `VirtualQuery` allocation base differs from the main executable image.
3. At a 50 ms minimum cadence using `GetTickCount64`, call the callback directly.
   There is no queue, worker thread, Papyrus invocation, or HIGGS/PLANCK API call
   from the dispatched client update. Increment only diagnostic counters on
   rejected or throttled calls.
4. Log endpoint activation and first successful dispatch once. On every failure
   category, fail closed and rate-limit logging; do not retry arbitrary pointers.

## Alternatives considered

| Alternative | Status | Reason |
| --- | --- | --- |
| Re-enable inherited VR VM update/destructor detours | Rejected | A live crash occurred in the destructor detour and address-library numeric mapping does not prove ABI/semantic identity. |
| Use the observed main-loop detour as a tick | Rejected for now | It is only a cadence observation; its call contract is not verified. |
| New Papyrus timer plus custom native and SKSE task bridge | Rejected pending strong evidence | It requires new script/PEX packaging and adds unproven task-queue threading/reentrancy behavior. |
| Dedicated new SKSEVR tick plugin | Rejected | Existing HIGGS bridge already owns an authenticated late game-update callback and keeps the package smaller. |
| No tick at all | Rejected | The connection-only client cannot poll the transport, so a user cannot connect. |

## Decisions for review

1. Is post-VRIK/post-HIGGS a sufficiently safe phase to invoke only the
   connection-only `World::Update()` at 20 Hz, given that callback implementation
   and HIGGS/PLANCK role? If not, identify a concrete existing callback/path in
   this workspace that is safer; do not propose an unverified address detour.
2. Is the process-local mapping/pointer validation adequate and minimal? Call out
   missing lifecycle, lifetime, ABI, or security checks.
3. Should the callback activate in `BeginMain` after `World::Create`, and what
   exact condition prevents update before the client is ready?
4. What static audit and one-user-run log gates should block package deployment?
5. Identify any reason this could affect controller input, HIGGS grabbing, or
   PLANCK ragdolls. The bridge must remain observational with respect to their
   APIs.

## Requested output

Verify the cited source first, then return a prioritized critique of the
proposal. State a single recommended minimal design, explicit reject/accept
criteria for the HIGGS callback, precise endpoint fields/checks, and runtime log
gates. Do not edit files, re-review the unrelated full port, propose a broad
CommonLib migration, or recommend enabling gameplay hooks.

## Sol xhigh disposition

Reviewed 2026-07-12. Rejected as the default driver: the HIGGS callback is
synchronous inside `PlayerCharacter::Update`, and connection-only
`World::Update()` drains queued work and all update subscribers. The adopted
design keeps HIGGS/PLANCK observational and moves cadence to the existing quest
timer plus a coalesced, one-shot `SKSETaskInterface` callback in the standalone
`SkyrimTogetherVRTickBridge` plugin. The endpoint remains process-local and
validates epoch, ready thread, allocation base, page protection, and reentrancy;
it becomes ready only after `BeginMain` completes and retires before `EndMain`.
