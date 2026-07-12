# SKSEVR Pre-Entry Bootstrap

The immersive launcher maps `SkyrimVR.exe` into `SkyrimTogetherVR.exe` and
calls the mapped entry point directly. SKSEVR's official loader injects its DLL
before resuming the target thread. Loading SKSEVR from the mapped game's CRT
startup hook was therefore too late: after the version-resource fix made
SKSEVR 2.0.12 acceptable, the DLL faulted during `LoadLibraryW` before it could
report successful initialization.

The launcher now starts one dedicated bootstrap helper after `RunTiltedInit()`
has loaded the VR address data and installed path/module routing. The helper
copies the manually mapped Skyrim VR TLS template into its own TLS slot, calls
`LoadScriptExender()`, and must complete successfully before `LC->gameMain()`
enters the mapped executable. This matches SKSEVR's separate injection-thread
topology without attempting to spawn a second game process. `TiltedOnlineApp::BeginMain()`
only observes and reports that one bootstrap result, so it cannot retry a failed
or late load after the game begins execution.

The mapper verifies that the copied TLS template fits the reserved launcher TLS
slot before the helper can call SKSEVR. A helper timeout is terminal: it flushes
the diagnostic log and terminates the launch process because an in-flight native
DLL load cannot be cancelled safely.

This does not use `sksevr_loader.exe`. That executable creates and injects into
a separate `SkyrimVR.exe` process, which would bypass the immersive launcher's
mapped-image hooks and client.

This source change requires a new Windows default package. The first runtime
log must show the helper's `started`, `applied mapped Skyrim VR TLS`, and
`returned from LoadScriptExender` markers followed by `entering mapped Skyrim
VR gameMain`. It must not show the helper timeout, the pre-entry bootstrap
error, or a repeat `LoadLibraryW` attempt from `BeginMain()`.
