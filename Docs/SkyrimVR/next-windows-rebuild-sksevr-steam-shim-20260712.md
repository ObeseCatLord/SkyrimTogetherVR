# Next Windows Rebuild: Official SKSEVR Steam Shim

## Why this rebuild is required

The direct pre-entry load of `sksevr_1_4_15.dll` makes SKSEVR load VRIK while
the mapped Skyrim VR executable has not reached its VCRUNTIME main-invoke
boundary. VRIK process attach then blocks in CRT initialization on an
uninitialized mapped-game critical section.

The launcher now mirrors the official Steam path:

1. Validate `sksevr_1_4_15.dll` and `sksevr_steam_loader.dll`.
2. Load only `sksevr_steam_loader.dll` from the existing TLS-prepared helper.
3. Enter mapped `SkyrimVR.exe` after the shim returns.
4. Let the shim patch `VCRUNTIME140!__telemetry_main_invoke_trigger` and load
   the SKSEVR core on the resumed game startup thread.

The installed package predates this source change. A Windows rebuild is required
before another game launch is useful.

## Build

From an x64 Visual Studio Developer Command Prompt in the repository root, build
the package with the target prerequisite gate when a Windows Skyrim VR target is
available:

```bat
BuildAndAuditSkyrimTogetherVR-Windows.bat --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites
```

Install only `artifacts\SkyrimTogetherVR\packages\default`. Keep full VRIK,
HIGGS, PLANCK, and all three `SkyrimTogetherVR*Bridge.dll` files enabled.

## Required target files

The target Skyrim VR root must contain both files from a matched official SKSEVR
2.0.12-or-newer installation. On the normal Steam shim-bootstrap path, the
loader validates matching core/shim version resources before it loads the shim:

```text
sksevr_steam_loader.dll
sksevr_1_4_15.dll
```

The strict target prerequisite audit rejects a target missing either file. Use
`--skyrim-vr "C:\Path\To\SkyrimVR" --require-prerequisites` with
`BuildAndAuditSkyrimTogetherVR-Windows.bat`, or use the installer, which enables
that gate by default.

After a successful shim bootstrap, the first authentication request observes the
deferred core module for up to 250 ms. If it remains absent, the request
explicitly reports SKSE as inactive and `tp_client.log` records the timeout.

## Expected evidence

On the next normal launch, collect these logs from one run:

* `logs\tp_client.log`:
  - `SKSEVR Steam bootstrap shim loaded`
  - `bootstrap helper completed before mapped game entry`
  - `entering mapped Skyrim VR gameMain`
  - `SKSEVR core module observed at expected game path`
* `Documents\My Games\Skyrim VR\SKSE\sksevr_steam_loader.log`:
  - `found iat`
  - `patched iat`
  - `HookMain: thread = ...`
* `Documents\My Games\Skyrim VR\SKSE\sksevr.log`:
  - `VRIK.dll ... loaded correctly`

Interpretation:

* Missing `patched iat` means the mapped executable did not present the expected
  VCRUNTIME IAT to the official shim.
* Missing `HookMain` means the mapped game did not reach the VCRUNTIME startup
  boundary.
* A repeated VRIK attach stall after `HookMain` falsifies startup phase as a
  sufficient fix and directs investigation to mapped image identity or layout.
