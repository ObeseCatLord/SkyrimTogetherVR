# Skyrim Together VR startup lifecycle senior brief

## Goal and scale

Define the correct, upstream-compatible flow from Skyrim VR launch through New
Game, RaceSex completion, player readiness, client connection, and recurring
updates. The result must work on Windows and Proton and must not depend on
DevBench or host automation in a distributed install. This is a solo-maintained
port; avoid broad framework work.

The immediate question is architectural: should the client run while RaceSex is
open, or should it preserve upstream's post-character gameplay boundary and make
the automation complete/bypass RaceSex correctly? Review both connection-only
bring-up and the future gameplay path, but do not re-review address aliases,
HIGGS/PLANCK/FBT pose protocols, controller bindings, or server deployment.

## Verified evidence

| Claim | Status and evidence |
| --- | --- |
| Upstream drives every client update from the game's VM update hook. | `[verified: original branch source]` `original-skyrim-together:Code/client/SkyrimVM64.cpp` detours address ID `53926`; `HookVMUpdate` calls `g_appInstance->Update()` only when `VMContext::inactive == 0`, then forwards to the game. |
| Upstream performs no RaceSex automation or explicit character-ready gate. | `[verified: original branch search/source]` startup creates the client and hooks; connection comes from the overlay. `TransportService::OnConnected` immediately dereferences `PlayerCharacter`, base NPC, parent cell, level, and calendar state. Correctly generated characters are an explicit assumption in `TiltedOnlineApp::Update`. |
| Upstream therefore assumes the user connects after normal character creation/load. | `[verified inference from the two facts above]` There is no safe pre-character authentication path; connecting while player state is transient would violate upstream's own dereference assumptions. |
| Main cannot reuse the full upstream VM hook set unchanged. | `[verified: retained live crash]` the inherited VR VM-destructor detour crashed at `0x140709f88`; the `VMContext` VR layout at `0x680` was never established. The default VR branch now retains only a forwarding outer-main-loop observer. |
| The outer-main-loop observer is not a frame cadence. | `[verified: live logs]` address ID `36564` entered only twice over minutes and rotated threads; it wraps a long-running outer loop. |
| Main currently uses Papyrus timer -> `SkyrimTogetherVRTickBridge.Tick()` -> coalesced `SKSETaskInterface::AddTask` -> process-local endpoint -> `TiltedOnlineApp::Update()`. | `[verified: main source]` the quest/migration PSC files, `Code/vr_tick_bridge/main.cpp`, and `Code/client/VRTickBridge.cpp`. |
| The SKSE task callback is not stably thread-owned. | `[verified: live bridge log]` callbacks in one process rotate among Windows thread IDs; this was already documented in `connection-thread-ownership-senior-brief-20260714.md`. The network transport now has a dedicated owner, but game-facing discovery still runs in these callbacks. |
| Papyrus cadence runs before New Game and stops while RaceSex pauses the VM. | `[verified: live timestamps and menu state]` sequences reached 145 before the renderer/New Game transition, then stopped while DevBench reported `RaceSex Menu`; direct DevBench calls to native `Tick()` resumed task dispatch. |
| The player was not actually uninitialized while RaceSex remained visible. | `[verified: live DevBench and status]` player `Shezarrine`, Nord, form `0x14`, cell `RealmLorkhan`, readiness `1`, online player ID `1`, and first interior-cell request were all observed. This proves enough state for the connection-only smoke lane, not completion of the engine's RaceSex transaction. |
| Generic RaceSex `kHide` is invalid. | `[verified: two live reproductions plus API shape]` DevBench `menu close` queues only `UI_MESSAGE_TYPE::kHide`. Each attempt caused a confirmation `MessageBoxMenu` and then access violation `0xc0000005` at game RVA `0x131aab` on the task callback thread. Current dump: `crash_UTC_2026-07-14_21-34-42.dmp`. CommonLib exposes `ConfirmAndNameCallback`, `ConfirmCloseRaceSexMenuCallback`, and `RaceSexMenu::ChangeName`; completion is callback/name-driven, not generic hiding. |
| The repeated fault lies in Skyrim VR's ExtraDataList region. | `[verified: address-library neighborhood]` RVA `0x131aab` is between mapped `ExtraDataList` routines. The exception context attempted an invalid read; static game bytes are Steam-encrypted, so the exact nested routine is not proven. |
| Natural Papyrus ticks resumed immediately after the invalid hide even after the external pump was stopped. | `[verified: service state plus logs]` after stopping `stvr-devbench-tick-pump.service`, `kHide` caused task callbacks on IDs 604/320 immediately before the crash. Thus “cadence never resumes” is disproven for this transition. |
| The VR connection gate is too weak for upstream parity. | `[verified: main source]` `VRConnectionService::IsVrPlayerReadyForConnection()` requires only readable player, base form, and parent cell. It does not require RaceSex closed or a stable post-load epoch. `DiscoveryService::OnUpdate()` reads world/cell/location every callback regardless of menu transition. |
| The live client did reach the remote server before the invalid close. | `[verified: local and remote evidence]` local state was online with player ID `1`, generation `1`; the server admitted `Skyrim VR Player` with the expected mod list. This validates transport/auth, not a playable startup flow. |

## Environment facts

| Item | Fact |
| --- | --- |
| Branches | `original-skyrim-together` is the upstream snapshot; `main` is the VR port. |
| Runtime | Skyrim VR 1.4.15, SKSEVR 2.0.12, GE-Proton, Monado/XRizer; Windows must also work. |
| Default build | Connection-only, with discovery and player-cell lane enabled; gameplay hooks disabled. |
| Automation | DevBench 1.9.1 REST plus a Monado Qwerty simulated headset. XRizer does not present Skyrim VR's SteamVR naming keyboard in this setup. |
| Build path | WinBoat Windows build script is available; rebuilds are expensive. |
| Current process | Crashed after the second invalid RaceSex hide; no live game must be preserved. |

## Open decisions and current lean

### 1. Define the startup/readiness boundary

**Lean:** preserve upstream semantics. Do not connect or run game-facing discovery
until character creation/load is complete, `RaceSex Menu` is closed through its
real transaction, a parent cell is stable across multiple gameplay ticks, and no
loading/fader transition is active. A prewritten command remains pending.

Rejected alternative: treat a named player and cell under an open RaceSex menu as
ready. It was enough to authenticate but exposed transient ExtraDataList reads and
is not upstream-compatible.

### 2. Choose the recurring update owner

**Lean:** do not make Papyrus run through RaceSex. Pausing there matches upstream's
`VMContext::inactive` gate. Keep transport ownership separate. For game-facing
updates, either validate a minimal VR form of address `53926` that does not read the
unverified `VMContext` layout, or add a proven VR gameplay/update event source. The
current rotating SKSE task executor may remain only for file/network work that does
not touch mutable engine objects.

Rejected alternatives: a native worker-thread heartbeat (wrong engine affinity),
the outer main loop (not a frame tick), and HIGGS/PLANCK callbacks (reentrant inside
other mods' player update work).

### 3. Complete automated New Game correctly

**Lean:** remove `kHide` and the `--allow-finalized-racesex` claim. For the simulated
headset, extend DevBench with a narrowly scoped RaceSex action that invokes the
engine's active confirmation callback and `RaceSexMenu::ChangeName` on its normal
task context, with runtime/menu validation. If that cannot be proven without a new
VR offset, create a fresh deterministic post-character save once and use it only as
the automated connection fixture. Normal users continue through the vanilla UI.

Rejected alternative: generic UI hide. It is conclusively unsafe and does not
perform the name/finalization transaction.

### 4. Contain transient player reads

**Lean:** make connection-only `World::Update` two-lane: transport/file command work
may continue on its current owner, while `DiscoveryService`/player-cell sampling is
disabled until the post-character readiness state is stable. Do not use broad SEH
to continue after engine access violations.

Potential overlap: decisions 1, 2, and 4 may reduce to one rule: upstream updates
game state only during an active gameplay VM phase. Merge them if the reviewed
implementation can encode that rule directly.

## Requested Sol Max review

Verify the original and main branch source before critiquing. Re-rank the decisions
and return one correct end-to-end flow, including:

1. What upstream actually assumes before connection and whether VR should ever tick
   through RaceSex.
2. The safest distributable update owner for transport-only work versus mutable
   game-state work, with exact source symbols to change.
3. The correct automation strategy for XRizer's missing naming keyboard, including
   whether `RaceSexMenu::ChangeName` is sufficient or must be paired with a specific
   callback/event.
4. Gates that prevent command consumption and discovery during load/RaceSex/player
   teardown, plus teardown/reconnect behavior.
5. A one-rebuild implementation and validation plan that does not merely move the
   same crash later.

Keep the response under 1,800 words. Do not edit files. Do not broaden into gameplay
offset translation, avatar replication, controller configuration, or server setup.
