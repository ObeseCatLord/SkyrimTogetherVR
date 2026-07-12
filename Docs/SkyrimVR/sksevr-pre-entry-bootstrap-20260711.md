# SKSEVR Pre-Entry Bootstrap

The immersive launcher maps `SkyrimVR.exe` into `SkyrimTogetherVR.exe` and
calls the mapped entry point directly. SKSEVR's official loader injects its DLL
before resuming the target thread. Loading SKSEVR from the mapped game's CRT
startup hook was therefore too late: after the version-resource fix made
SKSEVR 2.0.12 acceptable, the DLL faulted during `LoadLibraryW` before it could
report successful initialization.

The launcher now calls `LoadScriptExender()` after `RunTiltedInit()` has loaded
the VR address data and installed path/module routing, but before
`LC->gameMain()` enters the mapped executable. `TiltedOnlineApp::BeginMain()`
only observes and reports that one bootstrap result, so it cannot retry a failed
or late load after the game begins execution.

This does not use `sksevr_loader.exe`. That executable creates and injects into
a separate `SkyrimVR.exe` process, which would bypass the immersive launcher's
mapped-image hooks and client.

This source change requires a new Windows default package. The first runtime
log must show the pre-entry bootstrap's `SKSEVR module loaded` or `already
loaded` message before the mapped game's client startup hook. It must not show
the pre-entry bootstrap error or a repeat `LoadLibraryW` attempt from
`BeginMain()`.
