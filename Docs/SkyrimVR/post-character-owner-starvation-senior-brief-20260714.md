# Skyrim Together VR Post-Character Owner Starvation Senior Brief

## Goal And Scope

Decide the smallest correct update-dispatch architecture that lets the default
connection-only Skyrim Together VR client advance after character creation
without running game-memory or networking work on rotating Skyrim worker
threads. This is a single-user port under active runtime validation, not an
enterprise redesign. A wrong thread choice has already produced access
violations, so preserving engine thread ownership is more important than making
the current smoke test pass quickly.

Review only the default connection-only update path, lifecycle gate, tick bridge,
and evidence needed to choose its execution owner. Do not re-review the already
fixed UI singleton address contract, full gameplay synchronization, avatar/FBT
relay, packaging, server configuration, or controller automation except where
they constrain this decision.

## Verified Evidence

| Claim | Status | Evidence |
|---|---|---|
| The reviewed source and deployed package are commit `d16b8811f98c1b3f7ecb5e4be437264178f87c14`. | `[verified: git, manifest, SHA-256]` | Repository HEAD, package manifest, installed manifest, launcher hash, and alias CSV hash agree. |
| The Windows native build and package audit passed. | `[verified: WinBoat build output]` | Default client, four bridges, launcher, EarlyLoad, and TPProcess built successfully. Package audit found zero failures with 78 alias CSV rows and 2968 override CSV rows. |
| The Windows C++ tests passed. | `[verified: executed TPTests.exe]` | 184 assertions in 7 test cases, including binary VersionDb override/alias precedence and reverse-map cleanup. |
| The installed target passed the strict post-install package audit. | `[verified: local audit]` | SKSEVR, VR Address Library, VRIK, HIGGS, PLANCK, package hashes, and all eight rebuilt Papyrus hashes passed. |
| Exactly one remote server container is running and healthy on UDP 26099. | `[verified: remote Docker inspect/log]` | `skyrim-together-vr`, running with zero restarts; a prior build was admitted with the same ten-mod VR load order. |
| The current run passed the former UI singleton crash point. | `[verified: current runtime log and dump directory]` | Active owner thread 320 reached lifecycle `main_menu`, then automation selected New Game, completed the real RaceSex confirm/name transaction, accepted Realm of Lorkhan `Begin`, and reached `RealmLorkhan`. No dump newer than the old `0x140F43AD1` dump was created. |
| The current character is finalized and the simulation is advancing. | `[verified: live DevBench REST]` | Only `HUD Menu` is open; message box is closed; player is loaded in cell `RealmLorkhan`; game hour advanced across repeated reads. |
| The current lifecycle never advances after the main menu. | `[verified: live handoff file and automation timeout]` | `SkyrimTogetherVR.lifecycle` remains `state=suspended`, `reason=main_menu`, epoch 2, owner 320, zero player/base/cell IDs. Automation timed out before writing a connect command. |
| The SKSE/Papyrus bridge remains active after character creation. | `[verified: live tick log]` | Papyrus ticks and task sequences advance continuously beyond 700; task callbacks execute on changing worker IDs including 588, 596, 600, 604, and 608. |
| The hooked VM method reaches activation thread 320 in the main menu but not after the transition observed so far. | `[verified: current client log]` | Worker calls 608/596 were ignored, owner 320 logged calls 1 and 2, and lifecycle recorded `main_menu`. No later owner cadence or lifecycle update appeared while worker bridge activity continued. |
| Current source intentionally allows only activation thread 320 to consume an update permit. | `[verified: source]` | `Code/client/SkyrimVM64.cpp:75-108` compares each hook call with `TickBridge::GetActivationThreadId()` and calls `g_appInstance->Update()` only on equality plus a permit. |
| A permit is produced from rotating SKSE task workers. | `[verified: source and runtime]` | `Code/client/VRTickBridge.cpp:166-205` validates each task sequence and atomically sets `s_updatePermit=1`; task logs prove worker rotation. |
| `World::Update()` reads lifecycle/game state and advances networking. | `[verified: source]` | `Code/client/World.cpp:268-289` runs `VRLifecycleService::Update`, lifecycle boundary handling, runner, and update dispatch. It is not safe to invoke concurrently. |
| The original desktop client calls `World::Update()` from the game VM update hook after checking the desktop VM inactive field. | `[verified: source]` | Non-VR `SkyrimVM64.cpp:179-184`. The VR VM context layout is deliberately opaque and must remain so. |
| Binding to the first observed VR VM caller was already disproved. | `[verified: historical runtime and commit history]` | Startup workers call the VM virtual before the activation/game thread; the earlier first-caller design detected thread changes and disabled active dispatch. Commit `8b5b6cf5` changed ownership to activation thread. |
| Calling `World::Update()` while the main menu was open previously exposed the bad `UI::Get` address, now fixed. | `[verified: minidump/PDB analysis and current run]` | Old dump faulted at `SkyrimVR.exe+0xF43AD1` through `UI::GetMenuOpen`; current run passes this point using exact CommonLib VR ID 514178 and pointer guards. |

## Similar-Crash Audit Context

The historical recurring game RVA `0x131AAB` is not an unexplained recurrence:
it was reproduced by invalid generic `RaceSex Menu` `kHide` automation and is
prevented by the current real controller plus overlay-keyboard transaction. The
older `0xC53E1B`, `0xAB4F26`, and `0x133B008` signatures all predate the current
address-contract build. No access violation or new dump has occurred in the
current `d16b8811` run. A separate static audit is checking remaining singleton
IDs and unguarded direct-pointer consumers.

## Environment Facts

| Item | Value |
|---|---|
| Game | Skyrim VR 1.4.15 |
| Script extender | SKSEVR 2.0.12 |
| Runtime | Proton GE 10-34, XRizer, Monado Qwerty simulated HMD/controllers |
| Client package | default connection-only, `releasedbg` |
| Active client update mode | `STVR_VM_UPDATE_MODE=active` |
| Activation/game startup thread | Windows TID 320 |
| Current safe menus | only HUD open |
| Current cell | Realm of Lorkhan (`0x0801A5C6`) |
| Server | one Docker container, UDP 26099 |
| Compatibility mods detected | VRIK, HIGGS, PLANCK; gameplay hooks remain gated off |
| Live-process rule | Keep current game running; do not force-close RaceSex or manually pump native ticks |

## Open Decisions

### 1. Correct Owner For Connection-Only Updates

**Current lean:** keep one stable engine-owned thread, but do not assume the
activation thread is a recurring post-character VM-update caller. Identify and
hook a verified recurring main/game-thread boundary, then consume the existing
atomic permit there. Preserve `World::Update()` serialization and keep rotating
VM/SKSE workers observation-only.

Options:

1. Consume the permit from a verified recurring main/game-thread hook.
2. Treat a rotating VM worker as owner and serialize with a lock.
3. Run network state on a dedicated plugin thread and snapshot all game state on
   an engine thread.
4. Dispatch directly from SKSE task callbacks.

Rejected for now: options 2 and 4 because thread IDs rotate and service code was
not designed for arbitrary worker affinity; option 3 is much larger than the
connection-only proof requires.

### 2. Whether The Existing `MainLoop` Hook Is A Viable Owner

**Current lean:** do not activate it without cadence proof. The hook at project
ID 36564 is installed but has not logged a useful per-frame cadence in this live
run. Verify its semantics and actual thread/cadence before considering it.

Options:

1. Prove and use it as the permit consumer.
2. Keep it diagnostic and find another verified frame/update boundary.
3. Add only more observer telemetry in the next build before choosing.

Rejected for now: assuming its name means a frame loop; the runtime evidence does
not support that assumption.

### 3. Handling A Permit When The Safe Owner Stops Calling

**Current lean:** detect and report starvation explicitly without changing owner
automatically. A permit should remain coalesced, not accumulate unbounded work.

Options:

1. Add owner-starvation telemetry/status and fail closed.
2. Migrate owner after a timeout to a stable observed thread.
3. Execute on whichever thread first sees the permit.

Rejected for now: automatic migration and first-seen execution because startup
and gameplay workers have already shown multiple thread IDs.

### 4. Next-Build Scope

**Current lean:** if source changes are needed, make one architecture correction
plus enough observer assertions to prove thread ID, cadence, no overlap, menu
transition, and clean teardown in a single rebuild. Do not mix in gameplay hooks.

Options:

1. Implement a verified recurring owner now if source and binary evidence are
   conclusive.
2. Build observer-only probes first and require one more runtime pass.

Rejected for now: speculative owner changes that would merely trade starvation
for intermittent memory corruption.

## Suspected Overlap

Decisions 1 and 2 may be the same decision if ID 36564 can be proven to be a
stable recurring game-thread boundary. Decisions 1 and 3 overlap if the chosen
boundary naturally supplies starvation detection. Merge them if the evidence
supports it.

## Reviewer Request

Verify the claims against the cited source, current runtime logs, and relevant
CommonLib/SKSEVR definitions before critiquing. Rank the execution-owner options
by correctness and implementation cost. Identify any missing failure mode that
could cause a crash, deadlock, reentrancy, teardown race, or silent connection
stall. Recommend the smallest patch and exact proof obligations for the next
runtime test. If evidence is insufficient to select a hook, specify the minimal
observer instrumentation needed in one rebuild rather than guessing.

Do not edit files. Return a prioritized review with file/line evidence and
separate facts from inferences.
