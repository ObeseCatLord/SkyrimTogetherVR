# CEF Delay-Load Follow-Up

## Scope

This note supplements the Windows build handoff after a Linux/Proton launch of
the already-built default VR launcher failed during process startup.

## Observed Failure

- The first launch failed because the packaged launcher had no CEF runtime.
- Supplying the exact CEF `141.0.11` runtime resolved that loader error, but
  the process then crashed with access violation `0xc0000005` in the launcher
  image's `0x180...` range.
- The overlap with CEF's preferred image base led to an initial, incorrect CEF
  attribution. The later minidump shows that CEF was not loaded and that the
  fault was in launcher `.zdata`; see `player-singleton-startup-guard.md`.
- Import-table inspection of the old executable still showed a normal
  `libcef.dll` import. Delay loading remains valid packaging hardening for the
  connection-only launcher.

## Fix

Commit `3e340c4a` adds `/DELAYLOAD:libcef.dll` to the common immersive launcher
link settings. `delayimp` was already linked. The change applies to the default,
avatar-sync, and gameplay launcher executables, but not `TPProcess.exe`, which
is the explicit CEF subprocess.

The Windows packager now copies the complete xmake CEF `141.0.11` runtime into
launcher packages and records its version and file list in the package manifest.
The package audit rejects a launcher that either imports `libcef.dll` normally
or does not delay-import it.

## Current Status

The July 11 default package was rebuilt and deployed with the CEF delay import.
CEF hardening does not resolve the separate player-singleton startup fault. The
new `player-singleton-startup-guard.md` source change requires a newer Windows
default package and package audit before another Linux runtime attempt:

```bat
BuildSkyrimTogetherVR-Windows.bat -Mode release
VerifySkyrimTogetherVRWindowsPackages-Windows.bat --skyrim-vr "%SKYRIMVR_PATH%"
```

For a nonstandard xmake package cache, pass
`-CefRuntimeDirectory <CEF-141.0.11-bin-directory>` to the build script or set
`STVR_CEF_RUNTIME_DIR`. A passing verifier must report `libcef.dll` as a delay
import for `SkyrimTogetherVR.exe`.

After the rebuilt default package passes the audit, install it and use the
existing Linux launcher script to connect to the server.
