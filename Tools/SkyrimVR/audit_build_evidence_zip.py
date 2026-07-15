#!/usr/bin/env python3
"""Audit a SkyrimTogetherVR Windows build evidence zip."""

from __future__ import annotations

import argparse
import json
import pathlib
import tempfile
import zipfile

import audit_built_package
import collect_build_evidence


REQUIRED_ARCHIVE_ENTRIES = (
    "manifest.json",
    "summary.txt",
    "package_file_listing.txt",
    "commands/python-version.txt",
    "commands/windows-build-audit.txt",
)
REQUIRED_SOURCE_EVIDENCE_ENTRIES = tuple(
    f"source/{relative}" for relative in collect_build_evidence.SOURCE_EVIDENCE_FILES
)

REQUIRED_SOURCE_TEXT_TOKENS = {
    "source/SetupSkyrimTogetherVRBuildEnv-Windows.bat": (
        "Shared Windows build environment bootstrap",
        "vswhere.exe",
        "VsDevCmd.bat",
        "-arch=x64 -host_arch=x64",
    ),
    "source/BuildSkyrimTogetherVR-Windows.bat": (
        "SetupSkyrimTogetherVRBuildEnv-Windows.bat",
        "BuildSkyrimTogetherVR-Windows.ps1",
    ),
    "source/BuildSkyrimTogetherVR-AvatarSync-Windows.bat": (
        "SetupSkyrimTogetherVRBuildEnv-Windows.bat",
        "SkyrimTogetherVRClientAvatarSync",
        "SkyrimVRImmersiveLauncherAvatarSync",
    ),
    "source/BuildSkyrimTogetherVR-Gameplay-Windows.bat": (
        "SetupSkyrimTogetherVRBuildEnv-Windows.bat",
        "SkyrimTogetherVRGameplayClient",
        "SkyrimVRImmersiveLauncherGameplay",
    ),
    "source/BuildSkyrimTogetherVR-DLL-Windows.bat": (
        "SetupSkyrimTogetherVRBuildEnv-Windows.bat",
        "SkyrimTogetherVRVrikBridge",
        "SkyrimTogetherVRHiggsBridge",
        "SkyrimTogetherVRPlanckBridge",
        "SkyrimTogetherVRTickBridge",
        "SkyrimTogetherVRGameplayBridge",
        "ImmersiveElf",
    ),
    "source/BuildSkyrimTogetherVR-DLLs-Windows.bat": (
        "Compatibility alias",
        "BuildSkyrimTogetherVR-DLL-Windows.bat",
    ),
    "source/BuildSkyrimTogetherVR-ClientDLL-Windows.bat": (
        "client DLL",
        "does not emit a standalone SkyrimTogetherVR.dll",
        "BuildSkyrimTogetherVR-DLL-Windows.bat",
    ),
    "source/BuildSkyrimTogetherVR-Windows.ps1": (
        "SkyrimTogetherVR_BuildManifest.json",
        "CompilePapyrus",
        "Resolve-SkseVrSdk",
        "SKSEVR_SDK_ROOT",
        "Invoke-PapyrusCompile",
        "papyrusCompiled",
        "packageFlavor = $packageFlavor",
        "Copied SkyrimTogetherVR package snapshot to $packageSnapshotDir",
    ),
    "source/Tools/SkyrimVR/audit_built_package.py": (
        "audit_packaged_papyrus",
        "packaged Papyrus bytecode is missing VR token",
        "papyrus_pex_fixture",
    ),
    "source/Tools/SkyrimVR/audit_gamefiles.py": (
        "REQUIRED_UTILS_NATIVE_TOKENS",
        "REQUIRED_VR_MENU_PEX_TOKENS",
        "SetSkyrimTogetherConnectionConfig",
    ),
    "source/Tools/SkyrimVR/compile_papyrus.py": (
        "TARGET_SCRIPTS",
        "SkyrimTogetherVRConnectionMenu.psc",
        "--caprica",
    ),
    "source/AuditSkyrimTogetherVRGameplayRuntime-Windows.bat": (
        "AuditSkyrimTogetherVRRuntime-Windows.bat",
        "--gameplay",
    ),
    "source/CollectSkyrimTogetherVRGameplayEvidence-Windows.bat": (
        "CollectSkyrimTogetherVREvidence-Windows.bat",
        "--gameplay",
        "--require-connected",
    ),
    "source/AuditSkyrimTogetherVRGameplayEvidence-Windows.bat": (
        "AuditSkyrimTogetherVREvidence-Windows.bat",
        "--require-gameplay",
    ),
    "source/Code/xmake.lua": (
        "SkyrimTogetherVRClient",
        "SkyrimTogetherVRClientAvatarSync",
        "SkyrimTogetherVRGameplayClient",
        "SkyrimTogetherVRPlanckBridge",
        "SkyrimTogetherVRTickBridge",
        "SkyrimVRImmersiveLauncherAvatarSync",
        "SkyrimVRImmersiveLauncherGameplay",
    ),
    "source/Code/vr_common/VRHandoffPath.h": (
        "STVR_GAME_PATH",
        "GetModuleFileNameW",
        "SkyrimVR.exe",
        "Data",
        "SkyrimTogetherReborn",
    ),
    "source/Code/vr_common/VRTickBridge.h": (
        "STVR_TICK_BRIDGE_HANDLE",
        "kEndpointAbiVersion",
        "DispatchCallback",
    ),
    "source/Code/client/VRTickBridge.cpp": (
        "CreateFileMappingW",
        "EndpointState::Ready",
        "SKSE task callback accepted",
        "InterlockedExchange64(&s_pendingDispatchSequence, sequence)",
        "TryConsumeUpdatePermit",
        "update owner starved",
    ),
    "source/Code/vr_tick_bridge/xmake.lua": (
        "target(\"SkyrimTogetherVRTickBridge\")",
        "SKSEVR_SDK_ROOT",
    ),
    "source/Code/vr_tick_bridge/main.cpp": (
        "SKSETaskInterface",
        "NativeFunction0<StaticFunctionTag, bool>",
        "g_taskInterface->AddTask(task)",
    ),
    "source/Code/client/xmake.lua": (
        "build_vr_client(\"SkyrimTogetherVRClient\")",
        "build_vr_client(\"SkyrimTogetherVRClientAvatarSync\"",
        "build_vr_client(\"SkyrimTogetherVRGameplayClient\"",
        "TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC",
    ),
    "source/Code/immersive_launcher/xmake.lua": (
        "target(\"SkyrimVRImmersiveLauncher\")",
        "target(\"SkyrimVRImmersiveLauncherAvatarSync\")",
        "target(\"SkyrimVRImmersiveLauncherGameplay\")",
        "SkyrimTogetherVRAvatarSync",
        "SkyrimTogetherVRGameplay",
    ),
    "source/Code/immersive_launcher/loader/PathRerouting.cpp": (
        "SetEnvironmentVariableW(L\"STVR_GAME_PATH\"",
        "SetCurrentDirectoryW(gamePath.c_str())",
    ),
    "source/Code/vrik_bridge/xmake.lua": (
        "target(\"SkyrimTogetherVRVrikBridge\")",
        "set_basename(\"SkyrimTogetherVRVrikBridge\")",
    ),
    "source/Code/higgs_bridge/xmake.lua": (
        "target(\"SkyrimTogetherVRHiggsBridge\")",
        "set_basename(\"SkyrimTogetherVRHiggsBridge\")",
    ),
    "source/Code/planck_bridge/xmake.lua": (
        "target(\"SkyrimTogetherVRPlanckBridge\")",
        "set_basename(\"SkyrimTogetherVRPlanckBridge\")",
    ),
}

MODE_RUNTIME_FILES = {
    "default": audit_built_package.DEFAULT_REQUIRED_RUNTIME_FILES,
    "avatar-sync": audit_built_package.AVATAR_SYNC_REQUIRED_RUNTIME_FILES,
    "gameplay": audit_built_package.GAMEPLAY_REQUIRED_RUNTIME_FILES,
    "dll-only": audit_built_package.DLL_ONLY_REQUIRED_RUNTIME_FILES,
}

MODE_MANIFEST_RUNTIME_FILES = {
    "default": audit_built_package.manifest_runtime_files(),
    "avatar-sync": audit_built_package.manifest_runtime_files(avatar_sync=True),
    "gameplay": audit_built_package.manifest_runtime_files(gameplay=True),
    "dll-only": audit_built_package.manifest_runtime_files(dll_only=True),
}

MODE_EXPECTED_TARGETS = {
    "default": audit_built_package.DEFAULT_EXPECTED_MANIFEST_TARGETS,
    "avatar-sync": audit_built_package.AVATAR_SYNC_EXPECTED_MANIFEST_TARGETS,
    "gameplay": audit_built_package.GAMEPLAY_EXPECTED_MANIFEST_TARGETS,
    "dll-only": audit_built_package.DLL_ONLY_EXPECTED_MANIFEST_TARGETS,
}


def load_json(zf: zipfile.ZipFile, name: str, failures: list[str]) -> dict[str, object]:
    try:
        return json.loads(zf.read(name).decode("utf-8-sig"))
    except KeyError:
        failures.append(f"missing archive entry: {name}")
    except json.JSONDecodeError as exc:
        failures.append(f"{name} is not valid JSON: {exc}")
    except UnicodeDecodeError as exc:
        failures.append(f"{name} is not UTF-8: {exc}")
    return {}


def load_text(zf: zipfile.ZipFile, name: str, failures: list[str]) -> str:
    try:
        return zf.read(name).decode("utf-8")
    except KeyError:
        failures.append(f"missing archive entry: {name}")
    except UnicodeDecodeError as exc:
        failures.append(f"{name} is not UTF-8: {exc}")
    return ""


def package_manifest_errors(manifest: dict[str, object], *, mode: str) -> list[str]:
    errors: list[str] = []
    if manifest.get("schema") != audit_built_package.MANIFEST_SCHEMA:
        errors.append(f"schema={manifest.get('schema')!r}")
    if manifest.get("platform") != "windows":
        errors.append(f"platform={manifest.get('platform')!r}")
    if manifest.get("arch") != "x64":
        errors.append(f"arch={manifest.get('arch')!r}")

    expected_avatar_sync = mode == "avatar-sync"
    if bool(manifest.get("avatarSync")) != expected_avatar_sync:
        errors.append(f"avatarSync={manifest.get('avatarSync')!r} expected={expected_avatar_sync}")
    if manifest.get("packageFlavor") != mode:
        errors.append(f"packageFlavor={manifest.get('packageFlavor')!r} expected={mode!r}")
    if bool(manifest.get("gameplay")) != (mode == "gameplay"):
        errors.append(f"gameplay={manifest.get('gameplay')!r} expected={mode == 'gameplay'}")
    if manifest.get("stagedGameFiles") is not True:
        errors.append("stagedGameFiles is not true")
    if manifest.get("companionPanel") is not True:
        errors.append("companionPanel is not true")

    targets = manifest.get("targets")
    target_set = {str(target) for target in targets} if isinstance(targets, list) else set()
    if not isinstance(targets, list):
        errors.append("targets is not a list")
    missing_targets = [target for target in MODE_EXPECTED_TARGETS[mode] if target not in target_set]
    if missing_targets:
        errors.append("missing targets: " + ", ".join(missing_targets))
    if mode == "dll-only":
        unexpected_targets = [target for target in sorted(target_set) if target not in MODE_EXPECTED_TARGETS[mode]]
        if unexpected_targets:
            errors.append("unexpected dll-only targets: " + ", ".join(unexpected_targets))

    copied_artifacts = manifest.get("copiedArtifacts")
    artifact_set = {str(artifact) for artifact in copied_artifacts} if isinstance(copied_artifacts, list) else set()
    if not isinstance(copied_artifacts, list):
        errors.append("copiedArtifacts is not a list")
    required_artifacts = [pathlib.PurePath(path).name for path in MODE_MANIFEST_RUNTIME_FILES[mode]]
    missing_artifacts = [artifact for artifact in required_artifacts if artifact not in artifact_set]
    if missing_artifacts:
        errors.append("missing copied artifacts: " + ", ".join(missing_artifacts))
    if mode == "dll-only":
        forbidden = {"SkyrimTogetherVR.exe", "SkyrimTogetherVRAvatarSync.exe", "TPProcess.exe"}
        unexpected_artifacts = [artifact for artifact in sorted(artifact_set) if artifact in forbidden]
        if unexpected_artifacts:
            errors.append("unexpected dll-only copied artifacts: " + ", ".join(unexpected_artifacts))

    cef_runtime = manifest.get("cefRuntime")
    if mode == "dll-only":
        if cef_runtime is not None:
            errors.append("dll-only manifest unexpectedly includes CEF runtime metadata")
    elif not isinstance(cef_runtime, dict):
        errors.append("launcher manifest is missing CEF runtime metadata")
    else:
        if cef_runtime.get("version") != audit_built_package.CEF_RUNTIME_VERSION:
            errors.append(f"CEF runtime version={cef_runtime.get('version')!r}")
        files = cef_runtime.get("files")
        if not isinstance(files, list):
            errors.append("CEF runtime files is not a list")
        else:
            file_set = {str(path).replace("\\", "/") for path in files}
            missing_cef_files = [
                path for path in audit_built_package.CEF_RUNTIME_REQUIRED_FILES if path not in file_set
            ]
            if missing_cef_files:
                errors.append("CEF runtime files missing: " + ", ".join(missing_cef_files))

    return errors


def inline_patch_manifest_errors(manifest: dict[str, object]) -> list[str]:
    errors: list[str] = []
    if manifest.get("schema") != "SkyrimTogetherVR.inlinePatchAudit.v1":
        errors.append(f"schema={manifest.get('schema')!r}")

    summary = manifest.get("summary")
    if not isinstance(summary, dict):
        errors.append("summary is not an object")
        summary = {}

    expected_zero_summary_fields = (
        "activeDefaultVrPatchSites",
        "auditSafetyFailures",
        "defaultEnablementAllowed",
        "vrGuardPolicyFailures",
    )
    for field in expected_zero_summary_fields:
        if summary.get(field) != 0:
            errors.append(f"summary.{field}={summary.get(field)!r} expected=0")

    semantic_manual_review_required = summary.get("semanticManualReviewRequired")
    if not isinstance(semantic_manual_review_required, int) or semantic_manual_review_required < 0:
        errors.append(
            "summary.semanticManualReviewRequired must be a non-negative integer "
            f"(got {semantic_manual_review_required!r})"
        )
    active_sites = summary.get("activeUpstreamPatchSites")
    active_sites_with_disassembly = summary.get("activeSitesWithDisassemblyContext")
    active_sites_missing_disassembly = summary.get("activeSitesMissingDisassemblyContext")
    if not isinstance(active_sites, int) or active_sites < 0:
        errors.append(f"summary.activeUpstreamPatchSites must be a non-negative integer (got {active_sites!r})")
    if active_sites_missing_disassembly != 0:
        errors.append(
            "summary.activeSitesMissingDisassemblyContext="
            f"{active_sites_missing_disassembly!r} expected=0"
        )
    if isinstance(active_sites, int) and active_sites_with_disassembly != active_sites:
        errors.append(
            "summary.activeSitesWithDisassemblyContext="
            f"{active_sites_with_disassembly!r} expected={active_sites}"
        )

    if manifest.get("steamCegDetected") is True and manifest.get("decryptedForAnalysis") is not True:
        errors.append("steamCegDetected is true but decryptedForAnalysis is not true")

    for field in ("safetyFailures", "guardPolicyFailures", "missingSourceTokens"):
        value = manifest.get(field)
        if not isinstance(value, list):
            errors.append(f"{field} is not a list")
        elif value:
            errors.append(f"{field} is not empty")

    sites = manifest.get("sites")
    if not isinstance(sites, list):
        errors.append("sites is not a list")
        sites = []

    active_sites_missing_context = [
        str(site.get("name", "<unnamed>"))
        for site in sites
        if isinstance(site, dict)
        and site.get("activeUpstream") is True
        and not str(site.get("disassemblyContext", "")).strip()
    ]
    if active_sites_missing_context:
        errors.append("active inline patch site(s) missing disassembly context: " + ", ".join(active_sites_missing_context))

    active_sites_bad_review_status = [
        str(site.get("name", "<unnamed>"))
        for site in sites
        if isinstance(site, dict)
        and site.get("activeUpstream") is True
        and site.get("disassemblyReviewStatus") != "captured-manual-review-required"
    ]
    if active_sites_bad_review_status:
        errors.append(
            "active inline patch site(s) missing captured manual-review disassembly status: "
            + ", ".join(active_sites_bad_review_status)
        )

    active_sites_bad_line_count = [
        str(site.get("name", "<unnamed>"))
        for site in sites
        if isinstance(site, dict)
        and site.get("activeUpstream") is True
        and (not isinstance(site.get("disassemblyLineCount"), int) or site.get("disassemblyLineCount") <= 0)
    ]
    if active_sites_bad_line_count:
        errors.append("active inline patch site(s) missing positive disassembly line count: " + ", ".join(active_sites_bad_line_count))

    default_active_sites = [
        str(site.get("name", "<unnamed>"))
        for site in sites
        if isinstance(site, dict) and site.get("activeDefaultVr") is True
    ]
    if default_active_sites:
        errors.append("inline patch site(s) active in default VR: " + ", ".join(default_active_sites))

    default_enableable_sites = [
        str(site.get("name", "<unnamed>"))
        for site in sites
        if isinstance(site, dict) and site.get("defaultEnablementAllowed") is True
    ]
    if default_enableable_sites:
        errors.append("inline patch site(s) marked default-enableable: " + ", ".join(default_enableable_sites))

    return errors


def address_audit_manifest_errors(manifest: dict[str, object]) -> list[str]:
    errors: list[str] = []
    if manifest.get("schema") != "skyrim_together_vr_address_audit_v1":
        errors.append(f"schema={manifest.get('schema')!r}")

    summary = manifest.get("summary")
    if not isinstance(summary, dict):
        errors.append("summary is not an object")
        summary = {}

    expected_zero_summary_fields = (
        "missingNonRttiIds",
        "missingRttiIdsUsedByCast",
        "helperCsvValidationFailures",
    )
    for field in expected_zero_summary_fields:
        if summary.get(field) != 0:
            errors.append(f"summary.{field}={summary.get(field)!r} expected=0")

    for field in ("aeToSeHelperCsvRows", "addressOverrideHelperCsvRows", "uniqueNonRttiAddressIds"):
        value = summary.get(field)
        if not isinstance(value, int) or value <= 0:
            errors.append(f"summary.{field} must be positive (got {value!r})")

    runtime_helper_csvs = manifest.get("runtimeHelperCsvs")
    if not isinstance(runtime_helper_csvs, dict):
        errors.append("runtimeHelperCsvs is not an object")
    else:
        validation_failures = runtime_helper_csvs.get("validationFailures")
        if validation_failures != []:
            errors.append(f"runtimeHelperCsvs.validationFailures={validation_failures!r} expected=[]")

    return errors


def audit_archive(
    path: pathlib.Path,
    *,
    require_package: bool,
    require_mode: str | None,
    allow_command_failures: bool,
) -> int:
    failures: list[str] = []
    warnings: list[str] = []
    if not path.exists():
        print(f"Build evidence archive does not exist: {path}")
        return 1
    if not zipfile.is_zipfile(path):
        print(f"Build evidence archive is not a zip file: {path}")
        return 1

    with zipfile.ZipFile(path) as zf:
        names = set(zf.namelist())
        for entry in REQUIRED_ARCHIVE_ENTRIES:
            if entry not in names:
                failures.append(f"missing archive entry: {entry}")
        for entry in REQUIRED_SOURCE_EVIDENCE_ENTRIES:
            if entry not in names:
                failures.append(f"missing source evidence archive entry: {entry}")
        for entry, tokens in REQUIRED_SOURCE_TEXT_TOKENS.items():
            if entry not in names:
                failures.append(f"missing source text archive entry: {entry}")
                continue
            text = load_text(zf, entry, failures)
            missing_tokens = [token for token in tokens if token not in text]
            if missing_tokens:
                failures.append(
                    f"{entry} missing required source token(s): " + ", ".join(missing_tokens)
                )

        manifest = load_json(zf, "manifest.json", failures) if "manifest.json" in names else {}
        schema = manifest.get("schema")
        if schema != collect_build_evidence.SCHEMA:
            failures.append(f"unexpected manifest schema: {schema!r}")

        mode = str(manifest.get("mode", "<missing>"))
        if mode not in MODE_RUNTIME_FILES:
            failures.append(f"unexpected build evidence mode: {mode!r}")
        if require_mode and mode != require_mode:
            failures.append(f"build evidence mode is {mode!r}, expected {require_mode!r}")

        package_exists = bool(manifest.get("packageExists"))
        if require_package and not package_exists:
            failures.append("build evidence reports packageExists=false")

        commands = manifest.get("commands", [])
        if not isinstance(commands, list):
            failures.append("manifest commands field is not a list")
            commands = []
        commands_by_name = {
            str(command.get("name", "")): command
            for command in commands
            if isinstance(command, dict) and command.get("name")
        }
        for required_command in ("python-version", "windows-build-audit"):
            if required_command not in commands_by_name:
                failures.append(f"missing command evidence: {required_command}")

        if mode in MODE_RUNTIME_FILES and package_exists:
            audit_name = f"built-package-audit-{mode}"
            if audit_name not in commands_by_name:
                failures.append(f"missing command evidence: {audit_name}")

        failed_commands = [
            command
            for command in commands_by_name.values()
            if command.get("exitCode") not in (0, None)
        ]
        if failed_commands and not allow_command_failures:
            for command in failed_commands:
                failures.append(
                    "command failed {name}: exitCode={exit_code} output={output}".format(
                        name=command.get("name", "<unknown>"),
                        exit_code=command.get("exitCode", "<missing>"),
                        output=command.get("output", "<missing>"),
                    )
                )

        package_files = manifest.get("packageFiles", [])
        if not isinstance(package_files, list):
            failures.append("manifest packageFiles field is not a list")
            package_files = []
        package_file_count = manifest.get("packageFileCount")
        if isinstance(package_file_count, int) and package_file_count != len(package_files):
            failures.append(f"packageFileCount={package_file_count} but listed packageFiles={len(package_files)}")

        file_paths = {
            str(entry.get("path", ""))
            for entry in package_files
            if isinstance(entry, dict) and entry.get("path")
        }
        if mode in MODE_RUNTIME_FILES and package_exists:
            missing_runtime = [relative for relative in MODE_RUNTIME_FILES[mode] if relative not in file_paths]
            if missing_runtime:
                failures.append("package file listing missing runtime file(s): " + ", ".join(missing_runtime))
            if mode != "dll-only":
                required_staged = (
                    "SkyrimTogetherVR_BuildManifest.json",
                    "Data/SkyrimTogether.esp",
                    "Data/SKSE/Plugins/SkyrimTogetherVR_AE_to_SE.csv",
                    "Data/SKSE/Plugins/SkyrimTogetherVR_AddressOverrides.csv",
                    "LaunchSkyrimTogetherVRCompanion.bat",
                )
                missing_staged = [relative for relative in required_staged if relative not in file_paths]
                if missing_staged:
                    failures.append("package file listing missing staged file(s): " + ", ".join(missing_staged))

        copied_text_files = manifest.get("copiedPackageTextFiles", [])
        if package_exists:
            required_copied_text = {
                "package/SkyrimTogetherVR_BuildManifest.json",
                "package/Data/SKSE/Plugins/SkyrimTogetherVR_AE_to_SE.csv",
                "package/Data/SKSE/Plugins/SkyrimTogetherVR_AddressOverrides.csv",
            }
            copied_text_set = {str(entry) for entry in copied_text_files} if isinstance(copied_text_files, list) else set()
            missing_copied_text = sorted(required_copied_text - copied_text_set)
            if missing_copied_text:
                failures.append("missing copied package text file(s): " + ", ".join(missing_copied_text))

        copied_source_files = manifest.get("copiedSourceEvidenceFiles", [])
        copied_source_set = {str(entry) for entry in copied_source_files} if isinstance(copied_source_files, list) else set()
        if not isinstance(copied_source_files, list):
            failures.append("manifest copiedSourceEvidenceFiles field is not a list")
        missing_copied_source = sorted(set(REQUIRED_SOURCE_EVIDENCE_ENTRIES) - copied_source_set)
        if missing_copied_source:
            failures.append("missing copied source evidence file(s): " + ", ".join(missing_copied_source))

        inline_patch_manifest = (
            load_json(zf, "source/Docs/SkyrimVR/inline-patch-manifest.json", failures)
            if "source/Docs/SkyrimVR/inline-patch-manifest.json" in names
            else {}
        )
        if inline_patch_manifest:
            inline_patch_errors = inline_patch_manifest_errors(inline_patch_manifest)
            if inline_patch_errors:
                failures.append("inline patch source manifest validation failed: " + "; ".join(inline_patch_errors))

        address_audit_manifest = (
            load_json(zf, "source/Docs/SkyrimVR/address-audit.json", failures)
            if "source/Docs/SkyrimVR/address-audit.json" in names
            else {}
        )
        if address_audit_manifest:
            address_audit_errors = address_audit_manifest_errors(address_audit_manifest)
            if address_audit_errors:
                failures.append("address audit source manifest validation failed: " + "; ".join(address_audit_errors))

        package_manifest = (
            load_json(zf, "package/SkyrimTogetherVR_BuildManifest.json", failures)
            if "package/SkyrimTogetherVR_BuildManifest.json" in names
            else {}
        )
        if package_exists and mode in MODE_RUNTIME_FILES:
            if not package_manifest:
                failures.append("missing package build manifest copy")
            else:
                manifest_errors = package_manifest_errors(package_manifest, mode=mode)
                if manifest_errors:
                    failures.append("package build manifest validation failed: " + "; ".join(manifest_errors))

        print(f"Build evidence archive: {path}")
        print(f"Archive entries: {len(names)}")
        print(f"Manifest schema: {schema}")
        print(f"Mode: {mode}")
        print(f"Package exists: {package_exists}")
        print(f"Package file count: {len(package_files)}")
        print(f"Command count: {len(commands_by_name)}")
        print(f"Failed commands: {len(failed_commands)}")
        print(f"Copied source evidence files: {len(copied_source_set)}")
        for command in failed_commands:
            print(f"- {command.get('name', '<unknown>')}: exitCode={command.get('exitCode', '<missing>')}")
        print(f"Warnings: {len(warnings)}")
        for warning in warnings:
            print(f"- {warning}")
        print(f"Failures: {len(failures)}")
        for failure in failures:
            print(f"- {failure}")

    return 1 if failures else 0


def command_self_test() -> int:
    root = collect_build_evidence.repo_root()
    with tempfile.TemporaryDirectory(prefix="stvr-build-evidence-audit-") as temp:
        temp_root = pathlib.Path(temp)
        package = temp_root / "package"
        skyrim_vr = temp_root / "SkyrimVR"
        out_dir = temp_root / "out"
        audit_built_package.populate_test_package(package, avatar_sync=True)
        archive = collect_build_evidence.collect_evidence(
            root,
            package,
            skyrim_vr,
            out_dir,
            avatar_sync=True,
        )
        result = audit_archive(
            archive,
            require_package=True,
            require_mode="avatar-sync",
            allow_command_failures=False,
        )
        if result == 0:
            print(f"Build evidence zip audit self-test archive: {archive}")
        return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=pathlib.Path, nargs="?", help="build evidence zip to audit")
    parser.add_argument("--require-package", action="store_true", help="fail if the evidence reports no built package")
    parser.add_argument("--require-default", action="store_true", help="require the default package evidence mode")
    parser.add_argument("--require-avatar-sync", action="store_true", help="require the avatar-sync package evidence mode")
    parser.add_argument("--require-gameplay", action="store_true", help="require the gameplay package evidence mode")
    parser.add_argument("--require-dll-only", action="store_true", help="require the DLL-only package evidence mode")
    parser.add_argument("--allow-command-failures", action="store_true", help="report command failures without failing the audit")
    parser.add_argument("--self-test", action="store_true", help="run a temp-directory build evidence zip audit fixture")
    args = parser.parse_args()

    requested_modes = [
        ("default", args.require_default),
        ("avatar-sync", args.require_avatar_sync),
        ("gameplay", args.require_gameplay),
        ("dll-only", args.require_dll_only),
    ]
    require_mode = next((mode for mode, enabled in requested_modes if enabled), None)
    if sum(1 for _, enabled in requested_modes if enabled) > 1:
        parser.error("only one of --require-default, --require-avatar-sync, --require-gameplay, or --require-dll-only may be used")

    if args.self_test:
        return command_self_test()
    if not args.archive:
        parser.error("archive is required unless --self-test is used")

    return audit_archive(
        args.archive.expanduser().resolve(),
        require_package=args.require_package,
        require_mode=require_mode,
        allow_command_failures=args.allow_command_failures,
    )


if __name__ == "__main__":
    raise SystemExit(main())
