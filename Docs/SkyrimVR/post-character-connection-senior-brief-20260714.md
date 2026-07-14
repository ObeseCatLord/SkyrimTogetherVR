# Senior Brief: Post-Character-Creation Connection Flow

## Goal and scale

Trace and harden the complete Skyrim Together VR startup path from a fresh Skyrim VR
launch through New Game, RaceSex completion, Realm of Lorkhan placement, client
autoconnect, and dedicated-server admission. This is a solo-operator port and live
bring-up, not an enterprise redesign. The immediate deliverable is a repeatable test
that requires no headset/controller input and proves connection through both local
status and remote server evidence.

The reviewer is read-only. Verify the evidence against source and local artifacts,
challenge the framing, then return a prioritized critique and a concrete recommended
flow. Do not edit files.

## Verified evidence

| Claim | Status | Evidence |
| --- | --- | --- |
| DevBench 1.9.1 is installed and its Skyrim VR REST endpoint is live on `127.0.0.1:8921`. | [verified: live REST] | `GET /api/tools`; `inspect state` reports `vr=true`. |
| A fresh New Game can reach RaceSex and DevBench can close `RaceSex Menu`; afterward the player is in `RealmLorkhan` without loading a save. | [verified: live REST] | `menu list`, `menu close`, then `inspect scene` returned cell `RealmLorkhan` (`0x0801A5C6`). |
| The original deployed `SkyrimTogether.esp` was omitted from the live load order. | [verified: live REST] | `inspect mods` returned nine plugins and omitted Skyrim Together while including Realm. |
| The omission was caused by the ESP TES4 HEDR version. | [verified: controlled live isolation] | Both the 6,828-byte and older 6,516-byte ESP variants declared `1.71` and were omitted. Changing only the four-byte HEDR float to `1.70` made DevBench report `SkyrimTogether.esp` at live index 9. Realm's working ESP also declares `1.70`. |
| The current two-quest ESP is now normalized to HEDR `1.70`; its matching SEQ contains `0x02003DD0` and `0x02003DD1`. | [verified: binary/audit] | `GameFiles/SkyrimVR/SkyrimTogether.esp`; `GameFiles/SkyrimVR/Seq/SkyrimTogether.seq`; `add_startup_migration_quest.py --write --verify`. |
| With the normalized ESP, both quests exist in-game at `09003DD0` and `09003DD1`; the source quest is enabled and running. | [verified: live console via DevBench] | `help SkyrimTogether 4`; `sqv 09003DD0`. |
| The running source quest had no Papyrus scripts attached. | [verified: live console] | `sqv 09003DD0` reported `No papyrus scripts attached`. |
| Proton has separate `Data/Scripts` and `Data/scripts` directories; Skyrim Together PEX files were deployed only to lowercase `Data/scripts`. | [verified: filesystem] | Both directories exist in the test install; all eight Skyrim Together PEX files were in lowercase only before the live repair. |
| Skyrim explicitly failed to load every Skyrim Together PEX class from the lowercase deployment. | [verified: Papyrus runtime log] | Current `Papyrus.0.log` reports `Cannot open store for class` for the verify, migration, alias, spell, and tick-bridge classes, followed by base-type mismatch/bind failures. |
| The SKSE tick bridge DLL itself loads and registers its native Papyrus functions, but no Papyrus cadence call has occurred. | [verified: runtime log] | `SkyrimTogetherVRTickBridge.log` contains registration callbacks but no `papyrus_tick`, `task_enqueued`, or `task_run`. |
| `VRConnectionService` only polls the command file and `STVR_AUTOCONNECT` from `UpdateEvent`. | [verified: source] | `Code/client/Services/Generic/VRConnectionService.cpp::OnUpdate`. |
| The VR client emits `UpdateEvent` only when `TiltedOnlineApp::Update()` reaches `World::Update()`. | [verified: source] | `Code/client/TiltedOnlineApp.cpp`; `Code/client/World.cpp`. |
| The intended game-thread path is quest `OnUpdate` -> native `SkyrimTogetherVRTickBridge.Tick()` -> SKSE task -> validated endpoint dispatch -> `TiltedOnlineApp::Update()`. | [verified: source] | `GameFiles/SkyrimVR/Scripts/source/SkyrimTogetherVerifyLaunchScript.psc`, migration scripts, `Code/vr_tick_bridge/main.cpp`, and `Code/client/VRTickBridge.cpp`. |
| The command file and environment endpoint target `incidentalstoat.xyz:10578`; the dedicated server is healthy but has not admitted this client. | [verified: local files/remote Docker logs] | `Data/SkyrimTogetherReborn/SkyrimTogetherVR.command`; launch environment; remote container log remains at zero players. |
| The default VR build is still connection-only. | [verified: runtime/source] | Client log says gameplay sync hooks are disabled; `TiltedOnlineApp` connection-only branch still updates `World`. |

## Environment facts

| Item | Fact |
| --- | --- |
| Repo | `/home/obesecatlord/Documents/SkyrimModding/SkyrimTogetherVR` |
| DevBench source | sibling repo `/home/obesecatlord/Documents/SkyrimModding/devbench` |
| Runtime | Skyrim VR 1.4.15 under Proton/UMU and Monado |
| DevBench endpoint | `127.0.0.1:8921` |
| Active alternate start | Realm of Lorkhan, live plugin index 8 |
| Skyrim Together plugin | live plugin index 9 after HEDR normalization |
| SKSE bridge | `Data/SKSE/Plugins/SkyrimTogetherVRTickBridge.dll` |
| Canonical loose-script directory | `Data/Scripts` (capital `S` is observable under Proton) |
| Remote server | Docker container `skyrim-together-vr`, UDP 10578 |
| Current launch | direct profile, `STVR_AUTOCONNECT=incidentalstoat.xyz:10578` |

## Current plan and open decisions

### 1. Canonicalize packaged paths and reject incompatible ESP headers

**Current lean:** Rename the staged/package path from `Data/scripts` to
`Data/Scripts`, update the build/compiler/audit references, and make package audits
reject any Skyrim VR ESP whose HEDR is not exactly the `1.70` byte representation.
Keep the `1.70` normalization in the deterministic migration tool.

Rejected alternative: special-case only the local launch script to merge directory
case. That would leave exported Linux packages broken and hide the packaging defect.

### 2. Keep Papyrus cadence primary or add a native startup owner

**Current lean:** First prove the intended Papyrus path after canonical PEX deployment.
Retain the validated SKSE task endpoint and owner claim. Add a bounded native fallback
only if a clean restart still fails to dispatch, and require it to yield permanently
once Papyrus claims cadence.

Rejected alternative: call networking directly from the observed main-loop detour.
That bypasses the existing SKSE task/thread validation and has already shown different
thread IDs across calls.

### 3. Automate New Game without a new game-engine offset

**Current lean:** Use a host-side Linux `uinput` keyboard helper to focus Skyrim's KDE
window, navigate the Main Menu, and press New Game; use DevBench events to confirm
`RaceSex Menu`, close the stuck menu, wait for `RealmLorkhan`, and verify the plugin,
status, and server. This gives feedback at each state and avoids another native DLL
build.

Alternative: add a DevBench `game newGame` action. CommonLib's exposed
`BGSSaveLoadManager` has Save/Load methods but no mapped NewGame method, so doing this
now would require a new Skyrim VR address/signature and create another ABI risk.

Open question: whether `kHide` on RaceSex is sufficient for a real finalized player,
or whether automation must invoke RaceSex's accept callback/name event. The live Realm
transition succeeds, but the reviewer should identify downstream state that may remain
unset.

### 4. Define connection proof as multi-source evidence

**Current lean:** Require all of: tick bridge `task_run` success, local status file
`state=online`, client `ConnectedEvent`/transport log, and remote server showing one
player. Do not infer connection from Realm placement or an unconsumed command file.

## Suspected overlap

The case-sensitive PEX failure and apparent quest-lifecycle failure are one issue, not
two. The native fallback decision must be made only after the canonical script path is
proven. Likewise, RaceSex completion and player-readiness gating overlap: a hidden menu
is not enough if the player/base/cell state or name-finalization event is incomplete.

## Reviewer questions

1. Audit the complete call/event chain and identify every condition that can prevent
   autoconnect after character creation.
2. Decide whether canonical path/header fixes plus Papyrus cadence are sufficient, or
   whether a native pre-cadence fallback is still architecturally necessary.
3. Assess whether closing RaceSex with `kHide` leaves material player initialization
   incomplete; recommend the safest automatable completion mechanism.
4. Identify missing package/runtime assertions that would have caught both observed
   Linux/VR failures before launch.
5. Re-rank the implementation steps and specify the minimum evidence for declaring
   server connection successful.

## Depth budget and not-list

Review only the startup/cadence/connection path and directly related packaging and
automation. Do not re-review address-library gameplay mappings, avatar replication,
HIGGS/PLANCK synchronization, renderer hooks, general FUS compatibility, or server
administration. Do not propose enabling broad gameplay hooks as part of this fix.
