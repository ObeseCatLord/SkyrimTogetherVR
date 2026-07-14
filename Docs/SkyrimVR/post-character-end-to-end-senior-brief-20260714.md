# Skyrim Together VR post-character end-to-end senior review brief

## Goal and scale

Verify the complete default Skyrim Together VR connection-only path from a newly
created character through server authentication and the first exterior cell/grid
messages. Decide the smallest robust fix for the first-tick null call without
enabling Skyrim gameplay hooks that remain unvalidated for VR. This is a solo
porting project; favor explicit invariants and focused regression checks over
framework-scale refactors.

## Verified evidence

| Claim | Status and verification |
| --- | --- |
| The latest runtime reaches the first accepted SKSE task callback, then faults at RIP `0`. | **[verified: runtime log and Wine minidump]** `tp_client.log` records callback sequence 1 immediately before `c0000005` at `0x0`; WineDbg reports RIP/RAX 0. |
| The caller return address is `0x18068382a`. | **[verified: WineDbg stack]** This is the top return address in the latest dump. |
| The return address is in `DiscoveryService::VisitCell`, immediately after `Actor::GetCurrentLocation`. | **[verified: fresh PE/PDB]** PDB public data places `VisitCell` at section 7 offset `0x1366a0`; disassembly shows the call at `0x180683825` and return at `0x18068382a`. The callee public symbol is `Actor::GetCurrentLocation`. |
| `Actor::GetCurrentLocation` jumps through `FUNC_GetActorLocation` without checking it. | **[verified: source and disassembly]** `Actor.cpp` calls the global directly; its thunk loads the global and jumps to it. |
| The global is assigned only by `s_actorHooks`, which is run through `Initializer::RunAll`. | **[verified: source]** `Actor.cpp` assigns it in the actor initializer; `TiltedOnlineApp::InstallHooks2` is the only client call to `RunAll`. |
| Default VR deliberately does not run `InstallHooks2`. | **[verified: source and runtime log]** With unvalidated hooks disabled, `RunTiltedInit` calls `InstallVrBringupHooks`; the runtime logs “skipping unvalidated Skyrim gameplay hooks.” |
| Address ID `19812` is not missing. | **[verified: CommonLib NG and generated audit]** CommonLib NG uses SE ID `19385`, AE ID `19812`; the VR CSV maps `19385 -> 0x2aabf0`, and the generated alias maps `19385 -> 19812`. |
| The direct VR singleton/function corrections from the preceding review are present. | **[verified: source and address audit]** TES `516923`, TESDataHandler/ModManager `514141`, ProcessLists `514167`, Calendar `514287`, Actor level `36344`, INI collection `524557`; address audit has zero missing IDs. |
| The original and VR authentication protocol are materially the same after transport connection. | **[verified: branch comparison]** Both create `AuthenticationRequest`, send build/SKSE/MO2/password/player/mod/cell/time data, set `m_connected` on accepted response, dispatch user mods/settings, then dispatch `ConnectedEvent`. |
| The VR-specific connection entry point replaces the flat overlay UI with a command/environment handoff. | **[verified: source]** `VRConnectionService` polls command/environment input, waits for readable player/base form/parent cell, queues `TransportService::Connect`, and writes a status file. |
| Cell-only `PlayerService` intentionally avoids gameplay-mutating ConnectedEvent handlers. | **[verified: source]** In connection-only cell mode it subscribes only to Update, GridCellChangeEvent, and CellChangeEvent, and checks `m_transport.IsOnline()` before sending. |
| The unattended driver has state-based menu and connection checks. | **[verified: source]** It waits for Main Menu/RaceSex, uses host input plus a real emulated controller trigger, requires Realm of Lorkhan placement and stable player identity, then requires fresh online/playercell files, a nonzero local player ID, and at least one exterior plus grid request. |
| Realm of Lorkhan and SkyrimTogether are active in the direct test profile. | **[verified: deployed profile]** Both ESPs are enabled after VR masters/HIGGS/VRIK; automation also checks the active mod list and Realm cell editor ID. |
| Remote admission has not yet been demonstrated with the latest build. | **[verified: runtime result]** The first tick crashes before command processing can reach authentication. |

## Environment facts

| Fact | Value |
| --- | --- |
| Target runtime | Skyrim VR 1.4.15, SKSEVR task bridge |
| Default build flags | VR=1, connection-only=1, unvalidated hooks=0, discovery=1, player-cell=1 |
| Safe hook policy | startup/main-loop/render bring-up hooks only; HIGGS/PLANCK gameplay hooks remain disabled |
| Address source | VR Address Library CSV plus generated overrides/SE-to-AE aliases |
| Current runtime mode | Proton/Wine, Monado Qwerty controller automation, DevBench REST inspection |
| Current package | Windows Release build with matching PDB and package evidence |
| Server evidence | Docker server log admission must corroborate local online status |
| Original comparison branch | `original-skyrim-together` |

## Expected end-to-end flow

1. Tick bridge invokes `TiltedOnlineApp::Update`, which dispatches PreUpdate,
   drains the runner, then dispatches Update.
2. `VRConnectionService` waits for a readable player/base form/parent cell and
   consumes a connect command.
3. Runner invokes `TransportService::Connect`; transport callback constructs and
   sends `AuthenticationRequest`.
4. Accepted `AuthenticationResponse` sets online state and dispatches mod list,
   server settings, then `ConnectedEvent(playerId)`.
5. `VRConnectionService` writes online status; transport stores local player ID.
6. `DiscoveryService` produces a forced or changed grid/cell event using translated
   worldspace/cell IDs.
7. Cell-only `PlayerService` sends `ShiftGridCellRequest` and
   `EnterExteriorCellRequest`, then records request counters and IDs.
8. Verification requires both fresh local proof and remote server admission.

## Open decisions

### 1. Fix the accessor or initialize part of the actor hook batch

**Current lean:** in VR, make `Actor::GetCurrentLocation` resolve direct CommonLib
VR ID `19385` lazily with `VersionDbPtr`, return null if unavailable, and leave the
desktop/global hook path unchanged.

- Option A: lazy direct VR resolver in the accessor.
- Option B: split actor address initialization from hook installation and run the
  address-only subset in bring-up mode.
- Option C: call all of `Initializer::RunAll` and suppress individual hooks.

Rejected so far: C defeats the established safety boundary and risks installing
unvalidated HIGGS/PLANCK-conflicting hooks. B adds a second initializer lifecycle
for one read-only accessor unless the audit finds several more such dependencies.

### 2. Keep location telemetry on the game function or remove it from the default path

**Current lean:** keep the game function after making it self-resolving and
null-safe; location changes are useful but not required for authentication.

- Option A: use the corrected direct VR function.
- Option B: skip `GetCurrentLocation` entirely in default connection-only mode.

Rejected so far: raw player layout fields are less defensible because VR player
runtime layout remains only partially validated.

### 3. Force first cell/grid synchronization after authentication

**Current lean:** explicitly trigger `DiscoveryService::VisitCell(true)` from its
existing `ConnectedEvent` observer, provided the reviewed accessor set is safe.

- Option A: retain the current forced visit on ConnectedEvent.
- Option B: rely on ordinary PreUpdate change detection.

Rejected so far: B can fail to emit after pre-auth discovery has already populated
caches, leaving the server without initial cell/grid state.

### 4. Runtime proof bar

**Current lean:** require fresh local online/playercell proof, matching local player
ID, nonzero grid/exterior request counts, valid last IDs, and remote server admission.

- Option A: keep this dual-sided proof.
- Option B: accept local online state alone.

Rejected so far: B proves authentication response handling but not server-side
presence or post-auth cell synchronization.

## Suspected overlap

Decisions 1 and 2 may collapse if location telemetry is unnecessary for the
default connection proof. Decisions 1 and 3 overlap because a forced post-auth
visit immediately exercises every read-only game wrapper in the discovery path.
Merging these decisions is in scope.

## Review request

Verify the evidence against the actual source, PDB-backed call site, CommonLib NG,
and both branches before critiquing. Then:

1. Rank any remaining first-tick or post-auth crash/blocker paths.
2. Audit every default connection-only service call reachable without
   `Initializer::RunAll` for hidden `Real*`/`FUNC_*` dependencies.
3. Confirm whether direct VR ID `19385` is the correct robust fix and whether the
   pointer ABI is compatible with the existing `TGetLocation` declaration.
4. Trace authentication through ConnectedEvent and first cell/grid messages;
   identify any state ordering, subscription, or mod-translation gap.
5. Review the unattended Main Menu -> New Game -> RaceSex -> Realm -> connect
   automation for false-positive proof or an avoidable manual step.
6. Recommend focused source/audit/test changes that should be completed before the
   next Windows build.

## Depth budget and not-list

Spend depth on ABI/address correctness, event ordering, hidden initializer
dependencies, and proof integrity. Do not re-review gameplay/avatar synchronization,
remote actor spawning, flat overlay rendering, server deployment, general HIGGS or
PLANCK behavior, or unrelated address-library entries.
