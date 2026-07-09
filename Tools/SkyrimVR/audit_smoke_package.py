#!/usr/bin/env python3
import argparse
import ast
import pathlib
import re
import sys

import vr_paths

REQUIRED_STAGED_FILES = (
    "SkyrimTogether.esp",
    "Data/SKSE/Plugins/SkyrimTogetherVR_AE_to_SE.csv",
    "Data/SKSE/Plugins/SkyrimTogetherVR_AddressOverrides.csv",
    "scripts/SkyrimTogetherUtils.pex",
    "scripts/SkyrimTogetherVerifyLaunchScript.pex",
    "scripts/SkyrimTogetherPlayerAliasScript.pex",
    "scripts/SkyrimTogetherVRConnectionMenu.pex",
    "scripts/SkyrimTogetherVRConnectionSpellEffect.pex",
)

REQUIRED_SOURCE_FILES = (
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
    "Tools/SkyrimVR/vr_handoff.py",
    "Tools/SkyrimVR/vr_paths.py",
    "Tools/SkyrimVR/audit_runtime_handoff.py",
    "Tools/SkyrimVR/collect_runtime_evidence.py",
    "Tools/SkyrimVR/audit_runtime_evidence_zip.py",
    "Tools/SkyrimVR/install_vr_prereqs.py",
    "Tools/SkyrimVR/audit_built_package.py",
    "Tools/SkyrimVR/audit_gamefiles.py",
    "Tools/SkyrimVR/compile_papyrus.py",
    "Tools/SkyrimVR/install_built_package.py",
    "Tools/SkyrimVR/audit_vr_readiness.py",
    "Tools/SkyrimVR/collect_build_evidence.py",
    "Tools/SkyrimVR/audit_build_evidence_zip.py",
    "Tools/SkyrimVR/audit_final_handoff.py",
    "InstallSkyrimTogetherVR-Windows.bat",
    "AuditSkyrimTogetherVRReadiness-Windows.bat",
    "BuildAuditCollectSkyrimTogetherVR-Windows.bat",
    "CollectSkyrimTogetherVRBuildEvidence-Windows.bat",
    "AuditSkyrimTogetherVRBuildEvidence-Windows.bat",
    "AuditSkyrimTogetherVRFinalHandoff-Windows.bat",
    "VerifySkyrimTogetherVRWindowsPackages-Windows.bat",
    "BuildSkyrimTogetherVR-Windows.ps1",
)

PACKAGED_PYTHON_HELPERS = (
    "vr_handoff.py",
    "vr_paths.py",
    "audit_runtime_handoff.py",
    "collect_runtime_evidence.py",
    "audit_runtime_evidence_zip.py",
)

REQUIRED_PACKAGE_SCRIPT_TOKENS = (
    '$staleRuntimeArtifactBaseNames = @(',
    '$staleRuntimeArtifactExtensions = @(".exe", ".dll", ".pdb", ".lib", ".exp")',
    '$staleRuntimeArtifactDirs = @(',
    "Resolve-PathAgainstRepo",
    "Resolve-StagedVrGameFilesRoot",
    "SkyrimTogetherVR packages must stage game files from GameFiles\\SkyrimVR",
    "Refusing GameFilesRoot",
    'Resolve-StagedVrGameFilesRoot -Path $GameFilesRoot',
    "[switch]$PreflightOnly",
    "[switch]$CompilePapyrus",
    "Invoke-PapyrusCompile",
    "Papyrus compile preflight:",
    "Remove-Item -Force -LiteralPath $staleArtifactPath",
    '$packageDataDir = Join-Path $packageDir "Data"',
    '$staleRootGameFilePaths = @(',
    "Remove-Item -Recurse -Force -LiteralPath $stalePath",
    'if ($_.PSIsContainer -and $_.Name -ieq "Data")',
    "Destination $packageDataDir",
    '"SkyrimTogetherVRClient" { $expectedArtifactNames.Add("SkyrimTogetherVRClient.lib") }',
    '"SkyrimTogetherVRClientAvatarSync" { $expectedArtifactNames.Add("SkyrimTogetherVRClientAvatarSync.lib") }',
    '"SkyrimTogetherVRGameplayClient" { $expectedArtifactNames.Add("SkyrimTogetherVRGameplayClient.lib") }',
    '"SkyrimTogetherVRVrikBridge" { $expectedArtifactNames.Add("SkyrimTogetherVRVrikBridge.dll") }',
    '"SkyrimTogetherVRHiggsBridge" { $expectedArtifactNames.Add("SkyrimTogetherVRHiggsBridge.dll") }',
    '"SkyrimTogetherVRPlanckBridge" { $expectedArtifactNames.Add("SkyrimTogetherVRPlanckBridge.dll") }',
    '"SkyrimVRImmersiveLauncher" { $expectedArtifactNames.Add("SkyrimTogetherVR.exe") }',
    '"SkyrimVRImmersiveLauncherAvatarSync" { $expectedArtifactNames.Add("SkyrimTogetherVRAvatarSync.exe") }',
    '"SkyrimVRImmersiveLauncherGameplay" { $expectedArtifactNames.Add("SkyrimTogetherVRGameplay.exe") }',
    '"ImmersiveElf" { $expectedArtifactNames.Add("EarlyLoad.dll") }',
    '"TPProcess" { $expectedArtifactNames.Add("TPProcess.exe") }',
    'Join-Path $packageDir "Data\\SKSE\\Plugins"',
    "Copied staged VR game files from $gameFilesDir",
    "Get-LocalPythonImportModules",
    "Resolve-PackagedPythonHelpers",
    "audit_runtime_handoff.py",
    "vr_paths.py",
    "collect_runtime_evidence.py",
    "audit_runtime_evidence_zip.py",
    "Resolve-PackagedPythonHelpers -ToolDirectory $companionToolDir -SeedNames $packagedPythonHelperNames",
    "foreach ($helperName in $packagedPythonHelperNames)",
    "Running SkyrimTogetherVR Windows build preflight only; no targets will be built.",
    "Preflight completed; no targets were built.",
    "Found staged VR game files",
    "Resolved packaged Python helper closure:",
    "AuditSkyrimTogetherVRRuntime-Windows.bat",
    "AuditSkyrimTogetherVRAvatarSyncRuntime-Windows.bat",
    "AuditSkyrimTogetherVRGameplayRuntime-Windows.bat",
    "CollectSkyrimTogetherVREvidence-Windows.bat",
    "CollectSkyrimTogetherVRAvatarSyncEvidence-Windows.bat",
    "CollectSkyrimTogetherVRGameplayEvidence-Windows.bat",
    "AuditSkyrimTogetherVREvidence-Windows.bat",
    "AuditSkyrimTogetherVRAvatarSyncEvidence-Windows.bat",
    "AuditSkyrimTogetherVRGameplayEvidence-Windows.bat",
    "Copied VR companion panel helper import closure, companion launcher, runtime audit launchers, and evidence collector/audit launchers",
    'schema = "skyrim_together_vr_build_package_v1"',
    "gameplay = [bool]$gameplayPackage",
    '$packageFlavor = "gameplay"',
    '$dllOnlyPackage = ($targetSet.Count -eq 4)',
    '$packageFlavor = "default"',
    'Join-Path "packages" $packageFlavor',
    "packageFlavor = $packageFlavor",
    "packageSnapshotRoot = $packageSnapshotDir",
    "SkyrimTogetherVR_BuildManifest.json",
    "$packageManifest | ConvertTo-Json -Depth 5",
    "papyrusCompiled = [bool]$CompilePapyrus",
    "Wrote SkyrimTogetherVR build manifest",
    "Copied SkyrimTogetherVR package snapshot to $packageSnapshotDir",
)

REQUIRED_BUILT_PACKAGE_AUDIT_TOKENS = (
    "BRIDGE_RUNTIME_FILES",
    "DLL_ONLY_REQUIRED_RUNTIME_FILES",
    "AVATAR_SYNC_REQUIRED_RUNTIME_FILES",
    "GAMEPLAY_REQUIRED_RUNTIME_FILES",
    "DLL_ONLY_EXPECTED_MANIFEST_TARGETS",
    "GAMEPLAY_EXPECTED_MANIFEST_TARGETS",
    "DLL_ONLY_FORBIDDEN_RUNTIME_FILES",
    "GAMEPLAY_FORBIDDEN_RUNTIME_FILES",
    '"SkyrimTogetherVRAvatarSync.exe"',
    '"EarlyLoad.dll"',
    '"TPProcess.exe"',
    '"Data/SkyrimTogether.esp"',
    '"Data/scripts/SkyrimTogetherUtils.pex"',
    '"SkyrimTogether.esp"',
    '"scripts"',
    "DEFAULT_FORBIDDEN_RUNTIME_FILES",
    "AVATAR_SYNC_FORBIDDEN_RUNTIME_FILES",
    "MANIFEST_SCHEMA",
    "PACKAGED_PYTHON_HELPERS",
    "audit_gamefiles",
    "audit_packaged_papyrus",
    "packaged Papyrus bytecode is missing VR token",
    "papyrus_pex_fixture",
    "Package self-test did not reject stale SkyrimTogetherUtils.pex bytecode.",
    "Package self-test did not reject stale SE Papyrus bytecode.",
    "packaged_python_import_closure",
    "audit_build_manifest",
    '"SkyrimTogetherVR_BuildManifest.json"',
    "build manifest avatarSync does not match audit mode",
    "build manifest gameplay does not match audit mode",
    "build manifest packageFlavor does not match audit mode",
    "build manifest copiedArtifacts missing runtime artifact(s)",
    "dll-only build manifest has unexpected target(s)",
    "dll-only build manifest copiedArtifacts has unexpected runtime artifact(s)",
    "required_runtime_files",
    "expected_manifest_targets",
    "package_mode_name",
    "write_build_manifest",
    "run_self_test",
    '"--self-test"',
    '"--dll-only"',
    '"--gameplay"',
    "Gameplay package self-test unexpectedly failed:",
    "DLL-only package self-test unexpectedly failed:",
    "DLL-only package self-test did not reject stale default launcher.",
    "DLL-only package self-test did not reject stale TPProcess runtime.",
    "DLL-only package self-test did not reject full-package manifest targets.",
    "DLL-only package self-test did not reject full-package manifest artifacts.",
    "missing package Python helper: Tools/SkyrimVR/vr_paths.py",
    "Package self-test did not reject missing imported Python helper.",
    "write_x64_pe",
    "forbidden package file present: SkyrimTogetherVRAvatarSync.exe",
    "forbidden package file present: SkyrimTogetherVR.exe",
    "missing package file: Data/SkyrimTogether.esp",
    '"SkyrimTogetherVRAvatarSync.exe"',
    '"SkyrimTogetherVR.exe"',
    '"Data/SKSE/Plugins/SkyrimTogetherVRVrikBridge.dll"',
    '"Data/SKSE/Plugins/SkyrimTogetherVRHiggsBridge.dll"',
    '"Data/SKSE/Plugins/SkyrimTogetherVRPlanckBridge.dll"',
    "Package mode: {package_mode_name",
)

REQUIRED_INSTALLER_TOKENS = (
    "STALE_ROOT_GAME_FILE_PATHS",
    "DEFAULT_PACKAGE",
    "AVATAR_SYNC_PACKAGE",
    "GAMEPLAY_PACKAGE",
    "default_package_for_mode",
    "build_package_audit_command",
    "find_stale_target_paths",
    "cleanup_stale_root_files",
    "stale_root_files_block_handoff",
    '"--package-only"',
    "Package-only preflight",
    "cannot be combined with --install",
    '"--cleanup-stale-root-files"',
    '"--self-test"',
    "stale-root-file",
    "Refusing to continue while stale root-level staged files are present.",
    "Run once with --install --cleanup-stale-root-files",
    "shutil.rmtree(path)",
    "path.unlink()",
    "Install self-test passed.",
)

REQUIRED_DOC_TOKENS = {
    "Docs/SkyrimVR/windows-build.md": (
        "First VR Smoke-Test Package",
        "SkyrimTogetherVR.exe",
        "EarlyLoad.dll",
        "TPProcess.exe",
        "Data\\SkyrimTogether.esp",
        "Data\\scripts\\SkyrimTogetherUtils.pex",
        "Data\\SKSE\\Plugins\\SkyrimTogetherVRVrikBridge.dll",
        "Data\\SKSE\\Plugins\\SkyrimTogetherVRHiggsBridge.dll",
        "Data\\SKSE\\Plugins\\SkyrimTogetherVRPlanckBridge.dll",
        "Data\\SKSE\\Plugins\\SkyrimTogetherVR_AE_to_SE.csv",
        "Data\\SKSE\\Plugins\\SkyrimTogetherVR_AddressOverrides.csv",
        "SkyrimTogetherVR_BuildManifest.json",
        "Data\\SKSE\\Plugins\\version-1-4-15-0.csv",
        "The main VR client is linked into `SkyrimTogetherVR.exe`",
        "In `--avatar-sync` mode",
        "audit_runtime_handoff.py",
        "collect_runtime_evidence.py",
        "audit_runtime_evidence_zip.py",
        "audit_build_evidence_zip.py",
        "runtime_checklist.json",
        "AuditSkyrimTogetherVRRuntime-Windows.bat",
        "AuditSkyrimTogetherVRAvatarSyncRuntime-Windows.bat",
        "AuditSkyrimTogetherVRGameplayRuntime-Windows.bat",
        "CollectSkyrimTogetherVREvidence-Windows.bat",
        "CollectSkyrimTogetherVRAvatarSyncEvidence-Windows.bat",
        "CollectSkyrimTogetherVRGameplayEvidence-Windows.bat",
        "AuditSkyrimTogetherVREvidence-Windows.bat",
        "AuditSkyrimTogetherVRAvatarSyncEvidence-Windows.bat",
        "AuditSkyrimTogetherVRGameplayEvidence-Windows.bat",
        "InstallSkyrimTogetherVR-Windows.bat",
        "AuditSkyrimTogetherVRReadiness-Windows.bat",
        "BuildAuditCollectSkyrimTogetherVR-Windows.bat",
        "AuditSkyrimTogetherVRBuildEvidence-Windows.bat",
        "audit_built_package.py --self-test",
        "install_built_package.py --self-test",
        "--package-only",
        "--cleanup-stale-root-files",
        "audit_build_evidence_zip.py",
        "audit_final_handoff.py",
        "build manifest",
        "AuditSkyrimTogetherVRFinalHandoff-Windows.bat",
        "VerifySkyrimTogetherVRWindowsPackages-Windows.bat",
        "artifacts\\SkyrimTogetherVR\\packages\\default",
        "artifacts\\SkyrimTogetherVR\\packages\\avatar-sync",
        "artifacts\\SkyrimTogetherVR\\packages\\gameplay",
        "artifacts\\SkyrimTogetherVR\\packages\\dll-only",
        "package snapshot",
        "source-tree commands",
        "inline-patch-manifest.json",
        "address-audit.json",
        "source\\Docs\\SkyrimVR",
        "zero default-active/default-enableable inline patches",
        "positive disassembly context for every active inline patch site",
        "zero missing core non-RTTI address entries",
        "manifest-requested checklist lanes must pass",
        "avatarSyncAudit` always implies",
        "remote VRIK avatar readiness",
    ),
    "Docs/SkyrimVR/porting-status.md": (
        "Tools/SkyrimVR/audit_smoke_package.py",
        "first VR smoke-test package manifest",
        "BuildAuditCollectSkyrimTogetherVR-Windows.bat",
        "VerifySkyrimTogetherVRWindowsPackages-Windows.bat",
        "Data\\SKSE\\Plugins\\version-1-4-15-0.csv",
        "cleanup-stale-root-files",
        "audit_build_evidence_zip.py",
        "audit_final_handoff.py",
        "artifacts\\SkyrimTogetherVR\\packages\\default",
        "artifacts\\SkyrimTogetherVR\\packages\\avatar-sync",
        "artifacts\\SkyrimTogetherVR\\packages\\gameplay",
        "artifacts\\SkyrimTogetherVR\\packages\\dll-only",
        "inline-patch-manifest.json",
        "address-audit.json",
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


def repo_root():
    return pathlib.Path(__file__).resolve().parents[2]


def read_text(path):
    return path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""


def count_csv_rows(path):
    if not path.exists():
        return 0

    with path.open(encoding="utf-8-sig", errors="replace") as handle:
        return sum(1 for line in handle if line.strip())


def local_import_modules(path):
    try:
        tree = ast.parse(path.read_text(encoding="utf-8", errors="replace"), filename=str(path))
    except SyntaxError as exc:
        return set(), [f"{path}: Python syntax error while auditing imports: {exc}"]
    except OSError as exc:
        return set(), [f"{path}: could not read Python helper while auditing imports: {exc}"]

    modules = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                modules.add(alias.name.split(".", 1)[0])
        elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module:
            modules.add(node.module.split(".", 1)[0])

    return modules, []


def packaged_python_import_closure(tool_dir):
    failures = []
    required = set(PACKAGED_PYTHON_HELPERS)
    queue = list(PACKAGED_PYTHON_HELPERS)
    visited = set()

    while queue:
        helper = queue.pop(0)
        if helper in visited:
            continue
        visited.add(helper)
        path = tool_dir / helper
        if not path.exists():
            failures.append(f"packaged Python helper import seed is missing: Tools/SkyrimVR/{helper}")
            continue

        modules, import_failures = local_import_modules(path)
        failures.extend(import_failures)
        for module in sorted(modules):
            candidate = f"{module}.py"
            if not (tool_dir / candidate).exists():
                continue
            if candidate not in required:
                required.add(candidate)
                queue.append(candidate)

    return tuple(sorted(required)), failures


def installed_prerequisites(skyrim_vr):
    return {
        "sksevr": (skyrim_vr / "sksevr_1_4_15.dll").exists(),
        "address_library": (skyrim_vr / "Data" / "SKSE" / "Plugins" / "version-1-4-15-0.csv").exists(),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--package", type=pathlib.Path, default=pathlib.Path("GameFiles/SkyrimVR"))
    parser.add_argument("--skyrim-vr", type=pathlib.Path, default=vr_paths.default_skyrim_vr_path(), help=vr_paths.skyrim_vr_help())
    parser.add_argument(
        "--require-installed-prerequisites",
        action="store_true",
        help="fail if the target Skyrim VR install is missing SKSEVR or the public VR Address Library CSV",
    )
    args = parser.parse_args()

    root = repo_root()
    package = (root / args.package).resolve() if not args.package.is_absolute() else args.package.resolve()
    skyrim_vr = args.skyrim_vr.resolve()
    failures = []

    for relative_file in REQUIRED_STAGED_FILES:
        if not (package / relative_file).exists():
            failures.append(f"missing staged package file: {relative_file}")

    for relative_file in REQUIRED_SOURCE_FILES:
        if not (root / relative_file).exists():
            failures.append(f"missing source/package helper file: {relative_file}")

    helper_closure, import_failures = packaged_python_import_closure(root / "Tools" / "SkyrimVR")
    failures.extend(import_failures)
    for helper in helper_closure:
        relative = f"Tools/SkyrimVR/{helper}"
        if relative not in REQUIRED_SOURCE_FILES:
            failures.append(f"packaged Python helper import closure is not in REQUIRED_SOURCE_FILES: {relative}")

    ae_to_se_rows = count_csv_rows(package / "Data" / "SKSE" / "Plugins" / "SkyrimTogetherVR_AE_to_SE.csv")
    overrides_rows = count_csv_rows(package / "Data" / "SKSE" / "Plugins" / "SkyrimTogetherVR_AddressOverrides.csv")
    if ae_to_se_rows <= 1:
        failures.append("SkyrimTogetherVR_AE_to_SE.csv has no data rows")
    if overrides_rows <= 1:
        failures.append("SkyrimTogetherVR_AddressOverrides.csv has no data rows")

    script_text = read_text(root / "BuildSkyrimTogetherVR-Windows.ps1")
    for token in REQUIRED_PACKAGE_SCRIPT_TOKENS:
        if token not in script_text:
            failures.append(f"BuildSkyrimTogetherVR-Windows.ps1 missing token: {token}")
    for helper in helper_closure:
        if helper not in script_text:
            failures.append(f"BuildSkyrimTogetherVR-Windows.ps1 missing packaged Python helper token: {helper}")

    audit_built_package_text = read_text(root / "Tools" / "SkyrimVR" / "audit_built_package.py")
    for token in REQUIRED_BUILT_PACKAGE_AUDIT_TOKENS:
        if token not in audit_built_package_text:
            failures.append(f"audit_built_package.py missing token: {token}")
    for helper in helper_closure:
        if f'"Tools/SkyrimVR/{helper}"' not in audit_built_package_text:
            failures.append(f"audit_built_package.py missing packaged Python helper requirement: Tools/SkyrimVR/{helper}")

    installer_text = read_text(root / "Tools" / "SkyrimVR" / "install_built_package.py")
    for token in REQUIRED_INSTALLER_TOKENS:
        if token not in installer_text:
            failures.append(f"install_built_package.py missing token: {token}")

    if "SkyrimTogetherVR.dll" in script_text:
        failures.append("BuildSkyrimTogetherVR-Windows.ps1 should not package a main SkyrimTogetherVR.dll")

    target_config = read_text(root / "Code" / "immersive_launcher" / "TargetConfig.h")
    launcher_xmake = read_text(root / "Code" / "immersive_launcher" / "xmake.lua")
    if "#define CLIENT_DLL 0" not in target_config:
        failures.append("TargetConfig.h must keep CLIENT_DLL 0 for the launcher-linked VR client")
    if "SkyrimTogetherVR.dll" in target_config:
        failures.append("TargetConfig.h should not name a standalone SkyrimTogetherVR.dll client")
    if 'add_ldflags("/WHOLEARCHIVE:SkyrimTogetherVRClient", { force = true })' not in launcher_xmake:
        failures.append("SkyrimVRImmersiveLauncher must whole-archive link SkyrimTogetherVRClient")

    for relative_file, tokens in REQUIRED_DOC_TOKENS.items():
        text = read_text(root / relative_file)
        for token in tokens:
            if token not in text:
                failures.append(f"{relative_file} missing doc token: {token}")

    prereq = installed_prerequisites(skyrim_vr)
    if args.require_installed_prerequisites:
        if not prereq["sksevr"]:
            failures.append("target Skyrim VR install is missing sksevr_1_4_15.dll")
        if not prereq["address_library"]:
            failures.append(
                "target Skyrim VR install is missing Data/SKSE/Plugins/version-1-4-15-0.csv"
            )

    print(f"Staged package root: {package}")
    print(f"AE-to-SE CSV rows: {ae_to_se_rows}")
    print(f"Address override CSV rows: {overrides_rows}")
    print("Packaged Python helper closure: " + ", ".join(helper_closure))
    print(f"Installed SKSEVR DLL present: {prereq['sksevr']}")
    print(f"Installed VR Address Library CSV present: {prereq['address_library']}")
    print(f"Smoke package audit failures: {len(failures)}")
    for failure in failures:
        print(f"- {failure}")

    # Missing installed prerequisites are reported for the user's first-run checklist
    # but are not a repository/package failure.
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
