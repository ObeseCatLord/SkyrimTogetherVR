# SkyrimTogetherVR Windows Build Handoff Results

Date: July 8, 2026

## July 11, 2026 Rebuild Update (Current)

The repository was rebuilt on Windows after a source update. All four releasedbg package snapshots completed successfully and passed the non-runtime package audit:

- `default`: 254 files, completed at 09:38:07 local time.
- `avatar-sync`: 254 files, completed at 09:38:34 local time.
- `gameplay`: 254 files, completed at 09:39:11 local time.
- `dll-only`: 246 files, completed at 09:39:23 local time.

The build was run by `C:\Users\obesecatlord\Desktop\BuildSkyrimTogetherVR-All.bat`. It initializes the Visual Studio x64 environment, invokes the four package-specific build wrappers directly, regenerates Papyrus, verifies the four package snapshots, and writes timestamped logs under `C:\Users\obesecatlord\Desktop\SkyrimTogetherVR-BuildLogs`. The successful run logs are:

- `stvr-build-20260711-093740.log`
- `stvr-verify-20260711-093740.log`

### Corrections Made During This Rebuild

- `Code\client\Services\Generic\VRPoseService.cpp`: the local `applyCapabilityState` lambda now explicitly captures `buildBaseResult`, resolving MSVC errors C3493 and C2326.
- `Code\immersive_launcher\oobe\PathArgument.cpp`: corrected `acPath.ends_with(*"\\")` to the intended wide-string form `acPath.ends_with(L"\\")`, allowing the gameplay launcher target to compile.
- The desktop batch avoids `PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --all` because that workflow collects and audits evidence after each package. At the time of this build, the static evidence audit had a source/audit-contract mismatch: `Tools\SkyrimVR\audit_windows_build.py` expected the old Wine-script text `Wine is using its built-in powershell.exe stub`. That expectation was corrected on July 11, 2026 to the current wrapper diagnostic, `The selected PowerShell did not execute a command through Wine`.
- Several repository wrappers prefer `py -3` when `where py` succeeds. On this host that launcher is not executable. The desktop batch puts a local compatibility `py.cmd` ahead of it and otherwise passes the known Python executable explicitly. A durable repository improvement would make these wrappers prefer an explicitly supplied Python command, or test `py -3 --version` before selecting it.

### Runtime Prerequisite Caveat

The current Skyrim VR install contains the VRIK, HIGGS, and PLANCK DLLs, but not all of their required Papyrus scripts. The successful lightweight package audits therefore report zero package failures while noting:

- VRIK required files missing: 2 (`Data\Scripts\VRIK.pex`, `Data\Scripts\_vrik_qust_system.pex`)
- HIGGS required files missing: 1 (`Data\Scripts\HiggsVR.pex`)
- PLANCK required files missing: 1 (`Data\Scripts\planck.pex`)

This does not invalidate the built packages. It does mean that a strict readiness audit or runtime validation should wait until the actual VRIK, HIGGS, and PLANCK mod archives are installed into the target Skyrim VR directory.

This note summarizes the Windows build artifacts found in this repository after the Windows build handoff. It is intended to travel with the review handoff zip so the next agent can see what has already been built and what still needs validation.

## Built Package Snapshots

The following package snapshots exist under `artifacts\SkyrimTogetherVR\packages`:

- `default`
  - `SkyrimTogetherVR.exe`
  - `EarlyLoad.dll`
  - `TPProcess.exe`
  - `Data\SKSE\Plugins\SkyrimTogetherVRVrikBridge.dll`
  - `Data\SKSE\Plugins\SkyrimTogetherVRHiggsBridge.dll`
  - `Data\SKSE\Plugins\SkyrimTogetherVRPlanckBridge.dll`
- `avatar-sync`
  - `SkyrimTogetherVRAvatarSync.exe`
  - same bridge/runtime support files as default
- `gameplay`
  - `SkyrimTogetherVRGameplay.exe`
  - same bridge/runtime support files as default
- `dll-only`
  - `EarlyLoad.dll`
  - `Data\SKSE\Plugins\SkyrimTogetherVRVrikBridge.dll`
  - `Data\SKSE\Plugins\SkyrimTogetherVRHiggsBridge.dll`
  - `Data\SKSE\Plugins\SkyrimTogetherVRPlanckBridge.dll`

All four package manifests report:

- `platform`: `windows`
- `arch`: `x64`
- `mode`: `releasedbg`
- `stagedGameFiles`: `true`
- `papyrusCompiled`: `true`
- `companionPanel`: `true`

The manifests were generated from the Windows path `Y:\Documents\SkyrimModding\SkyrimTogetherVR` using the Skyrim VR install at `\\host.lan\Data\FasterGames\SteamLibrary\steamapps\common\SkyrimVR`.

## Latest Build Evidence Zips

Latest evidence zips:

- `artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-default-20260708-222519Z.zip`
  - SHA-256: `c23abbb38a0722caeb54cb8a18cef1a457a97e68be7e790c19c3552a4c9d914f`
- `artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-avatar-sync-20260708-222544Z.zip`
  - SHA-256: `3e9dff4ccbdb4e23e94879caa881fda16f70df07f1467a6c09ae2d77909d3d43`
- `artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-gameplay-20260708-222608Z.zip`
  - SHA-256: `0de3f5fa9467c398910a1f30df7876970796bb55c3e726cb5f414f43e885e5d2`
- `artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-dll-only-20260708-222622Z.zip`
  - SHA-256: `1d31640801fe29756399275be8e0e85ed1ab0a470ec41b2b8e482ddc140b9623`

The extracted summaries for these evidence bundles report exit code `0` for:

- Python version capture
- xmake version capture
- xmake target listing
- `Tools/SkyrimVR/audit_windows_build.py`
- `Tools/SkyrimVR/audit_built_package.py` in the matching package mode

The built-package audits report:

- AE-to-SE CSV rows: `62`
- Address override CSV rows: `2984`
- Installed SKSEVR DLL present: `True`
- Installed VR Address Library CSV present: `True`
- Installed VRIK DLL present: `True`
- Installed HIGGS DLL present: `True`
- Installed PLANCK DLL present: `True`
- Installed VRIK required files missing: `0`
- Installed HIGGS required files missing: `0`
- Installed PLANCK required files missing: `0`
- Built package audit failures: `0`

## Main Runtime Binary Hashes

Package executable and bridge hashes:

- `default\SkyrimTogetherVR.exe`
  - SHA-256: `4ee594bc5e844fbc8d295eac8c0e4516f5d7ed0d221ad2202dec157929b84d9a`
- `avatar-sync\SkyrimTogetherVRAvatarSync.exe`
  - SHA-256: `219e2e0132b2ceba7776d778ec9cc7e17c5c3f9f05ae19ba3b5d1504b0d8cc9c`
- `gameplay\SkyrimTogetherVRGameplay.exe`
  - SHA-256: `5c7b46336747047770e95576b3475de56c39b533995e4f08cb05f119a747a9ac`
- `SkyrimTogetherVRVrikBridge.dll`
  - SHA-256: `6cb25292b606713d3a7f2be955333443020608a907854b93e30f127e178d4c3b`
- `SkyrimTogetherVRHiggsBridge.dll`
  - SHA-256: `adbd94f252b413cb3ffb55fb3f7180bea4638e76752bae64bba7c353012a06fc`
- `SkyrimTogetherVRPlanckBridge.dll`
  - SHA-256: `1b82e501ec06d9a8278e48bba38e50c6d1566323b8286bc91ded1cc76819b100`
- `EarlyLoad.dll`
  - SHA-256: `2536cf79533f7377bff83239a7d2ee48ad301c6f959750613a29c9051039c5e7`
- `TPProcess.exe`
  - SHA-256: `1990e0b86bd240e111c24838846e6e7ddea6f5adc38220b69b591932082cc170`

Bridge DLL hashes are identical across default, avatar-sync, gameplay, and DLL-only package snapshots.

## Findings Document Status

The user reported a `stvr-windows-build-handoff-findings.md` document from the Windows handoff review UI. I searched the local repository, `Documents`, `Downloads`, `Backup\Downloads`, and `Desktop` paths visible from this Linux environment and did not find that file. If the markdown exists only in the review UI or on the Windows side, copy it into:

```text
Docs\SkyrimVR\stvr-windows-build-handoff-findings.md
```

or place it beside this note before handing the zip to another agent.

## What Is Proven

The current evidence proves:

- The Windows build produced runtime artifacts for default, avatar-sync, gameplay, and DLL-only package flavors.
- Package manifests were generated for all four flavors.
- VR Papyrus PEX files were regenerated with Caprica during packaging.
- Windows build wrapper/source contract audit passed.
- Built-package audit passed for all four flavors.
- The Windows audit environment saw SKSEVR, VR Address Library, VRIK, HIGGS, and PLANCK in the target Skyrim VR install.

## What Is Not Yet Proven

The current evidence does not prove:

- `VerifySkyrimTogetherVRWindowsPackages-Windows.bat` was run after the final `--all` package snapshots.
- Install dry-runs were run against the final package snapshots.
- Any package was installed into Skyrim VR.
- Skyrim VR launched successfully with these binaries.
- Runtime logs reached the startup/main-loop/VM/render breadcrumbs.
- VRIK/HIGGS/PLANCK bridge handoff files were produced at runtime.
- Two-client avatar-sync worked.
- Gameplay lanes worked.

## Recommended Next Steps

Before launching Skyrim VR:

1. Run the no-build snapshot verifier on Windows:

```bat
VerifySkyrimTogetherVRWindowsPackages-Windows.bat --include-fus --planck-archive "C:\Downloads\PLANCK.zip" --skyrim-vr "%SKYRIMVR_PATH%"
```

2. Dry-run the default install:

```bat
InstallSkyrimTogetherVR-Windows.bat --package "artifacts\SkyrimTogetherVR\packages\default" --package-only --skyrim-vr "%SKYRIMVR_PATH%"
InstallSkyrimTogetherVR-Windows.bat --package "artifacts\SkyrimTogetherVR\packages\default" --skyrim-vr "%SKYRIMVR_PATH%"
```

3. Install only after both dry-runs pass:

```bat
InstallSkyrimTogetherVR-Windows.bat --package "artifacts\SkyrimTogetherVR\packages\default" --skyrim-vr "%SKYRIMVR_PATH%" --install
```

4. Start runtime testing with the default package only. Do not begin with `avatar-sync` or `gameplay`.

5. After the first VR run, collect baseline runtime evidence:

```bat
AuditSkyrimTogetherVRRuntime-Windows.bat --require-connected --require-vrik --require-higgs
CollectSkyrimTogetherVREvidence-Windows.bat --require-vrik --require-higgs
```

Only move to `avatar-sync` after default startup/connection/VRIK/HIGGS/PLANCK handoff evidence is clean. Only move to `gameplay` after avatar-sync produces clean two-client VRIK/HIGGS evidence.
