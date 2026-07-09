# VR Handoff Bridge

`Tools/SkyrimVR/vr_handoff.py` is a desktop-side bridge for the current connection-only VR control surface. It reads the same status and telemetry files that the client writes, writes the same command file that `VRConnectionService` already watches, and remembers the last endpoint/password in `SkyrimTogetherVR.connection` for the in-game configured spell path.

This is the current safe replacement for the flat CEF/D3D11 overlay and DirectInput toggle path. It does not inject input hooks, render into the VR swap chain, call Skyrim UI classes, or require unvalidated menu layout assumptions.

## Commands

Print the handoff paths:

```sh
Tools/SkyrimVR/vr_handoff.py files
```

Write a connect command:

```sh
Tools/SkyrimVR/vr_handoff.py connect 127.0.0.1:10578
```

Write a connect command with a password:

```sh
Tools/SkyrimVR/vr_handoff.py connect 127.0.0.1:10578 --password optional_password
```

Read or write the remembered endpoint used by the in-game configured toggle:

```sh
Tools/SkyrimVR/vr_handoff.py config
Tools/SkyrimVR/vr_handoff.py config --endpoint 192.168.1.10:10578 --password optional_password
```

Write a disconnect command:

```sh
Tools/SkyrimVR/vr_handoff.py disconnect
```

Print current status and staged telemetry:

```sh
Tools/SkyrimVR/vr_handoff.py status
```

Print the consolidated remote-player readout:

```sh
Tools/SkyrimVR/vr_handoff.py players
```

Poll status:

```sh
Tools/SkyrimVR/vr_handoff.py status --watch --interval 1
```

Poll the remote-player readout:

```sh
Tools/SkyrimVR/vr_handoff.py players --watch --interval 1
```

Serve the local browser companion panel:

```sh
Tools/SkyrimVR/vr_handoff.py serve
```

The panel defaults to `http://127.0.0.1:8765/`, shows the same compact status and telemetry readouts as `status`, includes a `Remote Players` table built from the staged pose/avatar/remote-player proxy/movement/equipment/activation/magic/combat/projectile/grab files, and writes the same connect/disconnect command file as the CLI commands. It also shows HIGGS and PLANCK bridge status panels with bridge sequence/epoch values for stale-file diagnosis; the compatibility panel reads `SkyrimTogetherVR.compatibility` and reports `gameplayMode`, remote-avatar/proxy policy, movement/equipment/action/HIGGS/save-load lane policies, hook mode, HIGGS/PLANCK install/load state, and observation-only physics policy. The PLANCK panel reads `SkyrimTogetherVR.planck` and reports interface availability, cached build number, disabled current-hit polling, disabled last-hit-data probe state, the `not_polled_nontrivial_return_boundary` reason, and the `disabled_unvalidated_by_value_abi` boundary for leaving `GetLastHitData()` disabled. The default `proxy` column comes from `SkyrimTogetherVR.remoteplayers` and joins server player identity/cell state with the replicated pose, VRIK, movement, equipment, activation, magic, combat, projectile, grab, and HIGGS caches without creating actors or objects. It reports `avatarReady=yes` only when remote HMD, left-hand, right-hand, VRIK data, remote movement, and same-cell or same-worldspace data are all present; otherwise `blocker=` names the missing prerequisite. It reports `higgsAvatarReady=yes` only when that avatar-ready row also has a remote HIGGS relay row; otherwise `higgsBlocker=` names the missing HIGGS-aware prerequisite. The activation/magic/combat/projectile/grab fields are availability telemetry for staged lane validation and do not make the proxy replay gameplay. The `avatar` column is for the explicit avatar-sync build and includes the latest target/cache state plus combat-intent pose context, for example `spell=yes/yes` for spell origin/destination validity and `weapon=yes/no` for left/right weapon-offset validity. Connect actions also update the remembered endpoint in `SkyrimTogetherVR.connection`, so a later cast of the in-game configured power can use the same server. It is intended for a desktop browser, SteamVR desktop view, or another local companion surface while the flat CEF/D3D11 overlay remains disabled.

Useful panel options:

```sh
Tools/SkyrimVR/vr_handoff.py serve --port 8766
Tools/SkyrimVR/vr_handoff.py serve --endpoint 192.168.1.10:10578
Tools/SkyrimVR/vr_handoff.py serve --open-browser
Tools/SkyrimVR/vr_handoff.py serve --no-open-browser
```

The Windows package also includes a wrapper:

```bat
LaunchSkyrimTogetherVRCompanion.bat
```

Run it from the extracted package or Skyrim VR game folder. It locates `Tools\SkyrimVR\vr_handoff.py`, starts the same local browser panel, and opens the panel URL in the default browser. Pass `--no-open-browser` to keep the server running without opening a browser. By default the wrapper uses its own folder as `--game-path`; when invoked by `SkyrimTogetherVR.exe`, the launcher passes the selected Skyrim VR install as `--game-path` so the command/status files line up with the loaded game.

The VR launcher can also link to the packaged companion panel:

```bat
SkyrimTogetherVR.exe --companion
SkyrimTogetherVR.exe --companion-only
```

`--companion` starts `LaunchSkyrimTogetherVRCompanion.bat` next to the game launch, opens the local browser panel, and points it at the selected Skyrim VR install. `--companion-only` starts only the companion panel and exits before mapping `SkyrimVR.exe`. For packaged shortcuts, `STVR_LAUNCH_COMPANION=1` enables the same companion launch without changing command-line arguments; `--no-companion` overrides that environment variable.

The launcher also exports `STVR_GAME_PATH` before entering the mapped Skyrim VR executable. The client and the SKSEVR bridge DLLs use that value first when resolving `Data\SkyrimTogetherReborn`, then fall back to the SkyrimVR executable directory/current working directory. This keeps command/status, VRIK, HIGGS, PLANCK, and runtime evidence files aligned with the selected Skyrim VR install even when a wrapper changes the process working directory.

Run the smoke test:

```sh
Tools/SkyrimVR/vr_handoff.py self-test
```

After a user-run VR launch, the companion package also includes a no-launch runtime handoff audit:

```sh
Tools/SkyrimVR/audit_runtime_handoff.py
Tools/SkyrimVR/audit_runtime_handoff.py --require-connected --require-vrik --require-higgs
Tools/SkyrimVR/audit_runtime_handoff.py --avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs
Tools/SkyrimVR/audit_runtime_handoff.py --avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs --require-vr-pose-context
Tools/SkyrimVR/audit_runtime_handoff.py --avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs --require-movement-relay
Tools/SkyrimVR/collect_runtime_evidence.py --avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs
Tools/SkyrimVR/collect_runtime_evidence.py --avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs --require-vr-pose-context
Tools/SkyrimVR/collect_runtime_evidence.py --avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs --require-movement-relay
Tools/SkyrimVR/audit_runtime_evidence_zip.py artifacts/SkyrimTogetherVR/runtime-evidence/SkyrimTogetherVR-evidence-YYYYMMDD-HHMMSSZ.zip
```

The packaged runtime audit wrapper is also available from the extracted package or Skyrim VR root:

```bat
AuditSkyrimTogetherVRRuntime-Windows.bat --require-connected --require-vrik --require-higgs
AuditSkyrimTogetherVRRuntime-Windows.bat --avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs
AuditSkyrimTogetherVRRuntime-Windows.bat --avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs --require-vr-pose-context
AuditSkyrimTogetherVRRuntime-Windows.bat --avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs --require-movement-relay
AuditSkyrimTogetherVRAvatarSyncRuntime-Windows.bat
AuditSkyrimTogetherVRGameplayRuntime-Windows.bat
CollectSkyrimTogetherVREvidence-Windows.bat --avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs
CollectSkyrimTogetherVREvidence-Windows.bat --avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs --require-vr-pose-context
CollectSkyrimTogetherVREvidence-Windows.bat --avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs --require-movement-relay
CollectSkyrimTogetherVRGameplayEvidence-Windows.bat
AuditSkyrimTogetherVREvidence-Windows.bat "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-YYYYMMDD-HHMMSSZ.zip" --require-remote-player
AuditSkyrimTogetherVREvidence-Windows.bat "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-YYYYMMDD-HHMMSSZ.zip" --require-avatar-sync
AuditSkyrimTogetherVREvidence-Windows.bat "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-YYYYMMDD-HHMMSSZ.zip" --require-gameplay
AuditSkyrimTogetherVRGameplayEvidence-Windows.bat "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-YYYYMMDD-HHMMSSZ.zip"
AuditSkyrimTogetherVREvidence-Windows.bat "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-YYYYMMDD-HHMMSSZ.zip" --require-avatar-sync --require-vr-pose-context
AuditSkyrimTogetherVREvidence-Windows.bat "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-YYYYMMDD-HHMMSSZ.zip" --require-avatar-sync --require-movement-relay
```

The runtime handoff audit reads `logs/tp_client.log` plus the same `Data/SkyrimTogetherReborn` files used by the companion panel. It reports missing startup breadcrumbs, missing handoff files, absent local HMD/hand/movement data, player cell status schema failures, missing VRIK detection or VRIK API availability, missing HIGGS lanes, missing PLANCK bridge policy/interface data when PLANCK is installed, and missing remote/avatar readiness without launching Skyrim or mutating the install. `AuditSkyrimTogetherVRRuntime-Windows.bat` defaults `--game-path` to its own folder; pass `--game-path` if the packaged runtime audit wrapper is not being run from the Skyrim VR root. `AuditSkyrimTogetherVRAvatarSyncRuntime-Windows.bat` is the avatar-sync runtime audit wrapper and forwards to the same audit with `--avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs`. `AuditSkyrimTogetherVRGameplayRuntime-Windows.bat` is the full gameplay runtime wrapper and forwards strict connection, avatar, pose-context, and gameplay-relay requirements. When `--avatar-sync` is used, it also requires `SkyrimTogetherVR.avatar`. When `--require-remote-player` is used, the remote avatar readiness check includes each `remotePlayer.<id>` row's blocker, pose, VRIK detection, VRIK API, movement, and same-space state so a failed two-client VRIK smoke test points at the missing lane. Add `--require-vr-pose-context` or one `--require-*-relay` flag directly to this runtime audit when the just-finished VR run was meant to prove a specific pose or gameplay lane.

The runtime evidence collector is the handoff bundle step for failures or remote review. `Tools/SkyrimVR/collect_runtime_evidence.py` and the packaged evidence collector wrapper `CollectSkyrimTogetherVREvidence-Windows.bat` create a zip with `SkyrimTogetherVR_BuildManifest.json`, `logs\tp_client.log`, the handoff readouts/control files, discovered SKSEVR logs, `runtime_audit.txt`, `runtime_checklist.json`, `runtime_checklist.txt`, and `manifest.json`. The checklist maps observed handoff data back to the port's runtime lanes, including package build manifest, startup logging, connection, HMD/hand pose, VRIK, remote avatar readiness, HIGGS-aware remote avatar readiness, discovery schema, player cell status, movement, equipment, activation, magic, combat, projectile, grab, HIGGS/PLANCK compatibility policy, HIGGS relay, PLANCK bridge status, and save/load. Baseline/single-client evidence records remote-player, remote-avatar, and HIGGS-aware remote-avatar readiness as `not_required`; pass `--require-remote-player`, `--avatar-sync`, or `--gameplay` when the run is meant to prove two-client VRIK/HIGGS avatar readiness. The package build manifest check verifies Windows x64 package schema, expected default/avatar-sync/gameplay targets, copied artifacts, staged game files, and companion helper state. The discovery check validates grid fields, cached cell/worldspace ids, actor count/limit, and capped `actor.N.formId` rows structurally rather than expecting exact actor IDs. The player cell check validates readiness, request counters, last grid/cell ids, grid coords, and level-send state from `SkyrimTogetherVR.playercell`. The collector does not launch Skyrim or mutate the game install; source-tree runs default to `artifacts/SkyrimTogetherVR/runtime-evidence`, and packaged runs default to `Documents/SkyrimTogetherVRRuntimeEvidence`.

Use `--require-vr-pose-context` on the runtime audit or collector after a deliberate weapon/magic/projectile pose-context run. That mode requires weapon offset, spell/magic origin/aim, and arrow/projectile origin nodes in `SkyrimTogetherVR.pose`.

Use one relay flag at a time on the runtime audit or collector, for example `--require-movement-relay`, `--require-equipment-relay`, `--require-activation-relay`, `--require-magic-relay`, `--require-combat-relay`, `--require-projectile-relay`, `--require-grab-relay`, `--require-higgs-relay`, or `--require-saveload-observer`, when validating staged gameplay lanes independently. `--require-gameplay-relays` is for an intentional full-lane validation run.

The evidence zip audit is the offline review step. `Tools/SkyrimVR/audit_runtime_evidence_zip.py` and packaged evidence zip audit wrapper `AuditSkyrimTogetherVREvidence-Windows.bat` read a collected zip without needing access to the game install, check `manifest.json`, `package/SkyrimTogetherVR_BuildManifest.json`, `runtime_audit.txt`, `runtime_checklist.json`, `runtime_checklist.txt`, the required log/handoff entries, and fail on missing required files, invalid package build manifest, nonzero embedded runtime audit, or failed checklist lanes. Use `--require-remote-player` when reviewing a default two-client remote-player proxy run, use `--require-avatar-sync` when reviewing a mandatory VRIK/HIGGS remote-avatar validation run, use `--require-gameplay` when reviewing a full gameplay package run, add `--require-vr-pose-context` when the run was meant to prove weapon, magic, and projectile origin nodes, and add the matching `--require-*-relay` flag for one-by-one gameplay lane validation.

## Paths

By default the tool uses `SKYRIMVR_PATH` or `STVR_SKYRIM_VR` when set; otherwise the current working directory is used as a neutral fallback. Packaged Windows wrappers pass `--game-path` explicitly.

The tool reads and writes under:

```text
Data/SkyrimTogetherReborn
```

You can override either path:

```sh
Tools/SkyrimVR/vr_handoff.py --game-path "/path/to/SkyrimVR" status
Tools/SkyrimVR/vr_handoff.py --handoff-dir "/path/to/Data/SkyrimTogetherReborn" status
```

## Files

The bridge writes:

- `SkyrimTogetherVR.command`
- `SkyrimTogetherVR.connection`

The bridge reads:

- `SkyrimTogetherVR.status`
- `SkyrimTogetherVR.pose`
- `SkyrimTogetherVR.avatar`
- `SkyrimTogetherVR.remoteplayers`
- `SkyrimTogetherVR.movement`
- `SkyrimTogetherVR.inventory`
- `SkyrimTogetherVR.discovery`
- `SkyrimTogetherVR.playercell`
- `SkyrimTogetherVR.activation`
- `SkyrimTogetherVR.magic`
- `SkyrimTogetherVR.combat`
- `SkyrimTogetherVR.projectile`
- `SkyrimTogetherVR.grab`
- `SkyrimTogetherVR.compatibility`
- `SkyrimTogetherVR.higgs`
- `SkyrimTogetherVR.higgsnet`
- `SkyrimTogetherVR.saveload`

Missing readout files are reported as missing instead of treated as errors. This keeps the tool useful during staged bring-up where a service may be disabled or not yet written. If `SkyrimTogetherVR.connection` is missing, the remembered endpoint defaults to `127.0.0.1:10578` with an empty password.

## Boundary

This bridge intentionally stays outside Skyrim's rendering and input stack. The local browser panel uses the same file contract, but the next in-game overlay step should still wait until VR menu/input classes and swap-chain behavior are validated.

For in-game checks, `SkyrimTogetherVRConnectionMenu.ShowTelemetry()` uses the native `GetSkyrimTogetherTelemetryReadout()` path to show the same staged discovery, player cell, pose, remote-player proxy, avatar target, movement, equipment, activation, magic, combat, projectile, grab, HIGGS/PLANCK compatibility, HIGGS relay, and save/load state through `Debug.MessageBox`. The discovery section reports ready state, actor count/limit, current/center grid, player/cached cell and worldspace ids, location id, and first actor row. The player cell section reports readiness, online state, request counters, last sent grid/cell ids, and last level send state. The save/load section reports readiness/counts plus raw and server-mapped player, cell, and worldspace ids from `SkyrimTogetherVR.saveload`. The desktop `status` command and browser compatibility panel show `SkyrimTogetherVR.compatibility` HIGGS/PLANCK installed/loaded state, hook policy, `gameplayMode`, and per-lane policy fields; the browser HIGGS panel shows `SkyrimTogetherVR.higgs` bridge state plus `SkyrimTogetherVR.higgsnet` HIGGS relay status. The desktop `players` command and browser `Remote Players` table are the current non-invasive substitute for a remote-avatar debug overlay while the VR overlay path remains gated.
