# Connection-Only Mode

The default VR target currently builds with `TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY=1`.

This mode is for bringing up the client transport/session path before actor and gameplay sync are safe in Skyrim VR.

The default target enables only transport, mod mapping, connection handoff,
discovery, and network-only player cell synchronization. Pose, movement,
inventory, action observation, HIGGS relay, save/load observation, and the
remote-player proxy are compiled behind options that are disabled for the
default target and enabled by the explicit avatar-sync/gameplay targets.

The separate `SkyrimTogetherVRClientAvatarSync` and `SkyrimVRImmersiveLauncherAvatarSync` targets are the opt-in VRIK/HIGGS remote-avatar validation build. They keep staged connection-only mode enabled, add `TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC=1` to instantiate `CharacterService`, and enable `TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS=1`, while the default `SkyrimTogetherVRClient` target keeps actor mutation disabled.

The separate `SkyrimTogetherVRGameplayClient` and `SkyrimVRImmersiveLauncherGameplay` targets build `SkyrimTogetherVRGameplay.exe`. That package turns connection-only mode off and enables the normal gameplay service set plus VR relay/remote-avatar services, while still keeping the unvalidated flat-Skyrim hook batch, validated inline patches, and flat overlay disabled by default.

## Enabled

- The main-loop address is observed only. `World::Update() is not called from a VR hook`;
  the task bridge below is its sole default scheduler.
- `RunnerService` drains queued main-thread work.
- `SkyrimTogetherVRTickBridge.dll` registers one normal SKSEVR Papyrus native.
  The attached quest uses a 50 ms single-update timer to request one coalesced
  `SKSETaskInterface` task. Only that task dispatches `World::Update()` after
  endpoint, epoch, allocation-base, page-protection, and thread checks.
  It rejects any runtime other than Skyrim VR 1.4.15 with SKSEVR 2.0.12 (or a
  compatible later release index) because the legacy native-function ABI is
  runtime-address-specific.
- `TransportService` can poll/authenticate through the verified task path.
- `ModSystem` receives server mod mappings.
- `StringCacheService` receives and clears string-cache state.
- `DiscordService` is present so authentication can include Discord state when available.
- `VRConnectionService` owns environment autoconnect plus a file-based command/status handoff for launcher or future VR overlay integration.
- `Tools/SkyrimVR/vr_handoff.py` provides a desktop-side bridge and local browser companion panel for that file handoff, so connect/disconnect/status/readout workflows can be driven without the flat overlay or DirectInput hooks. Its `config` command and browser connect action write `SkyrimTogetherVR.connection` as the remembered endpoint. The launcher accepts `--no-open-browser` when the panel should stay background-only. Its `players` command consolidates broader avatar-sync/gameplay readouts when those targets are in use; those optional files are not required from the default target.
- `DiscoveryService` runs in narrow VR observation mode, discovering cell/grid/location changes while actor-handle enumeration remains disabled in the default target.
- `PlayerService` runs in VR network-only mode, sending cell/grid and level updates while leaving death, difficulty, party, dialogue, and respawn handling disconnected.
- `PlayerService` writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.playercell` with process session, accepted-connection generation, readiness, request counters, and last grid/cell IDs so fresh server-visible synchronization can be distinguished from stale files.
- `SkyrimTogetherUtils.psc` and `SkyrimTogetherVRConnectionMenu.psc/.pex` remain
  staged future UI source, but the default VR target does not register their
  flat-client natives. Do not use the bundled spell/effect as a connection
  control until that ABI is separately ported and reviewed. That deferred menu
  uses `Debug.MessageBox`; it is not part of the default control path.
- `SkyrimTogetherVerifyLaunchScript` and its player-load alias only keep the
  standalone tick bridge timer armed. They do not grant a connection spell.
- The default VR target installs no BSScript native-binding, registration, or
  signature detours. The standalone SKSEVR plugin owns its one normal Papyrus
  registration.
- `VRPoseService` captures local HMD, hand, weapon offset, spell/magic origin/aim, arrow-origin, and bow-node transforms.
- VRIK IK sync is mandatory for SkyrimTogetherVR, so the pose stream includes a VRIK lane for detection state, finger curl values, and camera/smoothing offsets sourced from `SkyrimTogetherVRVrikBridge` when its SKSEVR handoff file is available.
- `VRPoseService` sends `RequestVRPoseUpdate` at 20 Hz after connection and stores incoming `NotifyVRPoseUpdate` payloads by remote player id.
- `VRPoseService` writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.pose` as a read-only local/remote pose and VRIK IK handoff for external overlays, launchers, and the required remote-player avatar consumer.
- In explicit avatar-sync/gameplay targets, `VRRemotePlayerService` writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.remoteplayers` with remote HMD, left-hand, right-hand, VRIK, movement, same-space, and HIGGS-aware readiness fields. It is disabled in the default connection-proof target.
- `VRMovementService` runs in VR observation-only mode, polling local player cell/worldspace, position, rotation, and direction without enabling normal actor movement packets or remote actor movement.
- `VRMovementService` sends `RequestVRMovementUpdate` snapshots at 20 Hz when connected, stores incoming `NotifyVRMovementUpdate` snapshots by server player id, and keeps the normal actor/entity movement sync path disabled.
- `VRMovementService` writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.movement` as a local/remote movement handoff for validation before actor movement sync is enabled.
- `VRInventoryService` runs in VR observation-only mode, polling local weapon draw and equipped weapon/ammo/magic/power forms without installing inventory hooks, sending inventory packets, or mutating remote actors.
- `VRInventoryService` sends `RequestVREquipmentUpdate` snapshots at 1 Hz when connected, stores incoming `NotifyVREquipmentUpdate` snapshots by server player id, and keeps the normal actor/entity equipment sync path disabled.
- `VRInventoryService` writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.inventory` as a local/remote equipment handoff for validation before inventory/equipment sync is enabled. The readout reports `policy=equipment_snapshot_only` plus zeroed full-inventory traversal, mutation, remote-equipment mutation, and normal-inventory-packet fields so the staged equipment lane cannot be confused with full inventory sync.
- `VRActivationService` runs in VR observation-only mode, listening to `TESActivateEvent` and recording activated object/cell/worldspace ids without installing the normal activation hook, enabling `ObjectService`, or replaying remote activations.
- `VRActivationService` sends `RequestVRActivationEvent` snapshots when local activations occur, stores incoming `NotifyVRActivationEvent` snapshots by server player id, and keeps normal object assignment, lock sync, and remote `TESObjectREFR::Activate` calls disabled.
- `VRActivationService` writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.activation` as a local/remote activation telemetry handoff for validation before object activation sync is enabled.
- `VRMagicService` runs in VR observation-only mode, listening to player-centric `TESMagicEffectApplyEvent` notifications without installing spell-cast hooks, magic-target hooks, or enabling `MagicService`.
- `VRMagicService` sends `RequestVRMagicEffectEvent` snapshots when local player-centric magic effects occur, stores incoming `NotifyVRMagicEffectEvent` snapshots by server player id, and keeps normal spell-cast, interrupt-cast, add-target, remove-spell, and remote magic mutation disabled.
- `VRMagicService` writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.magic` as a local/remote magic-effect telemetry handoff for validation before magic sync is enabled.
- `VRCombatService` runs in VR observation-only mode, listening to player-centric `TESHitEvent` notifications without installing projectile hooks, enabling `CombatService`, or mutating remote combat state.
- `VRCombatService` sends `RequestVRCombatHitEvent` snapshots when local player-centric hit events occur, stores incoming `NotifyVRCombatHitEvent` snapshots by server player id, and keeps normal projectile replay, combat target updates, and actor mutation disabled.
- `VRCombatService` writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.combat` as a local/remote combat-hit telemetry handoff, including hitter, hittee, source, projectile, raw hit flags, and PLANCK hit classification, for validation before combat or projectile sync is enabled.
- `VRProjectileService` runs in VR observation-only mode, listening to player-centric `TESPlayerBowShotEvent` and `TESSpellCastEvent` notifications without installing projectile launch hooks, enabling `CombatService`, or creating remote projectiles.
- `VRProjectileService` sends `RequestVRProjectileEvent` snapshots when local bow-shot or spell-cast intent events occur, stores incoming `NotifyVRProjectileEvent` snapshots by server player id, and keeps normal `ProjectileLaunchRequest`, `NotifyProjectileLaunch`, and `Projectile::Launch` replay disabled.
- `VRProjectileService` writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.projectile` as a local/remote projectile intent telemetry handoff for validation before projectile sync is enabled.
- `VRGrabService` runs in VR observation-only mode, listening to `TESGrabReleaseEvent` through the CommonLib event-source offset without calling HIGGS APIs, installing grab/drop hooks, enabling `ObjectService`, or mutating physics state.
- `VRGrabService` sends `RequestVRGrabEvent` snapshots when local grab/release notifications occur, stores incoming `NotifyVRGrabEvent` snapshots by server player id, and keeps remote grab/drop replay disabled.
- `VRGrabService` writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.grab` as a local/remote grab/release telemetry handoff for validation before any HIGGS-specific bridge or object interaction sync is enabled.
- The client writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.compatibility` with HIGGS/PLANCK installed/loaded state, hook mode, unvalidated-hook suppression state, `gameplayMode`, remote-avatar/proxy policy, movement/equipment/action/HIGGS/save-load lane policy markers, and observation-only HIGGS/PLANCK policy markers for runtime validation before any PLANCK or HIGGS physics mutation is considered.
- `SkyrimTogetherVRHiggsBridge` writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.higgs` as an observation-only HIGGS bridge handoff with bridge loaded/sequence/epoch markers, API availability, callback registration, hand state, finger values, grab transform, and recent HIGGS events.
- `SkyrimTogetherVRPlanckBridge` writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.planck` as an observation-only PLANCK bridge handoff with bridge loaded/sequence/epoch markers, API request state, interface availability, cached build number, disabled current-hit polling, disabled last-hit-data probe state, and the `not_polled_nontrivial_return_boundary`/`disabled_unvalidated_by_value_abi` fields for leaving `GetLastHitData()` disabled.
- `VRHiggsService` runs in VR observation-only mode, reading `SkyrimTogetherVR.higgs` without requesting the HIGGS API from the main client, sending `RequestVRHiggsState` snapshots when connected, and storing incoming `NotifyVRHiggsState` snapshots by server player id without replaying remote grabs, changing hand state, changing collision, or mutating physics state.
- `VRHiggsService` writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.higgsnet` as a local/remote HIGGS state handoff for validation before any remote HIGGS visual consumer, ownership handling, or object interaction sync is enabled.
- `VRSaveLoadService` runs in VR observation-only mode, listening to `TESLoadGameEvent` and tracking local player readiness after a load without calling `BGSSaveLoadManager`, reconnecting transport, or enabling gameplay sync services.
- `VRSaveLoadService` writes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.saveload` as a local load-lifecycle handoff with readiness counters plus raw and server-mapped player/cell/worldspace identities for validation before full save/load recovery behavior is added.
- The authentication request is guarded against early player/cell state and fills normal player, mod, cell, level, and time data once the local player is loaded.

## Disabled

Connection-only mode does not instantiate:

- character assignment/spawn/ownership sync
- player mutation systems other than cell/grid/level network updates
- normal object, full inventory, full magic, actor-value, full combat/projectile, map, weather, quest, and party services
- behavior-variable patch initialization
- flat D3D11 overlay/render startup
- DirectInput overlay toggles

The normal initializer gameplay hook batch remains disabled by `TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS=0`.

## Build-Time Guard

`Tools/SkyrimVR/audit_bringup_hooks.py` audits the effective VR client target configuration in `Code/client/xmake.lua`. The default `SkyrimTogetherVRClient` target must keep `TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY=1`, `TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS=0`, `TP_SKYRIM_VR_ENABLE_VALIDATED_INLINE_PATCHES=0`, and `TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS=0`. The explicit `SkyrimTogetherVRClientAvatarSync` validation target may set `TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC=1` and `TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS=1` while keeping connection-only mode on. The explicit `SkyrimTogetherVRGameplayClient` target may set those remote-avatar flags and `TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY=0`, but it still keeps the dangerous unvalidated gameplay hook and inline-patch gates disabled.

## Expected First-Run Log Breadcrumbs

The first VR smoke test should show these one-time breadcrumbs in `logs/tp_client.log` before any gameplay systems are enabled:

- `SkyrimTogetherVR runtime flags:` with `connectionOnly=1`, `bringupHooks=1`, `unvalidatedHooks=0`, and `validatedInlinePatches=0`.
- `Installing SkyrimTogetherVR startup/main-loop/render bring-up hooks`.
- `Installing SkyrimTogetherVR main-loop observer:` with the resolved main-loop address.
- `SkyrimTogetherVR client startup hook reached`.
- `SkyrimTogetherVR main-loop observer reached` and cadence records.
- `SkyrimTogetherVR tick bridge endpoint prepared` before SKSEVR bootstrap.
- `SkyrimTogetherVR tick bridge endpoint ready on thread` after client startup.
- `SkyrimTogetherVR SKSE task tick dispatched World::Update` after the quest
  timer reaches the SKSEVR task queue.
- `SkyrimTogetherVRTickBridge: SKSEVR task and Papyrus bridge registered` in
  the SKSEVR log. A runtime-gate message instead means the installed game or
  SKSEVR version is incompatible and the client must not be tested.
- `Installing SkyrimTogetherVR renderer bring-up hook:` with the resolved renderer-init address.
- `SkyrimTogetherVR renderer init hook reached:` and `SkyrimTogetherVR renderer init completed` if the renderer hook executes.

If the renderer init address does not resolve, the client logs `SkyrimTogetherVR renderer bring-up hook skipped because renderer init address did not resolve` and leaves the renderer hook uninstalled rather than hooking a null pointer.

## Test Autoconnect

Because the flat overlay is disabled for VR, connection-only mode has an
environment-variable handoff path driven by the SKSEVR task bridge:

```cmd
set STVR_AUTOCONNECT=127.0.0.1:10578
set STVR_PASSWORD=optional_password
```

`STVR_AUTOCONNECT` is queued only after `PlayerCharacter` and its parent cell are available. This avoids authenticating while still on the main menu or during early game load.

The task bridge does not register HIGGS callbacks, call HIGGS or PLANCK APIs,
or intercept controller input. HIGGS and PLANCK remain observation-only
compatibility bridges.

## File Handoff

Connection-only mode also watches:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.command
```

and reads remembered in-game connection defaults from:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.connection
```

and writes:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.status
```

The packaged launcher sets `STVR_GAME_PATH` to the selected Skyrim VR install before the client starts. The client and the VRIK/HIGGS/PLANCK bridge DLLs use that path for the file handoff first, then fall back to the SkyrimVR executable directory or current working directory.

See `Docs/SkyrimVR/vr-connection-handoff.md` for the command and status format.

The desktop-side bridge for this file bus is documented in:

```text
Docs/SkyrimVR/vr-handoff-bridge.md
```

Use `Tools/SkyrimVR/vr_handoff.py players --watch --interval 1` during two-client testing to watch the consolidated remote-player readout without opening the flat overlay.

After a VR run, use `Tools/SkyrimVR/audit_runtime_handoff.py` or the packaged `AuditSkyrimTogetherVRRuntime-Windows.bat` to turn the client log and handoff files into a no-launch pass/fail report. The `--require-vrik` check requires both VRIK detection and VRIK API availability. For two-client VRIK/HIGGS validation, run `AuditSkyrimTogetherVRAvatarSyncRuntime-Windows.bat` or pass `--avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs`; the remote-player check prints each avatar row's blocker plus pose, VRIK detection, VRIK API, movement, same-space state, and HIGGS-aware avatar blocker. Add `--require-vr-pose-context` after intentionally drawing/equipping a weapon, equipping/casting magic, and drawing/firing a projectile or bow so weapon, magic, and projectile origin nodes are strict audit requirements. For the full gameplay package, run `AuditSkyrimTogetherVRGameplayRuntime-Windows.bat` after a deliberate gameplay-lane run. Use one `--require-*-relay` flag at a time on the runtime audit when validating movement, equipment, activation, magic, combat, projectile, grab, HIGGS, or save/load evidence; this keeps staged one-by-one bring-up from failing unrelated lanes. Use the runtime evidence collector `Tools/SkyrimVR/collect_runtime_evidence.py` or packaged `CollectSkyrimTogetherVREvidence-Windows.bat` when a run needs to be inspected later; it zips `SkyrimTogetherVR_BuildManifest.json`, `tp_client.log`, handoff readouts, discovered SKSEVR logs, a generated runtime audit, `runtime_checklist.json`, and a manifest without launching Skyrim or mutating the game install. Baseline evidence keeps remote-player, remote-avatar, and HIGGS-aware remote-avatar readiness as `not_required`; pass `--require-remote-player`, `--avatar-sync`, or `--gameplay` when the run is meant to prove two-client VRIK/HIGGS avatar readiness. The collector and evidence zip audit validate the package build manifest against the default, avatar-sync, or gameplay audit mode and accept the same `--require-vr-pose-context` and matching `--require-*-relay` flags for bundled or offline review.

When using the packaged Windows launcher, run `SkyrimTogetherVR.exe --companion` to open the local browser companion panel alongside Skyrim VR, or set `STVR_LAUNCH_COMPANION=1` for the same behavior from a shortcut. `SkyrimTogetherVR.exe --companion-only` opens the panel and exits before loading the game. In both launcher-driven modes, the panel reads and writes the file bus under the selected Skyrim VR install.

The default connection-only client also writes a readout-only remote-player proxy cache:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.remoteplayers
```

In the explicit broader targets, `VRRemotePlayerService` joins `NotifyPlayerJoined`/`NotifyPlayerCellChanged` identity and cell state with the existing remote pose, VRIK, movement, equipment, activation, magic, combat, projectile, grab, and HIGGS relay maps. It reports tracked player count, matched pose/VRIK/movement/equipment/action-lane/HIGGS counts, same-cell/same-worldspace flags, same-space count, avatar-validation readiness, and HIGGS-aware avatar-validation readiness for the companion `proxy` column and Papyrus telemetry. `avatarReady=yes` means the proxy has remote pose, VRIK, remote movement, and a same-cell or same-worldspace match; `higgsAvatarReady=yes` adds the remote HIGGS relay prerequisite. Otherwise `blocker=` or `higgsBlocker=` identifies the missing prerequisite. Activation, magic, combat, projectile, and grab proxy fields are readout-only lane availability flags; the service does not spawn actors, create marker objects, move scene nodes, equip items, replay HIGGS events, replay gameplay actions, or call PLANCK.

The pose relay also writes:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.pose
```

The VRIK SKSEVR bridge also writes:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.vrik
```

The HIGGS SKSEVR bridge also writes:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.higgs
```

The PLANCK SKSEVR bridge also writes:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.planck
```

The staged HIGGS observation relay also writes:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.higgsnet
```

`VRPoseService` folds the VRIK bridge file into the local `VRPoseUpdate` stream before relaying pose data to other clients. `VRHiggsService` keeps HIGGS API bridge state separate so HIGGS object/hand telemetry can be validated without mutating HIGGS or normal gameplay state. `SkyrimTogetherVRPlanckBridge` keeps PLANCK API heartbeat data separate from the combat-hit relay; it does not poll `GetLastHitData()`, mutate PLANCK settings, or replay active-ragdoll hits.

See `Docs/SkyrimVR/vr-pose-replication.md` for the pose status/readout format.

The movement relay also writes:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.movement
```

See `Docs/SkyrimVR/vr-movement-relay.md` for the movement status/readout format.

Discovery observation writes:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.discovery
```

See `Docs/SkyrimVR/vr-discovery.md` for the discovery status/readout format. The runtime audit and evidence zip checklist validate the discovery schema structurally, including grid fields, cached cell/worldspace ids, actor count/limit, and contiguous capped actor rows, before any actor spawn or ownership consumer is enabled.

The network-only player cell service also writes:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.playercell
```

The runtime audit and evidence zip checklist validate the player cell schema structurally, including readiness, request counters, last grid/cell `GameId` fields, last grid/cell coords, and level-send state.

The staged player location networking is documented in:

```text
Docs/SkyrimVR/vr-player-cell-sync.md
```

The staged local equipment observer is documented in:

```text
Docs/SkyrimVR/vr-inventory-observation.md
```

The player-id equipment relay is documented in:

```text
Docs/SkyrimVR/vr-equipment-relay.md
```

The staged activation telemetry relay is documented in:

```text
Docs/SkyrimVR/vr-activation-relay.md
```

The staged magic-effect telemetry relay is documented in:

```text
Docs/SkyrimVR/vr-magic-relay.md
```

The staged combat-hit telemetry relay is documented in:

```text
Docs/SkyrimVR/vr-combat-relay.md
```

The staged projectile intent telemetry relay is documented in:

```text
Docs/SkyrimVR/vr-projectile-relay.md
```

The staged grab/release telemetry relay is documented in:

```text
Docs/SkyrimVR/vr-grab-relay.md
```

The staged HIGGS observation relay is documented in:

```text
Docs/SkyrimVR/vr-higgs-relay.md
```

The staged save/load lifecycle observer is documented in:

```text
Docs/SkyrimVR/vr-saveload-observation.md
```

The compact status includes save/load readiness counters plus raw and server-mapped load cell/worldspace ids.
Detailed telemetry includes save/load raw/server player/cell/worldspace ids.

## Papyrus API

The VR package source declares these globals on `SkyrimTogetherUtils`:

```papyrus
bool Function ConnectToSkyrimTogether(string endpoint, string password = "") global native
bool Function DisconnectFromSkyrimTogether() global native
bool Function IsSkyrimTogetherConnected() global native
bool Function SetSkyrimTogetherConnectionConfig(string endpoint, string password = "") global native
string Function GetSkyrimTogetherConnectionState() global native
string Function GetSkyrimTogetherConfiguredEndpoint() global native
string Function GetSkyrimTogetherConfiguredPassword() global native
string Function GetSkyrimTogetherStatusSummary() global native
string Function GetSkyrimTogetherTelemetryReadout() global native
```

These declarations are staged source only. The VR client does not install the
flat-client BSScript native binder because its NativeFunction ABI is not yet
validated for Skyrim VR. The default connection path is `STVR_AUTOCONNECT` or
the command/config/status file handoff, not these functions.

The PEX files are still regenerated with the package so their source and
bytecode remain synchronized, but they are not a supported control surface in
the default VR package.

`SkyrimTogetherVRConnectionMenu.psc` layers a small script-friendly UI surface on top:

```papyrus
SkyrimTogetherVRConnectionMenu.ShowStatus()
SkyrimTogetherVRConnectionMenu.ShowTelemetry()
SkyrimTogetherVRConnectionMenu.ShowStatusAndTelemetry()
SkyrimTogetherVRConnectionMenu.Configure(endpoint, password)
SkyrimTogetherVRConnectionMenu.ConfigureAndConnect(endpoint, password)
SkyrimTogetherVRConnectionMenu.ConnectLocalhost()
SkyrimTogetherVRConnectionMenu.ConnectConfigured()
SkyrimTogetherVRConnectionMenu.Disconnect()
SkyrimTogetherVRConnectionMenu.ToggleLocalhost()
SkyrimTogetherVRConnectionMenu.ToggleConfigured()
```

That script has a matching `SkyrimTogetherVRConnectionMenu.pex` after the Papyrus rebuild, but its in-game spell/effect entry point is intentionally dormant in the default VR package pending a separate native-ABI port.

## Next Steps

- Do not validate the dormant `Skyrim Together VR` spell/power as a connection control. Validate the SKSEVR task tick plus the file/environment handoff first.
- Keep actor/gameplay services disabled in the default package until the explicit avatar-sync and gameplay packages pass runtime evidence.
- Use the discovery handoff to validate loaded actor/cell observation in the default, avatar-sync, and gameplay packages.
- Validate server-visible VR player cell/grid/level updates in the default, avatar-sync, and gameplay packages before making gameplay the recommended runtime path.
- Runtime-validate `SkyrimTogetherVR.remoteplayers` with two clients, then add an in-game remote VR pose consumer that renders debug markers or a proxy avatar before making character spawn/ownership sync a default path.
- Validate `SkyrimTogetherVR.movement` in-game with two clients before adding a remote movement visual consumer or enabling normal actor movement sync.
- Validate `SkyrimTogetherVR.inventory` and the staged remote draw/sheath cue in-game before enabling normal inventory/equipment application.
- Validate `SkyrimTogetherVR.activation` in-game before adding a remote activation visual consumer or enabling normal object activation replay.
- Validate `SkyrimTogetherVR.magic` in-game before adding a remote magic visual consumer or enabling normal spell-cast/effect application.
- Validate `SkyrimTogetherVR.combat` in-game before adding a remote combat visual consumer or enabling normal projectile/combat sync.
- Validate `SkyrimTogetherVR.projectile` in-game before adding a remote projectile visual consumer or enabling normal projectile replay.
- Validate `SkyrimTogetherVR.grab` in-game with vanilla grabs and HIGGS object grabs/drops before adding a remote grab visual consumer, HIGGS API callback bridge, or object interaction sync.
- Validate `SkyrimTogetherVR.compatibility` and `SkyrimTogetherVR.planck` in-game with HIGGS and PLANCK installed so the readouts show `planck.installed`, `planck.loaded`, `planck.interfaceAvailable`, and observation-only policy state before adding extended PLANCK hit-data relay or physics mutation.
- Validate `SkyrimTogetherVR.higgs` and `SkyrimTogetherVR.higgsnet` in-game with HIGGS installed while pulling, grabbing, dropping, stashing, consuming, colliding, and two-handing objects before adding a remote HIGGS visual consumer or object interaction sync.
- Validate `SkyrimTogetherVR.saveload` in-game while loading saves offline, connecting, and connected before adding any reconnect/resync behavior.
