#!/usr/bin/env python3
import pathlib
import re
import sys
import os


REQUIRED_TARGETS = (
    "SkyrimTogetherVRClient",
    "SkyrimTogetherVRVrikBridge",
    "SkyrimTogetherVRHiggsBridge",
    "SkyrimTogetherVRPlanckBridge",
    "SkyrimTogetherVRTickBridge",
    "SkyrimVRImmersiveLauncher",
    "ImmersiveElf",
    "TPProcess",
)

OPTIONAL_TARGETS = (
    "SkyrimTogetherVRClientAvatarSync",
    "SkyrimVRImmersiveLauncherAvatarSync",
    "SkyrimTogetherVRGameplayClient",
    "SkyrimVRImmersiveLauncherGameplay",
)

ROOT_XMAKE_TOKENS = (
    'option("skyrim_se", function()',
    'option("skyrim_ae", function()',
    'option("skyrim_vr", function()',
    'includes("Libraries")',
)

ENV_BATCH_TOKENS = (
    "Shared Windows build environment bootstrap",
    "Intentionally does not use setlocal",
    "where powershell.exe",
    "where cl.exe",
    "vswhere.exe",
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
    "VsDevCmd.bat",
    "-arch=x64 -host_arch=x64",
    "VsDevCmd.bat completed, but cl.exe is still not visible in PATH",
)

BUILD_BATCH_TOKENS = (
    "SetupSkyrimTogetherVRBuildEnv-Windows.bat",
    "STVR_ENV_RESULT",
    "BuildSkyrimTogetherVR-Windows.ps1",
    "powershell.exe",
)

SCRIPT_TOKENS = (
    "[switch]$SkipGameFiles",
    "[switch]$SkipCompanionPanel",
    "[switch]$CompilePapyrus",
    "[switch]$SkipPapyrusCompile",
    '[string]$PapyrusCompiler = ""',
    '[string]$SkseVrSdkRoot = ""',
    '[string]$Python = ""',
    "[switch]$PreflightOnly",
    '[string]$GameFilesRoot = "GameFiles\\SkyrimVR"',
    '[string]$CompanionToolRoot = "Tools\\SkyrimVR"',
    '[string]$CefRuntimeDirectory = ""',
    '$CefRuntimeVersion = "141.0.11"',
    "$CefRuntimeRequiredFiles = @(",
    "Resolve-CefRuntimeDirectory",
    "STVR_CEF_RUNTIME_DIR",
    "Copied complete CEF",
    "Resolve-PathAgainstRepo",
    "Resolve-SkseVrSdk",
    "Test-SkseVrSdkRoot",
    "SKSEVR_SDK_ROOT",
    "sksevr_2_00_12.7z",
    "Resolve-StagedVrGameFilesRoot",
    "Resolve-PythonCommandPrefix",
    "Test-PythonCommandPrefix",
    "Resolve-PapyrusCompilerForPreflight",
    "Invoke-PapyrusCompile",
    "SkyrimTogetherVR packages must stage game files from GameFiles\\SkyrimVR",
    "Refusing GameFilesRoot",
    'Resolve-StagedVrGameFilesRoot -Path $GameFilesRoot',
    "Remove-Item -Recurse -Force -LiteralPath $packageDir",
    '$staleRuntimeArtifactBaseNames = @(',
    '$staleRuntimeArtifactExtensions = @(".exe", ".dll", ".pdb", ".lib", ".exp")',
    '$staleRuntimeArtifactDirs = @(',
    "Remove-Item -Force -LiteralPath $staleArtifactPath",
    '$packageDataDir = Join-Path $packageDir "Data"',
    '$staleRootGameFilePaths = @(',
    "Remove-Item -Recurse -Force -LiteralPath $stalePath",
    'if ($_.PSIsContainer -and $_.Name -ieq "Data")',
    "Destination $packageDataDir",
    "Copied staged VR game files from $gameFilesDir",
    "Get-LocalPythonImportModules",
    "Resolve-PackagedPythonHelpers",
    "$packagedPythonHelperNames = @(",
    "Resolve-PackagedPythonHelpers -ToolDirectory $companionToolDir -SeedNames $packagedPythonHelperNames",
    "foreach ($helperName in $packagedPythonHelperNames)",
    "Copied VR companion panel helper import closure, companion launcher, runtime audit launchers, and evidence collector/audit launchers",
    "Running SkyrimTogetherVR Windows build preflight only; no targets will be built.",
    "Papyrus compile preflight:",
    "Preflight completed; no targets were built.",
    'Invoke-Xmake @("build", "-y", "TPTests")',
    'Invoke-Xmake @("run", "TPTests")',
    "Found staged VR game files",
    "Resolved packaged Python helper closure:",
    "LaunchSkyrimTogetherVRCompanion.bat",
    "AuditSkyrimTogetherVRRuntime-Windows.bat",
    "AuditSkyrimTogetherVRAvatarSyncRuntime-Windows.bat",
    "AuditSkyrimTogetherVRGameplayRuntime-Windows.bat",
    "CollectSkyrimTogetherVREvidence-Windows.bat",
    "CollectSkyrimTogetherVRAvatarSyncEvidence-Windows.bat",
    "CollectSkyrimTogetherVRGameplayEvidence-Windows.bat",
    "AuditSkyrimTogetherVREvidence-Windows.bat",
    "AuditSkyrimTogetherVRAvatarSyncEvidence-Windows.bat",
    "AuditSkyrimTogetherVRGameplayEvidence-Windows.bat",
    "vr_handoff.py",
    "vr_paths.py",
    "audit_runtime_handoff.py",
    "collect_runtime_evidence.py",
    "audit_runtime_evidence_zip.py",
    '"SkyrimTogetherVR"',
    '"SkyrimTogetherVRAvatarSync"',
    '"SkyrimTogetherVRGameplay"',
    '"SkyrimTogetherVRClient"',
    '"SkyrimTogetherVRClientAvatarSync"',
    '"SkyrimTogetherVRGameplayClient"',
    '"SkyrimTogetherVRVrikBridge"',
    '"SkyrimTogetherVRHiggsBridge"',
    '"SkyrimTogetherVRPlanckBridge"',
    '"SkyrimTogetherVRTickBridge"',
    '"EarlyLoad"',
    '"TPProcess"',
    "Data\\SKSE\\Plugins",
    "Expected VR artifacts were not found or copied",
    "SkyrimTogetherVRHiggsBridge.dll",
    "SkyrimTogetherVRPlanckBridge.dll",
    "SkyrimTogetherVRTickBridge.dll",
    '$_ -split ","',
    'schema = "skyrim_together_vr_build_package_v2"',
    "avatarSync = [bool]$avatarSyncPackage",
    "gameplay = [bool]$gameplayPackage",
    "papyrusCompiled = [bool]$CompilePapyrus",
    "Cannot package SkyrimTogetherVRTickBridge with -SkipPapyrusCompile",
    "$sourceProvenanceForPackage = Get-SourceProvenance",
    '$packageFlavor = "gameplay"',
    '$dllOnlyPackage = ($targetSet.Count -eq 6)',
    '$packageFlavor = "default"',
    'Join-Path "packages" $packageFlavor',
    "packageFlavor = $packageFlavor",
    "packageSnapshotRoot = $packageSnapshotDir",
    "SkyrimTogetherVR_BuildManifest.json",
    "$packageManifest | ConvertTo-Json -Depth 5",
    "Wrote SkyrimTogetherVR build manifest",
    "Copied SkyrimTogetherVR package snapshot to $packageSnapshotDir",
)

DLL_BATCH_TOKENS = (
    "SetupSkyrimTogetherVRBuildEnv-Windows.bat",
    "STVR_ENV_RESULT",
    "BuildSkyrimTogetherVR-Windows.ps1",
    "STVR_TARGETS=SkyrimTogetherVRVrikBridge,SkyrimTogetherVRHiggsBridge,SkyrimTogetherVRPlanckBridge,SkyrimTogetherVRTickBridge,SkyrimTogetherVRGameplayBridge,ImmersiveElf",
    '-Targets "%STVR_TARGETS%"',
    "SkyrimTogetherVRVrikBridge.dll",
    "SkyrimTogetherVRHiggsBridge.dll",
    "SkyrimTogetherVRPlanckBridge.dll",
    "SkyrimTogetherVRTickBridge.dll",
    "EarlyLoad.dll",
    "not SkyrimTogetherVR.dll",
    "-PreflightOnly",
)

DLL_ALIAS_BATCH_TOKENS = (
    "BuildSkyrimTogetherVR-DLL-Windows.bat",
    "Compatibility alias",
)

CLIENT_DLL_ALIAS_BATCH_TOKENS = (
    "BuildSkyrimTogetherVR-DLL-Windows.bat",
    "client DLL",
    "does not emit a standalone SkyrimTogetherVR.dll",
    "DLL-producing Windows targets",
    "SkyrimTogetherVRVrikBridge.dll",
    "SkyrimTogetherVRHiggsBridge.dll",
    "SkyrimTogetherVRPlanckBridge.dll",
    "SkyrimTogetherVRTickBridge.dll",
    "EarlyLoad.dll",
    "BuildSkyrimTogetherVR-Windows.bat",
)

LEGACY_BUILD_BATCH_TOKENS = (
    "BuildAndAuditSkyrimTogetherVR-Windows.bat",
    "Skyrim VR-only compatibility wrapper",
    "never builds",
    "Skyrim SE targets",
)

LEGACY_BUILD_BATCH_FORBIDDEN_TOKENS = (
    "xmake project",
    "xmake install",
    "distrib",
    "pnpm deploy:production",
    "SkyrimTogetherClient",
    "SkyrimImmersiveLauncher",
)

AVATAR_SYNC_BATCH_TOKENS = (
    "SetupSkyrimTogetherVRBuildEnv-Windows.bat",
    "STVR_ENV_RESULT",
    "BuildSkyrimTogetherVR-Windows.ps1",
    "-Targets SkyrimTogetherVRClientAvatarSync,SkyrimVRImmersiveLauncherAvatarSync,SkyrimTogetherVRVrikBridge,SkyrimTogetherVRHiggsBridge,SkyrimTogetherVRPlanckBridge,SkyrimTogetherVRTickBridge,SkyrimTogetherVRGameplayBridge,ImmersiveElf,TPProcess",
    "SkyrimTogetherVRAvatarSync.exe",
    "SkyrimTogetherVRVrikBridge.dll",
    "SkyrimTogetherVRHiggsBridge.dll",
    "SkyrimTogetherVRPlanckBridge.dll",
    "SkyrimTogetherVRTickBridge.dll",
    "EarlyLoad.dll",
    "TPProcess.exe",
    "two-client avatar-sync build",
)

GAMEPLAY_BATCH_TOKENS = (
    "SetupSkyrimTogetherVRBuildEnv-Windows.bat",
    "STVR_ENV_RESULT",
    "BuildSkyrimTogetherVR-Windows.ps1",
    "-Targets SkyrimTogetherVRGameplayClient,SkyrimVRImmersiveLauncherGameplay,SkyrimTogetherVRVrikBridge,SkyrimTogetherVRHiggsBridge,SkyrimTogetherVRPlanckBridge,SkyrimTogetherVRTickBridge,SkyrimTogetherVRGameplayBridge,ImmersiveElf,TPProcess",
    "SkyrimTogetherVRGameplay.exe",
    "SkyrimTogetherVRVrikBridge.dll",
    "SkyrimTogetherVRHiggsBridge.dll",
    "SkyrimTogetherVRPlanckBridge.dll",
    "SkyrimTogetherVRTickBridge.dll",
    "EarlyLoad.dll",
    "TPProcess.exe",
    "staged gameplay SkyrimTogetherVR package",
)

BUILD_AND_AUDIT_BATCH_TOKENS = (
    "BuildSkyrimTogetherVR-Windows.bat",
    "BuildSkyrimTogetherVR-AvatarSync-Windows.bat",
    "BuildSkyrimTogetherVR-Gameplay-Windows.bat",
    'set "BUILD_ARGS=!BUILD_ARGS! ^"%~1^""',
    'call "%ROOT%BuildSkyrimTogetherVR-Windows.bat" %BUILD_ARGS%',
    'call "%ROOT%BuildSkyrimTogetherVR-AvatarSync-Windows.bat" %BUILD_ARGS%',
    'call "%ROOT%BuildSkyrimTogetherVR-Gameplay-Windows.bat" %BUILD_ARGS%',
    "Tools\\SkyrimVR\\audit_built_package.py",
    "--avatar-sync",
    "--gameplay",
    "--skyrim-vr",
    "--require-prerequisites",
    "--compile-papyrus",
    "SKYRIMVR_PATH",
    "STVR_SKYRIM_VR",
    "--require-prerequisites requires --skyrim-vr or SKYRIMVR_PATH / STVR_SKYRIM_VR",
    "--require-installed-prerequisites --require-vrik --require-higgs --require-planck",
    'set "PACKAGE_MODE=releasedbg"',
    'if /I "%~1"=="-Mode"',
    'set "PACKAGE_ROOT=artifacts\\SkyrimTogetherVR\\%PACKAGE_MODE%"',
    '--package "%PACKAGE_ROOT%"',
    "py -3",
    "python",
    "does not install files and does not launch Skyrim",
)

BUILD_EVIDENCE_BATCH_TOKENS = (
    "Tools\\SkyrimVR\\collect_build_evidence.py",
    "--dll-only",
    "--gameplay",
    "--avatar-sync",
    "--skyrim-vr",
    "This script does not build targets, install files, or launch Skyrim",
    "py -3",
    "python",
)

BUILD_EVIDENCE_AUDIT_BATCH_TOKENS = (
    "Tools\\SkyrimVR\\audit_build_evidence_zip.py",
    "--require-package",
    "--require-avatar-sync",
    "--require-gameplay",
    "--require-dll-only",
    "This script does not build targets, install files, or launch Skyrim",
    "py -3",
    "python",
)

FINAL_HANDOFF_AUDIT_BATCH_TOKENS = (
    "Tools\\SkyrimVR\\audit_final_handoff.py",
    "auto-discovers the newest matching build/runtime evidence archives",
    "--build-default",
    "--build-avatar-sync",
    "--build-gameplay",
    "--build-dll-only",
    "--runtime-default",
    "--runtime-avatar-sync",
    "--runtime-gameplay",
    "This script does not build targets, install files, or launch Skyrim",
    "py -3",
    "python",
)

BUILD_AUDIT_COLLECT_BATCH_TOKENS = (
    "BuildAndAuditSkyrimTogetherVR-Windows.bat",
    "BuildAndAuditSkyrimTogetherVR-DLL-Windows.bat",
    "CollectSkyrimTogetherVRBuildEvidence-Windows.bat",
    "AuditSkyrimTogetherVRBuildEvidence-Windows.bat",
    "--avatar-sync",
    "--gameplay",
    "--dll-only",
    "--require-prerequisites",
    "--compile-papyrus",
    "--require-installed-prerequisites --require-vrik --require-higgs --require-planck",
    "--require-package",
    "--require-default",
    "--require-avatar-sync",
    "--require-gameplay",
    "--require-dll-only",
    "--allow-command-failures",
    'set "PACKAGE_MODE=releasedbg"',
    'if /I "%~1"=="-Mode"',
    'set "PACKAGE_ROOT=artifacts\\SkyrimTogetherVR\\%PACKAGE_MODE%"',
    '--package "%PACKAGE_ROOT%"',
    "Build/package audit failed with exit code",
    "Collecting build evidence anyway",
    "does not install files and does not launch Skyrim",
)

PREPARE_HANDOFF_BATCH_TOKENS = (
    "BuildAuditCollectSkyrimTogetherVR-Windows.bat",
    "BuildSkyrimTogetherVR-Windows.bat",
    "BuildSkyrimTogetherVR-AvatarSync-Windows.bat",
    "BuildSkyrimTogetherVR-Gameplay-Windows.bat",
    "BuildSkyrimTogetherVR-DLL-Windows.bat",
    "--all",
    "--default-only",
    "--avatar-sync-only",
    "--gameplay-only",
    "--dll-only",
    "--include-gameplay",
    "--include-dll-only",
    "--preflight-only",
    "--require-prerequisites",
    "--compile-papyrus",
    "--skyrim-vr",
    "SKYRIMVR_PATH",
    "STVR_SKYRIM_VR",
    "default and avatar-sync packages",
    "two-client VRIK/HIGGS avatar-sync handoff",
    "full gameplay package handoff",
    "DLL-producing partial package handoff",
    "gameplay=%DO_GAMEPLAY%",
    "does not install files and does not launch Skyrim",
    'set "BUILD_ARGS=!BUILD_ARGS! ^"%~1^""',
    "SkyrimTogetherVR Windows handoff completed",
)

DLL_BUILD_AND_AUDIT_BATCH_TOKENS = (
    "BuildSkyrimTogetherVR-DLL-Windows.bat",
    'set "BUILD_ARGS=!BUILD_ARGS! ^"%~1^""',
    'call "%ROOT%BuildSkyrimTogetherVR-DLL-Windows.bat" %BUILD_ARGS%',
    "Tools\\SkyrimVR\\audit_built_package.py",
    "--dll-only",
    "--skyrim-vr",
    "--require-prerequisites",
    "--compile-papyrus",
    "SKYRIMVR_PATH",
    "STVR_SKYRIM_VR",
    "--require-prerequisites requires --skyrim-vr or SKYRIMVR_PATH / STVR_SKYRIM_VR",
    "--require-installed-prerequisites --require-vrik --require-higgs --require-planck",
    'set "PACKAGE_MODE=releasedbg"',
    'if /I "%~1"=="-Mode"',
    'set "PACKAGE_ROOT=artifacts\\SkyrimTogetherVR\\%PACKAGE_MODE%"',
    '--package "%PACKAGE_ROOT%"',
    "Running DLL-only package audit",
    "py -3",
    "python",
    "does not install files and does not launch Skyrim",
)

READINESS_BATCH_TOKENS = (
    "Tools\\SkyrimVR\\audit_vr_readiness.py",
    'set "EXTRA_ARGS=!EXTRA_ARGS! ^"%~1^""',
    "--skyrim-vr",
    "--require-built-package",
    "--avatar-sync",
    "--gameplay",
    "--include-fus",
    "--skip-fus",
    "--planck-archive",
    "SKYRIMVR_PATH",
    "STVR_SKYRIM_VR",
    "This script does not build, install files, or launch Skyrim",
    "py -3",
    "python",
)

INSTALL_BATCH_TOKENS = (
    "Tools\\SkyrimVR\\install_built_package.py",
    'set "EXTRA_ARGS=!EXTRA_ARGS! ^"%~1^""',
    "--skyrim-vr",
    "--package-only",
    "--gameplay",
    "SKYRIMVR_PATH",
    "STVR_SKYRIM_VR",
    "--install",
    "Dry-run is the default",
    "Pass --package-only for a dry-run package/copy-plan preflight",
    "Python 3 was not found",
)

VERIFY_PACKAGES_BATCH_TOKENS = (
    "AuditSkyrimTogetherVRReadiness-Windows.bat",
    "InstallSkyrimTogetherVR-Windows.bat",
    "Tools\\SkyrimVR\\audit_built_package.py",
    "artifacts\\SkyrimTogetherVR\\packages\\default",
    "artifacts\\SkyrimTogetherVR\\packages\\avatar-sync",
    "artifacts\\SkyrimTogetherVR\\packages\\gameplay",
    "artifacts\\SkyrimTogetherVR\\packages\\dll-only",
    "--require-built-package",
    "--package-only",
    "--avatar-sync",
    "--gameplay",
    "--dll-only",
    "--require-installed-prerequisites --require-vrik --require-higgs --require-planck",
    "--include-fus",
    "--planck-archive",
    "SKYRIMVR_PATH",
    "STVR_SKYRIM_VR",
    "This script does not build targets, install files, or launch Skyrim",
    "py -3",
    "python",
)

LAUNCHER_TOKENS = (
    "Tools\\SkyrimVR\\vr_handoff.py",
    "--game-path",
    "serve",
    "py -3",
    "python",
)

WINE_SCRIPT_TOKENS = (
    "BuildSkyrimTogetherVR-Windows.ps1",
    'WINE_BIN="${WINE:-wine}"',
    'STVR_WINE_POWERSHELL',
    'STVR_XMAKE',
    '$PSVersionTable.PSVersion.ToString()',
    'powershell:wmain stub',
    "The selected PowerShell did not execute a command through Wine",
    "winepath -w",
    "-NoProfile",
    "-ExecutionPolicy",
)

DOC_TOKENS = {
    "Docs/SkyrimVR/windows-build.md": (
        "VR-only",
        "Docs\\SkyrimVR\\final-handoff-checklist.md",
        "BuildSkyrimTogetherVR-Wine.sh",
        "SetupSkyrimTogetherVRBuildEnv-Windows.bat",
        "BuildSkyrimTogetherVR-DLLs-Windows.bat",
        "BuildSkyrimTogetherVR-DLL-Windows.bat",
        "BuildSkyrimTogetherVR-ClientDLL-Windows.bat",
        "Build.bat",
        "BuildAndAuditSkyrimTogetherVR-DLL-Windows.bat",
        "BuildSkyrimTogetherVR-AvatarSync-Windows.bat",
        "BuildSkyrimTogetherVR-Gameplay-Windows.bat",
        "BuildAndAuditSkyrimTogetherVR-Windows.bat",
        "BuildAuditCollectSkyrimTogetherVR-Windows.bat",
        "PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat",
        "VerifySkyrimTogetherVRWindowsPackages-Windows.bat",
        "AuditSkyrimTogetherVRFinalHandoff-Windows.bat",
        "CollectSkyrimTogetherVRBuildEvidence-Windows.bat",
        "AuditSkyrimTogetherVRBuildEvidence-Windows.bat",
        "-PreflightOnly",
        "AuditSkyrimTogetherVRReadiness-Windows.bat",
        "InstallSkyrimTogetherVR-Windows.bat",
        "--package-only",
        "--planck-archive",
        "Tools/SkyrimVR/audit_built_package.py",
        "Tools/SkyrimVR/audit_vr_readiness.py",
        "Tools/SkyrimVR/install_built_package.py",
        "Tools/SkyrimVR/audit_runtime_handoff.py",
        "Tools/SkyrimVR/vr_paths.py",
        "Tools/SkyrimVR/collect_runtime_evidence.py",
        "Tools/SkyrimVR/audit_runtime_evidence_zip.py",
        "Tools/SkyrimVR/collect_build_evidence.py",
        "Tools/SkyrimVR/audit_build_evidence_zip.py",
        "Tools/SkyrimVR/audit_final_handoff.py",
        "--build-evidence-dir",
        "--runtime-evidence-dir",
        "runtime_checklist.json",
        "xmake build -y SkyrimTogetherVRClient",
        "SkyrimTogetherVRClientAvatarSync",
        "SkyrimVRImmersiveLauncherAvatarSync",
        "SkyrimTogetherVRGameplayClient",
        "SkyrimVRImmersiveLauncherGameplay",
        "Wine wrapper targets",
        "`TPProcess`",
        "`SkyrimTogetherVRVrikBridge`",
        "`SkyrimTogetherVRHiggsBridge`",
        "`SkyrimTogetherVRPlanckBridge`",
        "Data\\SKSE\\Plugins",
        "SkyrimTogetherVR_BuildManifest.json",
        "build manifest",
        "artifacts\\SkyrimTogetherVR\\packages\\default",
        "artifacts\\SkyrimTogetherVR\\packages\\avatar-sync",
        "artifacts\\SkyrimTogetherVR\\packages\\gameplay",
        "artifacts\\SkyrimTogetherVR\\packages\\dll-only",
        "package snapshot",
        "staged VR game files",
        "-SkipGameFiles",
        "-SkipCompanionPanel",
        "LaunchSkyrimTogetherVRCompanion.bat",
        "AuditSkyrimTogetherVRRuntime-Windows.bat",
        "AuditSkyrimTogetherVRAvatarSyncRuntime-Windows.bat",
        "AuditSkyrimTogetherVRGameplayRuntime-Windows.bat",
        "CollectSkyrimTogetherVREvidence-Windows.bat",
        "CollectSkyrimTogetherVRAvatarSyncEvidence-Windows.bat",
        "CollectSkyrimTogetherVRGameplayEvidence-Windows.bat",
        "AuditSkyrimTogetherVREvidence-Windows.bat",
        "AuditSkyrimTogetherVRAvatarSyncEvidence-Windows.bat",
        "AuditSkyrimTogetherVRGameplayEvidence-Windows.bat",
        "audit_runtime_handoff.py",
        "vr_paths.py",
        "collect_runtime_evidence.py",
        "audit_runtime_evidence_zip.py",
        "runtime_checklist.json",
        "inline-patch-manifest.json",
        "address-audit.json",
        "source\\SetupSkyrimTogetherVRBuildEnv-Windows.bat",
        "source\\BuildSkyrimTogetherVR-Windows.ps1",
        "source\\Code\\client\\xmake.lua",
        "source\\Docs\\SkyrimVR",
        "Windows build wrapper/target graph drift",
        "zero default-active/default-enableable inline patches",
        "positive disassembly context for every active inline patch site",
        "zero missing core non-RTTI address entries",
        "manifest-requested checklist lanes must pass",
        "avatarSyncAudit` always implies",
        "remote VRIK avatar readiness",
    ),
    "Docs/SkyrimVR/final-handoff-checklist.md": (
        "SkyrimTogetherVR Final Handoff Checklist",
        "auto-discover",
        "--build-evidence-dir",
        "--runtime-evidence-dir",
        "PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --preflight-only",
        "PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --all",
        "PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --gameplay-only",
        "VerifySkyrimTogetherVRWindowsPackages-Windows.bat",
        "--require-prerequisites",
        "AuditSkyrimTogetherVRReadiness-Windows.bat",
        "--require-built-package",
        "artifacts\\SkyrimTogetherVR\\packages\\default",
        "artifacts\\SkyrimTogetherVR\\packages\\avatar-sync",
        "artifacts\\SkyrimTogetherVR\\packages\\gameplay",
        "artifacts\\SkyrimTogetherVR\\packages\\dll-only",
        "InstallSkyrimTogetherVR-Windows.bat --package \"artifacts\\SkyrimTogetherVR\\packages\\default\" --package-only",
        "InstallSkyrimTogetherVR-Windows.bat --avatar-sync",
        "InstallSkyrimTogetherVR-Windows.bat --gameplay",
        "--cleanup-stale-root-files",
        "AuditSkyrimTogetherVRRuntime-Windows.bat --require-connected --require-vrik --require-higgs",
        "CollectSkyrimTogetherVREvidence-Windows.bat --require-vrik --require-higgs",
        "AuditSkyrimTogetherVRAvatarSyncRuntime-Windows.bat --require-vr-pose-context",
        "CollectSkyrimTogetherVRAvatarSyncEvidence-Windows.bat --require-vr-pose-context",
        "AuditSkyrimTogetherVRGameplayRuntime-Windows.bat",
        "CollectSkyrimTogetherVRGameplayEvidence-Windows.bat",
        "--require-gameplay-relays",
        "AuditSkyrimTogetherVREvidence-Windows.bat",
        "AuditSkyrimTogetherVRAvatarSyncEvidence-Windows.bat",
        "AuditSkyrimTogetherVRGameplayEvidence-Windows.bat",
        "Explicit runtime zip paths are also role-checked",
        "SkyrimTogetherVR-build-evidence",
        "inline-patch-manifest.json",
        "address-audit.json",
        "source\\SetupSkyrimTogetherVRBuildEnv-Windows.bat",
        "source\\BuildSkyrimTogetherVR-Windows.ps1",
        "source\\Code\\client\\xmake.lua",
        "source\\Docs\\SkyrimVR",
        "copied Windows build wrappers",
        "zero default-active/default-enableable inline patches",
        "positive disassembly context for every active inline patch site",
        "zero missing core non-RTTI address entries",
        "SkyrimTogetherVR-evidence-YYYYMMDD-HHMMSSZ.zip",
        "zero invalid avatar transform or movement writes",
        "strict `manifest.json` flags",
        "avatarSyncAudit` itself requires",
        "semantic local/remote payload checks",
    ),
    "Docs/SkyrimVR/porting-status.md": (
        "VR-only",
        "final-handoff-checklist.md",
        "BuildSkyrimTogetherVR-Wine.sh",
        "SetupSkyrimTogetherVRBuildEnv-Windows.bat",
        "BuildSkyrimTogetherVR-DLLs-Windows.bat",
        "BuildSkyrimTogetherVR-DLL-Windows.bat",
        "BuildSkyrimTogetherVR-ClientDLL-Windows.bat",
        "Build.bat",
        "BuildAndAuditSkyrimTogetherVR-DLL-Windows.bat",
        "BuildSkyrimTogetherVR-AvatarSync-Windows.bat",
        "BuildSkyrimTogetherVR-Gameplay-Windows.bat",
        "BuildAndAuditSkyrimTogetherVR-Windows.bat",
        "BuildAuditCollectSkyrimTogetherVR-Windows.bat",
        "PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat",
        "VerifySkyrimTogetherVRWindowsPackages-Windows.bat",
        "AuditSkyrimTogetherVRFinalHandoff-Windows.bat",
        "CollectSkyrimTogetherVRBuildEvidence-Windows.bat",
        "-PreflightOnly",
        "AuditSkyrimTogetherVRReadiness-Windows.bat",
        "InstallSkyrimTogetherVR-Windows.bat",
        "--package-only",
        "--planck-archive",
        "Tools/SkyrimVR/audit_built_package.py",
        "Tools/SkyrimVR/audit_vr_readiness.py",
        "Tools/SkyrimVR/install_built_package.py",
        "Tools/SkyrimVR/audit_runtime_handoff.py",
        "Tools/SkyrimVR/vr_paths.py",
        "Tools/SkyrimVR/collect_runtime_evidence.py",
        "Tools/SkyrimVR/audit_runtime_evidence_zip.py",
        "Tools/SkyrimVR/collect_build_evidence.py",
        "Tools/SkyrimVR/audit_final_handoff.py",
        "auto-discovers",
        "weakened build evidence zip",
        "avatar-sync runtime zip cannot pass as default runtime evidence",
        "runtime_checklist.json",
        "same-named Wine wrapper targets",
        "SkyrimTogetherVRClientAvatarSync",
        "SkyrimVRImmersiveLauncherAvatarSync",
        "SkyrimTogetherVRGameplayClient",
        "SkyrimVRImmersiveLauncherGameplay",
        "`TPProcess`",
        "`SkyrimTogetherVRVrikBridge`",
        "`SkyrimTogetherVRHiggsBridge`",
        "`SkyrimTogetherVRPlanckBridge`",
        "Data\\SKSE\\Plugins",
        "SkyrimTogetherVR_BuildManifest.json",
        "build manifest",
        "artifacts\\SkyrimTogetherVR\\packages\\default",
        "artifacts\\SkyrimTogetherVR\\packages\\avatar-sync",
        "artifacts\\SkyrimTogetherVR\\packages\\gameplay",
        "artifacts\\SkyrimTogetherVR\\packages\\dll-only",
        "package snapshot",
        "staged VR game files",
        "LaunchSkyrimTogetherVRCompanion.bat",
        "AuditSkyrimTogetherVRRuntime-Windows.bat",
        "AuditSkyrimTogetherVRAvatarSyncRuntime-Windows.bat",
        "AuditSkyrimTogetherVRGameplayRuntime-Windows.bat",
        "CollectSkyrimTogetherVREvidence-Windows.bat",
        "CollectSkyrimTogetherVRAvatarSyncEvidence-Windows.bat",
        "CollectSkyrimTogetherVRGameplayEvidence-Windows.bat",
        "AuditSkyrimTogetherVREvidence-Windows.bat",
        "AuditSkyrimTogetherVRAvatarSyncEvidence-Windows.bat",
        "AuditSkyrimTogetherVRGameplayEvidence-Windows.bat",
        "audit_runtime_handoff.py",
        "vr_paths.py",
        "collect_runtime_evidence.py",
        "audit_runtime_evidence_zip.py",
        "runtime_checklist.json",
        "inline-patch-manifest.json",
        "address-audit.json",
        "source\\SetupSkyrimTogetherVRBuildEnv-Windows.bat",
        "source\\BuildSkyrimTogetherVR-Windows.ps1",
        "source\\Code\\client\\xmake.lua",
        "Windows build wrapper/target graph drift checks",
        "zero default-active/default-enableable inline patch validation",
        "positive disassembly context for every active inline patch site",
        "zero missing core non-RTTI address validation",
        "disassemblyReviewStatus",
        "disassemblyContext",
        "manifest-requested runtime checklist lanes",
        "requiredMovementRelay",
        "avatarSyncAudit",
        "cannot be relaxed by omitting `requiredRemotePlayer`",
    ),
}

FORBIDDEN_XMAKE_TOKENS = {
    "Code/client/xmake.lua": (
        'build_client("SkyrimTogetherClient"',
        'add_defines("TP_SKYRIM_VR=0")',
    ),
    "Code/immersive_launcher/xmake.lua": (
        'target("SkyrimImmersiveLauncher")',
        'set_basename("SkyrimTogether")',
        'add_deps("SkyrimTogetherClient")',
        'add_defines("TP_SKYRIM_VR=0")',
    ),
}

LINUX_WRAPPER_TOKENS = (
    'local function build_wine_target(name)',
    'set_kind("phony")',
    'set_default(false)',
    '"BuildSkyrimTogetherVR-Wine.sh"',
    '"-Targets", target:name()',
    '"-NoPackage"',
    'build_wine_target(target_name)',
)


def repo_root():
    return pathlib.Path(__file__).resolve().parents[2]


def read_text(path):
    return path.read_text(encoding="utf-8", errors="replace")


def extract_default_targets(script):
    match = re.search(r"\[string\[\]\]\$Targets\s*=\s*@\((.*?)\)", script, re.S)
    if not match:
        return set()
    return set(re.findall(r'"([^"]+)"', match.group(1)))


def extract_required_targets(script):
    match = re.search(r"\$requiredTargets\s*=\s*@\((.*?)\)", script, re.S)
    if not match:
        return set()
    return set(re.findall(r'"([^"]+)"', match.group(1)))


def main():
    root = repo_root()
    script_path = root / "BuildSkyrimTogetherVR-Windows.ps1"
    env_batch_path = root / "SetupSkyrimTogetherVRBuildEnv-Windows.bat"
    build_batch_path = root / "BuildSkyrimTogetherVR-Windows.bat"
    legacy_build_batch_path = root / "Build.bat"
    dll_batch_path = root / "BuildSkyrimTogetherVR-DLL-Windows.bat"
    dll_alias_batch_path = root / "BuildSkyrimTogetherVR-DLLs-Windows.bat"
    client_dll_alias_batch_path = root / "BuildSkyrimTogetherVR-ClientDLL-Windows.bat"
    avatar_sync_batch_path = root / "BuildSkyrimTogetherVR-AvatarSync-Windows.bat"
    gameplay_batch_path = root / "BuildSkyrimTogetherVR-Gameplay-Windows.bat"
    build_and_audit_batch_path = root / "BuildAndAuditSkyrimTogetherVR-Windows.bat"
    build_audit_collect_batch_path = root / "BuildAuditCollectSkyrimTogetherVR-Windows.bat"
    prepare_handoff_batch_path = root / "PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat"
    build_evidence_batch_path = root / "CollectSkyrimTogetherVRBuildEvidence-Windows.bat"
    build_evidence_audit_batch_path = root / "AuditSkyrimTogetherVRBuildEvidence-Windows.bat"
    final_handoff_audit_batch_path = root / "AuditSkyrimTogetherVRFinalHandoff-Windows.bat"
    verify_packages_batch_path = root / "VerifySkyrimTogetherVRWindowsPackages-Windows.bat"
    dll_build_and_audit_batch_path = root / "BuildAndAuditSkyrimTogetherVR-DLL-Windows.bat"
    readiness_batch_path = root / "AuditSkyrimTogetherVRReadiness-Windows.bat"
    install_batch_path = root / "InstallSkyrimTogetherVR-Windows.bat"
    wine_script_path = root / "BuildSkyrimTogetherVR-Wine.sh"
    launcher_path = root / "LaunchSkyrimTogetherVRCompanion.bat"
    docs = {root / relative_path: tokens for relative_path, tokens in DOC_TOKENS.items()}

    failures = []
    script = read_text(script_path)
    env_batch = read_text(env_batch_path) if env_batch_path.exists() else ""
    build_batch = read_text(build_batch_path) if build_batch_path.exists() else ""
    legacy_build_batch = read_text(legacy_build_batch_path) if legacy_build_batch_path.exists() else ""
    dll_batch = read_text(dll_batch_path) if dll_batch_path.exists() else ""
    dll_alias_batch = read_text(dll_alias_batch_path) if dll_alias_batch_path.exists() else ""
    client_dll_alias_batch = read_text(client_dll_alias_batch_path) if client_dll_alias_batch_path.exists() else ""
    avatar_sync_batch = read_text(avatar_sync_batch_path) if avatar_sync_batch_path.exists() else ""
    gameplay_batch = read_text(gameplay_batch_path) if gameplay_batch_path.exists() else ""
    build_and_audit_batch = read_text(build_and_audit_batch_path) if build_and_audit_batch_path.exists() else ""
    build_audit_collect_batch = read_text(build_audit_collect_batch_path) if build_audit_collect_batch_path.exists() else ""
    prepare_handoff_batch = read_text(prepare_handoff_batch_path) if prepare_handoff_batch_path.exists() else ""
    build_evidence_batch = read_text(build_evidence_batch_path) if build_evidence_batch_path.exists() else ""
    build_evidence_audit_batch = read_text(build_evidence_audit_batch_path) if build_evidence_audit_batch_path.exists() else ""
    final_handoff_audit_batch = read_text(final_handoff_audit_batch_path) if final_handoff_audit_batch_path.exists() else ""
    verify_packages_batch = read_text(verify_packages_batch_path) if verify_packages_batch_path.exists() else ""
    dll_build_and_audit_batch = read_text(dll_build_and_audit_batch_path) if dll_build_and_audit_batch_path.exists() else ""
    readiness_batch = read_text(readiness_batch_path) if readiness_batch_path.exists() else ""
    install_batch = read_text(install_batch_path) if install_batch_path.exists() else ""
    wine_script = read_text(wine_script_path) if wine_script_path.exists() else ""
    launcher = read_text(launcher_path) if launcher_path.exists() else ""
    default_targets = extract_default_targets(script)
    required_targets = extract_required_targets(script)

    missing_default_targets = sorted(set(REQUIRED_TARGETS) - default_targets)
    missing_required_targets = sorted(set(REQUIRED_TARGETS) - required_targets)
    missing_env_batch_tokens = [token for token in ENV_BATCH_TOKENS if token not in env_batch]
    missing_build_batch_tokens = [token for token in BUILD_BATCH_TOKENS if token not in build_batch]
    missing_script_tokens = [token for token in SCRIPT_TOKENS if token not in script]
    missing_legacy_build_batch_tokens = [
        token for token in LEGACY_BUILD_BATCH_TOKENS if token not in legacy_build_batch
    ]
    present_legacy_build_batch_forbidden_tokens = [
        token for token in LEGACY_BUILD_BATCH_FORBIDDEN_TOKENS if token in legacy_build_batch
    ]
    missing_dll_batch_tokens = [token for token in DLL_BATCH_TOKENS if token not in dll_batch]
    missing_dll_alias_batch_tokens = [token for token in DLL_ALIAS_BATCH_TOKENS if token not in dll_alias_batch]
    missing_client_dll_alias_batch_tokens = [
        token for token in CLIENT_DLL_ALIAS_BATCH_TOKENS if token not in client_dll_alias_batch
    ]
    missing_avatar_sync_batch_tokens = [token for token in AVATAR_SYNC_BATCH_TOKENS if token not in avatar_sync_batch]
    missing_gameplay_batch_tokens = [token for token in GAMEPLAY_BATCH_TOKENS if token not in gameplay_batch]
    missing_build_and_audit_batch_tokens = [token for token in BUILD_AND_AUDIT_BATCH_TOKENS if token not in build_and_audit_batch]
    missing_build_audit_collect_batch_tokens = [
        token for token in BUILD_AUDIT_COLLECT_BATCH_TOKENS if token not in build_audit_collect_batch
    ]
    missing_prepare_handoff_batch_tokens = [
        token for token in PREPARE_HANDOFF_BATCH_TOKENS if token not in prepare_handoff_batch
    ]
    missing_build_evidence_batch_tokens = [token for token in BUILD_EVIDENCE_BATCH_TOKENS if token not in build_evidence_batch]
    missing_build_evidence_audit_batch_tokens = [
        token for token in BUILD_EVIDENCE_AUDIT_BATCH_TOKENS if token not in build_evidence_audit_batch
    ]
    missing_final_handoff_audit_batch_tokens = [
        token for token in FINAL_HANDOFF_AUDIT_BATCH_TOKENS if token not in final_handoff_audit_batch
    ]
    missing_verify_packages_batch_tokens = [
        token for token in VERIFY_PACKAGES_BATCH_TOKENS if token not in verify_packages_batch
    ]
    missing_dll_build_and_audit_batch_tokens = [
        token for token in DLL_BUILD_AND_AUDIT_BATCH_TOKENS if token not in dll_build_and_audit_batch
    ]
    missing_readiness_batch_tokens = [token for token in READINESS_BATCH_TOKENS if token not in readiness_batch]
    missing_install_batch_tokens = [token for token in INSTALL_BATCH_TOKENS if token not in install_batch]
    missing_wine_script_tokens = [token for token in WINE_SCRIPT_TOKENS if token not in wine_script]
    missing_launcher_tokens = [token for token in LAUNCHER_TOKENS if token not in launcher]
    root_xmake = read_text(root / "xmake.lua")
    code_xmake = read_text(root / "Code/xmake.lua")
    immersive_launcher_xmake = read_text(root / "Code/immersive_launcher/xmake.lua")
    missing_root_xmake_tokens = [token for token in ROOT_XMAKE_TOKENS if token not in root_xmake]
    missing_linux_wrapper_tokens = [token for token in LINUX_WRAPPER_TOKENS if token not in code_xmake]

    if missing_default_targets:
        failures.append(f"default target list is missing: {', '.join(missing_default_targets)}")
    if missing_required_targets:
        failures.append(f"required target check is missing: {', '.join(missing_required_targets)}")
    if missing_script_tokens:
        failures.append(f"script tokens missing: {', '.join(missing_script_tokens)}")
    if '"/DELAYLOAD:libcef.dll"' not in immersive_launcher_xmake:
        failures.append("VR launcher xmake file is missing /DELAYLOAD:libcef.dll")
    if not env_batch_path.exists():
        failures.append(f"Windows build environment bootstrap is missing: {env_batch_path}")
    if missing_env_batch_tokens:
        failures.append(f"Windows build environment bootstrap tokens missing: {', '.join(missing_env_batch_tokens)}")
    if not build_batch_path.exists():
        failures.append(f"Windows build batch file is missing: {build_batch_path}")
    if missing_build_batch_tokens:
        failures.append(f"Windows build batch tokens missing: {', '.join(missing_build_batch_tokens)}")
    if not legacy_build_batch_path.exists():
        failures.append(f"Legacy Build.bat wrapper is missing: {legacy_build_batch_path}")
    if missing_legacy_build_batch_tokens:
        failures.append(f"Legacy Build.bat wrapper tokens missing: {', '.join(missing_legacy_build_batch_tokens)}")
    if present_legacy_build_batch_forbidden_tokens:
        failures.append(
            "Legacy Build.bat wrapper forbidden tokens present: "
            + ", ".join(present_legacy_build_batch_forbidden_tokens)
        )
    if not dll_batch_path.exists():
        failures.append(f"DLL build batch file is missing: {dll_batch_path}")
    if missing_dll_batch_tokens:
        failures.append(f"DLL build batch tokens missing: {', '.join(missing_dll_batch_tokens)}")
    if not dll_alias_batch_path.exists():
        failures.append(f"DLL build batch alias is missing: {dll_alias_batch_path}")
    if missing_dll_alias_batch_tokens:
        failures.append(f"DLL build batch alias tokens missing: {', '.join(missing_dll_alias_batch_tokens)}")
    if not client_dll_alias_batch_path.exists():
        failures.append(f"Client DLL compatibility batch alias is missing: {client_dll_alias_batch_path}")
    if missing_client_dll_alias_batch_tokens:
        failures.append(
            "Client DLL compatibility batch alias tokens missing: "
            + ", ".join(missing_client_dll_alias_batch_tokens)
        )
    if not avatar_sync_batch_path.exists():
        failures.append(f"Avatar-sync build batch file is missing: {avatar_sync_batch_path}")
    if missing_avatar_sync_batch_tokens:
        failures.append(f"Avatar-sync build batch tokens missing: {', '.join(missing_avatar_sync_batch_tokens)}")
    if not gameplay_batch_path.exists():
        failures.append(f"Gameplay build batch file is missing: {gameplay_batch_path}")
    if missing_gameplay_batch_tokens:
        failures.append(f"Gameplay build batch tokens missing: {', '.join(missing_gameplay_batch_tokens)}")
    if not build_and_audit_batch_path.exists():
        failures.append(f"Build-and-audit batch file is missing: {build_and_audit_batch_path}")
    if missing_build_and_audit_batch_tokens:
        failures.append(f"Build-and-audit batch tokens missing: {', '.join(missing_build_and_audit_batch_tokens)}")
    if not build_audit_collect_batch_path.exists():
        failures.append(f"Build/audit/collect batch file is missing: {build_audit_collect_batch_path}")
    if missing_build_audit_collect_batch_tokens:
        failures.append(
            "Build/audit/collect batch tokens missing: "
            + ", ".join(missing_build_audit_collect_batch_tokens)
        )
    if not prepare_handoff_batch_path.exists():
        failures.append(f"Prepare handoff batch file is missing: {prepare_handoff_batch_path}")
    if missing_prepare_handoff_batch_tokens:
        failures.append(
            "Prepare handoff batch tokens missing: "
            + ", ".join(missing_prepare_handoff_batch_tokens)
        )
    if not build_evidence_batch_path.exists():
        failures.append(f"Build evidence batch file is missing: {build_evidence_batch_path}")
    if missing_build_evidence_batch_tokens:
        failures.append(f"Build evidence batch tokens missing: {', '.join(missing_build_evidence_batch_tokens)}")
    if not build_evidence_audit_batch_path.exists():
        failures.append(f"Build evidence audit batch file is missing: {build_evidence_audit_batch_path}")
    if missing_build_evidence_audit_batch_tokens:
        failures.append(
            "Build evidence audit batch tokens missing: "
            + ", ".join(missing_build_evidence_audit_batch_tokens)
        )
    if not final_handoff_audit_batch_path.exists():
        failures.append(f"Final handoff audit batch file is missing: {final_handoff_audit_batch_path}")
    if missing_final_handoff_audit_batch_tokens:
        failures.append(
            "Final handoff audit batch tokens missing: "
            + ", ".join(missing_final_handoff_audit_batch_tokens)
        )
    if not verify_packages_batch_path.exists():
        failures.append(f"Package snapshot verifier batch file is missing: {verify_packages_batch_path}")
    if missing_verify_packages_batch_tokens:
        failures.append(
            "Package snapshot verifier batch tokens missing: "
            + ", ".join(missing_verify_packages_batch_tokens)
        )
    if not dll_build_and_audit_batch_path.exists():
        failures.append(f"DLL build-and-audit batch file is missing: {dll_build_and_audit_batch_path}")
    if missing_dll_build_and_audit_batch_tokens:
        failures.append(
            "DLL build-and-audit batch tokens missing: "
            + ", ".join(missing_dll_build_and_audit_batch_tokens)
        )
    if not readiness_batch_path.exists():
        failures.append(f"Readiness batch file is missing: {readiness_batch_path}")
    if missing_readiness_batch_tokens:
        failures.append(f"Readiness batch tokens missing: {', '.join(missing_readiness_batch_tokens)}")
    if not install_batch_path.exists():
        failures.append(f"Install batch file is missing: {install_batch_path}")
    if missing_install_batch_tokens:
        failures.append(f"Install batch tokens missing: {', '.join(missing_install_batch_tokens)}")
    if not wine_script_path.exists():
        failures.append(f"Wine build wrapper is missing: {wine_script_path}")
    elif not os.access(wine_script_path, os.X_OK):
        failures.append(f"Wine build wrapper is not executable: {wine_script_path}")
    if missing_wine_script_tokens:
        failures.append(f"Wine script tokens missing: {', '.join(missing_wine_script_tokens)}")
    if missing_launcher_tokens:
        failures.append(f"launcher tokens missing: {', '.join(missing_launcher_tokens)}")
    if missing_root_xmake_tokens:
        failures.append(f"root xmake bootstrap tokens missing: {', '.join(missing_root_xmake_tokens)}")
    if missing_linux_wrapper_tokens:
        failures.append(f"Linux Wine xmake wrapper tokens missing: {', '.join(missing_linux_wrapper_tokens)}")

    for target in REQUIRED_TARGETS + OPTIONAL_TARGETS:
        if f'"{target}"' not in code_xmake:
            failures.append(f"Linux Wine xmake wrapper is missing target: {target}")

    target_patterns = {
        "SkyrimTogetherVRClient": 'build_vr_client("SkyrimTogetherVRClient")',
        "SkyrimTogetherVRClientAvatarSync": 'build_vr_client("SkyrimTogetherVRClientAvatarSync"',
        "SkyrimTogetherVRGameplayClient": 'build_vr_client("SkyrimTogetherVRGameplayClient"',
        "SkyrimTogetherVRVrikBridge": 'target("SkyrimTogetherVRVrikBridge")',
        "SkyrimTogetherVRHiggsBridge": 'target("SkyrimTogetherVRHiggsBridge")',
        "SkyrimTogetherVRPlanckBridge": 'target("SkyrimTogetherVRPlanckBridge")',
        "SkyrimTogetherVRTickBridge": 'target("SkyrimTogetherVRTickBridge")',
        "SkyrimVRImmersiveLauncher": 'target("SkyrimVRImmersiveLauncher")',
        "SkyrimVRImmersiveLauncherAvatarSync": 'target("SkyrimVRImmersiveLauncherAvatarSync")',
        "SkyrimVRImmersiveLauncherGameplay": 'target("SkyrimVRImmersiveLauncherGameplay")',
        "ImmersiveElf": 'target("ImmersiveElf")',
        "TPProcess": 'target("TPProcess")',
    }

    for target, pattern in target_patterns.items():
        if target == "SkyrimTogetherVRClient" or target == "SkyrimTogetherVRClientAvatarSync" or target == "SkyrimTogetherVRGameplayClient":
            target_file = root / "Code/client/xmake.lua"
        elif target == "SkyrimTogetherVRVrikBridge":
            target_file = root / "Code/vrik_bridge/xmake.lua"
        elif target == "SkyrimTogetherVRHiggsBridge":
            target_file = root / "Code/higgs_bridge/xmake.lua"
        elif target == "SkyrimTogetherVRPlanckBridge":
            target_file = root / "Code/planck_bridge/xmake.lua"
        elif target == "SkyrimTogetherVRTickBridge":
            target_file = root / "Code/vr_tick_bridge/xmake.lua"
        elif target == "SkyrimVRImmersiveLauncher" or target == "SkyrimVRImmersiveLauncherAvatarSync" or target == "SkyrimVRImmersiveLauncherGameplay":
            target_file = root / "Code/immersive_launcher/xmake.lua"
        elif target == "ImmersiveElf":
            target_file = root / "Code/immersive_elf/xmake.lua"
        else:
            target_file = root / "Code/tp_process/xmake.lua"

        if pattern not in read_text(target_file):
            failures.append(f"xmake target definition not found for {target} in {target_file}")

    for relative_path, tokens in FORBIDDEN_XMAKE_TOKENS.items():
        text = read_text(root / relative_path)
        present_tokens = [token for token in tokens if token in text]
        if present_tokens:
            failures.append(f"{relative_path}: forbidden non-VR target tokens present: {', '.join(present_tokens)}")

    for doc_path, tokens in docs.items():
        doc = read_text(doc_path)
        missing_doc_tokens = [token for token in tokens if token not in doc]
        if missing_doc_tokens:
            failures.append(f"{doc_path}: missing docs tokens: {', '.join(missing_doc_tokens)}")

    print(f"Windows build script default targets: {len(default_targets)}")
    print(f"Windows build script required targets: {len(required_targets)}")
    print(f"Missing default targets: {len(missing_default_targets)}")
    print(f"Missing required targets: {len(missing_required_targets)}")
    print(f"Missing script tokens: {len(missing_script_tokens)}")
    print(f"Missing Windows build environment bootstrap tokens: {len(missing_env_batch_tokens)}")
    print(f"Missing Windows build batch tokens: {len(missing_build_batch_tokens)}")
    print(f"Missing legacy Build.bat tokens: {len(missing_legacy_build_batch_tokens)}")
    print(f"Forbidden legacy Build.bat tokens: {len(present_legacy_build_batch_forbidden_tokens)}")
    print(f"Missing build/audit/collect batch tokens: {len(missing_build_audit_collect_batch_tokens)}")
    print(f"Missing prepare handoff batch tokens: {len(missing_prepare_handoff_batch_tokens)}")
    print(f"Missing build evidence batch tokens: {len(missing_build_evidence_batch_tokens)}")
    print(f"Missing build evidence audit batch tokens: {len(missing_build_evidence_audit_batch_tokens)}")
    print(f"Missing final handoff audit batch tokens: {len(missing_final_handoff_audit_batch_tokens)}")
    print(f"Missing package snapshot verifier batch tokens: {len(missing_verify_packages_batch_tokens)}")
    print(f"Missing DLL build batch tokens: {len(missing_dll_batch_tokens)}")
    print(f"Missing client DLL compatibility batch tokens: {len(missing_client_dll_alias_batch_tokens)}")
    print(f"Missing DLL build-and-audit batch tokens: {len(missing_dll_build_and_audit_batch_tokens)}")
    print(f"Missing avatar-sync build batch tokens: {len(missing_avatar_sync_batch_tokens)}")
    print(f"Missing gameplay build batch tokens: {len(missing_gameplay_batch_tokens)}")
    print(f"Missing readiness batch tokens: {len(missing_readiness_batch_tokens)}")
    print(f"Missing Wine script tokens: {len(missing_wine_script_tokens)}")
    print(f"Missing launcher tokens: {len(missing_launcher_tokens)}")
    print(f"Missing root xmake bootstrap tokens: {len(missing_root_xmake_tokens)}")
    print(f"Missing Linux xmake wrapper tokens: {len(missing_linux_wrapper_tokens)}")
    print(f"Failures: {len(failures)}")

    for failure in failures:
        print(f"- {failure}")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
