# Skyrim Together VR Transport Thread-Ownership Senior Brief

## Goal and scale

Choose the smallest correct runtime architecture that lets the Skyrim VR client
connect without blocking Skyrim's task executor, while preserving a path to
full gameplay synchronization. This is a solo-maintained port, but the result
must be distributable on Windows and Proton and must not depend on this machine.

The requested review is read-only. It must verify the evidence before choosing
a design. Do not re-review address aliases, the fixed authentication game reads,
server deployment, controller bindings, or character creation.

## Verified runtime evidence

| Claim | Status and evidence |
| --- | --- |
| Character creation completed and a usable player exists. | **[verified: runtime handoff]** `SkyrimTogetherVR.playercell` reported `ready=1`, player form `0x14`, a nonzero session, Nord level 1, and Realm of Lorkhan cell `0x0801A5C6`. |
| The remote server is healthy and empty. | **[verified: SSH 2026-07-14]** exactly one `skyrim-together-vr` Docker container is Up; it reports port 10578 and zero players. Host DNS resolves to the expected server. |
| The previous connection-time crash is fixed. | **[verified: live process and log]** the client remains alive after issuing connect; no vectored exception or dump follows. The VR auth path no longer reads player level/time/cell. |
| The command is consumed but no connection callback/authentication occurs. | **[verified: `logs/tp_client.log`]** it logs `queueing connection to incidentalstoat.xyz:10578`, then only two normal task ticks. It never logs the DNS result, `OnConnected`, or `VR authentication snapshot ready`. Status remains `state=connecting`, `online=0`, generation 0. |
| One world-update callback blocks after connect. | **[verified: bridge cadence]** before connect, `task_run` advances about 74 sequences per five seconds. After sequence 1227, only sequence 1229 appears roughly 150 seconds later; client log output and status stop at the connect attempt. Papyrus continued enqueueing before the blocked callback. |
| Tick callbacks do not have a stable owner thread. | **[verified: logs and source]** successful callbacks rotate across Windows thread IDs 560, 564, 568, 572, 576, and 580. `ClientUpdateTask::Run` directly invokes the launcher callback at `Code/vr_tick_bridge/main.cpp:273-305`; that callback calls `g_appInstance->Update()` at `Code/client/VRTickBridge.cpp:168-170`. |
| The client networking implementation assumes mutable single-owner state but does not enforce it. | **[verified: source]** `Client::Update` mutates clock/statistics, installs a `thread_local` callback owner, calls `RunCallbacks`, calls `uv_run`, then drains the connection at `Libraries/TiltedConnect/Code/connect/src/Client.cpp:168-202`. `Connect(string)` creates a libuv request on the same loop at lines 65-130. No lock or owner-thread assertion exists. |
| The main-loop observer is not a frame tick. | **[verified: runtime/source]** address ID 36564 entered only twice over several minutes; its hook wraps a long-running outer loop at `Code/client/SkyrimVM64.cpp:23-39`. It cannot drive per-frame updates. |
| Prior VM hooks are unsafe. | **[verified: retained crash evidence]** inherited VR VM-destructor detouring crashed. The VM update object's ABI/layout remains unverified. Static audits intentionally forbid those hooks in the default VR branch. |
| Direct HIGGS callback dispatch was previously rejected. | **[verified: prior Sol disposition]** HIGGS invokes callbacks synchronously inside player update; running the entire dispatcher there risks reentrant engine work and interference with HIGGS/PLANCK. |

## Environment facts

| Item | Fact |
| --- | --- |
| Runtime | Skyrim VR 1.4.15, SKSEVR 2.0.12, Windows binary under GE-Proton 10-34; Windows must also work. |
| Network library | TiltedConnect wraps standalone GameNetworkingSockets plus libuv DNS. Server uses the same protocol. |
| Current cadence | Papyrus timer -> standalone SKSE plugin -> coalesced `SKSETaskInterface::AddTask` -> process-local validated callback -> `World::Update`. |
| Current service mode | Connection-only. Transport, connection command, player-cell network lane, discovery, Discord/string cache are active; gameplay services are compile-time disabled. |
| Compatibility | HIGGS and PLANCK are loaded. VRIK and full-body tracking must remain possible. No solution may hook another mod's private offsets. |
| Build cost | A Windows/WinBoat build is available but each runtime iteration is expensive, so add enough diagnostics and structural tests for one decisive rebuild. |

## Open decisions

### 1. How should transport acquire a stable owner?

**Current lean:** move low-level `Client` DNS/GameNetworkingSockets pumping to a
dedicated, joinable transport thread. That thread alone owns the libuv loop and
network interface. It enqueues received byte buffers and connection-state
records; the existing world update drains those records and performs dispatcher
callbacks on the current Skyrim task context. Outgoing serialized packets go
through a thread-safe queue. Do not invoke gameplay dispatchers from the network
thread.

Alternative A, pin all `World::Update` work to the HIGGS callback, is rejected
because it is synchronous inside player/HIGGS update and was already reviewed
as unsafe for arbitrary dispatcher work. Alternative B, restore inherited VM
hooks, is rejected because ABI identity is unproven and one hook already
crashed. Alternative C, put a mutex around rotating task callbacks, is rejected
because serialization does not establish libuv or callback thread ownership.

### 2. Can this be fixed more narrowly inside TiltedConnect?

**Current lean:** if GameNetworkingSockets itself is demonstrably safe to pump
from rotating threads and only libuv DNS has affinity trouble, resolve hostnames
outside the runtime or use numeric-IP parsing before connect. However, the
distributed client must support hostnames, reconnect, close/cancel, and Windows;
hard-coding the current IP is rejected.

The reviewer should determine whether the 150-second behavior points more
strongly to `uv_run`, `ConnectByIPAddress`, `RunCallbacks`, or another call, and
whether one rebuild should first contain only bounded before/after breadcrumbs.

### 3. Where should game-facing message dispatch occur long-term?

**Current lean:** preserve a two-phase model: network I/O on its owner thread,
then bounded inbound/outbound queue draining during a game-safe tick. The
current SKSE task path is adequate only for connection-only code that performs
no arbitrary engine calls; gameplay enablement still requires a verified VR
game-thread update source.

Alternative, let the transport thread trigger EnTT and game services directly,
is rejected because those services assume game-thread engine access and EnTT
dispatcher mutation is not synchronized.

## Suspected overlap

Decisions 1 and 3 may be one ownership boundary: a correct transport actor plus
a game-thread mailbox. Merge them if that produces a smaller design. A purely
diagnostic rebuild and the final architecture are separate decisions; prefer a
single rebuild only if the evidence supports an implementation with explicit
stage breadcrumbs and timeouts.

## Required review output

1. Verify the cited source and challenge any incorrect inference.
2. Rank the likely blocking call and explain the 150-second cadence.
3. Recommend one minimal architecture, including thread/lifetime ownership,
   connect/close/reconnect ordering, callback routing, queue bounds, and teardown.
4. Name exact files/symbols to change and tests/audits/runtime breadcrumbs that
   should gate deployment.
5. Identify any design that works for connection-only but creates a dead end for
   gameplay, HIGGS/PLANCK/VRIK, or full-body pose networking.

Depth budget: approximately 1,800 words. Do not edit files, browse broadly,
re-propose known-unsafe VM offsets, or analyze unrelated gameplay services.
