# Windows Build Script

For the short end-to-end Windows/MSVC handoff sequence, see `Docs\SkyrimVR\final-handoff-checklist.md`.

## Automated WinBoat Build

Agents working from the Linux checkout should commit and push `main`, then use
the checked-in host helper:

```bash
Tools/SkyrimVR/build_winboat_gameplay.sh
```

It verifies that the Linux worktree is clean and its commit is on
`github/main`, creates a fresh detached worktree in WinBoat, initializes every
pinned submodule, and runs the gameplay build, package audit, build-evidence
collector, and evidence audit. The helper does not install files and does not
launch Skyrim. On success it prints the Windows worktree, gameplay package, and
evidence archive paths. It removes previous generated WinBoat build worktrees
before creating the new one, preventing each iteration from permanently adding
several gigabytes to the VM disk. Set `STVR_WINBOAT_REPO` or `WINBOAT_POWERSHELL` only
when the local layout differs from the defaults documented in `AGENTS.md`.

Install the daily two-day retention job on the Linux host with:

```bash
Tools/SkyrimVR/install_build_cleanup_timer.sh
```

The timer cleans only generated Skyrim Together worktrees and package output,
then asks Windows to TRIM its virtual disk. Cleanup is process-locked, and the
WinBoat build helper performs an immediate guest cleanup and retrim before each
build. Run
`Tools/SkyrimVR/cleanup_build_storage.sh --max-age-days 0 --trim` for an
immediate manual cleanup. If WinBoat is offline, scheduled cleanup skips the VM
without treating that as a failure.

On native Windows, the equivalent audited command is:

```bat
BuildAuditCollectSkyrimTogetherVR-Windows.bat --gameplay
```

Caprica is discovered from an explicit `-PapyrusCompiler`, `CAPRICA`, `PATH`,
`C:\Tools\Caprica\Caprica.exe`, or the repository-adjacent `_refs` locations.

## CommonLib Gameplay Adapter

The VR gameplay adapter uses the maintained alandtse `CommonLibVR` `ng` branch,
pinned as `Libraries\CommonLibSSE-NG` at release `v4.37.0`. The exact commit,
Skyrim VR runtime, SKSEVR SDK, and VR Address Library hashes are recorded in
`Dependencies\SkyrimVR.lock.json`.

The PowerShell build initializes this submodule recursively whenever
`SkyrimTogetherVRGameplayBridge` is selected. For a manual source checkout or
dependency-only verification:

```bat
git submodule update --init --recursive -- Libraries/CommonLibSSE-NG
python Tools/SkyrimVR/verify_runtime_lock.py --source-only
```

Do not replace the pin with a separately installed CommonLib binary. The plugin
and its headers must come from the same locked revision, and Windows xmake
configuration must keep `--skyrim_se=y --skyrim_ae=y --skyrim_vr=y` so the
CommonLib layout ABI matches its published all-runtime configuration.

Use this from Windows when Visual Studio/MSVC and xmake are available:

```bat
BuildSkyrimTogetherVR-Windows.bat
```

The legacy root `Build.bat` is kept as a Skyrim VR-only compatibility wrapper. It delegates to `BuildAndAuditSkyrimTogetherVR-Windows.bat` and does not run the old generic `xmake install -o distrib` path or any Skyrim SE target graph.

For a no-build preflight on the Windows machine, run:

```bat
BuildSkyrimTogetherVR-Windows.bat -PreflightOnly
BuildSkyrimTogetherVR-Windows.bat -PreflightOnly -Xmake C:\Tools\xmake\xmake.exe
```

The preflight configures/checks the Windows xmake target graph, verifies the VR targets are visible, checks staged VR game files, resolves the packaged Python helper import closure, and verifies the packaged companion/runtime/evidence wrappers. It exits before building any targets.

To build only the DLL-producing targets, use:

```bat
BuildSkyrimTogetherVR-DLL-Windows.bat
BuildSkyrimTogetherVR-DLL-Windows.bat -PreflightOnly
BuildSkyrimTogetherVR-ClientDLL-Windows.bat
```

`BuildSkyrimTogetherVR-DLLs-Windows.bat` is also provided as a compatibility alias for the same command. `BuildSkyrimTogetherVR-ClientDLL-Windows.bat` is a wording-compatible alias for handoffs that refer to "the client DLL"; it still builds the real DLL-producing targets below because the main VR client is launcher-linked as `SkyrimTogetherVR.exe`, not emitted as a standalone `SkyrimTogetherVR.dll`.

`BuildSkyrimTogetherVR-Windows.bat`, `BuildSkyrimTogetherVR-AvatarSync-Windows.bat`, `BuildSkyrimTogetherVR-Gameplay-Windows.bat`, and `BuildSkyrimTogetherVR-DLL-Windows.bat` all call `SetupSkyrimTogetherVRBuildEnv-Windows.bat` first. They work from an x64 Native Tools Command Prompt for Visual Studio. If `cl.exe` is not already in `PATH`, the setup helper tries to locate Visual Studio with `vswhere.exe` and load `VsDevCmd.bat -arch=x64 -host_arch=x64` before invoking xmake.

This builds and packages:

- `Data\SKSE\Plugins\SkyrimTogetherVRVrikBridge.dll`
- `Data\SKSE\Plugins\SkyrimTogetherVRHiggsBridge.dll`
- `Data\SKSE\Plugins\SkyrimTogetherVRPlanckBridge.dll`
- `Data\SKSE\Plugins\SkyrimTogetherVRTickBridge.dll`
- `Data\SKSE\Plugins\SkyrimTogetherVRGameplayBridge.dll`
- `EarlyLoad.dll`

The main VR client is launcher-linked as `SkyrimTogetherVR.exe`; the package should not contain a main `SkyrimTogetherVR.dll`.

To build those DLL-producing targets and immediately audit the partial DLL package, use:

```bat
BuildAndAuditSkyrimTogetherVR-DLL-Windows.bat
BuildAndAuditSkyrimTogetherVR-DLL-Windows.bat --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites
```

The DLL-only audit requires the five SKSEVR bridge DLLs and `EarlyLoad.dll`, verifies the generated package manifest lists only `SkyrimTogetherVRVrikBridge`, `SkyrimTogetherVRHiggsBridge`, `SkyrimTogetherVRPlanckBridge`, `SkyrimTogetherVRTickBridge`, `SkyrimTogetherVRGameplayBridge`, and `ImmersiveElf`, and rejects stale launcher or `TPProcess.exe` artifacts. This is useful for checking the native bridge DLL build, but it is not the full installable Skyrim Together VR runtime package.

To build the explicit VRIK/HIGGS remote-avatar validation package, use:

```bat
BuildSkyrimTogetherVR-AvatarSync-Windows.bat
```

This builds and packages:

- `SkyrimTogetherVRAvatarSync.exe`
- `Data\SKSE\Plugins\SkyrimTogetherVRVrikBridge.dll`
- `Data\SKSE\Plugins\SkyrimTogetherVRHiggsBridge.dll`
- `Data\SKSE\Plugins\SkyrimTogetherVRPlanckBridge.dll`
- `Data\SKSE\Plugins\SkyrimTogetherVRTickBridge.dll`
- `Data\SKSE\Plugins\SkyrimTogetherVRGameplayBridge.dll`
- `EarlyLoad.dll`
- `TPProcess.exe`

This is the two-client avatar-sync build. It leaves the default `SkyrimTogetherVR.exe` connection-only path separate.

To build the staged gameplay package, use:

```bat
BuildSkyrimTogetherVR-Gameplay-Windows.bat
```

This builds and packages:

- `SkyrimTogetherVRGameplay.exe`
- `Data\SKSE\Plugins\SkyrimTogetherVRVrikBridge.dll`
- `Data\SKSE\Plugins\SkyrimTogetherVRHiggsBridge.dll`
- `Data\SKSE\Plugins\SkyrimTogetherVRPlanckBridge.dll`
- `Data\SKSE\Plugins\SkyrimTogetherVRTickBridge.dll`
- `Data\SKSE\Plugins\SkyrimTogetherVRGameplayBridge.dll`
- `EarlyLoad.dll`
- `TPProcess.exe`

This is the opt-in staged gameplay build. It enables the VR observation relays
and the CommonLib-owned remote-avatar lifecycle, retained-identity root/spatial
movement, and named humanoid animation graph snapshot slice. It does not
instantiate the legacy desktop mutation services; inventory, combat, magic,
equipment, skeleton, HIGGS, PLANCK, and FBT mutation remain disabled. Exact
animation action replay is also still gated on Skyrim VR ABI proof. The
unvalidated flat-Skyrim hook batch and flat D3D overlay remain disabled.

To build and immediately audit the produced package in one Windows command, use:

```bat
PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat
PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --all --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites
PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --preflight-only
VerifySkyrimTogetherVRWindowsPackages-Windows.bat --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
VerifySkyrimTogetherVRWindowsPackages-Windows.bat --include-fus --planck-archive "C:\Downloads\PLANCK.zip" --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
BuildAndAuditSkyrimTogetherVR-Windows.bat
BuildAndAuditSkyrimTogetherVR-Windows.bat --avatar-sync
BuildAndAuditSkyrimTogetherVR-Windows.bat --gameplay
BuildAndAuditSkyrimTogetherVR-Windows.bat --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites
BuildAuditCollectSkyrimTogetherVR-Windows.bat
BuildAuditCollectSkyrimTogetherVR-Windows.bat --avatar-sync
BuildAuditCollectSkyrimTogetherVR-Windows.bat --gameplay
BuildAuditCollectSkyrimTogetherVR-Windows.bat --dll-only
BuildAuditCollectSkyrimTogetherVR-Windows.bat --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites
BuildAuditCollectSkyrimTogetherVR-Windows.bat --compile-papyrus --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites -- -PapyrusCompiler "C:\Tools\Caprica\Caprica.exe"
```

`PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat` is the top-level handoff runner. By default it runs the default package and the explicit two-client avatar-sync package through build, package audit, build-evidence collection, and evidence-zip audit. Add `--include-gameplay`, `--include-dll-only`, or `--all` when the gameplay package or DLL-producing partial package should be checked in the same session. Use `--default-only`, `--avatar-sync-only`, `--gameplay-only`, or `--dll-only` to isolate one mode. `--preflight-only` calls the matching build wrappers with `-PreflightOnly`, so it configures/checks the Windows target graph, staged files, SKSEVR SDK resolution, Papyrus compiler inputs, and packaged helper closure without compiling targets. Normal package builds regenerate the VR PEX files automatically; `--compile-papyrus` is retained as an explicit compatibility switch.

The package manifest is a full-payload inventory. It records a SHA-256 for every staged package file except the self-referential manifest itself, plus the committed Git revision and a content fingerprint of the source tree captured before normal PEX generation. Normal package builds require a clean Git worktree; generated PEX payload hashes are recorded separately and do not make a clean package appear dirty. For an intentional source-modified developer build, forward `-AllowDirtySource`; the manifest then marks that source state as dirty and approved and includes its unique content fingerprint. The built-package audit rejects unknown source states, unapproved dirty states, missing payload entries, and payload hash mismatches.

Each package build still updates `artifacts\SkyrimTogetherVR\releasedbg` for compatibility, then copies a stable package snapshot to the matching flavor path under `artifacts\SkyrimTogetherVR\packages`: `artifacts\SkyrimTogetherVR\packages\default`, `artifacts\SkyrimTogetherVR\packages\avatar-sync`, `artifacts\SkyrimTogetherVR\packages\gameplay`, or `artifacts\SkyrimTogetherVR\packages\dll-only`. Use `VerifySkyrimTogetherVRWindowsPackages-Windows.bat` after `--all` to audit those snapshots, run default/avatar-sync/gameplay readiness with `--require-built-package`, run the DLL-only package audit, and run default/avatar-sync/gameplay install dry-runs without rebuilding, installing, or launching Skyrim.

The build-and-audit wrapper does not install files and does not launch Skyrim. By default it audits only the package tree. Add `--skyrim-vr` and `--require-prerequisites` when the Windows environment can see the target Skyrim VR install and you want the package audit to require SKSEVR, VR Address Library, VRIK, HIGGS, and PLANCK before handoff. Instead of `--skyrim-vr`, `--require-prerequisites` can use `SKYRIMVR_PATH` or `STVR_SKYRIM_VR`; without one of those explicit paths it fails before building. `BuildAuditCollectSkyrimTogetherVR-Windows.bat` wraps the matching build-and-audit command, then runs `CollectSkyrimTogetherVRBuildEvidence-Windows.bat`, then audits the newest evidence zip with `AuditSkyrimTogetherVRBuildEvidence-Windows.bat`. If the build or package audit fails, it still collects evidence and exits with the original failure code.

Pass PowerShell build options after `--`. The build-and-audit wrappers preserve each forwarded argument as a separate quoted token, so paths with spaces such as `-- -Xmake "C:\Program Files\xmake\xmake.exe"` are forwarded intact. Before a final package handoff, pass `-- -PapyrusCompiler "C:\Path\To\Caprica.exe"` or set `CAPRICA`; this regenerates the VR-specific `Data\Scripts\*.pex` files before packaging. `--preflight-only` checks Python, Caprica, source, SDK, and output paths without compiling targets. When `-Mode release` or `-Mode debug` is forwarded after `--`, the wrapper audits and collects evidence from the matching `artifacts\SkyrimTogetherVR\<mode>` package instead of always reading `releasedbg`.

After any Windows build attempt, successful or failed, collect a no-build handoff archive with:

```bat
CollectSkyrimTogetherVRBuildEvidence-Windows.bat
CollectSkyrimTogetherVRBuildEvidence-Windows.bat --dll-only
CollectSkyrimTogetherVRBuildEvidence-Windows.bat --avatar-sync --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
CollectSkyrimTogetherVRBuildEvidence-Windows.bat --gameplay --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
AuditSkyrimTogetherVRBuildEvidence-Windows.bat "artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-default-YYYYMMDD-HHMMSSZ.zip" --require-package --require-default
AuditSkyrimTogetherVRBuildEvidence-Windows.bat "artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-avatar-sync-YYYYMMDD-HHMMSSZ.zip" --require-package --require-avatar-sync
AuditSkyrimTogetherVRBuildEvidence-Windows.bat "artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-gameplay-YYYYMMDD-HHMMSSZ.zip" --require-package --require-gameplay
AuditSkyrimTogetherVRBuildEvidence-Windows.bat "artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-dll-only-YYYYMMDD-HHMMSSZ.zip" --require-package --require-dll-only
```

The collector wrapper runs `Tools/SkyrimVR/collect_build_evidence.py`. It does not compile, install files, or launch Skyrim. It captures Python/xmake metadata, visible xmake targets, the Windows build-script audit, package audit output when a package exists, package text metadata, copied Windows build wrappers such as `source\SetupSkyrimTogetherVRBuildEnv-Windows.bat` and `source\BuildSkyrimTogetherVR-Windows.ps1`, copied VR xmake target files such as `source\Code\client\xmake.lua`, copied source-side audit artifacts under `source\Docs\SkyrimVR\...`, and hashes/listings for runtime binaries into `artifacts\SkyrimTogetherVR\build-evidence`. The copied source evidence includes `inline-patch-manifest.json`, `inline-patch-audit.md`, `address-audit.json`, and `address-audit.md` so the handoff archive proves the inline patch and VR Address Library audit state that accompanied the build. `AuditSkyrimTogetherVRBuildEvidence-Windows.bat` runs `Tools\SkyrimVR\audit_build_evidence_zip.py` against a collected zip so the handoff archive itself can be checked later for package-audit failures, manifest mode mismatches, missing runtime artifact listings, command failures, Windows build wrapper/target graph drift, zero default-active/default-enableable inline patches, positive disassembly context for every active inline patch site, and zero missing core non-RTTI address entries. `AuditSkyrimTogetherVRFinalHandoff-Windows.bat` wraps `Tools\SkyrimVR\audit_final_handoff.py` (`Tools/SkyrimVR/audit_final_handoff.py` in source-tree commands) and audits the selected default/avatar-sync/gameplay/DLL-only build evidence plus default/avatar-sync/gameplay runtime evidence together against the final checklist. With no explicit zip paths, it auto-discovers the newest matching archives from the standard build/runtime evidence folders; use `--build-evidence-dir` or `--runtime-evidence-dir` for moved archives.

On Linux, the same Windows build can be attempted through a Wine prefix that has Windows PowerShell, xmake, Visual Studio/MSVC or a compatible Windows toolchain, and the required package cache available. This is an experimental path, not a substitute for a successful Windows/MSVC build. A deployable package must come from a complete, audited build; see `wine-build-status-20260710.md` for the latest local result.

```sh
./BuildSkyrimTogetherVR-Wine.sh
```

The Wine wrapper delegates to `BuildSkyrimTogetherVR-Windows.ps1`. Set `WINE=/path/to/wine` to choose a Wine binary, `WINEPREFIX=/path/to/prefix` to choose the prefix, `STVR_WINE_POWERSHELL` if PowerShell has a non-default executable name, and `STVR_XMAKE` to pass a specific Windows xmake path through `-Xmake`.

The wrapper probes PowerShell before running the build script. Wine's built-in `powershell.exe` is only a stub on some installs and can exit successfully without executing the script; other PowerShell builds can also fail to execute a command under Wine. The wrapper rejects either outcome. Use a working Windows PowerShell installation in the prefix or set `STVR_WINE_POWERSHELL` to a working executable; otherwise use the Windows/MSVC handoff path.

After a package has passed audit, Windows users can dry-run the install with:

```bat
InstallSkyrimTogetherVR-Windows.bat --package-only --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
InstallSkyrimTogetherVR-Windows.bat --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
InstallSkyrimTogetherVR-Windows.bat --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --install
InstallSkyrimTogetherVR-Windows.bat --avatar-sync --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
InstallSkyrimTogetherVR-Windows.bat --gameplay --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
```

The install wrapper is a dry-run unless `--install` is passed. `--package-only` is a dry-run preflight for the built package layout and copy plan before target Skyrim VR prerequisites are installed; it intentionally skips the target SKSEVR, VR Address Library, VRIK, HIGGS, and PLANCK checks and cannot be combined with `--install`. Instead of `--skyrim-vr`, you can set `SKYRIMVR_PATH` or `STVR_SKYRIM_VR`.

Linux xmake configurations also expose explicit Wine wrapper targets for the same Windows artifacts. These targets are not part of the default Linux build; invoke them directly when the Wine/MSVC prefix is ready:

```sh
xmake build -y SkyrimTogetherVRClient
xmake build -y SkyrimTogetherVRClientAvatarSync
xmake build -y SkyrimTogetherVRGameplayClient
xmake build -y SkyrimTogetherVRVrikBridge
xmake build -y SkyrimTogetherVRHiggsBridge
xmake build -y SkyrimTogetherVRPlanckBridge
xmake build -y SkyrimVRImmersiveLauncher
xmake build -y SkyrimVRImmersiveLauncherAvatarSync
xmake build -y SkyrimVRImmersiveLauncherGameplay
```

Each wrapper target calls `BuildSkyrimTogetherVR-Wine.sh -Targets <target> -NoPackage`, so normal Linux server/test builds do not attempt to run Wine.

The script configures xmake for Windows x64 releasedbg mode, confirms the VR targets are visible, builds the VR-only target set:

- `SkyrimTogetherVRClient`
- `SkyrimTogetherVRVrikBridge`
- `SkyrimTogetherVRHiggsBridge`
- `SkyrimTogetherVRPlanckBridge`
- `SkyrimVRImmersiveLauncher`
- `ImmersiveElf`
- `TPProcess`

and copies the staged VR game files, VR companion panel helper, packaged panel launcher, and matching build artifacts into:

```text
artifacts\SkyrimTogetherVR\releasedbg
artifacts\SkyrimTogetherVR\packages\default
artifacts\SkyrimTogetherVR\packages\avatar-sync
artifacts\SkyrimTogetherVR\packages\gameplay
artifacts\SkyrimTogetherVR\packages\dll-only
```

`releasedbg` is the most recent package for the selected xmake mode. The `packages\...` directories are per-flavor package snapshots intended for post-build readiness checks and install dry-runs after building multiple modes in one handoff session.

Packaging is strict for requested targets. If a requested target builds but its expected artifact is not copied, for example `Data\SKSE\Plugins\SkyrimTogetherVRPlanckBridge.dll`, the script fails instead of producing a partial package.

The current xmake graph is VR-only. It defines `SkyrimTogetherVRClient` as a static client library linked into `SkyrimVRImmersiveLauncher`; the normal Skyrim SE `SkyrimTogetherClient` and `SkyrimImmersiveLauncher` targets are intentionally absent. The launcher target uses the VR target config and produces the `SkyrimTogetherVR` launch artifact; `SkyrimTogetherVRVrikBridge` produces the SKSEVR bridge DLL for VRIK IK handoff; `SkyrimTogetherVRHiggsBridge` produces the SKSEVR bridge DLL for observation-only HIGGS state/callback handoff; `SkyrimTogetherVRPlanckBridge` produces the SKSEVR bridge DLL for observation-only PLANCK API heartbeat/build-number handoff with hit polling disabled; `ImmersiveElf` produces `EarlyLoad.dll`; `TPProcess` produces the companion UI process.

For VRIK/HIGGS remote-avatar validation, the graph also exposes `SkyrimTogetherVRClientAvatarSync` and `SkyrimVRImmersiveLauncherAvatarSync`. These are not in the default package target list. They build `SkyrimTogetherVRAvatarSync.exe` with staged connection-only mode still enabled, `TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC=1`, and `TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS=1`, so remote player HMD/hand pose and VRIK API packets can be checked against spawned remote actor scene nodes during two-client testing without bringing up the full gameplay service set.

For staged gameplay validation, the graph exposes `SkyrimTogetherVRGameplayClient` and `SkyrimVRImmersiveLauncherGameplay`. These build `SkyrimTogetherVRGameplay.exe` with `TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY=0`, `TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC=1`, `TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS=1`, `TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS=0`, and `TP_SKYRIM_VR_ENABLE_FLAT_OVERLAY=0`. This target uses the VR-safe observation service graph and the CommonLib gameplay bridge for remote actor lifecycle/root movement. It deliberately does not instantiate the legacy desktop mutation services; use it only after the default and avatar-sync packages pass their no-launch gates.

Useful options:

```bat
BuildSkyrimTogetherVR-Windows.bat -Mode debug
Build.bat -- -Mode release
PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --all -- -Mode release
PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --preflight-only -- -Xmake C:\Tools\xmake\xmake.exe
BuildSkyrimTogetherVR-Windows.bat -Toolchain msvc
BuildSkyrimTogetherVR-Windows.bat -CefRuntimeDirectory "C:\Users\you\AppData\Local\.xmake\packages\c\cef\141.0.11\<package-id>\bin"
BuildSkyrimTogetherVR-Windows.bat -Targets SkyrimTogetherVRClient
BuildSkyrimTogetherVR-DLL-Windows.bat -PreflightOnly
BuildSkyrimTogetherVR-DLL-Windows.bat -Mode release
BuildSkyrimTogetherVR-DLLs-Windows.bat -Mode release
BuildSkyrimTogetherVR-ClientDLL-Windows.bat -Mode release
BuildAndAuditSkyrimTogetherVR-DLL-Windows.bat -- -Mode release
BuildSkyrimTogetherVR-AvatarSync-Windows.bat -Mode release
BuildAndAuditSkyrimTogetherVR-Windows.bat -- -Mode release
BuildAndAuditSkyrimTogetherVR-Windows.bat --avatar-sync -- -Mode release
BuildAndAuditSkyrimTogetherVR-Windows.bat --gameplay -- -Mode release
CollectSkyrimTogetherVRBuildEvidence-Windows.bat --package artifacts\SkyrimTogetherVR\release --avatar-sync
CollectSkyrimTogetherVRBuildEvidence-Windows.bat --package artifacts\SkyrimTogetherVR\release --gameplay
InstallSkyrimTogetherVR-Windows.bat --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
InstallSkyrimTogetherVR-Windows.bat --package-only --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
BuildSkyrimTogetherVR-Windows.bat -Targets SkyrimTogetherVRClientAvatarSync,SkyrimVRImmersiveLauncherAvatarSync
BuildSkyrimTogetherVR-Windows.bat -Targets SkyrimTogetherVRGameplayClient,SkyrimVRImmersiveLauncherGameplay
BuildSkyrimTogetherVR-Windows.bat -NoPackage
BuildSkyrimTogetherVR-Windows.bat -SkipGameFiles
BuildSkyrimTogetherVR-Windows.bat -SkipCompanionPanel
BuildSkyrimTogetherVR-Windows.bat -BuildUi
BuildSkyrimTogetherVR-Windows.bat -PreflightOnly
BuildSkyrimTogetherVR-Windows.bat -SkseVrSdkRoot "C:\SDKs\sksevr_2_00_12"
```

If `SkyrimTogetherVRClient`, `SkyrimTogetherVRVrikBridge`, `SkyrimTogetherVRHiggsBridge`, `SkyrimTogetherVRPlanckBridge`, `SkyrimTogetherVRTickBridge`, `SkyrimTogetherVRGameplayBridge`, `SkyrimVRImmersiveLauncher`, `ImmersiveElf`, or `TPProcess` are not listed after configuration, the script fails before building. That usually means xmake was not configured for Windows, or Visual Studio/MSVC is not visible to xmake.

The default package build regenerates the staged PEX files unless `-NoPackage` or `-SkipGameFiles` is supplied. `-SkipPapyrusCompile` is rejected for a package that includes `SkyrimTogetherVRTickBridge`, because the matching tick PEX must be rebuilt with the DLL. The build resolves the official SKSEVR 2.0.12 SDK for that target: provide `-SkseVrSdkRoot`/`SKSEVR_SDK_ROOT` only when its bridge-critical source hashes match the pinned archive, or let the script download the archive into `Tools\SkyrimVR\.sdk` after SHA-256 verification. Automatic extraction needs `7z.exe` or `7zz.exe` in `PATH` (or a normal 7-Zip install).

The PowerShell script keeps `-GameFilesRoot` for compatibility with older local wrappers, but this port is VR-only. When staged game files are included, the path must resolve to this repository's `GameFiles\SkyrimVR`; passing `GameFiles\Skyrim` or another external staging tree fails before packaging. Use `-SkipGameFiles` only for artifact-only build checks.

The default package includes:

```text
SkyrimTogetherVR.exe
SkyrimTogetherVR_BuildManifest.json
libcef.dll and the complete CEF 141.0.11 runtime/resource tree
EarlyLoad.dll
TPProcess.exe
Data\SkyrimTogether.esp
Data\Scripts\SkyrimTogetherUtils.pex
Data\Scripts\SkyrimTogetherVerifyLaunchScript.pex
Data\Scripts\SkyrimTogetherPlayerAliasScript.pex
Data\Scripts\SkyrimTogetherVRTickBridge.pex
Data\Scripts\SkyrimTogetherVRConnectionMenu.pex
Data\Scripts\SkyrimTogetherVRConnectionSpellEffect.pex
Data\SKSE\Plugins\SkyrimTogetherVRVrikBridge.dll
Data\SKSE\Plugins\SkyrimTogetherVRHiggsBridge.dll
Data\SKSE\Plugins\SkyrimTogetherVRPlanckBridge.dll
Data\SKSE\Plugins\SkyrimTogetherVRTickBridge.dll
Data\SKSE\Plugins\SkyrimTogetherVRGameplayBridge.dll
Data\SKSE\Plugins\SkyrimTogetherVR_AE_to_SE.csv
Data\SKSE\Plugins\SkyrimTogetherVR_AddressOverrides.csv
LaunchSkyrimTogetherVRCompanion.bat
AuditSkyrimTogetherVRRuntime-Windows.bat
AuditSkyrimTogetherVRAvatarSyncRuntime-Windows.bat
AuditSkyrimTogetherVRGameplayRuntime-Windows.bat
CollectSkyrimTogetherVREvidence-Windows.bat
CollectSkyrimTogetherVRAvatarSyncEvidence-Windows.bat
CollectSkyrimTogetherVRGameplayEvidence-Windows.bat
AuditSkyrimTogetherVREvidence-Windows.bat
AuditSkyrimTogetherVRAvatarSyncEvidence-Windows.bat
AuditSkyrimTogetherVRGameplayEvidence-Windows.bat
Tools\SkyrimVR\vr_handoff.py
Tools\SkyrimVR\vr_paths.py
Tools\SkyrimVR\audit_runtime_handoff.py
Tools\SkyrimVR\collect_runtime_evidence.py
Tools\SkyrimVR\audit_runtime_evidence_zip.py
```

`Data\Scripts` is intentionally capitalized. Proton/Linux treats a sibling
`Data\scripts` directory as a different tree, while Skyrim VR opens the canonical
capitalized path; the package audit rejects either lowercase-only staging or both
variants together. The packaged `SkyrimTogether.esp` must also retain TES4 HEDR
version `1.70`. Skyrim VR silently omits the `1.71` desktop header variant.

The avatar-sync package substitutes `SkyrimTogetherVRAvatarSync.exe`; the gameplay package substitutes `SkyrimTogetherVRGameplay.exe`.

The source `GameFiles\SkyrimVR` folder is staged like a Skyrim `Data` folder. The Windows build script normalizes that content into package `Data\...` before install, while root-level launch artifacts and helper wrappers stay next to `SkyrimTogetherVR.exe`.

The packaged companion helpers also include the shared source module `Tools/SkyrimVR/vr_paths.py` so `vr_handoff.py`, runtime audits, and evidence collection resolve `--game-path`, `SKYRIMVR_PATH`, and `STVR_SKYRIM_VR` consistently outside the source tree. The Windows packager computes the local Python helper import closure for these scripts and copies the closure automatically. The package audits compute the same closure, so adding a new local helper import now fails the package gate if that helper is not present in the produced package.

Before copying new artifacts, the script removes known stale runtime outputs from package root and `Data\SKSE\Plugins`. This keeps default and `--avatar-sync` packages mode-specific even if both are built into `artifacts\SkyrimTogetherVR\releasedbg` during the same session.

The package root also contains `SkyrimTogetherVR_BuildManifest.json`. It records the build schema, mode, Windows x64 target list, default/avatar-sync/gameplay/DLL-only `packageFlavor`, copied artifacts, package snapshot root, whether staged game files and companion helpers were packaged, the CEF runtime version/file list for launcher packages, and whether the build script regenerated Papyrus bytecode. Launcher packages copy the complete xmake-installed CEF `141.0.11` runtime tree, including `libcef.dll`, `resources.pak`, and `locales\en-US.pak`; DLL-only packages intentionally do not contain CEF. The script finds the xmake package below `%LOCALAPPDATA%\.xmake\packages\c\cef\141.0.11` automatically. Set `STVR_CEF_RUNTIME_DIR` or forward `-CefRuntimeDirectory` only when using a nonstandard xmake cache. The launcher delay-loads `libcef.dll`, so the default VR connection-only path does not initialize CEF before a flat overlay is explicitly enabled. `audit_built_package.py` requires this metadata and tree, verifies `libcef.dll` is a delay import rather than a normal import, and inspects packaged PEX bytecode for the VR connection native/menu tokens so stale scripts cannot pass the package gate.

After a Windows package build, validate the actual package tree before copying it into Skyrim VR:

```sh
python3 Tools/SkyrimVR/audit_built_package.py --self-test
python3 Tools/SkyrimVR/install_built_package.py --self-test
python3 Tools/SkyrimVR/collect_build_evidence.py --self-test
python3 Tools/SkyrimVR/audit_build_evidence_zip.py --self-test
python3 Tools/SkyrimVR/audit_built_package.py --package artifacts/SkyrimTogetherVR/releasedbg --dll-only
python3 Tools/SkyrimVR/audit_built_package.py --package artifacts/SkyrimTogetherVR/releasedbg --skyrim-vr "/path/to/SkyrimVR" --require-installed-prerequisites
python3 Tools/SkyrimVR/audit_built_package.py --package artifacts/SkyrimTogetherVR/packages/default --skyrim-vr "/path/to/SkyrimVR" --require-installed-prerequisites
python3 Tools/SkyrimVR/audit_built_package.py --package artifacts/SkyrimTogetherVR/packages/avatar-sync --avatar-sync --skyrim-vr "/path/to/SkyrimVR" --require-installed-prerequisites
python3 Tools/SkyrimVR/audit_built_package.py --package artifacts/SkyrimTogetherVR/packages/gameplay --gameplay --skyrim-vr "/path/to/SkyrimVR" --require-installed-prerequisites
python3 Tools/SkyrimVR/audit_built_package.py --package artifacts/SkyrimTogetherVR/packages/dll-only --dll-only --skyrim-vr "/path/to/SkyrimVR" --require-installed-prerequisites
```

The self-tests create temporary package/install trees and check that the audit accepts clean default/avatar-sync/gameplay/DLL-only layouts while rejecting root-level staged game files, stale opposite-mode launcher executables, stale non-DLL artifacts in DLL-only mode, and stale/missing VR Papyrus bytecode tokens. The installer self-test also verifies dry-run stale cleanup does not mutate files and real cleanup removes only the old root-level staged game paths.

For a single no-launch readiness gate that includes the source audits, installed VR prerequisites, and the built package when present, run:

```sh
python3 Tools/SkyrimVR/audit_vr_readiness.py --skyrim-vr "/path/to/SkyrimVR"
python3 Tools/SkyrimVR/audit_vr_readiness.py --skyrim-vr "/path/to/SkyrimVR" --require-built-package
```

The first command warns if `artifacts/SkyrimTogetherVR/releasedbg` has not been produced yet. The second command fails until the Windows-built package exists and passes `audit_built_package.py`.

On Windows, the same readiness gate is wrapped by:

```bat
AuditSkyrimTogetherVRReadiness-Windows.bat --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
AuditSkyrimTogetherVRReadiness-Windows.bat --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-built-package
AuditSkyrimTogetherVRReadiness-Windows.bat --avatar-sync --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-built-package
AuditSkyrimTogetherVRReadiness-Windows.bat --gameplay --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-built-package
AuditSkyrimTogetherVRReadiness-Windows.bat --planck-archive "C:\Downloads\PLANCK.zip" --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
AuditSkyrimTogetherVRReadiness-Windows.bat --package "artifacts\SkyrimTogetherVR\packages\default" --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-built-package
AuditSkyrimTogetherVRReadiness-Windows.bat --avatar-sync --package "artifacts\SkyrimTogetherVR\packages\avatar-sync" --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-built-package
AuditSkyrimTogetherVRReadiness-Windows.bat --gameplay --package "artifacts\SkyrimTogetherVR\packages\gameplay" --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-built-package
VerifySkyrimTogetherVRWindowsPackages-Windows.bat --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
```

This wrapper does not build, install files, or launch Skyrim. It uses `--skip-fus` by default because the local FUS modlist audit is host-specific; pass `--include-fus` when the Windows environment has the FUS install available and should include that check. The source audit scans the selected FUS profile plus the deployed SkyrimVR root it can infer from the FUS folder; use `Tools\SkyrimVR\audit_fus_dll_compat.py --installed-root ...` for an explicit deployed root or `--skip-installed-root` when only the MO2 profile should be scanned. Instead of `--skyrim-vr`, you can set `SKYRIMVR_PATH` or `STVR_SKYRIM_VR`. Use `--planck-archive` or `STVR_PLANCK_ARCHIVE` when you want the readiness gate to validate the downloaded PLANCK zip contents as well as the source/API guard policy. Forwarded arguments are preserved as quoted tokens, so Skyrim VR roots and PLANCK archive filenames with spaces stay intact.

After the package audit passes, install the package into the target Skyrim VR folder with a dry run first:

```sh
python3 Tools/SkyrimVR/install_built_package.py --skyrim-vr "/path/to/SkyrimVR"
python3 Tools/SkyrimVR/install_built_package.py --skyrim-vr "/path/to/SkyrimVR" --package-only
python3 Tools/SkyrimVR/install_built_package.py --skyrim-vr "/path/to/SkyrimVR" --install
```

The installer runs `audit_built_package.py` before copying, requires the installed VRIK/HIGGS/PLANCK prerequisite stack by default, and never launches Skyrim. Use `--package-only` for a dry-run package/copy-plan preflight that skips target prerequisite checks, then omit `--package-only` once the target Skyrim VR prerequisite stack is installed. Without `--package`, the installer selects the stable package snapshot for the requested mode: `packages\default`, `packages\avatar-sync`, or `packages\gameplay`. Use `--avatar-sync` when installing the explicit remote-avatar validation package and `--gameplay` when installing the full gameplay package. The Windows install wrapper preserves forwarded quoted arguments for `--skyrim-vr`, `--package`, and other install options, so package and game paths with spaces are safe.

If the dry run reports `stale-root-file` entries, it exits non-zero. Those paths are leftovers from an older bad package layout that copied `SkyrimTogether.esp`, `scripts`, `meshes`, or `SkyrimTogetherRebornBehaviors` directly under the Skyrim VR root instead of under `Data`. Remove them during the install with:

```sh
python3 Tools/SkyrimVR/install_built_package.py --skyrim-vr "/path/to/SkyrimVR" --install --cleanup-stale-root-files
```

For the explicit remote-avatar validation build, require the avatar-sync executable instead:

```sh
BuildSkyrimTogetherVR-AvatarSync-Windows.bat
BuildAndAuditSkyrimTogetherVR-Windows.bat --avatar-sync
python3 Tools/SkyrimVR/audit_built_package.py --package artifacts/SkyrimTogetherVR/releasedbg --skyrim-vr "/path/to/SkyrimVR" --avatar-sync --require-installed-prerequisites
python3 Tools/SkyrimVR/audit_vr_readiness.py --skyrim-vr "/path/to/SkyrimVR" --avatar-sync --require-built-package
python3 Tools/SkyrimVR/install_built_package.py --skyrim-vr "/path/to/SkyrimVR" --avatar-sync --install
```

In `--avatar-sync` mode, the package audit still requires `EarlyLoad.dll`, `TPProcess.exe`, and the VRIK/HIGGS/PLANCK/tick bridge DLLs; only the launcher executable requirement changes from `SkyrimTogetherVR.exe` to `SkyrimTogetherVRAvatarSync.exe`. The audit rejects the opposite launcher executable for the selected mode, so stale default/avatar-sync packages cannot be mistaken for each other.

For the staged gameplay build, require the gameplay executable instead:

```sh
BuildSkyrimTogetherVR-Gameplay-Windows.bat
BuildAndAuditSkyrimTogetherVR-Windows.bat --gameplay
python3 Tools/SkyrimVR/audit_built_package.py --package artifacts/SkyrimTogetherVR/releasedbg --skyrim-vr "/path/to/SkyrimVR" --gameplay --require-installed-prerequisites
python3 Tools/SkyrimVR/audit_vr_readiness.py --skyrim-vr "/path/to/SkyrimVR" --gameplay --require-built-package
python3 Tools/SkyrimVR/install_built_package.py --skyrim-vr "/path/to/SkyrimVR" --gameplay --install
```

In `--gameplay` mode, the package audit requires `SkyrimTogetherVRGameplay.exe`, the VRIK/HIGGS/PLANCK/tick bridge DLLs, `EarlyLoad.dll`, and `TPProcess.exe`. It rejects stale default and avatar-sync launcher executables, and the runtime evidence collector validates the package manifest with `gameplayAudit=1`.

When testing the mandatory VRIK/HIGGS avatar lane, HIGGS compatibility, or PLANCK compatibility on the target install, add `--require-vrik`, `--require-higgs`, or `--require-planck` so the audit fails before launch if those SKSEVR plugins are not installed. PLANCK is installed as `Data\SKSE\Plugins\activeragdoll.dll`; the SkyrimTogetherVR package also installs `Data\SKSE\Plugins\SkyrimTogetherVRPlanckBridge.dll`, which writes `SkyrimTogetherVR.planck`. The post-run runtime audit's `--require-vrik` check requires both VRIK detection and the VRIK bridge API lane, and the default runtime checklist now requires `SkyrimTogetherVR.compatibility` to show HIGGS/PLANCK observation-only policy plus unvalidated-hook suppression state and `SkyrimTogetherVR.planck` to show PLANCK bridge loaded/sequence/epoch state, interface state, disabled current-hit polling, and disabled last-hit-data probe state when PLANCK is installed.

The repository also includes a local prerequisite installer/auditor for the VR stack:

```sh
python3 Tools/SkyrimVR/install_vr_prereqs.py --skyrim-vr "/path/to/SkyrimVR" --require --enable-plugins
python3 Tools/SkyrimVR/install_vr_prereqs.py --skyrim-vr "/path/to/SkyrimVR" --install --require --enable-plugins
python3 Tools/SkyrimVR/install_vr_prereqs.py --skyrim-vr "/path/to/SkyrimVR" --install --require --enable-plugins --planck-source "C:\Downloads\PLANCK.zip"
python3 Tools/SkyrimVR/audit_planck_compat.py --planck-archive "C:\Downloads\PLANCK.zip" --require-planck-archive
```

The first command is audit-only. The second copies VRIK, HIGGS, and PLANCK loose files into the target Skyrim VR `Data` folder from discovered or explicitly provided mod folders/download archives and ensures `vrik.esp` and `higgs_vr.esp` are enabled in `plugins.txt` when that file can be found. Use `--vrik-source`, `--higgs-source`, `--planck-source`, `STVR_VRIK_SOURCE`, `STVR_HIGGS_SOURCE`, `STVR_PLANCK_SOURCE`, or `STVR_PLANCK_ARCHIVE` when the source archives are not in the local default folders. `STVR_MO2_MODS`, `STVR_MO2_MOD_DIRS`, `STVR_BACKUP_DOWNLOADS`, `STVR_DOWNLOADS`, and `STVR_NORDIC_MODS` can also prepend portable source-root search paths without editing `install_vr_prereqs.py`. Use `--planck-archive` or `STVR_PLANCK_ARCHIVE` with `audit_planck_compat.py` when you want the source audit to validate the downloaded PLANCK zip contents.

## First VR Smoke-Test Package

The main VR client is linked into `SkyrimTogetherVR.exe`; the package should not contain a main `SkyrimTogetherVR.dll`. The SKSEVR DLLs in `Data\SKSE\Plugins` include narrow VRIK/HIGGS/PLANCK observation bridges, the task-backed client tick bridge, and the alandtse CommonLibSSE-NG gameplay bridge that exclusively owns validated VR actor handles and mutations.

Before the first runtime smoke test, the target Skyrim VR install still needs the public VR Address Library file:

```text
Data\SKSE\Plugins\version-1-4-15-0.csv
```

The generated helper CSVs `Data\SKSE\Plugins\SkyrimTogetherVR_AE_to_SE.csv` and `Data\SKSE\Plugins\SkyrimTogetherVR_AddressOverrides.csv` are packaged with SkyrimTogetherVR, but they do not replace the public `version-1-4-15-0.csv`.

Use the package audit as a repository/package gate:

```sh
python3 Tools/SkyrimVR/audit_smoke_package.py
```

Use the strict prerequisite gate immediately before launching the first VR smoke test:

```sh
python3 Tools/SkyrimVR/audit_smoke_package.py --skyrim-vr "/path/to/SkyrimVR" --require-installed-prerequisites
```

Run `LaunchSkyrimTogetherVRCompanion.bat` from the extracted package or Skyrim VR game folder to serve the local browser panel at `http://127.0.0.1:8765/` and open it in the default browser. Pass `--no-open-browser` to keep the panel server running without opening a browser.

After a VR run, audit the runtime handoff files without launching Skyrim:

```sh
AuditSkyrimTogetherVRRuntime-Windows.bat
AuditSkyrimTogetherVRRuntime-Windows.bat --require-connected --require-vrik --require-higgs
AuditSkyrimTogetherVRRuntime-Windows.bat --avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs
AuditSkyrimTogetherVRRuntime-Windows.bat --avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs --require-vr-pose-context
AuditSkyrimTogetherVRRuntime-Windows.bat --gameplay
AuditSkyrimTogetherVRAvatarSyncRuntime-Windows.bat
AuditSkyrimTogetherVRGameplayRuntime-Windows.bat
CollectSkyrimTogetherVREvidence-Windows.bat
CollectSkyrimTogetherVREvidence-Windows.bat --require-remote-player --require-vrik
CollectSkyrimTogetherVREvidence-Windows.bat --avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs
CollectSkyrimTogetherVREvidence-Windows.bat --avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs --require-vr-pose-context
CollectSkyrimTogetherVREvidence-Windows.bat --gameplay --require-connected --require-remote-player --require-vrik --require-higgs --require-vr-pose-context --require-gameplay-relays
CollectSkyrimTogetherVRAvatarSyncEvidence-Windows.bat
CollectSkyrimTogetherVRAvatarSyncEvidence-Windows.bat --require-vr-pose-context
CollectSkyrimTogetherVRGameplayEvidence-Windows.bat
AuditSkyrimTogetherVREvidence-Windows.bat "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-YYYYMMDD-HHMMSSZ.zip"
AuditSkyrimTogetherVREvidence-Windows.bat "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-YYYYMMDD-HHMMSSZ.zip" --require-remote-player
AuditSkyrimTogetherVREvidence-Windows.bat "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-YYYYMMDD-HHMMSSZ.zip" --require-avatar-sync
AuditSkyrimTogetherVREvidence-Windows.bat "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-YYYYMMDD-HHMMSSZ.zip" --require-gameplay
AuditSkyrimTogetherVRGameplayEvidence-Windows.bat "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-YYYYMMDD-HHMMSSZ.zip"
AuditSkyrimTogetherVREvidence-Windows.bat "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-YYYYMMDD-HHMMSSZ.zip" --require-avatar-sync --require-vr-pose-context
AuditSkyrimTogetherVRAvatarSyncEvidence-Windows.bat "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-YYYYMMDD-HHMMSSZ.zip"
AuditSkyrimTogetherVRAvatarSyncEvidence-Windows.bat "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-YYYYMMDD-HHMMSSZ.zip" --require-vr-pose-context
python3 Tools/SkyrimVR/audit_runtime_handoff.py
python3 Tools/SkyrimVR/audit_runtime_handoff.py --require-connected --require-vrik --require-higgs
python3 Tools/SkyrimVR/audit_runtime_handoff.py --avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs
python3 Tools/SkyrimVR/audit_runtime_handoff.py --avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs --require-vr-pose-context
python3 Tools/SkyrimVR/audit_runtime_handoff.py --gameplay
python3 Tools/SkyrimVR/collect_runtime_evidence.py --avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs
python3 Tools/SkyrimVR/collect_runtime_evidence.py --gameplay --require-vr-pose-context --require-gameplay-relays
python3 Tools/SkyrimVR/audit_runtime_evidence_zip.py artifacts/SkyrimTogetherVR/runtime-evidence/SkyrimTogetherVR-evidence-YYYYMMDD-HHMMSSZ.zip
python3 Tools/SkyrimVR/audit_runtime_evidence_zip.py artifacts/SkyrimTogetherVR/runtime-evidence/SkyrimTogetherVR-evidence-YYYYMMDD-HHMMSSZ.zip --require-gameplay
```

`AuditSkyrimTogetherVRRuntime-Windows.bat` is packaged next to the launcher and defaults `--game-path` to its own folder, which is correct after installing or extracting the package into the Skyrim VR root. Pass `--game-path` to point it at a different Skyrim VR install. It accepts the same strict pose-context and gameplay-relay flags as the evidence collector, so one-lane runtime validation can fail immediately before you bundle evidence.

`AuditSkyrimTogetherVRAvatarSyncRuntime-Windows.bat` is a shortcut for the required two-client VRIK/HIGGS avatar-sync validation flags and forwards any extra arguments to the runtime audit wrapper.

`CollectSkyrimTogetherVREvidence-Windows.bat` is the packaged runtime evidence collector. It does not launch Skyrim or mutate the game install; it copies `SkyrimTogetherVR_BuildManifest.json`, `logs\tp_client.log`, `Data\SkyrimTogetherReborn` handoff files, discovered SKSEVR logs, `runtime_audit.txt`, `runtime_checklist.json`, `runtime_checklist.txt`, and `manifest.json` into a zip. The checklist maps the observed runtime files back to the major VR lanes: package build manifest, startup logging, connection, HMD/hand pose, VRIK, remote avatar readiness, HIGGS-aware remote avatar readiness, discovery schema, player cell status, movement, equipment, activation, magic, combat, projectile, grab, HIGGS relay, PLANCK bridge status, HIGGS/PLANCK compatibility policy, and save/load. When relay lanes are required, the checklist requires lane-specific local and remote payload content rather than just nonzero counters: movement/equipment sequence values, activation/grab object `GameId` fields, magic effect plus caster/target/player context, combat player-side hit context, projectile source plus origin/destination or weapon/spell intent context, and HIGGS bridge/API/hand/event observation content. Single-client/baseline evidence reports remote-player, remote-avatar, and HIGGS-aware remote-avatar lanes as `not_required` until you pass `--require-remote-player` or collect with `--avatar-sync`; use those strict flags for two-client VRIK/HIGGS avatar validation. Source-tree runs write under `artifacts\SkyrimTogetherVR\runtime-evidence`; packaged runs write under `Documents\SkyrimTogetherVRRuntimeEvidence` unless `--out` is supplied. The package build manifest check fails if the collected package mode does not match the default, `--avatar-sync`, or `--gameplay` audit mode.

`CollectSkyrimTogetherVRAvatarSyncEvidence-Windows.bat` is the strict evidence shortcut for the mandatory two-client VRIK/HIGGS avatar lane. It forwards `--avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs` to the collector, and accepts extra pose-context or relay flags for deliberate validation runs.

`CollectSkyrimTogetherVRGameplayEvidence-Windows.bat` is the strict evidence shortcut for the full gameplay package. It forwards `--gameplay --require-connected --require-remote-player --require-vrik --require-higgs --require-vr-pose-context --require-gameplay-relays`, so the zip manifest records `gameplayAudit=1` and is checked against `SkyrimTogetherVRGameplay.exe`.

Use `--require-vr-pose-context` on the runtime audit or collector after a deliberate pose-context run where you draw/equip a weapon, equip or cast magic, and draw/fire a bow or projectile spell. It requires the `SkyrimTogetherVR.pose` handoff to contain weapon offset, spell/magic origin/aim, and arrow/projectile origin nodes instead of treating those action-dependent lanes as optional.

Use individual relay flags such as `--require-movement-relay`, `--require-equipment-relay`, `--require-activation-relay`, `--require-magic-relay`, `--require-combat-relay`, `--require-projectile-relay`, `--require-grab-relay`, `--require-higgs-relay`, and `--require-saveload-observer` on the runtime audit or collector when validating one staged gameplay lane at a time. Use `--require-gameplay-relays` only after a deliberate full-lane run that exercises every staged relay enough to write semantic local and remote readout fields, not just relay counts.

`AuditSkyrimTogetherVREvidence-Windows.bat` is the packaged evidence zip audit. It reads a previously collected evidence zip without needing the Skyrim VR folder, checks `manifest.json`, `package\SkyrimTogetherVR_BuildManifest.json`, `runtime_audit.txt`, `runtime_checklist.json`, required handoff/log entries, and fails if required files, package manifest validation, or runtime checklist lanes failed. The zip audit treats `manifest.json` as authoritative: if the archive was collected with strict flags such as `requiredRemotePlayer`, `requiredWeaponPose`, `requiredMovementRelay`, `avatarSyncAudit`, or `gameplayAudit`, those manifest-requested checklist lanes must pass even if the reviewer does not repeat the matching `--require-*` flags during zip audit. `avatarSyncAudit` always implies the connection, local VRIK API, HIGGS bridge, remote-player proxy, remote VRIK avatar readiness, remote VRIK/HIGGS avatar readiness, and actor-target checklist lanes. Pass `--require-remote-player` for a strict two-client remote-player proxy/VRIK-avatar evidence review, pass `--require-avatar-sync` for the strict two-client VRIK/HIGGS avatar-sync evidence zip audit, pass `--require-gameplay` for the full gameplay evidence zip audit, pass `--require-vr-pose-context` to require weapon/magic/projectile pose-context checklist lanes, pass a specific `--require-*-relay` flag for staged one-lane validation, or `--allow-failed-checks` when you want a report from a known-bad run without a failing exit code.

`AuditSkyrimTogetherVRAvatarSyncEvidence-Windows.bat` is the matching strict evidence-audit shortcut. Pass the collected zip path first; it appends `--require-avatar-sync` and forwards extra strict pose-context or relay flags.

`AuditSkyrimTogetherVRGameplayEvidence-Windows.bat` is the full gameplay evidence-audit shortcut. Pass the collected zip path first; it appends `--require-gameplay`.
