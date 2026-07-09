# SkyrimTogetherVR Final Handoff Checklist

This checklist is the short Windows/MSVC handoff path. None of these commands launch Skyrim unless the command name explicitly says install, and the install step still does not launch Skyrim.

## 1. Windows Preflight

Run this first on the Windows build machine:

```bat
PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --preflight-only --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites
PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --preflight-only --compile-papyrus --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites -- -PapyrusCompiler "C:\Tools\Caprica\Caprica.exe"
```

Expected result:

- Windows x64 xmake targets are visible.
- Staged `GameFiles\SkyrimVR` package files are found.
- Papyrus compile inputs are resolved when `--compile-papyrus` is used.
- Packaged Python helper closure resolves.
- SKSEVR, VR Address Library, VRIK, HIGGS, and PLANCK prerequisites are visible when `--require-prerequisites` is used.

## 2. Build Package Evidence

Run the main handoff command:

```bat
PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --all --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites
PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --all --compile-papyrus --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites -- -PapyrusCompiler "C:\Tools\Caprica\Caprica.exe"
PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --gameplay-only --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites
```

This builds/audits/collects evidence for:

- default package: `SkyrimTogetherVR.exe`
- mandatory VRIK/HIGGS avatar-sync package: `SkyrimTogetherVRAvatarSync.exe`
- full gameplay package: `SkyrimTogetherVRGameplay.exe`
- DLL-only partial package: `SkyrimTogetherVRVrikBridge.dll`, `SkyrimTogetherVRHiggsBridge.dll`, `SkyrimTogetherVRPlanckBridge.dll`, `EarlyLoad.dll`

Expected result:

- Package audit passes for each selected mode.
- `SkyrimTogetherUtils.pex` and `SkyrimTogetherVRConnectionMenu.pex` contain the VR connection config native/menu tokens, including `SetSkyrimTogetherConnectionConfig` and `ConfigureAndConnect`.
- Stable package snapshots are written to `artifacts\SkyrimTogetherVR\packages\default`, `artifacts\SkyrimTogetherVR\packages\avatar-sync`, `artifacts\SkyrimTogetherVR\packages\gameplay`, and `artifacts\SkyrimTogetherVR\packages\dll-only`; `artifacts\SkyrimTogetherVR\releasedbg` remains the most recent package for compatibility.
- Build evidence zips such as `SkyrimTogetherVR-build-evidence-default-YYYYMMDD-HHMMSSZ.zip` are written under `artifacts\SkyrimTogetherVR\build-evidence`.
- Build evidence zips include copied source evidence for the Windows build wrappers, `source\SetupSkyrimTogetherVRBuildEnv-Windows.bat`, `source\BuildSkyrimTogetherVR-Windows.ps1`, VR xmake target files such as `source\Code\client\xmake.lua`, plus `inline-patch-manifest.json`, `inline-patch-audit.md`, `address-audit.json`, and `address-audit.md` under `source\Docs\SkyrimVR\...`.
- Evidence zip audits pass with `--require-package` and the matching mode flag.

## 3. Built-Package Readiness

After the Windows build succeeds, require the package in the no-launch readiness gate:

```bat
VerifySkyrimTogetherVRWindowsPackages-Windows.bat --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
AuditSkyrimTogetherVRReadiness-Windows.bat --package "artifacts\SkyrimTogetherVR\packages\default" --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-built-package
AuditSkyrimTogetherVRReadiness-Windows.bat --avatar-sync --package "artifacts\SkyrimTogetherVR\packages\avatar-sync" --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-built-package
AuditSkyrimTogetherVRReadiness-Windows.bat --gameplay --package "artifacts\SkyrimTogetherVR\packages\gameplay" --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-built-package
```

Expected result:

- The wrapper and both explicit readiness commands exit with zero readiness failures.
- `VerifySkyrimTogetherVRWindowsPackages-Windows.bat` also passes the DLL-only audit and default/avatar-sync/gameplay install dry-runs.
- No missing-package warning remains.
- The package manifest mode matches the audit mode.

## 4. Install Dry Run, Then Install

Dry-run the default package first:

```bat
InstallSkyrimTogetherVR-Windows.bat --package "artifacts\SkyrimTogetherVR\packages\default" --package-only --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
InstallSkyrimTogetherVR-Windows.bat --package "artifacts\SkyrimTogetherVR\packages\default" --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
```

Install only after both dry runs pass:

```bat
InstallSkyrimTogetherVR-Windows.bat --package "artifacts\SkyrimTogetherVR\packages\default" --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --install
```

For the avatar-sync validation package, repeat with `--avatar-sync`:

```bat
InstallSkyrimTogetherVR-Windows.bat --avatar-sync --package "artifacts\SkyrimTogetherVR\packages\avatar-sync" --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
InstallSkyrimTogetherVR-Windows.bat --avatar-sync --package "artifacts\SkyrimTogetherVR\packages\avatar-sync" --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --install
```

For the full gameplay package, repeat with `--gameplay`:

```bat
InstallSkyrimTogetherVR-Windows.bat --gameplay --package "artifacts\SkyrimTogetherVR\packages\gameplay" --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
InstallSkyrimTogetherVR-Windows.bat --gameplay --package "artifacts\SkyrimTogetherVR\packages\gameplay" --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --install
```

If stale root-level `SkyrimTogether.esp`, `scripts`, `meshes`, or `SkyrimTogetherRebornBehaviors` files are reported, remove them during install with:

```bat
InstallSkyrimTogetherVR-Windows.bat --package "artifacts\SkyrimTogetherVR\packages\default" --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --install --cleanup-stale-root-files
```

## 5. Runtime Evidence After User VR Runs

After a default-package VR run, audit and collect baseline evidence:

```bat
AuditSkyrimTogetherVRRuntime-Windows.bat --require-connected --require-vrik --require-higgs
CollectSkyrimTogetherVREvidence-Windows.bat --require-vrik --require-higgs
```

After the mandatory two-client VRIK/HIGGS avatar-sync run, audit and collect strict avatar evidence:

```bat
AuditSkyrimTogetherVRAvatarSyncRuntime-Windows.bat --require-vr-pose-context
CollectSkyrimTogetherVRAvatarSyncEvidence-Windows.bat --require-vr-pose-context
```

After a deliberate full gameplay-lane run, require every staged relay:

```bat
AuditSkyrimTogetherVRGameplayRuntime-Windows.bat --require-gameplay-relays
CollectSkyrimTogetherVRGameplayEvidence-Windows.bat --require-gameplay-relays
```

Audit the collected evidence zips:

```bat
AuditSkyrimTogetherVREvidence-Windows.bat "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-YYYYMMDD-HHMMSSZ.zip"
AuditSkyrimTogetherVRAvatarSyncEvidence-Windows.bat "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-YYYYMMDD-HHMMSSZ.zip" --require-vr-pose-context
AuditSkyrimTogetherVRGameplayEvidence-Windows.bat "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-YYYYMMDD-HHMMSSZ.zip"
```

## 6. Final Evidence Bundle Audit

After build evidence and runtime evidence have been collected, audit the full handoff bundle from the repository/build root. This is a source-tree handoff tool because it reads build evidence from `artifacts\SkyrimTogetherVR\build-evidence`; it is not one of the runtime helper scripts installed into the Skyrim VR folder.

```bat
AuditSkyrimTogetherVRFinalHandoff-Windows.bat
AuditSkyrimTogetherVRFinalHandoff-Windows.bat --build-default "artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-default-YYYYMMDD-HHMMSSZ.zip" --build-avatar-sync "artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-avatar-sync-YYYYMMDD-HHMMSSZ.zip" --build-gameplay "artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-gameplay-YYYYMMDD-HHMMSSZ.zip" --build-dll-only "artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-dll-only-YYYYMMDD-HHMMSSZ.zip" --runtime-default "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-default.zip" --runtime-avatar-sync "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-avatar-sync.zip"
AuditSkyrimTogetherVRFinalHandoff-Windows.bat --build-default "artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-default-YYYYMMDD-HHMMSSZ.zip" --build-avatar-sync "artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-avatar-sync-YYYYMMDD-HHMMSSZ.zip" --build-gameplay "artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-gameplay-YYYYMMDD-HHMMSSZ.zip" --build-dll-only "artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-dll-only-YYYYMMDD-HHMMSSZ.zip" --runtime-default "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-default.zip" --runtime-avatar-sync "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-avatar-sync.zip" --runtime-gameplay "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-gameplay.zip" --require-gameplay-runtime
```

With no explicit zip paths, the final audit auto-discovers the newest matching build evidence from `artifacts\SkyrimTogetherVR\build-evidence` and the newest matching runtime evidence from the standard runtime evidence directory by reading each zip manifest. Use `--build-evidence-dir` or `--runtime-evidence-dir` if those archives were moved. The first explicit-path command is the default plus mandatory avatar-sync acceptance audit. The second explicit-path command is the strict full gameplay-relay acceptance audit after movement, equipment, activation, magic, combat, projectile, grab, HIGGS, and save/load lanes have all been deliberately exercised.

Explicit runtime zip paths are also role-checked by manifest flags. A default runtime zip must not be an avatar-sync zip, an avatar-sync zip must not be a gameplay zip, and a gameplay zip must carry `gameplayAudit=1` plus every staged relay requirement.

## Exit Criteria

The port is ready for VR testing only when:

- `PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --all ... --require-prerequisites` passes.
- `VerifySkyrimTogetherVRWindowsPackages-Windows.bat ...` passes against the default/avatar-sync/gameplay/DLL-only package snapshots.
- Build evidence zip audits prove the copied Windows build wrappers and VR xmake target files match the handoff contract, plus zero default-active/default-enableable inline patches, positive disassembly context for every active inline patch site, and zero missing core non-RTTI address entries in the copied source evidence.
- Default, avatar-sync, and gameplay readiness pass with `--require-built-package`.
- Default, avatar-sync, and gameplay install dry runs pass.
- Default runtime evidence proves connection, VRIK, HIGGS, PLANCK bridge status, and HIGGS/PLANCK compatibility policy state.
- Avatar-sync runtime evidence proves remote player, VRIK/HIGGS, hand pose, HMD pose, weapon/magic/projectile pose context, and zero invalid avatar transform or movement writes.
- Gameplay runtime evidence is collected from the `SkyrimTogetherVRGameplay.exe` package only after those lanes are deliberately exercised and semantic local/remote payload checks pass.
- Runtime evidence zip audits enforce the strict `manifest.json` flags recorded by the collector, so a zip collected with avatar-sync, gameplay, pose-context, or gameplay-relay requirements cannot later pass as a relaxed audit. `avatarSyncAudit` itself requires connection, local VRIK API, HIGGS bridge, remote-player proxy, remote VRIK avatar readiness, remote VRIK/HIGGS avatar readiness, and actor-target checklist lanes.
- `AuditSkyrimTogetherVRFinalHandoff-Windows.bat` passes for the collected build/runtime evidence set.
