# Wine Build Status - July 10, 2026

## Result

The local Linux Wine/MSVC experiment is **not a valid build path yet**. It did not produce a complete SkyrimTogetherVR client package, and no output from it was installed into Skyrim VR.

Use the Windows/MSVC handoff workflow for the next deployable package:

```bat
PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --gameplay-only --skyrim-vr "%SKYRIMVR_PATH%" --require-prerequisites
AuditSkyrimTogetherVRReadiness-Windows.bat --gameplay --package "artifacts\SkyrimTogetherVR\packages\gameplay" --skyrim-vr "%SKYRIMVR_PATH%" --require-built-package
InstallSkyrimTogetherVR-Windows.bat --gameplay --package "artifacts\SkyrimTogetherVR\packages\gameplay" --skyrim-vr "%SKYRIMVR_PATH%" --install
```

## What Was Confirmed

- Wine can invoke the installed MSVC compiler and an official Windows CMake binary.
- CMake can configure a minimal MSVC project when the Windows SDK library directories are supplied explicitly to the linker.
- The project Wine wrapper correctly rejects a PowerShell executable that exits without executing its probe command.

## Blocking Issues

- The portable PowerShell 7 executable started under Wine but did not execute `-Command` or `-File` payloads reliably.
- xmake package builds did not preserve the Windows SDK library paths for CMake linker probes. Package configuration therefore failed before the SkyrimTogetherVR targets could compile.
- The prior July 8 gameplay package embeds an older source revision, so it cannot prove or deploy the current source state.

## Scope Boundary

The Wine prefix and downloaded tool binaries are local diagnostic tooling, not project dependencies and not part of the supported build contract. Do not deploy files from that environment.

## Follow-up

After a fresh Windows build succeeds, retain its package manifest and build-evidence zip with the review handoff. A future Linux build effort should use a reproducible Windows SDK/MSVC installation under Wine or a dedicated Windows CI runner rather than further patching xmake's private package cache.
