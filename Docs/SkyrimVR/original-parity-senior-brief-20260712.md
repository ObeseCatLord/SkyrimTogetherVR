# Original Skyrim Together Parity Review Brief

## Goal

Determine whether `main` contains every functional subsystem required for a
Skyrim VR client to join, remain joined to, and participate in a Skyrim
Together server after the current startup-quest repair lands. This is a solo
operator port, not an enterprise compatibility program. The review must reject
claims of completion that are only build, launch, or endpoint readiness.

The requested outcome is a usable Skyrim Together VR client under
Linux/Proton/Monado with SKSEVR, HIGGS, PLANCK, and VRIK. It must connect to the
dedicated server and support the applicable original-client multiplayer
behavior, or identify exact unimplemented behavior that prevents that claim.

## Branches And Scope

| Item | Value |
| --- | --- |
| Original baseline | `original-skyrim-together` at `9d81ef07d68e4bb2bd94fca246e798a564b7fb92` |
| Current committed VR main | `main` at `a3b0ea6308793d6c910b9d5f4344e249366b1e92` |
| Review target | current worktree, including the uncommitted one-shot startup migration/cadence-owner work |
| Runtime | Skyrim VR 1.4.15, SKSEVR 2.0.12, Proton GE 10-34 through `umu-run`, Monado/XRizer |
| Server | one healthy Docker `skyrim-together-vr` instance on UDP 10578 |

Do a broad code and behavior comparison. Prioritize the execution path from
launcher to `World::Update`, transport connection, service dispatch, remote
player behavior, and server protocol parity. Review the VR build/package and
Linux launch path only where it can prevent these outcomes.

## Verified Runtime Facts

| Claim | Evidence |
| --- | --- |
| The VR launcher is live under Proton/Monado. | [verified: current process `SkyrimTogetherVR.exe --exePath ...SkyrimVR.exe --no-companion`; XRizer reports Bigscreen Beyond and Valve Knuckles] |
| The mapped game reaches renderer initialization. | [verified: installed `logs/tp_client.log`] `renderer init completed`. |
| The main client endpoint is ready on the game thread. | [verified: same log] `SkyrimTogetherVR tick bridge endpoint ready on thread 336`. |
| SKSEVR loads HIGGS, PLANCK, VRIK, and all SkyrimTogetherVR bridges. | [verified: current `Documents/My Games/Skyrim VR/SKSE/sksevr.log`] |
| A player exists in the running session. | [verified: current HIGGS/PLANCK logs] `Player Character Proxy changed`. |
| The server is healthy and idle. | [verified: current Docker logs/stats] `skyrim-together-vr` is listening on UDP 10578, zero players, roughly 0.7% CPU and 8 MiB memory. |
| The launcher writes a connect command. | [verified: `Data/SkyrimTogetherReborn/SkyrimTogetherVR.command`] endpoint `incidentalstoat.xyz:10578`. |
| The command is not consumed. | [verified: same directory and server] no status/readout files, zero server players. |
| The old startup quest never reaches native Papyrus. | [verified: `Data/SKSE/Plugins/SkyrimTogetherVRTickBridge.log`] registration completes, but no `papyrus_arm`, `papyrus_tick`, task enqueue, or task-run entry. |
| The old save encountered the plugin before an SEQ file existed. | [verified: current save predates deployed SEQ; the original quest is Start Enabled/Run Once and has no lifecycle events after SEQ deployment] |
| The current main process logs a deliberately restricted mode. | [verified: current `tp_client.log`] `connection-only mode: gameplay sync services are disabled`; discovery/movement/inventory/activation/magic/combat/projectile/grab/HIGGS/save-load log as observation-only or network-only; remote player proxy is readout-only. |

## Original Startup And VR Replacement

1. Original `Code/client/SkyrimVM64.cpp:23-29` invokes
   `g_appInstance->Update()` from the active VM update hook, with the original
   SE addresses `53926`, `36564`, and `40412` at lines 45-58.
2. Direct reuse of that SE path previously crashed in VR at the VM destructor
   hook. It remains rejected.
3. VR currently uses `GameFiles/SkyrimVR` Start Game Enabled quest scripts to
   call `Code/vr_tick_bridge/main.cpp`; the bridge queues one SKSE task and
   validates the mapped callback before dispatching `World::Update`.
4. The prior SEQ index was wrong in two ways: it included a master-owned T03
   override and checked raw DNAM bit `0x0001` rather than the engine's
   `StartsEnabled` bit `0x0010`. The in-progress work corrects this and adds a
   one-shot migration quest plus an atomic native cadence owner so a stale save
   can reach exactly one timer.

## In-Progress Quest Repair

This worktree, not yet compiled or deployed, adds:

| Area | Intended behavior |
| --- | --- |
| `generate_seq.py` | include only plugin-owned QUST records with `StartsEnabled` (`0x0010`), excluding inherited master overrides |
| `add_startup_migration_quest.py` | append QUST `0x02003DD1`, bound to migration-specific scripts, while retaining the original quest |
| Papyrus/native bridge | old and migration quest scripts claim owner 1 or 2; the process-local atomic lets only the first owner schedule ticks |
| Packaging audits | require migration PEX files and the updated SEQ/ESP layout |

The migration uses a new FormID because quest flags are save-baked; it does not
call the main client's untranslated SE `TESQuest` wrapper, install a VM hook,
or add a native recurring task.

## Open Decisions

### 1. Functional parity threshold

**Current lean:** a shipped VR port cannot be called Skyrim Together functional
if it merely connects while remote player/avatar, movement, interaction,
inventory, magic, combat, projectiles, save/load, party, and discovery behavior
remain disabled or readout-only. The review must distinguish genuinely
implemented VR replacements from diagnostic handoff files.

Alternatives:

- A. Treat server connection as done. Rejected: it would hide the current
  explicit connection-only/observation-only log mode.
- B. Restore original hooks mechanically. Rejected: prior VR crash evidence and
  unvalidated SE addresses make this unsafe.
- C. Assess each service's transport, inbound application, and VR-safe game
  effect path. Current lean.

### 2. Quest migration design

**Current lean:** retain the original quest and add one new migration quest,
with an atomic native cadence owner. This is intended to repair stale saves
without duplicate clean-install timers.

Alternatives:

- A. SEQ-only. Insufficient for the already-baked local save.
- B. Duplicate quest without a guard. Rejected: dual timers.
- C. Direct quest/native VM start. Rejected: invalid API/runtime boundary.

### 3. Build boundary

**Current lean:** the quest repair requires a Windows build only for the
SKSEVR bridge DLL and Papyrus PEX files. It should not cause a broad gameplay
hook port. Build after this review disposition and only after all changes are
integrated.

## Review Questions

1. Verify the actual original-to-main execution and protocol/service paths.
   Which original client/server features are functionally represented in main,
   which are intentionally disabled, and which are missing or unsafe?
2. Are there any other pre-connection blockers beyond the quest lifecycle:
   bootstrap, endpoint mapping, SKSEVR ABI, transport, address layout, plugin
   loading, Linux/Proton, server protocol, or required VR mods?
3. Is the migration/cadence-owner design sound for clean and stale saves?
   Identify exact code changes required before one Windows build, or reject it.
4. Produce a staged acceptance matrix: connection-only verification first, then
   remote avatar/movement and each remaining multiplayer lane. State what is
   proven now versus only represented by code.
5. Identify any human decisions that cannot be resolved technically. Otherwise
   give a prioritized implementation sequence, not a vague list.

## Not In Scope

- No suggestion to restore the known-crashing original VM/destructor hooks.
- No recommendation to ignore HIGGS/PLANCK/VRIK coexistence; inspect their
  integration only for concrete regression risk.
- Do not rewrite the complete port or make edits. This is read-only.
- Do not treat a green build, present handoff file, or declared service as proof
  that the game effect is live.

## Output Contract

Maximum 1,800 words. Verify before critique. Lead with ranked blockers, then a
table mapping original feature families to VR implementation state and evidence.
End with a disposition table for the quest design and an exact pre-build and
post-build acceptance checklist. Cite file paths and line numbers for all
load-bearing conclusions.

## Sol-Max Review Disposition

| Recommendation | Disposition |
| --- | --- |
| Treat the default target as a Stage 0 connection milestone, not full multiplayer parity. | Adopted. The current default intentionally disables or observes multiple gameplay lanes. |
| Correct SEQ ownership and `StartsEnabled` detection. | Adopted. The generated asset now contains only `0x02003DD0` and `0x02003DD1`. |
| Add a new migration quest for stale saves. | Adopted. The staged ESP carries `0x02003DD1` with migration-specific quest and alias scripts. |
| Prevent dual timers with a process-local owner. | Adapted. Both `OnInit`/load paths claim an owner, and both `OnUpdate` handlers now exit without re-registering when they lose ownership. |
| Make package audit validate the startup records and generated SEQ. | Adopted. Built-package audit now rejects a missing/malformed migration record, expected FormID, or non-exact SEQ. |
| Restore original SE VM/destructor hooks. | Rejected. The prior VR crash and untranslated address boundary remain disqualifying. |
| Claim that the default target has full Skyrim Together gameplay parity after connection succeeds. | Rejected. Remote effects, avatar application, party/UI, and world/save lanes require separate validated work. |

## Accepted Stage 0

The next Windows build is limited to proving the default VR target can safely
reach the existing server from a stale or clean save: exactly one Papyrus
cadence owner, bridge task dispatch, command consumption, authenticated online
status, and one server player. Remote gameplay parity is explicitly not claimed
by this milestone and remains a separate implementation program.
