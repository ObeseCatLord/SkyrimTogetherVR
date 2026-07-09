# VR Connection Handoff

Connection-only mode does not start the flat CEF/D3D11 overlay. `VRConnectionService` provides a small file-based handoff surface that a launcher, desktop helper, or future VR overlay can use without touching Win32 input hooks.

`Tools/SkyrimVR/vr_handoff.py` is the current desktop helper for this protocol. It can write connect/disconnect commands, remember the endpoint/password used by the in-game configured spell path, and summarize the status, pose, movement, equipment, discovery, activation, magic, combat, projectile, and save/load readout files.

## Command File

Write commands to:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.command
```

Supported connect format:

```ini
action=connect
endpoint=127.0.0.1:10578
password=optional_password
```

Shorthand connect formats are also accepted:

```text
connect 127.0.0.1:10578
```

or:

```text
127.0.0.1:10578
```

Supported disconnect format:

```ini
action=disconnect
```

or:

```text
disconnect
```

After a command is accepted, the client renames it to `SkyrimTogetherVR.command.processed`. Invalid commands are renamed to `SkyrimTogetherVR.command.error`.

The helper writes the same format:

```sh
Tools/SkyrimVR/vr_handoff.py connect 127.0.0.1:10578
Tools/SkyrimVR/vr_handoff.py disconnect
```

## Remembered Connection Config

The desktop helper and browser panel also write:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.connection
```

Current format:

```ini
endpoint=127.0.0.1:10578
password=optional_password
```

This file is not an action queue. It is the remembered endpoint/password consumed by `SkyrimTogetherVRConnectionMenu.ToggleConfigured()` and the packaged `Skyrim Together VR` power. If it is missing, the native default is `127.0.0.1:10578` with an empty password.

The helper can manage it directly:

```sh
Tools/SkyrimVR/vr_handoff.py config
Tools/SkyrimVR/vr_handoff.py config --endpoint 192.168.1.10:10578 --password optional_password
```

## Status File

The client writes:

```text
Data/SkyrimTogetherReborn/SkyrimTogetherVR.status
```

Current fields:

```ini
state=offline
online=0
playerId=0
commandFile=...\Data\SkyrimTogetherReborn\SkyrimTogetherVR.command
error=optional_error_text
```

Known states are:

- `offline`
- `waiting_for_player`
- `connecting`
- `online`
- `disconnecting`
- `error`

The helper can summarize the status and staged telemetry files:

```sh
Tools/SkyrimVR/vr_handoff.py status
Tools/SkyrimVR/vr_handoff.py status --watch --interval 1
```

It can also serve a local browser companion panel for the same file-bus surface:

```sh
Tools/SkyrimVR/vr_handoff.py serve
```

The panel defaults to `http://127.0.0.1:8765/`, refreshes the staged readouts once per second, and writes connect/disconnect commands without touching Skyrim's render or input stack.

## Autoconnect

The existing environment-variable path remains available:

```cmd
set STVR_AUTOCONNECT=127.0.0.1:10578
set STVR_PASSWORD=optional_password
```

Both environment and file commands wait until `PlayerCharacter` and its parent cell are available before connecting. This avoids authenticating from the main menu or early loading screens.

## Papyrus Natives

For a VR-native UI that lives inside Skyrim's menu/script systems, the VR source package also declares:

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

These functions are active in the VR connection-only build and use the same `VRConnectionService` queue and status state as the command file. Explicit `ConnectToSkyrimTogether(endpoint, password)` calls also save that endpoint/password to `SkyrimTogetherVR.connection`, so later configured toggles use the same target. `SkyrimTogetherUtils.pex` must be regenerated after native declaration changes; the endpoint setter source should be treated as pending until the final Papyrus build pass updates the packaged `.pex`.

`GetSkyrimTogetherStatusSummary()` is intended for VR-safe message-box surfaces. It returns a compact multi-line summary with connection state, online state, local player id, discovery state, player cell/grid/level counters, local pose/movement/equipment/activation/magic-effect/combat-hit/projectile availability, remote pose/movement/equipment/activation/magic-effect/combat-hit/projectile counts, save/load readiness counters, and raw plus server-mapped load cell/worldspace ids.

`GetSkyrimTogetherTelemetryReadout()` is the detailed VR-safe readout. It returns discovery fields, player cell/grid/level handoff fields, local HMD/hand positions when available, first remote pose/movement/equipment samples by server player id, staged activation/magic/combat/projectile/save-load counters, and save/load raw/server player/cell/worldspace ids.

## In-Game Power

The VR package also includes a staged in-game entry point:

- `SkyrimTogetherVerifyLaunchScript` grants `SkyrimTogether.esp` form `0x1825` on quest init and player load.
- The VR ESP names that spell `Skyrim Together VR` and patches its effect VMAD to `SkyrimTogetherVRConnectionSpellEffect`.
- `SkyrimTogetherVRConnectionSpellEffect.pex` calls `SkyrimTogetherVRConnectionMenu.ToggleConfigured()`.

The default cast action toggles the remembered endpoint from `SkyrimTogetherVR.connection` and reports status with `Debug.MessageBox`. If no remembered endpoint exists, it falls back to `127.0.0.1:10578` with an empty password. `ShowStatus()` uses `GetSkyrimTogetherStatusSummary()` and `ShowTelemetry()` uses `GetSkyrimTogetherTelemetryReadout()` so the same script surface can verify discovery, player cell/grid/level traffic, replicated pose/movement/equipment/activation/magic-effect/combat-hit/projectile state, load readiness, and server-mapped load location ids without opening the flat D3D overlay. This is packaged but still needs in-game VR validation.

## VR Papyrus Menu Source

`GameFiles/SkyrimVR/scripts/source/SkyrimTogetherVRConnectionMenu.psc` is a source-level control surface for the ESP spell-effect entry point and any future quest, book, or MCM binding. It provides:

- `ShowStatus()`
- `ShowTelemetry()`
- `ShowStatusAndTelemetry()`
- `Configure(endpoint, password)`
- `ConfigureAndConnect(endpoint, password)`
- `Connect(endpoint, password)`
- `ConnectConfigured()`
- `ConnectLocalhost()`
- `Disconnect()`
- `Toggle(endpoint, password)`
- `ToggleConfigured()`
- `ToggleLocalhost()`

The functions call the `SkyrimTogetherUtils` natives above and use `Debug.MessageBox` for VR-safe status feedback instead of the flat D3D overlay.
