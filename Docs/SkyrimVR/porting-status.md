# SkyrimTogetherVR Porting Status

This repository is a VR-targeted working copy of TiltedEvolution/Skyrim Together.

## 2026-07-16 Full Gameplay Source Tranche

The active gameplay target is no longer connection-only or observation-only.
The mapped client translates original Skyrim Together protocol messages while
the maintained alandtse CommonLib SKSEVR plugin owns validated Skyrim VR events,
retained actor handles, object references, and game mutation. Historical stage
notes below describe how the port reached this boundary; they are not the
current feature gate.

Current source includes canonical remote actor lifecycle and movement,
animation graph snapshots, appearance, equipment and inventory,
actor values, death/respawn, owned NPCs, objects, combat, projectiles, magic,
quests, dialogue, packages, party/world state, HMD/hands/FBT pose, HIGGS object
compatibility, and PLANCK damage/ragdoll deduplication. Multi-record command and
event producers now reserve complete contiguous ring transactions, and
transient network text, respawn, and local package changes retain bounded retry
state across queue backpressure but never across session/lifecycle identity.
Stateful and inventory retries preserve one global order while last-value state
coalescing moves to the newest position. Client messages are serialized into a
bounded generation-scoped owner-thread transport queue, and NPC ownership uses
an expiring, session-bound, single-use server grant followed by an explicit
completion acknowledgement.

Gameplay protocol revision 7 carries token-bound ownership and one final-state
VR equipment transaction. Worn inventory plus selected spells/shout are
validated before one server mutation and one compatible-client notify; the
receiver publishes the complete staged CommonLib command sequence with one
bridge-ring reservation and clears staging at lifecycle boundaries. Desktop
recipients do not yet receive VR-owned final equipment because translating the
transaction back into several legacy notifications would reintroduce partial
mutation. VR recipients continue to handle desktop transaction-zero updates.

The current source tranche has not yet been compiled or runtime-tested. The
remaining deliberate source limitations are full remote face tint/morph/texture
generation, native respawn fade, remote VRIK finger/calibration application,
direct PLANCK physical grab/ragdoll replay, exact `ActorMediator::PerformAction`
capture/replay, and Vivox voice. Exact actions remain fail-closed until form
translation, capability renegotiation, and `TESActionData` lifetime are proven
for Skyrim VR. The upstream
scripted-object-animation producer is also compiled out on the original branch;
VR supports incoming replay but does not install a guessed hook against its
Address Library registration rows. See
`Docs/SkyrimVR/original-gameplay-parity-checklist.md` for the authoritative
source/build/runtime matrix and next-stage order.

## 2026-07-14 FBT Networking Stage

- Added body-pose format 1 to the matched VR pose protocol: pelvis local translation/rotation plus six leg-bone local rotations, capture sequence, and skeleton-root generation.
- Added a process-local ABI 3 callback from `SkyrimTogetherVRHiggsBridge` to the launcher. The post-VRIK/post-HIGGS callback publishes through a nonblocking SRW mailbox; stale data fails closed after 250 ms.
- Added shared server/client ingress validation for all VR pose transforms, strict right-handed body bases, bounded pelvis translation, rotation-only limb payloads, VRIK finger values, and camera offsets. VRIK-only updates remain relayable.
- Added FBT installed/loaded compatibility reporting and local/remote body telemetry. Remote body data is cache-only; no remote skeleton writes are enabled because the local HIGGS callback is not an actor-specific remote animation barrier.
- Reviewed the supplied SkyrimVR-FBT source as an interoperability reference. Its archive has no redistribution license or build project, so it is not vendored or packaged. The installed VRIK `0x69277C5C` profile is documented in `fbt-compatibility.md` with timestamp mismatch disabled.
- The body schema is fixed-order and requires matched client/server builds. Body `FormatVersion` is validation for the nested lane, not protocol negotiation.

## Historical Connection-Only Bring-up Boundary

This section records the former connection-only VR bring-up build and is kept
only for implementation-history traceability. It is not the current gameplay
target or package boundary. In that former build, a normal SKSEVR
Papyrus native requests a coalesced one-shot `SKSETaskInterface` task from the
quest's 50 ms timer. That task validates a process-local launcher endpoint and
publishes one atomic permit; it never calls the client. Exact Skyrim VR
`Main::Draw` address-library ID `35560` at RVA `0x5B9330` is the only candidate
client owner. It defaults to `STVR_VM_UPDATE_MODE=observe`, which records
cadence, thread, depth, gaps, and original-call duration while always forwarding
to Skyrim. `active` mode lets only an outermost activation-thread draw consume a
permit and call `World::Update()` under a single-entry guard. The inherited VM
destructor and outer main-loop detours remain disabled. The opaque VM update
detour at ID `53926` is forwarding telemetry only and never dereferences the VR
VM context or calls the client.

For Skyrim VR 1.4.15, project-local ID `53926` maps to RVA `0x12765B0` with the
`void (this, float)` ABI. This is `BSScript::Internal::VirtualMachine::Update`
virtual slot 4, derived from CommonLibSSE-NG's VR vtable at RVA `0x18E2148` and
the corresponding executable vtable entry. The earlier `0x9869D0` override was
incorrect and is rejected by `audit_bringup_hooks.py`.
ID `35560` is independently pinned to the VR Address Library database signature
`void Main::Draw(Main*, uint32_t, bool)` and is rejected if its RVA or
provenance changes.
Exact VR WinMain ID `35545` is pinned to RVA `0x5B4290`; the immersive launcher
wraps that inner function so `EndMain()` and endpoint retirement run before the
mapped CRT exit path. The wrapper is idempotent with the existing startup IAT
callback and leaves detours/mappings resident until process exit.

That former default target installed no flat-client BSScript native-binding,
registration, or signature detours. As a result, `SkyrimTogetherUtils` and the
connection-menu spell remain staged future UI source rather than active
connection controls. After the observer gate passes, launch with
`STVR_VM_UPDATE_MODE=active` and use `STVR_AUTOCONNECT` or the companion
command/config file handoff for testing. HIGGS and PLANCK remain
observation-only and are not used to drive client updates.

The implementation-history bullets below are retained for traceability. Where
they describe a former VM/BSScript/spell control path, this current boundary
supersedes them.

## Implemented Foundations

- Added a VR-only xmake target graph with `SkyrimTogetherVRClient`, `SkyrimTogetherVRVrikBridge`, `SkyrimTogetherVRHiggsBridge`, `SkyrimTogetherVRPlanckBridge`, and `SkyrimVRImmersiveLauncher`; the normal Skyrim SE `SkyrimTogetherClient` and `SkyrimImmersiveLauncher` targets are intentionally absent.
- Added explicit experimental VRIK/HIGGS remote-avatar validation targets `SkyrimTogetherVRClientAvatarSync` and `SkyrimVRImmersiveLauncherAvatarSync`, which build `SkyrimTogetherVRAvatarSync.exe` in staged connection-only mode. The current targets use `VRAvatarService` plus the CommonLib gameplay bridge for canonical actor lifecycle, retained-identity root/spatial movement, and named humanoid animation graph snapshots; HMD/hand/VRIK data remains observation-only.
- Added `BuildSkyrimTogetherVR-Windows.ps1` plus `BuildSkyrimTogetherVR-Windows.bat` so the Windows/MSVC build can be run from a machine where the client/launcher targets are visible. `SetupSkyrimTogetherVRBuildEnv-Windows.bat` is the shared Windows environment bootstrap for the default, avatar-sync, and DLL-only build wrappers; it keeps x64 Native Tools Prompt runs working and tries `vswhere.exe` plus `VsDevCmd.bat -arch=x64 -host_arch=x64` when `cl.exe` is not already visible. The shared PowerShell script supports `-PreflightOnly` for a no-build Windows target/package-helper sanity check before compiling, writes the latest package to `artifacts\SkyrimTogetherVR\releasedbg`, and copies per-flavor package snapshots to `artifacts\SkyrimTogetherVR\packages\default`, `artifacts\SkyrimTogetherVR\packages\avatar-sync`, and `artifacts\SkyrimTogetherVR\packages\dll-only`. The legacy root `Build.bat` is now a Skyrim VR-only compatibility wrapper that delegates to `BuildAndAuditSkyrimTogetherVR-Windows.bat` instead of the old generic xmake/distrib path. Added `BuildSkyrimTogetherVR-DLL-Windows.bat`, the plural compatibility alias `BuildSkyrimTogetherVR-DLLs-Windows.bat`, and the wording-compatible `BuildSkyrimTogetherVR-ClientDLL-Windows.bat` alias as DLL-focused wrappers for `SkyrimTogetherVRVrikBridge.dll`, `SkyrimTogetherVRHiggsBridge.dll`, `SkyrimTogetherVRPlanckBridge.dll`, and `EarlyLoad.dll`; the main VR client remains launcher-linked as `SkyrimTogetherVR.exe`, not `SkyrimTogetherVR.dll`. Added `BuildAndAuditSkyrimTogetherVR-DLL-Windows.bat` plus `Tools\SkyrimVR\audit_built_package.py --dll-only` so the partial DLL package can be audited without requiring launcher artifacts. Added `CollectSkyrimTogetherVRBuildEvidence-Windows.bat` plus `Tools\SkyrimVR\collect_build_evidence.py` so Windows build/package evidence can be zipped after a successful or failed build attempt without compiling again. Added `AuditSkyrimTogetherVRBuildEvidence-Windows.bat` plus `Tools\SkyrimVR\audit_build_evidence_zip.py` so a collected Windows build evidence archive can be audited offline for package-audit failures, manifest mode mismatches, missing runtime artifact listings, and command failures. Added `BuildAuditCollectSkyrimTogetherVR-Windows.bat` as a one-command Windows handoff wrapper that builds/audits the selected default, avatar-sync, or DLL-only package, always collects build evidence, audits the newest evidence zip, and still exits with the original build/package failure code when the build step fails. Added `VerifySkyrimTogetherVRWindowsPackages-Windows.bat` as a no-build/no-install/no-launch verifier for the default/avatar-sync/DLL-only package snapshots after a multi-mode handoff build. Added `BuildSkyrimTogetherVR-AvatarSync-Windows.bat` as a one-command wrapper for the explicit two-client VRIK/HIGGS remote-avatar validation package. Added `BuildAndAuditSkyrimTogetherVR-Windows.bat` so a Windows/MSVC machine can build the default or avatar-sync package and immediately run the no-launch built-package audit before install. Added `AuditSkyrimTogetherVRReadiness-Windows.bat` as the no-build/no-install/no-launch Windows wrapper for `Tools\SkyrimVR\audit_vr_readiness.py`.
- Added `Docs\SkyrimVR\final-handoff-checklist.md` as the concise Windows/MSVC end-to-end checklist covering preflight, default/avatar-sync/DLL-only build evidence, built-package readiness, install dry runs, install, default runtime evidence, avatar-sync runtime evidence, full gameplay relay evidence, and final exit criteria.
- Added `Tools\SkyrimVR\audit_final_handoff.py` (`Tools/SkyrimVR/audit_final_handoff.py` in source-tree commands) and `AuditSkyrimTogetherVRFinalHandoff-Windows.bat` so the collected default/avatar-sync/DLL-only build evidence and default/avatar-sync/gameplay runtime evidence can be audited together without launching Skyrim. The final audit auto-discovers the newest matching build/runtime evidence archives from the standard folders, with `--build-evidence-dir` and `--runtime-evidence-dir` available for moved archives. Its self-test now also creates a weakened build evidence zip missing `source\SetupSkyrimTogetherVRBuildEnv-Windows.bat` and requires that final handoff audit to fail. Explicit runtime zip paths are role-checked by manifest flags, so an avatar-sync runtime zip cannot pass as default runtime evidence and a full gameplay-relay zip cannot pass as the narrower avatar-sync runtime evidence.
- Added `BuildSkyrimTogetherVR-Wine.sh` so Linux hosts can drive the same Windows packaging script through a Wine prefix that contains Windows PowerShell, xmake, and an MSVC-compatible Windows toolchain; the wrapper rejects Wine's built-in `powershell.exe` stub before it can report a false successful build.
- Added same-named Wine wrapper targets on non-Windows xmake configurations for `SkyrimTogetherVRClient`, `SkyrimTogetherVRClientAvatarSync`, `SkyrimTogetherVRGameplayClient`, `SkyrimTogetherVRVrikBridge`, `SkyrimTogetherVRHiggsBridge`, `SkyrimTogetherVRPlanckBridge`, `SkyrimVRImmersiveLauncher`, `SkyrimVRImmersiveLauncherAvatarSync`, `SkyrimVRImmersiveLauncherGameplay`, `ImmersiveElf`, and `TPProcess`; these wrappers are explicit-only and delegate to `BuildSkyrimTogetherVR-Wine.sh -Targets <target> -NoPackage`.
- Added `Tools/SkyrimVR/audit_smoke_package.py` for first VR smoke-test package manifest validation. It checks staged VR game files, helper CSVs, companion helper files, expected Windows artifacts, and the launcher-linked client layout. The target Skyrim VR install still needs the public `Data\SKSE\Plugins\version-1-4-15-0.csv` from VR Address Library in addition to the generated helper CSVs; run it with `--require-installed-prerequisites` before a first runtime smoke test to fail on missing SKSEVR or public VR Address Library files.
- Added `Tools/SkyrimVR/install_vr_prereqs.py` to audit or install the local VRIK/HIGGS/PLANCK runtime prerequisite stack into the target Skyrim VR `Data` folder. It can copy VRIK/HIGGS/PLANCK from discovered local sources or explicit `--vrik-source`, `--higgs-source`, `--planck-source`, `STVR_VRIK_SOURCE`, `STVR_HIGGS_SOURCE`, `STVR_PLANCK_SOURCE`, or `STVR_PLANCK_ARCHIVE` paths, can prepend portable source-root searches with `STVR_MO2_MODS`, `STVR_MO2_MOD_DIRS`, `STVR_BACKUP_DOWNLOADS`, `STVR_DOWNLOADS`, and `STVR_NORDIC_MODS`, and can update `plugins.txt` for `vrik.esp` and `higgs_vr.esp` when `--enable-plugins` is used.
- Added `Tools/SkyrimVR/audit_fus_dll_compat.py` for the native DLL mods under `/home/obesecatlord/Games/FUS/mods` and the deployed SkyrimVR root when present. It reads the selected FUS profile from `ModOrganizer.ini`, checks active SKSE/root DLLs for SkyrimTogether hard-blocked names, reports root proxy and duplicate deployed-path warnings, verifies the VRIK/HIGGS/PLANCK/Skyrim VR Tools stack, validates the launcher-side blocklist/greylist policy that handles those DLLs, can emit a full JSON DLL inventory with selected-profile and installed-root records, and documents the result in `Docs/SkyrimVR/fus-dll-compatibility.md`.
- Added `Tools/SkyrimVR/install_built_package.py` to audit and install a Windows-built `artifacts/SkyrimTogetherVR/releasedbg` package into the target Skyrim VR folder. It is dry-run by default, runs the built-package audit before copying, requires VRIK/HIGGS/PLANCK installed prerequisites by default, supports a dry-run-only `--package-only` package/copy-plan preflight before target prerequisites are installed, honors `SKYRIMVR_PATH` or `STVR_SKYRIM_VR`, and never launches Skyrim. Added `InstallSkyrimTogetherVR-Windows.bat` as the Windows dry-run/default install wrapper for the same flow.
- Added `Tools/SkyrimVR/audit_vr_readiness.py` as the no-launch top-level gate. It checks the VR xmake target graph, source/staged package audits, Address Library coverage, inline-patch policy, CommonLib-informed layout audit, VRIK/HIGGS/PLANCK compatibility gates, FUS native DLL compatibility, installed runtime prerequisites, and the Windows-built package when `--require-built-package` is used. `--planck-archive` or `STVR_PLANCK_ARCHIVE` can be used to validate a downloaded PLANCK zip without editing local Python paths.
- Added non-mutating PLANCK-aware combat telemetry: `TESHitEvent::GetRawFlagsData()` reconstructs the 32-bit word at the CommonLib hit-event flags offset, `VRCombatHitEvent` now carries source id, projectile id, raw hit flags, and `PlanckHit`, and the combat handoff plus companion/evidence fixtures expose `rawHitFlags`/`planckHit`. This does not call PLANCK APIs or read `GetLastHitData()`; Windows build/runtime validation still remains.
- Added `Tools/SkyrimVR/audit_remote_player_proxy.py` to lock down the explicit broader-target `SkyrimTogetherVR.remoteplayers` writer/parser/doc schema, run a temp-directory parser fixture through `vr_handoff.build_readout_payload()`, expose separate VRIK avatar and HIGGS-aware avatar readiness fields, and keep the proxy readout free of actor/object/equipment/HIGGS/PLANCK mutation calls.
- Added `Tools/SkyrimVR/audit_runtime_handoff.py`, packaged next to `vr_handoff.py`, for post-launch no-mutation validation of `logs/tp_client.log` and the `Data/SkyrimTogetherReborn` handoff files after the user runs Skyrim VR. Added packaged runtime audit wrapper `AuditSkyrimTogetherVRRuntime-Windows.bat` so the same post-run audit can be launched from the extracted package or Skyrim VR root without typing the Python path. Added `AuditSkyrimTogetherVRAvatarSyncRuntime-Windows.bat` as the one-command two-client VRIK/HIGGS avatar-sync runtime audit wrapper and `AuditSkyrimTogetherVRGameplayRuntime-Windows.bat` as the strict full gameplay wrapper. Added `Tools/SkyrimVR/collect_runtime_evidence.py` and packaged `CollectSkyrimTogetherVREvidence-Windows.bat` as the runtime evidence collector for zipping `tp_client.log`, handoff files, discovered SKSEVR logs, a generated runtime audit, `runtime_checklist.json`, `runtime_checklist.txt`, and `manifest.json` without launching Skyrim or mutating the game install. Added `CollectSkyrimTogetherVRAvatarSyncEvidence-Windows.bat` as the strict two-client VRIK/HIGGS avatar-sync evidence collector shortcut and `CollectSkyrimTogetherVRGameplayEvidence-Windows.bat` as the full gameplay evidence collector shortcut. Added `Tools/SkyrimVR/audit_runtime_evidence_zip.py` and packaged `AuditSkyrimTogetherVREvidence-Windows.bat` as the evidence zip audit for reviewing a collected archive without needing the original game folder. Added `AuditSkyrimTogetherVRAvatarSyncEvidence-Windows.bat` and `AuditSkyrimTogetherVRGameplayEvidence-Windows.bat` as the matching strict avatar-sync/gameplay evidence zip audit shortcuts.
- Added VR launcher identity in `TargetConfig.h`:
  - executable: `SkyrimVR.exe`
  - product: `Skyrim Together VR`
  - Steam app ID: `611670`
- Added SKSEVR-aware loading:
  - searches for `sksevr_1_4_15.dll`
  - accepts SKSEVR initialization through `DllMain`
  - does not require the `StartSKSE` export for VR
- Added a separate `SkyrimTogetherVRVrikBridge` SKSEVR plugin target:
  - exports `SKSEPlugin_Query`/`SKSEPlugin_Load`
  - requests the VRIK API through the documented `0xF2AFAEE6` SKSE messaging request to receiver `VRIK`
  - writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.vrik` with VRIK detection, API availability, bridge epoch, cached finger curl, and cached camera/smoothing offset fields
  - includes bridge loaded, sequence, and epoch markers; `VRPoseService` reads the bridge at the 20 Hz pose cadence, accepts sequence resets only when the epoch changes, and otherwise falls back to installed-detected/API-unavailable state
  - the VRIK bridge writer serializes cached snapshot data instead of polling VRIK API getters from its writer thread; live per-frame VRIK finger/camera snapshots still need a validated main-thread/update-phase callback before they are treated as real-time data
  - is packaged by the Windows build script under `Data\SKSE\Plugins`
- Added a separate `SkyrimTogetherVRHiggsBridge` SKSEVR plugin target:
  - exports `SKSEPlugin_Query`/`SKSEPlugin_Load`
  - requests the HIGGS API through the documented `0xF9279A57` SKSE messaging request to receiver `HIGGS` after SKSE `PostLoad` or `PostPostLoad`
  - registers HIGGS pulled, grabbed, dropped, stashed, consumed, collision, and two-handing callbacks
  - registers a post-VRIK/post-HIGGS update callback to snapshot HIGGS getters on the HIGGS-owned update phase; the bridge writer thread serializes only cached snapshot data and recent event fields
  - writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.higgs` with HIGGS detection, API availability, callback registration, snapshot availability/sequence, hand state, finger values, grab transform, and recent event fields
  - includes bridge loaded, sequence, and epoch markers; `VRHiggsService` reads the bridge at the bridge cadence, tolerates bridge reload sequence resets by epoch, and marks `SkyrimTogetherVR.higgsnet` ready after current bridge data has parsed
  - is packaged by the Windows build script under `Data\SKSE\Plugins`
- Added a separate `SkyrimTogetherVRPlanckBridge` SKSEVR plugin target:
  - exports `SKSEPlugin_Query`/`SKSEPlugin_Load`
  - requests the PLANCK API through the documented `0x92F38745` SKSE messaging request to receiver `PLANCK` after SKSE `PostPostLoad`
  - writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.planck` with bridge loaded/sequence/epoch markers, PLANCK detection, interface request/availability, cached build number, disabled current-hit polling, disabled last-hit-data probe state, and observation-only policy fields
  - leaves `GetLastHitData()` disabled with `planck.lastHitDataProbeEnabled=0`, `planck.lastHitDataReason=not_polled_nontrivial_return_boundary`, and `planck.lastHitDataBoundary=disabled_unvalidated_by_value_abi` until the by-value `PlanckHitData` ABI is validated against SKSEVR/CommonLib headers
  - does not call PLANCK mutators, actor ignore APIs, aggression APIs, ragdoll-collision ignore APIs, setting setters, HIGGS mutators, or hit replay logic
  - is packaged by the Windows build script under `Data\SKSE\Plugins`
- Added a staged HIGGS observation relay:
  - enables `VRHiggsService` in observation-only mode through `TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE=1`
  - reads `Data/SkyrimTogetherReborn/SkyrimTogetherVR.higgs` without requesting the HIGGS API from the main client
  - adds `RequestVRHiggsState`/`NotifyVRHiggsState` as a VR-only player-id HIGGS bridge state stream for connection-only mode
  - adds `VRHiggsRelayService` on the server to rebroadcast HIGGS state snapshots without requiring spawned character or object entities
  - writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.higgsnet` with local/remote HIGGS relay status while keeping mutating HIGGS APIs, object sync, grab/drop replay, collision changes, and physics mutation disabled
- Added VR Address Library CSV loading:
  - loads `Data/SKSE/Plugins/version-1-4-15-0.csv`
  - applies `SkyrimTogetherVR_AddressOverrides.csv`
  - applies `SkyrimTogetherVR_AE_to_SE.csv`
- Added a conservative VR bring-up mode:
  - launcher startup hook is still committed
  - only exact WinMain and `Main::Draw` startup/update-owner hooks are installed when `TP_SKYRIM_VR_ENABLE_BRINGUP_HOOKS=1`; VM-update and renderer-init diagnostic hooks are excluded from the default target
  - Skyrim gameplay hooks are skipped unless `TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS=1`
  - flat-SE byte-level inline patches require `TP_SKYRIM_VR_ENABLE_VALIDATED_INLINE_PATCHES`, a per-site `TP_SKYRIM_VR_INLINE_PATCH_*` flag, and a per-site `*_VR_RESOLVED` gate before they can compile into VR
  - all inline patch site flags and all `*_VR_RESOLVED` gates default to `0`, so unresolved SE addends cannot be enabled by setting only the broad/per-site inline-patch flags; these patches stay disabled in the default VR target even if the broader gameplay hook batch is enabled later for testing
- Added a HIGGS compatibility guard:
  - detects `higgs_vr.dll` or `higgs.dll` through the DLL load shim
  - detects installed HIGGS at `Data/SKSE/Plugins/higgs_vr.dll` or `Data/SKSE/Plugins/higgs.dll`
  - refuses to install the unvalidated gameplay hook batch while HIGGS is installed
  - keeps `SkyrimTogetherVRHiggsBridge` observation-only and does not call mutating HIGGS APIs
  - documents the HIGGS-owned hand/grab/weapon-collision/physics boundaries and staged bridge plan in `Docs/SkyrimVR/higgs-compatibility.md`
- Added a PLANCK compatibility guard:
  - detects `activeragdoll.dll` through the DLL load shim
  - detects installed PLANCK at `Data/SKSE/Plugins/activeragdoll.dll`
  - refuses to install the unvalidated gameplay hook batch while HIGGS or PLANCK is installed
  - writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.compatibility` with HIGGS/PLANCK installed/loaded state, hook mode, unvalidated-hook suppression state, explicit `gameplayMode`, per-lane policy fields, and `higgsPolicy`/`planckPolicy` observation-only markers for the companion panel, Papyrus telemetry, and runtime evidence checklist
  - keeps main-client PLANCK handling observation-only; the separate `SkyrimTogetherVRPlanckBridge` performs only the API heartbeat and cached build-number readout, keeps current-hit polling disabled, and does not mutate PLANCK actor ignore/settings state or replay PLANCK physical hits
  - documents the PLANCK active-ragdoll/HIGGS coupling and pinned 0.8.0 API shape in `Docs/SkyrimVR/planck-compatibility.md`
- Added one-time bring-up logging:
  - address database load count
  - runtime flag summary for connection-only, bring-up hook, unvalidated hook, and validated inline-patch state
  - client startup hook
  - resolved Main::Draw owner address
  - Main::Draw owner cadence, depth, gap, original duration, and permit sequence
  - initial VR HMD/hand/spell/arrow node pointer capture
- Added `Tools/SkyrimVR/audit_bringup_hooks.py` for repeatable validation that the default VR client keeps dangerous hooks/inline patches disabled while retaining only the exact WinMain and Main::Draw startup/update-owner hooks.
- `Tools/SkyrimVR/audit_bringup_hooks.py` also parses the effective VR client target configuration in `Code/client/xmake.lua`, so `SkyrimTogetherVRClient` must stay connection-only with unvalidated gameplay hooks, validated inline patches, and remote avatar actor targets disabled; `SkyrimTogetherVRClientAvatarSync` and `SkyrimTogetherVRGameplayClient` may enable the guarded remote-avatar actor-target validation path, and they still keep the dangerous hook gates disabled.
- Added VR connection-only mode:
  - `TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY=1` on the VR target
  - uses exact address-library ID `35560` `Main::Draw` as the candidate owner;
    the SKSEVR task bridge only publishes coalesced permits, and neither the
    VM-update nor outer main-loop hook is installed
  - selects `off`, `observe`, or `active` at runtime with
    `STVR_VM_UPDATE_MODE`, defaulting to `observe` so a new binary cannot run
    client work before its VM cadence and owner are proven
  - gates updates and connection behind a monotonic lifecycle epoch with closed
    RaceSex/loading UI and stable player/base/cell identities
  - keeps transport/session, runner, mod mapping, and string cache services active; the Discord SDK callback thread is disabled in connection-only mode
  - skips character sync, the full player/gameplay sync service set, behavior patching, DirectInput overlay toggles, and flat D3D11 overlay/render startup
  - adds `VRConnectionService` for guarded `STVR_AUTOCONNECT=host:port`, optional `STVR_PASSWORD`, and file-based command/status handoff without a flat overlay
  - adds `Tools/SkyrimVR/vr_handoff.py` as a desktop/launcher-side bridge for connect, disconnect, HIGGS bridge status, HIGGS relay status, and status/telemetry readout through the existing file bus
  - enables `DiscoveryService` in VR observation-only mode through `TP_SKYRIM_VR_ENABLE_DISCOVERY_SERVICE=1`
  - writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.discovery` with cell/grid/location and discovered actor form-id state while keeping actor sync consumers disabled
  - enables `PlayerService` in VR network-only mode through `TP_SKYRIM_VR_ENABLE_PLAYER_CELL_SERVICE=1`
  - sends only player cell/grid/level requests from the VR player service; death, respawn, difficulty, party, dialogue, and beast-form mutation paths remain disconnected
  - writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.playercell` with player readiness, online state, player id, level state, cell/grid request counters, last grid/cell ids, last grid/cell coords, and skipped/translation-failure counters for server-visible player-location validation
  - updates server exterior-cell handling so player cell state is recorded even before a spawned character entity exists
  - enables `VRMovementService` in observation-only mode through `TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE=1`
  - writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.movement` with local/remote player-id movement snapshots without enabling normal actor movement packets or remote actor movement application
  - enables `VRInventoryService` in observation-only mode through `TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE=1`
  - writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.inventory` with local weapon draw and equipped weapon/ammo/magic/power form ids plus explicit `policy=equipment_snapshot_only`, `fullInventoryTraversal=0`, `inventoryMutation=0`, `remoteEquipmentMutation=0`, and `normalInventoryPackets=0` boundary fields without enabling inventory hooks, inventory packets, equipped-form application, or `EquipManager` mutation
  - adds `RequestVREquipmentUpdate`/`NotifyVREquipmentUpdate` as a VR-only player-id equipment snapshot stream for connection-only mode
  - adds `VREquipmentRelayService` on the server to rebroadcast equipped-form snapshots without requiring spawned character entities
  - enables `VRActivationService` in observation-only mode through `TP_SKYRIM_VR_ENABLE_ACTIVATION_OBSERVATION_SERVICE=1`
  - writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.activation` with local/remote activation telemetry without enabling `ObjectService`, object assignment, lock sync, remote activation replay, or HIGGS-sensitive object mutation
  - adds `RequestVRActivationEvent`/`NotifyVRActivationEvent` as a VR-only player-id activation telemetry stream for connection-only mode
  - adds `VRActivationRelayService` on the server to rebroadcast activation snapshots without requiring spawned character or object entities
  - enables `VRMagicService` in observation-only mode through `TP_SKYRIM_VR_ENABLE_MAGIC_OBSERVATION_SERVICE=1`
  - writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.magic` with local/remote player-centric magic-effect telemetry without enabling `MagicService`, spell-cast hooks, magic-target hooks, remote spell casting, or remote magic-effect application
  - adds `RequestVRMagicEffectEvent`/`NotifyVRMagicEffectEvent` as a VR-only player-id magic-effect telemetry stream for connection-only mode
  - adds `VRMagicRelayService` on the server to rebroadcast magic-effect snapshots without requiring spawned character entities
  - enables `VRCombatService` in observation-only mode through `TP_SKYRIM_VR_ENABLE_COMBAT_OBSERVATION_SERVICE=1`
  - writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.combat` with local/remote player-centric hit-event telemetry without enabling `CombatService`, projectile hooks, projectile replay, combat target mutation, or remote actor mutation
  - adds `RequestVRCombatHitEvent`/`NotifyVRCombatHitEvent` as a VR-only player-id combat-hit telemetry stream for connection-only mode
  - adds `VRCombatRelayService` on the server to rebroadcast combat-hit snapshots without requiring spawned character entities
  - enables `VRProjectileService` in observation-only mode through `TP_SKYRIM_VR_ENABLE_PROJECTILE_OBSERVATION_SERVICE=1`
  - writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.projectile` with local/remote player-centric bow-shot and spell-cast intent telemetry without enabling `CombatService`, `ProjectileLaunchedEvent`, `ProjectileLaunchRequest`, `NotifyProjectileLaunch`, `HookLaunch`, `Projectile::Launch`, or remote projectile creation
  - adds `RequestVRProjectileEvent`/`NotifyVRProjectileEvent` as a VR-only player-id projectile intent telemetry stream for connection-only mode
  - adds `VRProjectileRelayService` on the server to rebroadcast projectile intent snapshots without requiring spawned character entities
  - enables `VRGrabService` in observation-only mode through `TP_SKYRIM_VR_ENABLE_GRAB_OBSERVATION_SERVICE=1`
  - writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.grab` with local/remote grab-release telemetry from `TESGrabReleaseEvent` without calling HIGGS APIs, enabling `ObjectService`, replaying remote grabs/drops, or mutating physics state
  - adds `RequestVRGrabEvent`/`NotifyVRGrabEvent` as a VR-only player-id grab-release telemetry stream for connection-only mode
  - adds `VRGrabRelayService` on the server to rebroadcast grab-release snapshots without requiring spawned character or object entities
  - enables `VRHiggsService` in observation-only mode through `TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE=1`
  - writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.higgsnet` with local/remote HIGGS bridge state without requesting HIGGS from the main client, replaying remote HIGGS events, mutating hand/collision state, or enabling object sync
  - adds `RequestVRHiggsState`/`NotifyVRHiggsState` as a VR-only player-id HIGGS state telemetry stream for connection-only mode
  - adds `VRHiggsRelayService` on the server to rebroadcast HIGGS state snapshots without requiring spawned character or object entities
  - enables `VRSaveLoadService` in observation-only mode through `TP_SKYRIM_VR_ENABLE_SAVELOAD_OBSERVATION_SERVICE=1`
  - writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.saveload` with `TESLoadGameEvent` counts, local player readiness after load, and raw plus server-mapped player/cell/worldspace identities without calling `BGSSaveLoadManager`, reconnecting transport, or mutating actor/gameplay state
  - retains `SkyrimTogetherUtils` Papyrus declarations and compiled `SkyrimTogetherVRConnectionMenu.psc/.pex` as staged future UI source; they are not bound or used as default VR connection controls
  - exposes `GetSkyrimTogetherStatusSummary()` to Papyrus so the VR message-box status surface can show remembered endpoint, local player id, discovery grid/cell/actor state, player cell/grid/level counters, pose/movement/equipment/activation/magic-effect/combat-hit/projectile/grab/HIGGS readiness, remote pose/movement/equipment/activation/magic-effect/combat-hit/projectile/grab/HIGGS counts, and save/load readiness counters plus raw/server load cell and worldspace ids
  - exposes `GetSkyrimTogetherTelemetryReadout()` to Papyrus so the VR message-box surface can show compact discovery, player cell/grid/level handoff state, local and remote pose/movement/equipment, action-lane, HIGGS samples, and save/load raw/server player/cell/worldspace ids without enabling the flat overlay or actor sync consumers
  - retains the VR package spell/effect source as dormant future UI work; `SkyrimTogetherVerifyLaunchScript` does not grant it on quest init or player load
  - adds `Tools/SkyrimVR/patch_vr_menu_esp.py` to reapply or check the staged VR connection spell/effect patch
  - removes the VR BSScript native-binding/register/signature detours and uses the standalone SKSEVR tick bridge for its only default Papyrus native
  - documents the mode in `Docs/SkyrimVR/connection-only-mode.md`
- Added a VR overlay/input boundary gate:
  - the default VR target uses the companion panel/file handoff instead of flat CEF/D3D11 overlay or unvalidated in-game Papyrus control
  - the renderer-init VR branch logs and returns before installing the desktop WndProc, flat overlay render system, or DirectInput toggle path
  - `Tools/SkyrimVR/audit_vr_overlay_boundary.py` verifies the connection-only service boundary, renderer-init early-return path, companion/browser surface, Papyrus `Debug.MessageBox` surface, and docs
  - documents the boundary in `Docs/SkyrimVR/vr-overlay-boundary.md`
- Added VR pose snapshot capture:
  - `PlayerCharacter::VRNodeData` provides a typed view of CommonLibSSE-NG's documented Skyrim VR node block at `PlayerCharacter + 0x3F0`
  - `VR/VRPlayerPose.*` reads HMD/hand/weapon/spell/arrow/bow nodes through `PlayerCharacter::GetVRNodeData()`
  - captures `NiAVObject::world` transform data for HMD, hands, weapon offsets, spell/magic origin/aim, arrow origin/destination, and bow nodes
  - writes local and remote weapon offset, spell/magic origin/aim, arrow origin/destination, and bow aim/rotation validity to `SkyrimTogetherVR.pose` so objective 9 can be runtime-validated from the packaged evidence zip
  - uses the local `NiAVObject` data layout instead of duplicating raw transform offset reads
  - VRIK IK sync is mandatory for SkyrimTogetherVR, so `VRPoseUpdate` now carries an explicit `VRVrikData` lane with VRIK detection state, left/right finger curl values, and camera/smoothing offsets from the `SkyrimTogetherVRVrikBridge` SKSEVR handoff file when available
  - `VRPoseService` writes local and remote `.vrik.*` fields into `Data/SkyrimTogetherReborn/SkyrimTogetherVR.pose`, and the VR Papyrus telemetry readout reports local/remote VRIK lane availability
  - `VRPoseService` ticks in the explicit avatar-sync and gameplay targets, logs local pose snapshots, sends `RequestVRPoseUpdate` at 20 Hz when connected, and stores remote `NotifyVRPoseUpdate` payloads; it is disabled in the default connection-proof target
  - `VRPoseService` ignores stale remote sequence numbers, clears remote pose state immediately on `DisconnectedEvent`, and prunes remote pose state on offline ticks, `NotifyPlayerLeft`, or three seconds without an update
  - `VRPoseService` writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.pose` with local/remote node transforms for launcher, external overlay, or future proxy-marker consumers
  - the explicit avatar-sync and gameplay targets construct `VRRemotePlayerService`, which writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.remoteplayers` with HIGGS-aware avatar-validation readiness by joining server player notifications with enabled relay maps, without spawning actors, creating marker objects, moving scene nodes, equipping items, replaying gameplay actions, or replaying HIGGS/PLANCK state; the default connection-proof target keeps this proxy disabled
  - the staged avatar-sync build constructs `VRPoseService` and `VRAvatarService` independently; pose/VRIK observations are not yet applied to CommonLib-owned remote actors
  - remote pose/equipment caches remain service-owned observation data and are not a second source of actor identity or mutation authority
  - the default `SkyrimTogetherVRClient` requests only gameplay-bridge lifecycle capability and does not create or move remote actors
  - `SkyrimTogetherVRClientAvatarSync` builds with `TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY=1` and `TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC=1`; CommonLib owns actor lifecycle, retained-identity root/spatial movement, and named humanoid graph snapshots, while skeleton, equipment, HIGGS, PLANCK, and FBT mutations remain disabled
  - the staged avatar-sync branch keeps `VRConnectionService`, so `SkyrimTogetherVRAvatarSync.exe` can still use `STVR_AUTOCONNECT` and the `SkyrimTogetherVR.command` file bus without depending on the flat overlay or enabling the full object/inventory/magic/combat gameplay services
  - `SkyrimTogetherVRGameplayClient` and `SkyrimVRImmersiveLauncherGameplay` build `SkyrimTogetherVRGameplay.exe` with connection-only mode off, the VR-safe observation graph enabled, and the CommonLib gameplay bridge owning remote actor lifecycle, retained-identity root/spatial movement, and named humanoid graph snapshots; legacy desktop mutation services, unvalidated flat-Skyrim hooks, and the flat D3D overlay remain disabled
  - `VRAvatarService` consumes canonical character spawn/remove and reference-movement messages, rejects malformed or stale location/graph data, and submits bounded lifecycle, root/spatial, and graph-snapshot commands to the CommonLib bridge; direct HMD/hand/VRIK skeleton writes remain disabled
  - the former `CharacterService` avatar handoff schema is legacy; `VRAvatarService` now writes the versioned `commonlib_bridge_v2` schema, and the strict avatar runtime audit requires an active lifecycle epoch, a successful remote actor create, an accepted canonical remote movement packet, at least one submitted root update and animation snapshot, no pending visibility spawn, and zero rejected commands, animation snapshots, or invalid transforms
  - the bridge-native avatar handoff reports local assignment, tracked/active avatars, create/update/destroy command counts, and same-space observations without claiming that VRIK skeleton writes are enabled
  - `Tools/SkyrimVR/vr_handoff.py` reads the `avatar` handoff file and shows the latest avatar target result in the companion panel `Remote Players` table; the VR Papyrus status and telemetry message boxes also include the avatar target summary
  - `VRPoseRelayService` rebroadcasts pose updates by server player id without requiring spawned actors or ownership sync, drops empty, zero-sequence, and non-increasing-sequence packets, throttles each server player id to the intended 20 Hz pose lane, and clears relay state on player leave
  - documents the current pose relay in `Docs/SkyrimVR/vr-pose-replication.md`
- Added VR movement snapshot relay:
  - `VRMovementUpdate` carries cell id, worldspace id, position, rotation, and direction
  - `VRMovementService` sends local movement snapshots at 20 Hz when connected, receives remote snapshots by server player id, and ignores stale remote sequence numbers
  - `VRMovementRelayService` stamps outgoing `NotifyVRMovementUpdate` messages with the authenticated server player id, drops zero-sequence and non-increasing movement updates, throttles each server player id to the intended 20 Hz lane, clears relay state on player leave, and relays accepted snapshots to other clients
  - the relay intentionally bypasses `ClientReferencesMoveRequest`, `ServerReferencesMoveRequest`, spawned actor lookup, ownership views, and actor movement mutation
  - documents the current relay in `Docs/SkyrimVR/vr-movement-relay.md`
- Added VR equipment snapshot relay:
  - `VREquipmentUpdate` carries weapon draw state plus left/right weapon, ammo, left/right spell, and shout/power `GameId` values
  - `VRInventoryService` sends equipment snapshots at 1 Hz when connected, receives remote snapshots by server player id, and ignores stale remote sequence numbers
  - the explicit avatar-sync and gameplay builds attach matching remote equipment snapshots to remote player entities as `RemoteVREquipmentComponent` data alongside `RemoteVRPoseComponent`, queue only the remote weapon draw/sheath visual through the existing delayed actor helper, and report equipment cache match/upsert/stale plus draw-queue counts through the avatar handoff
  - `VREquipmentRelayService` stamps outgoing `NotifyVREquipmentUpdate` messages with the authenticated server player id, drops zero-sequence and non-increasing equipment updates, throttles each server player id to the intended 1 Hz equipment lane, clears relay state on player leave, and relays accepted snapshots to other clients
  - the relay and avatar component path intentionally bypass `RequestEquipmentChanges`, `NotifyEquipmentChanges`, remote equipped-form application, inventory mutation, and `EquipManager`
  - documents the current relay in `Docs/SkyrimVR/vr-equipment-relay.md`
- Added VR activation telemetry relay:
  - `VRActivationEvent` carries activated object id, cell id, worldspace id, position, form type, and open state
  - `VRActivationService` observes Bethesda `TESActivateEvent` notifications, sends local activation events when connected, and receives remote snapshots by server player id
  - `VRActivationRelayService` stamps outgoing `NotifyVRActivationEvent` messages with the authenticated server player id, drops zero-sequence, missing-object-id, and non-increasing activation events, applies a per-player event-lane burst cap, clears relay state on player leave, and relays accepted events to other clients
  - the relay intentionally bypasses `ActivateRequest`, `NotifyActivate`, `ObjectService`, object assignment, lock sync, spawned actor/object entity lookup, and remote `TESObjectREFR::Activate`
  - documents the current relay in `Docs/SkyrimVR/vr-activation-relay.md`
- Added VR magic-effect telemetry relay:
  - `VRMagicEffectEvent` carries magic effect id, caster id, target id, caster/target positions, caster/target form types, and player caster/target flags
  - `VRMagicService` observes Bethesda `TESMagicEffectApplyEvent` notifications only when the local player is the caster or target, sends local magic-effect events when connected, and receives remote snapshots by server player id
  - `VRMagicRelayService` stamps outgoing `NotifyVRMagicEffectEvent` messages with the authenticated server player id, drops zero-sequence, missing-effect/context, and non-increasing magic-effect events, applies a per-player event-lane burst cap, clears relay state on player leave, and relays accepted events to other clients
  - the relay intentionally bypasses `SpellCastRequest`, `NotifySpellCast`, `InterruptCastRequest`, `NotifyInterruptCast`, `AddTargetRequest`, `NotifyAddTarget`, `RemoveSpellRequest`, `MagicService`, spawned actor entity lookup, `MagicCaster`, `MagicTarget`, `CastSpellImmediate`, and remote effect mutation
  - documents the current relay in `Docs/SkyrimVR/vr-magic-relay.md`
- Added VR combat-hit telemetry relay:
  - `VRCombatHitEvent` carries hitter id, hittee id, source id, projectile id, hitter/hittee positions, raw hit flags, PLANCK hit classification, hitter/hittee form types, and player hitter/hittee flags
  - `VRCombatService` observes Bethesda `TESHitEvent` notifications only when the local player is the hitter or hittee, sends local combat-hit events when connected, and receives remote snapshots by server player id
  - `VRCombatRelayService` stamps outgoing `NotifyVRCombatHitEvent` messages with the authenticated server player id, drops zero-sequence, missing player-hit context, and non-increasing combat-hit events, applies a per-player event-lane burst cap, clears relay state on player leave, and relays accepted events to other clients
  - the relay intentionally bypasses `ProjectileLaunchRequest`, `NotifyProjectileLaunch`, `ProjectileLaunchedEvent`, `Projectile::Launch`, `CombatService`, spawned actor entity lookup, `SetCombatTarget`, `SetCombatTargetEx`, and combat/actor mutation
  - documents the current relay in `Docs/SkyrimVR/vr-combat-relay.md`
- Added VR projectile intent telemetry relay:
  - `VRProjectileEvent` carries event kind, source id, weapon/ammo/spell ids, VR origin/destination positions, origin/destination validity, bow shot power, and sun-gazing state
  - `VRProjectileService` observes Bethesda `TESPlayerBowShotEvent` and local-player `TESSpellCastEvent` notifications, sends local projectile intent events when connected, and receives remote snapshots by server player id
  - `VRProjectileRelayService` stamps outgoing `NotifyVRProjectileEvent` messages with the authenticated server player id, drops zero-sequence, missing source/intent-context, and non-increasing projectile-intent events, applies a per-player event-lane burst cap, clears relay state on player leave, and relays accepted events to other clients
  - the relay intentionally bypasses `ProjectileLaunchRequest`, `NotifyProjectileLaunch`, `ProjectileLaunchedEvent`, `Projectile::Launch`, `Projectile::LaunchData`, `HookLaunch`, `CombatService`, spawned actor entity lookup, and remote projectile creation
  - documents the current relay in `Docs/SkyrimVR/vr-projectile-relay.md`
- Added VR grab/release telemetry relay:
  - `VRGrabEvent` carries grabbed object id, cell id, worldspace id, position, form type, and grabbed/released state
  - `VRGrabService` observes Bethesda `TESGrabReleaseEvent` notifications through the CommonLib `ScriptEventSourceHolder` grab-release event source, sends local grab events when connected, and receives remote snapshots by server player id
  - `VRGrabRelayService` stamps outgoing `NotifyVRGrabEvent` messages with the authenticated server player id, drops zero-sequence, missing-object-id, and non-increasing grab/release events, applies a per-player event-lane burst cap, clears relay state on player leave, and relays accepted events to other clients
  - the relay intentionally bypasses HIGGS API calls, `ObjectService`, remote grab/drop replay, object activation, projectile replay, hand/collision state, and physics mutation
  - documents the current relay in `Docs/SkyrimVR/vr-grab-relay.md`
- Added VR HIGGS observation relay:
  - `VRHiggsState` carries bridge/API availability, callback registration, two-handing state, left/right hand state, finger values, optional grab transform, and latest HIGGS event data
  - `VRHiggsService` reads the standalone SKSEVR HIGGS bridge file, sends local HIGGS state when connected, and receives remote snapshots by server player id
  - `VRHiggsRelayService` stamps outgoing `NotifyVRHiggsState` messages with the authenticated server player id, drops zero-sequence, empty-observation, and non-increasing HIGGS state updates, throttles each server player id to the intended 4 Hz HIGGS lane, clears relay state on player leave, and relays accepted snapshots to other clients
  - the relay intentionally bypasses `IHiggsInterface001`, HIGGS callback registration, mutating HIGGS API calls, object sync, remote grab/drop replay, hand/collision state mutation, and physics mutation in the main client
  - documents the current relay in `Docs/SkyrimVR/vr-higgs-relay.md`
- Added VR save/load lifecycle observation:
  - `VRSaveLoadService` observes Bethesda `TESLoadGameEvent` notifications and records load counts plus local player readiness after load
  - the handoff records raw local player/cell/worldspace form IDs and best-effort server-mapped `GameId` rows for later recovery-policy validation
  - the observer intentionally bypasses `BGSSaveLoadManager`, transport reconnect/disconnect policy, gameplay service activation, actor cleanup, and remote state mutation
  - documents the current observer in `Docs/SkyrimVR/vr-saveload-observation.md`
- Added a VR handoff bridge:
  - `Tools/SkyrimVR/vr_handoff.py` writes `SkyrimTogetherVR.command` connect/disconnect requests, writes the remembered `SkyrimTogetherVR.connection` endpoint/password for the in-game configured power, and reads the staged status, pose, movement, equipment, discovery, player cell, activation, magic, combat, projectile, grab, HIGGS bridge, HIGGS relay, and save/load readout files
  - `Tools/SkyrimVR/vr_handoff.py config` can read or write that remembered endpoint without issuing a connect command
  - `Tools/SkyrimVR/vr_handoff.py players` and the browser `Remote Players` table consolidate remote pose, movement, equipment, activation, magic, combat, projectile, grab, and HIGGS readouts by server player id for VR-safe two-client testing
  - `Tools/SkyrimVR/vr_handoff.py serve` exposes the same file-bus control/readout surface as a local browser companion panel for desktop, SteamVR desktop view, or future launcher embedding while the flat overlay stays disabled; `--open-browser` opens the panel URL after the server starts and `--no-open-browser` keeps it headless
  - `LaunchSkyrimTogetherVRCompanion.bat` is packaged with `Tools\SkyrimVR\vr_handoff.py` so Windows artifacts can start the companion panel from the extracted package or Skyrim VR folder without needing the source tree, opening the panel in the default browser by default
  - `AuditSkyrimTogetherVRRuntime-Windows.bat` is packaged with `Tools\SkyrimVR\audit_runtime_handoff.py` so post-run VR smoke-test evidence can be checked from the extracted package or Skyrim VR folder without launching Skyrim again
  - `AuditSkyrimTogetherVRAvatarSyncRuntime-Windows.bat` is packaged as the avatar-sync runtime audit wrapper for the mandatory two-client VRIK/HIGGS validation path
  - `AuditSkyrimTogetherVRGameplayRuntime-Windows.bat` is packaged as the full gameplay runtime audit wrapper for the `SkyrimTogetherVRGameplay.exe` package
  - `CollectSkyrimTogetherVREvidence-Windows.bat` is packaged with `Tools\SkyrimVR\collect_runtime_evidence.py` so `SkyrimTogetherVR_BuildManifest.json`, post-run logs, handoff files, discovered SKSEVR logs, the generated runtime audit, `runtime_checklist.json`, `runtime_checklist.txt`, and a manifest can be bundled for review without launching Skyrim again or writing into the game install by default
  - `CollectSkyrimTogetherVRAvatarSyncEvidence-Windows.bat` is packaged as the strict evidence collector shortcut for the mandatory two-client VRIK/HIGGS avatar-sync lane, forwarding `--avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs` plus any additional pose-context or relay flags
  - `CollectSkyrimTogetherVRGameplayEvidence-Windows.bat` is packaged as the strict evidence collector shortcut for the full gameplay package, forwarding `--gameplay --require-connected --require-remote-player --require-vrik --require-higgs --require-vr-pose-context --require-gameplay-relays`
  - `AuditSkyrimTogetherVREvidence-Windows.bat` is packaged with `Tools\SkyrimVR\audit_runtime_evidence_zip.py` so collected runtime evidence zips can be checked later for required files, package build manifest validity, embedded audit status, and checklist failures without access to the original game folder
  - `AuditSkyrimTogetherVRAvatarSyncEvidence-Windows.bat` is packaged as the matching strict evidence zip audit shortcut for the two-client VRIK/HIGGS avatar-sync lane, appending `--require-avatar-sync` to the normal evidence zip audit
  - `AuditSkyrimTogetherVRGameplayEvidence-Windows.bat` is packaged as the matching strict evidence zip audit shortcut for the full gameplay lane, appending `--require-gameplay` to the normal evidence zip audit
  - the runtime evidence checklist and evidence zip audit require the installed `SkyrimTogetherVR_BuildManifest.json` and validate that the Windows x64 package target set, copied artifacts, staged game files, companion helpers, and default/avatar-sync/gameplay mode match the run being audited
  - `--require-vr-pose-context` on the runtime handoff audit, collector, and evidence zip audit makes weapon offset, spell/magic origin/aim, and arrow/projectile origin nodes strict evidence requirements after a deliberate weapon/magic/projectile pose-context run
  - the runtime handoff audit, runtime evidence checklist, and evidence zip audit now validate the `SkyrimTogetherVR.discovery` schema structurally, including ready state, grid fields, cached cell/worldspace ids, actor count/limit, and contiguous capped actor form-id rows, across the default, avatar-sync, and gameplay packages
  - the same audit/evidence path validates the `SkyrimTogetherVR.playercell` schema structurally, including readiness, request counters, last grid/cell `GameId` fields, grid coords, and level-send state
  - per-lane runtime/evidence flags such as `--require-movement-relay`, `--require-equipment-relay`, `--require-activation-relay`, `--require-magic-relay`, `--require-combat-relay`, `--require-projectile-relay`, `--require-grab-relay`, `--require-higgs-relay`, and `--require-saveload-observer` support validating re-enabled gameplay systems one by one without failing unrelated lanes; required relay lanes now demand semantic local and remote payload evidence, including movement/equipment sequence values, activation/grab object `GameId` fields, magic effect plus caster/target/player context, combat player-side hit context, projectile source plus origin/destination or weapon/spell intent context, and HIGGS bridge/API/hand/event observation content; `--require-gameplay-relays` is available for deliberate full-lane validation runs
  - the VR launcher has a launcher-side companion panel link: `SkyrimTogetherVR.exe --companion`, `SkyrimTogetherVR.exe --companion-only`, and `STVR_LAUNCH_COMPANION=1` locate and start `LaunchSkyrimTogetherVRCompanion.bat`, open the local browser panel, and pass the selected Skyrim VR install as the helper `--game-path` without enabling the flat D3D11 overlay
  - `Tools/SkyrimVR/audit_vr_handoff.py` checks that the bridge and documentation continue to cover the expected file-bus contract
  - documents the current launcher-side bridge in `Docs/SkyrimVR/vr-handoff-bridge.md`
- Added a CommonLibSSE-NG layout audit in `Docs/SkyrimVR/commonlib-layout-audit.md`.
- Added narrow CommonLibSSE-NG-informed VR accessors for the staged connection-only paths:
  - `RuntimeLayout.h` now starts a typed ABI translation layer with generic offset helpers plus CommonLib/local shim traits for `TESForm`, `TESFullName`, `BGSKeywordForm`, `TESGlobal`, `Calendar`/`TimeData`, `TESCamera`, `PlayerCamera`, `NiCamera`, `TESActorBaseData`, `TESActorBase`, `TESRaceForm`, `TESNPC`, `BGSOutfit`, `TESLeveledList`, `BGSBipedObjectForm`, `TESObjectARMO`, `EffectSetting`, `MagicItem`, `SpellItem`, `EffectItem`, `MagicCaster`, `ActorMagicCaster`, `ActiveEffect`, `ValueModifierEffect`, `MagicTarget::AddTargetData`, `TESQuest`, `TESWorldSpace`, `TESObjectREFR`, `TESObjectCELL`, `LoadedCellData`, `TES`, `AIProcess`, `MiddleProcess`, `PlayerCharacter`, `Actor`, `CombatController`, `CombatTarget`, `CombatGroup`, `CombatInventory`, `BSScript::IVirtualMachine`, `EventDispatcherManager`, `Projectile`, `IMenu`, `UI`, `MenuControls`, `ExtraContainerChanges`, `ExtraFactionChanges`, inventory extra-data payloads, `ExtraTextDisplayData`, local equip hook payloads, `TESContainer`, `InventoryEntry`, `ExtraDataList`, `BGSSaveGameBuffer`, `BGSSaveFormBuffer`, `BGSLoadGameBuffer`, `BGSLoadFormBuffer`, and `BSGraphics::RendererData`; their semantic getters/setters or slot guards use those traits instead of embedding CommonLib offsets directly
  - `TESObjectREFR` base form/object reference, rotation, position, parent cell, loaded data, and extra data list offsets
  - `TESForm` form flags, form id, in-game form flags, and form type offsets, with the class's own save/change/identity helpers plus activation, equipment, object, magic/combat observer, and cell-filter form-type decisions routed through accessors
  - staged VR movement, equipment, discovery, activation, magic, combat, projectile, save/load, transport authentication, network-only player-service identity, gated quest-service identity reads, weather diagnostics, and debug/diagnostic form-id displays now use `GetFormIdData()` instead of raw `formID` member access
  - full actor-sync `CharacterService` identity, temporary-form, ownership-transfer, cell/worldspace, and delayed-3D bookkeeping paths now use CommonLib-aware accessors such as `GetFormIdData()`, `IsTemporary()`, `GetParentCellData()`, `GetPositionData()`, and `GetRotationData()` instead of raw SE-era member reads
  - full object/weather sync service identity, cell/worldspace, lock-level, and lock-flag bookkeeping paths now use CommonLib-aware accessors such as `GetFormIdData()`, `GetBaseFormData()`, `GetFormTypeData()`, `GetParentCellData()`, `GetPositionData()`, `GetLockLevelData()`, and `GetFlagsData()` instead of raw SE-era member reads
  - full magic, actor-value, subtitle, save/load form-id serialization, and lock-change hook bookkeeping now route form, lock, and save/load buffer reads through CommonLib-aware accessors such as `GetFormIdData()`, `GetLockLevelData()`, `BGSSaveFormBuffer::GetBufferData()`, and `BGSLoadFormBuffer::GetPositionData()` before those gameplay systems are considered for VR re-enablement
  - the unverified `BGSSaveLoadManager::SaveData` payload is gated through accessors that preserve the old non-VR save-stub behavior but no-op/null-return in VR until that manager payload is mapped
  - `TESGlobal` value offset, with player-service connection state, party-state writes, calendar sync, authentication time capture, and calendar debug display routed through `GetValueData()`/`SetValueData()`
  - `Calendar`/`TimeData` game-year/month/day/hour/days-passed/time-scale global pointer offsets, with calendar sync, authentication time capture, and calendar debug display routed through `TimeData` pointer accessors before touching the underlying `TESGlobal` values
  - `TESActorBaseData` actor-base flags and faction-array offsets, plus the `TESActorBase` actor-data subobject offset, with essential-flag staging and faction reads routed through `GetActorBaseData()`/`GetFactionsData()`
  - `TESLeveledList` entry pointer/count offsets and `ExtraFactionChanges` faction-change array offsets, with actor body-piece inference and actor extra-faction reads routed through `GetEntriesData()` rather than raw leveled-list or extra-data member traversal; the leveled-item body-piece path avoids runtime `Cast<TESLevItem>` while that RTTI ID remains unresolved in the VR address sources
  - inventory extra-data payload offsets for count, charge, health, enchantment, poison, soul, custom display names, and enchantment effect items, plus `EnchantmentItem` `ENIT` data offsets, with `TESObjectREFR::GetItemFromExtraData()`, spell classification/effect lookup, add-target event capture, and constructed enchantment/effect payloads routed through accessors before full inventory/magic sync is considered for VR re-enablement; the old custom display-name replay block remains disabled until its crash-prone construction path is revalidated
  - local `EquipData`, `MagicEquipData`, and `ShoutEquipData` hook payload offsets, with equipment event capture routed through accessors instead of direct `apData->count`, `apData->pSlot`, `apData->pEquipSlot`, or `apData->bQueueEquip`; these payloads are still tentative until the equip hooks are checked against VR disassembly
  - `SpellItem` `SPIT` data offsets, with spell-cast, add-target, and projectile concentration-spell filters routed through `GetCastingTypeData()` before full magic/projectile sync is considered for VR re-enablement
  - `MagicCaster` and `ActorMagicCaster` desired-target, current-spell, state, caster-actor, magic-node, and casting-source offsets, with spell-cast capture, interrupt capture, normal magic service actor lookup, and player debug current-spell display routed through accessors before normal magic sync is considered for VR re-enablement
  - `ActiveEffect` spell, target, elapsed-time, duration, magnitude, flags, and casting-source offsets, with perk-adjustment mutation, invisibility target lookup, and active-effect debug display routed through accessors before active-effect hooks are considered for VR re-enablement
  - `ValueModifierEffect` actor-value id and value offsets, with health-effect hook filtering routed through `GetActorValueIndexData()` before active-effect application is considered for VR re-enablement
  - `MagicTarget::AddTargetData` caster/spell/effect/magnitude/casting-source/dual-cast offsets, with add-target event capture and remote-effect replay construction routed through accessors before full magic sync is considered for VR re-enablement
  - `TESFullName`, `BGSKeywordForm`, `TESActorBase` full-name/keyword subobjects, `EffectSetting`, `MagicItem`, `TESQuest`, and `TESWorldSpace` name/keyword/archetype/effect-list/quest-state offsets, with spell-effect classification, dynamic enchant effect traversal, quest/debug/Discord/transport naming, NPC spawn logging, and quest type/stage/priority reads routed through accessors
  - `TESRaceForm` race pointer and selected `TESNPC` tail fields for class, combat style, original-race/template slots, default outfit, head-parts, tint color, relationships, and face/tint data, with NPC initialization/default outfit/head-part paths routed through accessors
  - `BGSOutfit` outfit item array, `BGSBipedObjectForm` slot mask, and `TESObjectARMO` body-slot mask offsets, with body-piece/default-outfit checks and armor form-type filters routed through accessors
  - `TESObjectCELL` cell flags, worldspace, loaded cell data, loaded-cell encounter zone, and a guarded reference-data accessor that disables full reference traversal in VR until the CommonLib reference container is ported; the non-VR fallback reference-array loop now uses local helper accessors rather than direct `pReferenceData->refArray`/`capacity` reads
  - `TES` singleton grid-cell pointer, center/current grid coordinates, interior/exterior cell buffers, and active image-space modifier list offsets used by discovery/debug/player revive paths
  - `AIProcess` and `MiddleProcess` middle-high process, direction, active-effect, commanding-actor, and equipped-object offsets
  - `Actor` magic target, actor value owner, actor state, bool-bits, current process, combat controller, cached magic casters, selected spells, selected power/shout, race, actor flag, and actor-value modifier offsets
  - `CombatController` combat group, inventory, attacker/target handles, started-combat byte, target selector array, active target selector, and cached attacker offsets for gated combat/debug paths
  - `CombatTarget`, `CombatGroup`, and `CombatInventory` target handle, attacker count, target array, and maximum range offsets for gated combat/debug paths; `CombatTargetSelector` controller/target-handle/priority/flag reads now route through local VR `RuntimeLayout` accessors, but remain tentative until the selector layout is validated against Skyrim VR
  - `BSScript::IVirtualMachine` native-binding slot, object size, and `bool BindNativeMethod` return shape for the VR connection-menu native registration path
  - `EventDispatcherManager`/CommonLib `ScriptEventSourceHolder` active event-source offsets, VR tail size, and active event payload layouts for activation, grab-release, hit, load-game, magic-effect, spell-cast, and bow-shot observers
  - `PlayerCharacter` VR node-data, `INFO_RUNTIME_DATA::skills`, `PlayerSkills::Data`, and `GAME_STATE_DATA::difficulty` offsets, plus empty guarded tint/objective list accessors until those VR runtime lists are mapped
  - player assignment packets now carry `HasFaceTints` and `HasQuestContent` availability bits so VR clients do not publish the current empty tint/objective fallback lists as authoritative data
  - character assignment now reads actor base form, parent cell, position, and rotation through the CommonLib-informed accessors rather than the shifted flat local fields
  - remote-spawn rotation setup now writes through `TESObjectREFR::SetRotationData()` instead of raw `Actor::rotation` members
  - actor-value setup, actor level-mod extra data, actor save bool-bits staging, inventory weapon-draw updates, magic selected-spell lookup/effect application, magic equipment snapshots, magic queue actor-name logging, beast-form/vampire-lord detection, animation event capture/replay/serialization, behavior-variable graph compatibility, interpolation direction updates, commanded-actor setup, actor flag helpers, faction reads, body-piece checks, actor spawn logging, spawned-reference base-form lookup, player NPC initialization, facegen base-form lookup, debug/diagnostic views, object discovery, activation checks, cell reference filtering, cell worldspace/loaded-data reads, unequip-all selected shout clearing, and pickup hooks now use the same accessor paths for the shifted actor/object/cell data
  - `ExtraContainerChanges::Data`, `TESContainer`, `InventoryEntry`, and `ExtraDataList` expose CommonLib-style wrappers through `RuntimeLayout`, and local inventory/equipped reads use those wrappers instead of raw container-change, base-container, equipped-entry, or extra-data-list dereferences
  - `BSGraphics::RendererData` exposes CommonLib-style renderer-data wrappers through `RuntimeLayout`, and render init now retrieves the render window, D3D11 device, and immediate context through those wrappers instead of raw `self->Data` field traversal
  - VR movement, inventory, activation, magic, combat, projectile, grab, save/load, connection, transport authentication, and player-service staging now use those accessors instead of raw shifted `Actor`/`TESObjectREFR` members or stale active-event payload fields
  - the full `TESObjectREFR`, `Actor`, and `PlayerCharacter` inheritance/vtable/runtime-data model is still not ported, so normal actor/object/gameplay sync remains gated
- Reconciled the local `Projectile::LaunchData` shim with CommonLibSSE-NG's field layout and routed projectile capture/replay through launch-data accessors:
  - asserts size `0xA8` and the projectile launch/replay field offsets from origin through `forceConeOfFire`
  - adds a VR-aware `Projectile` runtime power accessor for CommonLib's `0x188` power field and routes launch/hook power reads and writes through it instead of the shifted local `fPower` field
  - stops treating offset `0x50` as a Y angle and offset `0x7C` as a bool; the protocol fields remain only for compatibility
  - keeps normal projectile hooks and replay gated until the VR launch call site and HIGGS-sensitive projectile behavior are validated
- Reconciled the local menu/input shims with CommonLibSSE-NG's documented storage used by the current gated paths:
  - `IMenu` keeps the shared early fields and adds the VR-only `0x30..0x40` tail while preserving the SE `0x30` size
  - menu flag readers and writers now use `IMenu::GetMenuFlagsData()`/`SetMenuFlagsData()` instead of raw `uiMenuFlags` reads/writes
  - `UI` routes the CommonLib `menuStack`/`menuMap`/pause-counter offsets through `RuntimeLayout` and sends menu lookup/debug traversal through `GetMenuStackData()`/`GetMenuMapData()`
  - `MenuControls` now routes the CommonLib `0x88` size and the `0x80..0x84` state bytes through `RuntimeLayout` instead of the stale local `0x90` size, and `SetToggle()` writes through `SetToggleData()`
  - menu/input hooks remain disabled by default until VR menu stack behavior, `InputEvent`, and inline patch call sites are validated
- Patched the local `NiAVObject` shim with explicit SE/VR member layout assertions:
  - SE `userData` at `0xF8`, size `0x110`
  - VR `userData` at `0x110`, size `0x138`
  - shared `world` transform at `0x7C`
  - the VR-only `Unk_VRFunc` virtual slot is declared before the normal `NiAVObject` add-on virtuals so the local vtable boundary matches CommonLibSSE-NG VR at that point
  - the first add-on virtual slots now use CommonLib's `PerformOp`, `AttachProperty`, `SetMaterialNeedsUpdate`, `SetDefaultMaterialNeedsUpdateFlag`, and `GetObjectByName` names/signatures
  - unapproved `NiAVObject` virtuals are now audit-quarantined: `GetObjectByName` is the only approved add-on virtual call, and `audit_commonlib_layout.py` fails direct calls to `Unk_VRFunc`, `PerformOp`, `AttachProperty`, `SetMaterialNeedsUpdate`, or `SetDefaultMaterialNeedsUpdateFlag`
- Patched the local `NiNode` shim with CommonLib child-array layout:
  - `NiTObjectArray` now uses CommonLib-style `_data`, `_capacity`, `_freeIdx`, `_size`, and `_growthSize` fields with offset assertions
  - children are now `NiTObjectArray<NiPointer<NiAVObject>>`
  - SE `children` at `0x110`, size `0x128`
  - VR `children` at `0x138`, size `0x150`
  - `TESCamera::GetNiCamera()` now uses `GetChildrenData()` and unwraps `NiPointer` entries; `audit_commonlib_layout.py` fails direct child-array access outside the shim
- Patched the local camera shims against CommonLibSSE-NG:
  - `TESCamera` now records the CommonLib camera-root/current-state/enabled storage with offset and size assertions
  - `PlayerCamera` no longer duplicates the base camera fields; its `0x38..0x168` runtime tail is opaque until individual fields are needed and validated
  - `PlayerCamera::WorldPtToScreenPt3()` now relies on `TESCamera::GetNiCamera()` instead of checking a stale shadow `cameraNode` field
  - `audit_commonlib_layout.py` now fails stale camera field declarations and direct camera layout reads outside the camera shims
- Patched the local `NiCamera` projection shim against CommonLibSSE-NG:
  - SE `worldToCam`/`runtimeData2` offsets are asserted at `0x110`/`0x150`, size `0x188`
  - VR `worldToCam`, frustum pointer, opaque array block, and `runtimeData2` offsets are asserted at `0x138`, `0x178`, `0x180`, and `0x1CC`, size `0x208`
  - instance `WorldPtToScreenPt3()` now uses the object runtime-data matrix and port plus CommonLib static projection ID `70640` instead of the old unvalidated instance function ID `70639`
  - fixed the old projection wrapper bug that passed the Y output pointer for both Y and Z
  - `HUDMenuUtils::WorldPtToScreenPt3()` and the static projection helper now return false if their function/global pointers are unresolved
  - `audit_commonlib_layout.py` now fails stale `NiCamera` layout/projection signatures and reports a separate `NiCamera` projection boundary count
- Seeded `GameFiles/SkyrimVR` from the existing Skyrim game files.
- Updated the VR copy of `SkyrimTogetherVerifyLaunchScript.psc` source text.
- Added `Tools/SkyrimVR/audit_gamefiles.py` for repeatable ESP/Papyrus/behavior package validation.
- Added `Tools/SkyrimVR/audit_vr_only.py` for repeatable checks that this repository exposes only the Skyrim VR client/launcher target config and SKSEVR loader path.
- Added `Tools/SkyrimVR/audit_vr_services.py` for repeatable VR connection-only service gating validation.
- Added `Tools/SkyrimVR/audit_vrik_ik.py` for repeatable checks that the mandatory VRIK IK data lane remains wired through the pose protocol, service, telemetry, tests, and docs.
- Added `Tools/SkyrimVR/compile_papyrus.py` plus compile-only Skyrim Papyrus flags/import stubs.
- Regenerated `SkyrimTogetherVerifyLaunchScript.pex`, `SkyrimTogetherPlayerAliasScript.pex`, `SkyrimTogetherUtils.pex`, `SkyrimTogetherVRConnectionMenu.pex`, and `SkyrimTogetherVRConnectionSpellEffect.pex` with Caprica v0.3.0 for the initial VR menu pass. After the later `SetSkyrimTogetherConnectionConfig()` and `Configure*()` source additions, the final Windows handoff must run the build wrapper with `--compile-papyrus` so those PEX files are regenerated again before VR testing.
- Normalized two VR behavior variable filenames from `__Int.txt` to `__int.txt` so the existing case-sensitive loader recognizes them.

## Address Audit

Run:

```sh
Tools/SkyrimVR/audit_addresses.py
```

Current generated report:

- `Docs/SkyrimVR/address-audit.md`
- `GameFiles/SkyrimVR/Data/SKSE/Plugins/SkyrimTogetherVR_AE_to_SE.csv`
- `GameFiles/SkyrimVR/Data/SKSE/Plugins/SkyrimTogetherVR_AddressOverrides.csv`

Current result:

- 3080 unique address IDs scanned.
- 273 unique non-RTTI address IDs scanned.
- 0 missing non-RTTI address IDs after release/addrlib/exact `sse_vr` resolution.
- 47 RTTI IDs remain unresolved.
- 61 AE-to-SE helper CSV data rows and 2983 address override helper CSV data rows validated.
- `audit_addresses.py` now fails the readiness gate if non-RTTI address coverage regresses, a missing RTTI ID is used by `Cast<>`, or either generated helper CSV has a bad header, duplicate ID, unsorted rows, invalid source/offset fields, or too few data rows.

`addrlib.csv` entries are partially automated and must be validated before gameplay hooks are enabled.

## Inline Patch Audit

Run:

```sh
Tools/SkyrimVR/audit_inline_patches.py
```

Current generated report:

- `Docs/SkyrimVR/inline-patch-audit.md`
- `Docs/SkyrimVR/inline-patch-manifest.json`

Current result:

- The installed `SkyrimVR.exe` is Steam CEG-protected; `audit_inline_patches.py` now detects the entry stub, restores the stolen first 16 `.text` bytes, and AES-CBC decrypts `.text` into a temporary analysis image before reading bytes or running `llvm-objdump`.
- The audit also writes `Docs/SkyrimVR/inline-patch-manifest.json`, a machine-readable handoff manifest with each patch site's VR address, bytes, static check, `vrDecision`, `semanticValidationStatus`, `disassemblyReviewStatus`, `disassemblyLineCount`, `disassemblyContext`, `defaultEnablementAllowed`, and required activation gates.
- 19 inline patch sites tracked.
- 14 active in the normal upstream initializer batch.
- 0 active in the default VR target because `TP_SKYRIM_VR_ENABLE_VALIDATED_INLINE_PATCHES=0` and all per-site `TP_SKYRIM_VR_INLINE_PATCH_*` flags default to `0`.
- 14 active upstream inline patch sites are now VR resolution-gated; each requires both its per-site flag and its matching `*_VR_RESOLVED` gate before it can compile into a VR build.
- 2 source-blocked for VR because no valid VR call boundary is currently encoded.
- 5 disabled/commented in the current source.
- 2 active upstream call-site patches are not call opcodes in decrypted VR bytes.
- 14 active upstream sites still require manual semantic VR disassembly validation.
- 19 tracked sites and all 14 active upstream sites have copied VR disassembly context in the report and manifest; build-evidence zip audits now fail if an active site lacks that context.
- 0 inline patch sites are currently default-enableable.
- 0 VR guard policy failures and 0 audit safety failures; the audit now fails if any active upstream inline patch can compile into VR without a dedicated `*_VR_RESOLVED` gate defaulting to `0`, or if any inline patch becomes active in the default VR target.
- `UI active menu queue SwapCall` now has an audited VR candidate addend `0x67b` instead of the SE-derived `0x682`; the candidate is a real `call` opcode targeting `0x140f24510`, but still requires semantic hook review before enabling.
- `Render timer SwapCall` remains unresolved for VR; the SE addend `0x9` is not a call opcode in VR and no valid nearby in-text call target was found, so the source patch uses `TP_SKYRIM_ALLOW_SOURCE_BLOCKED_VR_INLINE_PATCH` and compiles out for every `TP_SKYRIM_VR` target regardless of broad, site, or `TP_SKYRIM_VR_INLINE_PATCH_RENDER_TIMER_VR_RESOLVED` flags.
- `BSTaskletManager constructor SwapCall` remains unresolved for VR; the SE addend `0x63` is not a call opcode in VR and no valid nearby in-text call target was found, so the source patch uses `TP_SKYRIM_ALLOW_SOURCE_BLOCKED_VR_INLINE_PATCH` and compiles out for every `TP_SKYRIM_VR` target regardless of broad, site, or `TP_SKYRIM_VR_INLINE_PATCH_TASKLET_MANAGER_CTOR_VR_RESOLVED` flags.
- 0 default-VR call-site opcode mismatches.

The VR target does not install the normal initializer batch by default. Keep `TP_SKYRIM_VR_ENABLE_VALIDATED_INLINE_PATCHES=0` until each byte-level inline patch has been validated against VR disassembly or replaced with a VR-specific hook. When a site is validated, enable only its matching `TP_SKYRIM_VR_INLINE_PATCH_*` flag and matching `*_VR_RESOLVED` gate rather than the whole batch.

## VR Service Audit

Run:

```sh
Tools/SkyrimVR/audit_vr_services.py
```

Current generated report:

- `Docs/SkyrimVR/vr-services-audit.md`

Current result:

- `VRMovementService` is wired only through `TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE`.
- `VRInventoryService` is wired only through `TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE`.
- `VRActivationService` is wired only through `TP_SKYRIM_VR_ENABLE_ACTIVATION_OBSERVATION_SERVICE`.
- `VRMagicService` is wired only through `TP_SKYRIM_VR_ENABLE_MAGIC_OBSERVATION_SERVICE`.
- `VRCombatService` is wired only through `TP_SKYRIM_VR_ENABLE_COMBAT_OBSERVATION_SERVICE`.
- `VRProjectileService` is wired only through `TP_SKYRIM_VR_ENABLE_PROJECTILE_OBSERVATION_SERVICE`.
- `VRGrabService` is wired only through `TP_SKYRIM_VR_ENABLE_GRAB_OBSERVATION_SERVICE`.
- `VRHiggsService` is wired only through `TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE`.
- `VRSaveLoadService` is wired only through `TP_SKYRIM_VR_ENABLE_SAVELOAD_OBSERVATION_SERVICE`.
- The VR target enables the observer flag and the non-VR client target disables it.
- `VRMovementService.cpp` contains the expected `SkyrimTogetherVR.movement` handoff fields and contains no forbidden normal actor-sync movement tokens.
- The VR movement relay protocol, server relay, and encoding tests contain the expected request/notify tokens.
- `VRInventoryService.cpp` contains the expected `SkyrimTogetherVR.inventory` handoff fields, including the explicit equipment-snapshot policy and zeroed full-inventory/mutation/normal-packet boundary fields.
- `VRInventoryService.cpp` contains no forbidden full-sync tokens for normal inventory/equipment requests, notify handlers, inventory/equip overrides, `EquipManager`, or full inventory accessors.
- The VR equipment relay protocol, server relay, and encoding tests contain the expected request/notify tokens.
- `VRActivationService.cpp` contains the expected `SkyrimTogetherVR.activation` handoff fields and contains no forbidden object-sync, lock-sync, remote activation replay, inventory/equip, or HIGGS-sensitive hook tokens.
- The VR activation relay protocol, server relay, and encoding tests contain the expected request/notify tokens.
- `VRMagicService.cpp` contains the expected `SkyrimTogetherVR.magic` handoff fields and contains no forbidden full magic-sync, spell-cast, add-target, remove-spell, remote effect application, or HIGGS-sensitive hook tokens.
- The VR magic relay protocol, server relay, and encoding tests contain the expected request/notify tokens.
- `VRCombatService.cpp` contains the expected `SkyrimTogetherVR.combat` handoff fields and contains no forbidden full combat-sync, projectile replay, combat target mutation, actor mutation, or HIGGS-sensitive hook tokens.
- The VR combat relay protocol, server relay, and encoding tests contain the expected request/notify tokens.
- `VRProjectileService.cpp` contains the expected `SkyrimTogetherVR.projectile` handoff fields and contains no forbidden full projectile-sync, launch hook, projectile replay, combat service, actor lookup, actor mutation, or HIGGS-sensitive hook tokens.
- The VR projectile relay protocol, server relay, and encoding tests contain the expected request/notify tokens.
- `VRGrabService.cpp` contains the expected `SkyrimTogetherVR.grab` handoff fields and contains no forbidden HIGGS API, grab/drop hook, object activation, object-sync, projectile, or physics mutation tokens.
- `Tools/SkyrimVR/audit_vr_grab.py` passes and confirms the CommonLib grab-release event-source offset, payload layout, observer gating, relay protocol, handoff file, and HIGGS-safe no-mutation boundary.
- `VRHiggsService.cpp` contains the expected `SkyrimTogetherVR.higgs` reader and `SkyrimTogetherVR.higgsnet` handoff fields and contains no forbidden HIGGS API, mutating HIGGS callback/control, object-sync, projectile, equipment, or gameplay mutation tokens.
- `Tools/SkyrimVR/audit_vr_higgs.py` passes and confirms the HIGGS state payload, observer gating, relay protocol, handoff file, companion/Papyrus readouts, and no-mutation boundary.
- `VRSaveLoadService.cpp` contains the expected `SkyrimTogetherVR.saveload` handoff fields, including raw and server-mapped player/cell/worldspace identities, and contains no forbidden save/load manager, reconnect/disconnect, gameplay service, actor mutation, or projectile-sync tokens.
- `PlayerService.cpp` contains the expected `SkyrimTogetherVR.playercell` handoff fields for the VR network-only player cell/grid/level request path. Every VR package with `TP_SKYRIM_VR_ENABLE_PLAYER_CELL_SERVICE=1`, including gameplay, returns before subscribing desktop death, difficulty, respawn, dialogue, party, and beast-form callbacks.
- Inventory prerequisite layouts have required static assertion/accessor tokens for `ExtraContainerChanges`, `TESContainer`, `InventoryEntry`, `ExtraDataList`, `AIProcess`, `MiddleProcess`, and the local equip hook payloads, including the middle-high process pointer plus active-effects, commanding-actor, direction, equipped-object, and equip-payload accessors.
- `TESObjectREFR::GetItemCountInInventory()` and `TESObjectREFR::GetInventory()` use the local CommonLib-style `InventoryChanges`, `TESContainer`, and `ExtraDataList` wrappers instead of raw `GetContainerChanges()->entries`, base-container `entries/count`, or extra-data-list storage traversal.
- The stale oversized `ExtraContainerChanges::Entry` padding token is absent.
- Projectile launch-data layout has required static assertion/accessor tokens for the CommonLibSSE-NG field sequence, `HookLaunch`/remote replay avoid raw `arData.*` and `launchData.*` payload reads/writes, and stale `fYAngle`/`bUnkBool1` layout tokens are absent.
- `IMenu`, `UI`, and `MenuControls` have required static assertion/accessor tokens for the CommonLibSSE-NG menu flag/menu-stack/input-control offsets, and stale `MenuControls` `0x90`/`canBeOpened` tokens are absent.
- `PlayerCharacter` has required CommonLibSSE-NG VR `INFO_RUNTIME_DATA::skills`, `PlayerSkills::Data`, and `GAME_STATE_DATA::difficulty` offset tokens, and debug/status views avoid raw shifted player skill/objective/cell/position/spell/shout reads.
- Player assignment uses tint/objective availability flags, and the server only stores those payloads when `HasFaceTints`/`HasQuestContent` are set.
- `RuntimeLayout.h` carries the required `TESFullName`, `BGSKeywordForm`, `TESGlobal`, `Calendar`/`TimeData`, `TESCamera`, `PlayerCamera`, `NiCamera`, `TESActorBaseData`, `TESActorBase`, `TESRaceForm`, `TESNPC`, `BGSOutfit`, `BGSBipedObjectForm`, `TESObjectARMO`, `EffectSetting`, `MagicItem`, `SpellItem`, `MagicCaster`, `ActorMagicCaster`, `ActiveEffect`, `ValueModifierEffect`, `MagicTarget::AddTargetData`, `TESQuest`, `TESWorldSpace`, `TESObjectREFR`, `TESObjectCELL`, `LoadedCellData`, `AIProcess`, `MiddleProcess`, `PlayerCharacter` runtime-data, `Actor`, `Projectile`, `IMenu`, `UI`, `MenuControls`, `ExtraContainerChanges`, `ExtraTextDisplayData`, local equip hook payloads, `TESContainer`, `InventoryEntry`, `ExtraDataList`, and `BSGraphics::RendererData` CommonLib/local shim offset traits, and their shims expose the required CommonLib-informed VR accessors, including magic target, camera root/projection, and actor value owner accessors, for the staged observer/authentication paths.
- `MagicCaster`/`ActorMagicCaster`/`Actor`/`ActiveEffect`/`ValueModifierEffect`/`MagicTarget::AddTargetData`/`SpellItem`/`EnchantmentItem` have required static assertion/accessor tokens for current-spell, desired-target, caster-actor, magic-node, casting-source, cached actor caster fields, active-effect spell/target/timing/magnitude/flag fields, value-modifier actor-value fields, add-target payload fields, spell casting-type fields, and enchantment `ENIT` fields; spell-cast/debug/caster-generation/active-effect/add-target/projectile paths avoid raw `pCasterActor`, `pCurrentSpell`, `hDesiredTarget`, `eCastingSource`, `Actor::casters[]`, `pSpell`, `pTarget`, `fElapsedSeconds`, `fDuration`, `fMagnitude`, `uiFlags`, `actorValueIndex`, `arData.p*`, `data.p*`, or `eCastingType` reads/writes.
- `OverlayService` reads actor health modifiers through `GetTemporaryHealthModifierData()` rather than the shifted flat `healthModifiers.temporaryModifier` field.
- The staged VR services, transport authentication, network-only player service, and gated quest service contain the expected accessor calls.
- `Tools/SkyrimVR/audit_commonlib_layout.py` passes and confirms the core actor/reference/equipment/player/combat/projectile/magic/container/animation paths plus behavior-variable compatibility, `DiscoveryService`, `TransportService`, `PlayerService`, `DiscordService`, and `QuestService` avoid direct `formID`, `formType`, form flags, name, keyword, effect-list, actor process, base form, extra-data, actor-state, actor-value-owner, and magic-target member reads. It also reports 0 `NiAVObject` virtual boundary failures, 0 `NiTObjectArray` layout failures, 0 `NiNode` layout boundary failures, 0 camera layout boundary failures, and 0 `NiCamera` projection boundary failures.
- The audit rejects raw shifted game-object member reads such as `pPlayer->baseForm`, `pPlayer->parentCell`, `pPlayer->position`, `pPlayer->rotation`, `pPlayer->actorState`, `pPlayer->magicItems`, `pPlayer->equippedShout`, and `pPlayer->currentProcess` in the VR staging paths.
- `DiscoveryService`, `DiscordService`, and the VR save/load status writer avoid the SE-era `PlayerCharacter::locationForm` and direct parent-cell reads in VR-observation paths; they use `GetCurrentLocation()` and `GetParentCellEx()` instead.

## GameFiles Audit

Run:

```sh
Tools/SkyrimVR/audit_gamefiles.py
```

Current generated report:

- `Docs/SkyrimVR/gamefiles-audit.md`
- `Docs/SkyrimVR/papyrus-rebuild.md`

Current result:

- `SkyrimTogether.esp` masters are `Skyrim.esm` and `Update.esm`; both exist in the installed Skyrim VR `Data` directory.
- No stale SE launch/path strings were found in the ESP.
- Papyrus sources/PEX files match by count: 12 `.psc` sources and 12 `.pex` files.
- `SkyrimTogetherVerifyLaunchScript.pex` no longer contains stale SE launch/path strings.
- `SkyrimTogetherUtils.pex` now contains the VR connection native declarations.
- `SkyrimTogetherVRConnectionMenu.psc/.pex` is present for the VR-safe message-box control surface and uses `GetSkyrimTogetherStatusSummary` plus `GetSkyrimTogetherTelemetryReadout`.
- `SkyrimTogetherPlayerAliasScript.pex` re-arms the tick bridge timer on player load; it does not grant a connection spell.
- `SkyrimTogetherVRConnectionSpellEffect.pex` is present and routes the spell effect to `ToggleConfigured()`.
- The VR ESP contains the `SkyrimTogetherVRConnectionSpellEffect` VMAD token and `Skyrim Together VR` spell text.
- `PetFramework_PetQuest.psc` now maps to `PetFramework_PetQuest.pex` with exact source/PEX case.
- Behavior variable suffixes are now clean for the current case-sensitive loader.
- The default VR-only gamefiles audit no longer requires the legacy `GameFiles/Skyrim` tree. Pass `--compare-se` when historical SE/VR behavior file parity should be reported.

## Build Verification

### Canonical avatar foundation hardening (2026-07-15)

- Packed server entity ID `0` is now valid. The VR client stores local
  assignment as an optional value instead of overloading zero as an unassigned
  sentinel.
- The CommonLib bridge identity is a checked EnTT slot/version split with
  modulo-4095 generation ordering, sentinel rejection, and exact round-trip
  tests. Same-generation visibility recreation requires a completed destroy
  followed by a newer action ID.
- Create/destroy results are correlated to exact pending action IDs; root
  failures are correlated to the latest sequence. Lost create acknowledgements
  use bounded idempotent retry, while accepted destroy uncertainty retires the
  lifecycle.
- Failed bridge retirement latches `cleanupRequired`, disables avatar
  capability, and blocks a later authentication acceptance until cleanup
  succeeds.
- Root interpolation uses shortest-arc quaternion nlerp and stops submitting
  after convergence. Fast remove/re-enter transitions retain a pending respawn
  until the old adapter handle is confirmed destroyed.
- `SkyrimTogetherVR.avatar` now reports `localServerAssigned`,
  `visualPolicy=player_template_fallback`, and `cleanupRequired`. The runtime
  handoff self-test deliberately validates a connected local server ID of zero.
- Linux `TPTests` pass with 371 assertions in 16 cases. The no-launch source,
  runtime-handoff, build-wrapper, CommonLib lock, and VRIK audits pass. The
  exact hardening commit `94544550` also passed a clean WinBoat gameplay build:
  Windows `TPTests` passed 526 assertions in 30 cases, all required targets
  compiled, and the package/evidence audits reported zero failures.

Verified on this Linux workspace:

```sh
Tools/SkyrimVR/audit_vr_services.py
Tools/SkyrimVR/audit_vr_grab.py
Tools/SkyrimVR/audit_vrik_ik.py
Tools/SkyrimVR/audit_remote_player_proxy.py
Tools/SkyrimVR/audit_vr_only.py
Tools/SkyrimVR/audit_commonlib_layout.py
Tools/SkyrimVR/audit_vr_handoff.py
Tools/SkyrimVR/vr_handoff.py self-test
Tools/SkyrimVR/audit_bringup_hooks.py
Tools/SkyrimVR/audit_smoke_package.py
Tools/SkyrimVR/audit_gamefiles.py
Tools/SkyrimVR/audit_addresses.py
Tools/SkyrimVR/audit_inline_patches.py
Tools/SkyrimVR/audit_vr_readiness.py
git diff --check
~/.local/bin/xmake build -y CommonLib
~/.local/bin/xmake build -y SkyrimEncoding
~/.local/bin/xmake build -y TPTests
~/.local/bin/xmake run TPTests
~/.local/bin/xmake build -y SkyrimTogetherServer
```

Current result:

- `SkyrimEncoding` builds with the VR pose, VR movement, VR equipment, VR activation, VR magic-effect, VR combat-hit, VR projectile intent, and VR grab-release protocol files.
- `CommonLib` builds in the current Linux configuration.
- `TPTests` builds and passes on Linux: 371 assertions in 16 test cases.
- `SkyrimTogetherServer` builds and includes `VRPoseRelayService`, `VRMovementRelayService`, `VREquipmentRelayService`, `VRActivationRelayService`, `VRMagicRelayService`, `VRCombatRelayService`, `VRProjectileRelayService`, `VRGrabRelayService`, and `VRHiggsRelayService`.
- `SkyrimTogetherServer` builds with player exterior-cell state updates that do not require a spawned character.
- Final source-side compile checks on this Linux host pass. The newest clean
  WinBoat/MSVC gameplay build is discovery/handshake commit `3cf4aa0e`; its
  503-file package and paired evidence archive passed independent audits and
  embed network version `stvr-v0.1.0-alpha.1-39-g3cf4aa0e`. The preceding
  `5f7943c2` runtime proved player-only discovery completes in Realm and reaches
  the dedicated server, then exposed an invalid WinBoat-generated client
  version `none`. The build now fetches tags and fails if generated or packaged
  network provenance is invalid. The exact `3cf4aa0e` server then admitted the
  client, proving the handshake; PDB-assisted Wine SEH tracing isolated the
  immediate client exit to the gameplay package incorrectly enabling desktop
  PlayerService death-system callbacks. The network-only predicate now follows
  the VR player-cell service flag and flushed stage checkpoints cover the next
  acceptance run. See
  `windows-gameplay-build-result-20260716-discovery-handshake.md`.
- `Tools/SkyrimVR/vr_handoff.py self-test` passes for temp-directory command writing, readout parsing, remote-player proxy aggregation, and consolidated remote-player table generation.
- `Tools/SkyrimVR/audit_runtime_handoff.py --self-test` covers temp-directory log breadcrumb, local pose/movement, VRIK detection plus VRIK API availability, HIGGS, per-player remote avatar blocker with VRIK API state, explicit avatar file presence, remote-player, strict weapon/magic/projectile pose-context checks, strict staged gameplay relay checks with semantic local and remote payload fields, and avatar-sync handoff validation; rerun it during the final validation phase after the source-only work is complete.
- `Tools/SkyrimVR/collect_runtime_evidence.py --self-test` and `Tools/SkyrimVR/audit_runtime_evidence_zip.py --self-test` are included in `audit_vr_readiness.py`; their fixtures now require `SkyrimTogetherVR_BuildManifest.json`, validate the package build manifest against avatar-sync mode, embed it in `manifest.json`, include it under `package/` in the evidence zip, and enforce manifest-requested runtime checklist lanes such as `requiredRemotePlayer`, `requiredWeaponPose`, `requiredMovementRelay`, and `avatarSyncAudit` even when the zip audit is run without repeating strict CLI flags. `avatarSyncAudit` itself now requires connection, local VRIK API, HIGGS bridge, remote-player proxy, remote VRIK avatar readiness, remote VRIK/HIGGS avatar readiness, and actor-target checklist lanes, so a two-client VRIK/HIGGS avatar evidence zip cannot be relaxed by omitting `requiredRemotePlayer`.
- `Tools/SkyrimVR/collect_build_evidence.py --self-test` and `Tools/SkyrimVR/audit_build_evidence_zip.py --self-test` pass and are included in `audit_vr_readiness.py`; they create and audit a no-build handoff archive with xmake/Python metadata, visible target output, Windows build-script audit output, built-package audit output when a package exists, copied text package metadata, copied Windows build wrappers such as `source\SetupSkyrimTogetherVRBuildEnv-Windows.bat` and `source\BuildSkyrimTogetherVR-Windows.ps1`, copied VR xmake target files such as `source\Code\client\xmake.lua`, copied source evidence for `inline-patch-manifest.json`, `inline-patch-audit.md`, `address-audit.json`, and `address-audit.md`, SHA-256 package file listings, manifest mode validation, runtime artifact listing checks, Windows build wrapper/target graph drift checks, zero default-active/default-enableable inline patch validation, positive disassembly context for every active inline patch site, zero missing core non-RTTI address validation, and command-failure reporting.
- Baseline runtime evidence now reports remote-player proxy, remote-avatar readiness, and HIGGS-aware remote-avatar readiness as `not_required` unless collected or audited with `--require-remote-player` or `--avatar-sync`, so single-client startup/pose/prerequisite evidence can be reviewed without falsely failing the two-client VRIK/HIGGS avatar lane.
- `Tools/SkyrimVR/audit_vr_handoff.py` covers the desktop/launcher-side bridge for the expected command, status, staged telemetry files, `players` command, browser `Remote Players` table, HIGGS bridge status, PLANCK bridge status, packaged helper, browser-opening flags, and launcher-side companion panel link.
- `Tools/SkyrimVR/audit_vr_overlay_boundary.py` passes and confirms the default VR build stays on the companion/Papyrus control surfaces, keeps the flat CEF/D3D11 overlay out of connection-only mode, leaves the renderer-init VR branch as a log-and-return path, and keeps gameplay-package `OverlayService`/debug/input callers guarded when the flat overlay app and desktop render window are absent.
- `Tools/SkyrimVR/audit_bringup_hooks.py` confirms the default VR target keeps
  the VM destructor hook, dangerous inline patches, and remote avatar actor
  targets disabled while preserving the opaque runtime-selectable VM observer,
  first-run startup, Main::Draw observer, render-init breadcrumbs, and the
  explicit avatar-sync-only target configuration.
- `SkyrimTogetherVRTickBridge` is a standalone SKSEVR plugin with normal
  Papyrus registration and one-shot task dispatch. It validates a process-local
  launcher endpoint before publishing one coalesced update permit. It never
  calls `World::Update()` on the SKSE task thread and has no HIGGS, PLANCK,
  VRIK, renderer, input, or gameplay hook dependency.
  `Tools/SkyrimVR/audit_tick_bridge.py` guards the boundary.
- `Tools/SkyrimVR/audit_smoke_package.py` covers the first VR smoke-test package manifest for the launcher-linked client executable, `SkyrimTogetherVR_BuildManifest.json`, `EarlyLoad.dll`, `TPProcess.exe`, VRIK/HIGGS/PLANCK SKSEVR bridge DLL locations, generated address helper CSVs, VR ESP/Papyrus entry files, and companion helper files.
- `Tools/SkyrimVR/audit_built_package.py` is available for the post-Windows-build package gate. It checks either the latest `artifacts/SkyrimTogetherVR/releasedbg` tree or the stable package snapshots under `artifacts/SkyrimTogetherVR/packages` for the default launcher, explicit avatar-sync executable, or DLL-only artifact set, `SkyrimTogetherVR_BuildManifest.json`, shared `EarlyLoad.dll`/`TPProcess.exe`/VRIK bridge/HIGGS bridge/PLANCK bridge runtime files, staged ESP/Papyrus/helper files, generated address CSV rows, SKSE bridge DLL placement, forbidden main `SkyrimTogetherVR.dll` output, x64 PE headers on runtime artifacts, packaged PEX bytecode tokens for the VR connection native/menu path, and optional installed SKSEVR/VRIK/HIGGS/PLANCK prerequisites. Its `--self-test` mode creates temporary package trees and verifies the package audit accepts clean default/avatar-sync/DLL-only layouts with matching build manifest target sets while rejecting stale opposite-mode launchers, missing `Data\SkyrimTogether.esp`, wrong manifest mode, stale/missing Papyrus bytecode tokens, and root-level staged files.
- `Tools/SkyrimVR/audit_vr_readiness.py` passes the no-launch source/prerequisite gates with a warning when the Windows-built package is absent, and fails with `--require-built-package` until the selected package path exists and passes the built-package audit. Use `VerifySkyrimTogetherVRWindowsPackages-Windows.bat` after `PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --all` to run readiness against `artifacts\SkyrimTogetherVR\packages\default` and `artifacts\SkyrimTogetherVR\packages\avatar-sync` plus the DLL-only snapshot audit.
- `Tools/SkyrimVR/install_built_package.py` and `InstallSkyrimTogetherVR-Windows.bat` are available for the final no-launch deploy step after the Windows package passes audit. Run once with `--package-only` when only the package layout/copy plan should be checked before target prerequisites are installed, then run once without `--install` for the strict target-prerequisite dry run, then run with `--install` to copy the package into the target Skyrim VR folder. Its `--self-test` mode verifies stale-root detection, strict-vs-package-only audit command construction, and cleanup behavior. Old root-level `SkyrimTogether.esp`, `scripts`, `meshes`, or `SkyrimTogetherRebornBehaviors` paths left by earlier bad package layouts are a hard handoff failure unless the installer is run with `--install --cleanup-stale-root-files` to remove them.
- `Tools/SkyrimVR/audit_fus_dll_compat.py` resolves the local `/home/obesecatlord/Games/FUS` wrapper folder to `/home/obesecatlord/LargeGames/FUS - MO2 2.5.1`, scans the selected `FUS RO DAH (Basic + Appearance + Gameplay)` profile, and also scans the deployed SkyrimVR root when present. It confirms scanned FUS native DLLs do not include SkyrimTogether hard-blocked DLL names; reports duplicate selected-profile deployed DLL paths, root proxy DLLs, and `EngineFixesVR.ini` settings as review warnings; validates the launcher-side hard block list, `EngineFixesVR.dll` compatibility rewrite policy, and HIGGS/PLANCK hook-gating tokens; and `--json-report artifacts/SkyrimTogetherVR/fus-dll-compatibility.json` writes a schema `skyrim_together_vr_fus_dll_compat_v1` report covering selected-profile DLL records plus installed-root DLL records.
- `Tools/SkyrimVR/vr_paths.py` is packaged with the companion helpers so `vr_handoff.py`, runtime audits, and evidence collection can resolve `--game-path`, `SKYRIMVR_PATH`, and `STVR_SKYRIM_VR` consistently outside the source tree. `BuildSkyrimTogetherVR-Windows.ps1` now computes and copies the local Python helper import closure for the packaged companion/runtime/evidence scripts. `Tools/SkyrimVR/audit_smoke_package.py` and `Tools/SkyrimVR/audit_built_package.py` compute the same closure, so a new local helper import cannot silently be omitted from the Windows package.
- `Tools/SkyrimVR/audit_higgs_bridge.py` passes and confirms the HIGGS bridge target, packaging, companion readout, documentation, callback registration, post-VRIK/post-HIGGS snapshot capture, no-mutating-call boundary, and `IHiggsInterface001` virtual method order against the upstream `_refs/higgs/include/higgsinterface001.h` reference.
- `Tools/SkyrimVR/audit_vrik_ik.py` covers the mandatory VRIK IK data lane in `VRPoseUpdate`, `VRPoseService`, Papyrus telemetry, encoding tests, and pose documentation.
- `Tools/SkyrimVR/audit_vr_services.py` passes and confirms the staged movement, inventory, activation, magic, combat, projectile, and save/load observers avoid the normal full-sync actor movement/equipment/inventory/object/magic/combat/projectile paths and save/load mutation paths while allowing only the VR movement/equipment/activation/magic/combat/projectile relay sends; it also checks grab service wiring, remote-player proxy readout-only wiring, player-cell handoff coverage, and relay protocol registration.
- `Tools/SkyrimVR/audit_remote_player_proxy.py` confirms the explicit broader-target `SkyrimTogetherVR.remoteplayers` contract matches the writer, companion parser, Papyrus readout, and docs while keeping the proxy service readout-only.
- `Tools/SkyrimVR/audit_vr_grab.py` passes and confirms the staged grab observer avoids HIGGS API mutation, grab/drop hooks, object activation, object sync, projectile replay, and physics mutation while allowing only the VR grab-release telemetry relay.
- `Tools/SkyrimVR/audit_vr_only.py` passes and confirms the client/launcher xmake graph, launcher target config, and script-extender loader are Skyrim VR-only.
- `Tools/SkyrimVR/audit_vr_only.py` and `Tools/SkyrimVR/audit_windows_build.py` now also guard the legacy root `Build.bat` so it cannot regress to the old generic `xmake project`/`xmake install -o distrib` path or reference the removed Skyrim SE client/launcher targets.
- `Tools/SkyrimVR/audit_gamefiles.py`, `Tools/SkyrimVR/audit_addresses.py`, `Tools/SkyrimVR/audit_inline_patches.py`, and `git diff --check` pass. `Docs/SkyrimVR/address-audit.json` now records schema `skyrim_together_vr_address_audit_v1`, 3080 referenced address IDs, 273 non-RTTI IDs, zero missing non-RTTI IDs, zero missing RTTI IDs used by `Cast<>`, 61 AE-to-SE helper rows, and 2983 address override helper rows for regression checks.
- Inventory/container entry shims now assert the CommonLib-informed offsets needed by the read-only equipment observer, while full inventory traversal remains gated.
- The current Linux xmake target list exposes only server/test/common-library targets; Windows client/launcher artifacts still require the Windows or Wine/MSVC xmake configuration.
- `BuildSkyrimTogetherVR-Windows.bat` is available for Windows verification, `BuildSkyrimTogetherVR-DLL-Windows.bat`, `BuildSkyrimTogetherVR-DLLs-Windows.bat`, or `BuildSkyrimTogetherVR-ClientDLL-Windows.bat` is available when only the DLL-producing targets are needed, `BuildSkyrimTogetherVR-AvatarSync-Windows.bat` builds the explicit two-client VRIK/HIGGS remote-avatar validation package, and `BuildSkyrimTogetherVR-Gameplay-Windows.bat` builds the staged gameplay package. Pass `-PreflightOnly` to the wrappers to configure/check the Windows xmake target graph, verify staged VR game files, resolve packaged Python helper imports, and exit before any target build. `BuildAuditCollectSkyrimTogetherVR-Windows.bat` combines build/package audit, evidence collection, and evidence zip audit for default, `--avatar-sync`, `--gameplay`, or `--dll-only` handoffs while still collecting evidence on failure. `PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat` is the top-level Windows handoff driver; by default it runs the default and avatar-sync packages, `--include-gameplay` adds the gameplay package, and `--all` runs default, avatar-sync, gameplay, and DLL-only package handoffs. The shared script copies the companion panel, default/avatar-sync/gameplay runtime audit wrappers, default/avatar-sync/gameplay evidence collection/audit wrappers, packaged Python helper closure, writes `SkyrimTogetherVR_BuildManifest.json`, writes stable snapshots under `artifacts\SkyrimTogetherVR\packages\default`, `avatar-sync`, `gameplay`, and `dll-only`, and fails if a requested target's expected runtime/static artifact is missing. The built-package audit rejects stale opposite-mode launchers and mismatched build manifest modes for default, avatar-sync, gameplay, and DLL-only packages so back-to-back builds cannot pass with stale target metadata.
- The launcher-side companion link is implemented in source and audited statically, including selected Skyrim VR install propagation. The launcher also exports `STVR_GAME_PATH`, and the client plus SKSEVR bridge DLLs use `Code/vr_common/VRHandoffPath.h` to resolve `Data\SkyrimTogetherReborn` from that selected install before falling back to executable/current-directory paths. This still needs Windows artifact build and runtime validation.

Windows/MSVC through the checked-in WinBoat helper is the supported client build path. The Linux xmake graph exposes Wine wrapper targets, but native Wine package/toolchain probing remains less reliable than the clean detached WinBoat workflow. Use `Tools/SkyrimVR/build_winboat_gameplay.sh` after committing and pushing the exact source revision.

## Unbuilt Gameplay Source Review Closure (2026-07-16)

The post-fix Sol max review found no P0 or confirmed ABI/wire blocker. Its P1
capture/transport findings are implemented through bridge ABI 12, capability
revision 20, authenticated `ArmLocalCapture`, accepted native baselines, mapped
FIFO/coalesced transport retry, and owner-thread enforcement. Final equipment
now waits for a correlated successful CommonLib end result before committing
its replay ledger; dropped bridge acknowledgements time out into bounded,
idempotent equipment or spawn-state retries. Server equipment ledgers fail
closed at capacity and clean up on player/entity lifecycle events; appearance
replay uses normal range
interest in both directions after committed cell transitions. The complete
disposition is
`full-gameplay-source-postfix-senior-disposition-20260716.md`.
The subsequent main integration and follow-up review provenance are recorded in
`full-gameplay-source-integration-disposition-20260716.md`.

This tranche remains intentionally unbuilt. Address artifact regeneration,
static/source checks, commit/push, the single WinBoat gameplay build, package
audit, handoff refresh, and exact client/server deployment are the next stage.

## Remaining High-Risk Work

- Run `PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --all` from Windows with Visual Studio/MSVC available and verify the default, avatar-sync, gameplay, and DLL-only artifacts compile plus their package audits and build-evidence audits pass, then run `VerifySkyrimTogetherVRWindowsPackages-Windows.bat --skyrim-vr "C:\Path\To\SkyrimVR"` before copying any package into Skyrim VR.
- Run the explicit VRIK/HIGGS remote-avatar validation package from `artifacts\SkyrimTogetherVR\packages\avatar-sync`, then collect strict avatar-sync runtime evidence with `CollectSkyrimTogetherVRAvatarSyncEvidence-Windows.bat --require-vr-pose-context`.
- Run the staged gameplay package from `artifacts\SkyrimTogetherVR\packages\gameplay`, then collect strict gameplay runtime evidence with `CollectSkyrimTogetherVRGameplayEvidence-Windows.bat`.
- Validate every inline patch against Skyrim VR disassembly before enabling gameplay hooks.
- Validate `SkyrimTogetherVR.discovery` in-game for the default, avatar-sync, and gameplay packages so actor discovery evidence is available before promoting any package to a recommended runtime path.
- Validate VR player cell/grid/level request flow against a server in the default, avatar-sync, and gameplay packages so the shared network-only `PlayerService` path is covered by the same evidence.
- Validate `SkyrimTogetherVR.movement` in-game while walking/turning before adding an in-game remote movement visual consumer or enabling normal actor movement application.
- Validate `SkyrimTogetherVR.inventory` in-game while changing weapons, spells, ammo, and powers, and validate the staged remote draw/sheath visual cue before enabling normal inventory/equipment application.
- Validate `SkyrimTogetherVR.activation` in-game while activating doors, containers, harvestables, books, and HIGGS-grabbable objects before adding an in-game remote activation visual consumer or enabling normal object activation replay.
- Validate `SkyrimTogetherVR.magic` in-game while self-casting, applying outgoing effects, and receiving hostile or healing effects before adding an in-game remote magic visual consumer or enabling normal spell-cast/effect application.
- Validate `SkyrimTogetherVR.combat` in-game while hitting actors and being hit before adding an in-game remote combat visual consumer or enabling normal projectile/combat sync.
- Validate `SkyrimTogetherVR.projectile` in-game while firing bows and casting projectile spells before adding an in-game remote projectile visual consumer or enabling normal projectile replay.
- Validate `SkyrimTogetherVR.grab` in-game with vanilla grabs and HIGGS object grabs/drops before adding an in-game remote grab visual consumer, HIGGS API callback bridge, or object interaction sync.
- Validate `SkyrimTogetherVR.compatibility` and `SkyrimTogetherVR.planck` in-game with HIGGS and PLANCK installed so runtime evidence proves installed/loaded state, PLANCK bridge interface/build availability, disabled current-hit and last-hit polling, observation-only policy, and unvalidated-hook suppression before adding extended PLANCK hit-data relay or physics mutation.
- Validate `SkyrimTogetherVR.higgs` and `SkyrimTogetherVR.higgsnet` in-game with HIGGS installed while pulling, grabbing, dropping, stashing, consuming, colliding, and two-handing objects before adding a remote HIGGS visual consumer or object interaction sync.
- Validate `SkyrimTogetherVR.saveload` in-game while loading saves offline, connecting, and connected before adding reconnect/resync or actor cleanup behavior.
- Continue porting core RE layouts and vtable assumptions using CommonLibSSE-NG VR headers:
  - remaining `NiAVObject` virtual names/signatures after `GetObjectByName` before allowing any additional add-on virtual calls
  - `Actor`
  - `PlayerCharacter`
  - `TESObjectREFR`
  - `Projectile`
  - `IVirtualMachine`
  - menus/input/camera classes
  - VR runtime validation of inventory/container traversal, packet flow, and remote equipment mutation
  - see `Docs/SkyrimVR/commonlib-layout-audit.md`
- Replace or branch flat D3D11/Win32 overlay assumptions for an in-game VR overlay.
- Validate the launcher-side companion panel link from a Windows-built `SkyrimTogetherVR.exe`.
- Runtime-validate `SkyrimTogetherVRAvatarSync.exe` with two clients, VRIK installed, and HIGGS installed, then implement a separately reviewed remote-embodiment path; direct scene-node writes remain gated off.
- Runtime-validate the avatar-sync `SkyrimTogetherVR.remoteplayers` proxy readout with two clients before considering any default-target expansion.
- Do not use the dormant `Skyrim Together VR` spell/power as a connection control until the flat-client BSScript native ABI has been separately ported and reviewed.
- Runtime-validate the dedicated HIGGS bridge plus networked HIGGS intent/state relay; keep mutating HIGGS APIs disabled until ownership/conflict handling exists.
- Re-enable systems one by one after validation:
  - movement
  - equipment
  - activation
  - inventory
  - magic
  - projectiles
  - combat
  - full save/load recovery
