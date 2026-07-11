# CEF Delay-Load Follow-Up

## Scope

This note supplements the Windows build handoff after a Linux/Proton launch of
the already-built default VR launcher failed during process startup.

## Observed Failure

- The first launch failed because the packaged launcher had no CEF runtime.
- Supplying the exact CEF `141.0.11` runtime resolved that loader error, but
  the process then crashed with access violation `0xc0000005` at
  `libcef.dll + 0x60a27f`.
- The default VR target is intentionally connection-only and disables the flat
  overlay, so it should not initialize CEF during startup.
- Import-table inspection showed the old `SkyrimTogetherVR.exe` imported
  `libcef.dll` normally. That makes the Windows loader initialize CEF before
  the connection-only target can avoid its overlay path.

## Fix

Commit `3e340c4a` adds `/DELAYLOAD:libcef.dll` to the common immersive launcher
link settings. `delayimp` was already linked. The change applies to the default,
avatar-sync, and gameplay launcher executables, but not `TPProcess.exe`, which
is the explicit CEF subprocess.

The Windows packager now copies the complete xmake CEF `141.0.11` runtime into
launcher packages and records its version and file list in the package manifest.
The package audit rejects a launcher that either imports `libcef.dll` normally
or does not delay-import it.

## Required Rebuild Gate

The installed executable predates this fix and cannot be made delay-loaded by
copying DLLs beside it. On Windows, build from commit `3e340c4a` or newer, then
run the package verifier before installation:

```bat
BuildSkyrimTogetherVR-Windows.bat -Mode release
VerifySkyrimTogetherVRWindowsPackages-Windows.bat --skyrim-vr "%SKYRIMVR_PATH%"
```

For a nonstandard xmake package cache, pass
`-CefRuntimeDirectory <CEF-141.0.11-bin-directory>` to the build script or set
`STVR_CEF_RUNTIME_DIR`. A passing verifier must report `libcef.dll` as a delay
import for `SkyrimTogetherVR.exe`.

Do not redeploy the old launcher. After the rebuilt default package passes the
audit, install it and use the existing Linux launcher script to connect to the
server.
