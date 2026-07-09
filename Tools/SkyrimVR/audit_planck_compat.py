#!/usr/bin/env python3
"""Audit PLANCK compatibility guards for the SkyrimTogetherVR port."""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import sys
import zipfile


UPSTREAM_PLANCK_INTERFACE = pathlib.Path("../_refs/activeragdoll/include/planckinterface001.h")
PLANCK_ARCHIVE_ENV_VARS = ("STVR_PLANCK_ARCHIVE", "PLANCK_ARCHIVE")

EXPECTED_PLANCK_METHODS = [
    "GetBuildNumber",
    "Deprecated1",
    "Deprecated2",
    "AddIgnoredActor",
    "RemoveIgnoredActor",
    "AddAggressionIgnoredActor",
    "RemoveAggressionIgnoredActor",
    "SetAggressionLowTopic",
    "SetAggressionHighTopic",
    "AddRagdollCollisionIgnoredActor",
    "RemoveRagdollCollisionIgnoredActor",
    "GetLastHitData",
    "GetCurrentHitEvent",
    "GetSettingDouble",
    "SetSettingDouble",
]

REQUIRED_ARCHIVE_FILES = (
    "Scripts/planck.pex",
    "SKSE/Plugins/activeragdoll.dll",
    "SKSE/Plugins/activeragdoll.ini",
    "Source/Scripts/planck.psc",
)

REQUIRED_TOKENS = {
    "Code/immersive_launcher/stubs/DllBlocklist.cpp": (
        "g_IsPlanckActive",
        "IsPlanck",
        'L"activeragdoll.dll"',
    ),
    "Code/immersive_launcher/stubs/DllBlocklist.h": (
        "IsPlanck",
        "g_IsPlanckActive",
    ),
    "Code/immersive_launcher/stubs/FileMapping.cpp": (
        "stubs::IsPlanck(name)",
        "stubs::g_IsPlanckActive = true",
    ),
    "Code/client/main.cpp": (
        "BuildVRCompatibilityStatus",
        "WriteVRCompatibilityStatusFile",
        "PLANCK detected; keeping SkyrimTogetherVR in PLANCK/HIGGS-compatible hook mode",
        "VRPhysicsCompatibilityModInstalled",
        "HIGGS or PLANCK is installed; refusing to install unvalidated SkyrimTogetherVR gameplay hooks",
    ),
    "Code/client/VRCompatibilityStatus.cpp": (
        "SkyrimTogetherVR.compatibility",
        'L"activeragdoll.dll"',
        "planck.installed",
        "planck.loaded",
        "planckPolicy=observation_only",
        "unvalidatedGameplayHooksSuppressed",
    ),
    "Code/client/TiltedOnlineApp.cpp": (
        "stubs::g_IsPlanckActive",
        "WriteVRCompatibilityStatusFile",
        "PLANCK SKSE plugin is loaded; active-ragdoll compatibility guard is active",
    ),
    "Code/client/Games/PapyrusFunctions.cpp": (
        "SkyrimTogetherVR.compatibility",
        "SkyrimTogetherVR.planck",
        "planck.loaded",
        "planckPolicy",
        "AppendPlanckTelemetry",
    ),
    "Code/planck_bridge/main.cpp": (
        "kPlanckMessageGetInterface",
        "IPlanckInterface001",
        "GetBuildNumber",
        "GetCurrentHitEvent",
        "sizeof(PlanckMessage*)",
        "SkyrimTogetherVR.planck",
        "bridge.sequence",
        "planck.currentHitEventObservationOnly",
        "planck.lastHitDataProbeEnabled",
        "planck.lastHitDataBoundary",
        "not_polled_nontrivial_return_boundary",
        "disabled_unvalidated_by_value_abi",
        "observation_only",
        "SKSEPlugin_Query",
        "SKSEPlugin_Load",
    ),
    "Code/planck_bridge/xmake.lua": (
        'target("SkyrimTogetherVRPlanckBridge")',
        'set_basename("SkyrimTogetherVRPlanckBridge")',
    ),
    "Code/encoding/Structs/VRCombatHitEvent.h": (
        "RawHitFlags",
        "PlanckHit",
    ),
    "Code/client/Services/Generic/VRCombatService.cpp": (
        "kPlanckHitEventMagicNumber",
        "GetRawFlagsData",
        "IsPlanckHit",
        ".planckHit",
    ),
    "Code/xmake.lua": (
        'includes("planck_bridge")',
        "SkyrimTogetherVRPlanckBridge",
    ),
    "Tools/SkyrimVR/vr_handoff.py": (
        "SkyrimTogetherVR.compatibility",
        "SkyrimTogetherVR.planck",
        "build_compatibility_summary",
        "build_planck_summary",
        "planckBridgeSeq",
        "planckLoaded",
        "planckInterface",
        "planckCurrentHitObservationOnly",
        "planckLastHitProbe",
        "planckLastHitBoundary",
        "planckPolicy",
    ),
    "Tools/SkyrimVR/collect_runtime_evidence.py": (
        "vr_physics_compatibility",
        "planck_bridge",
        "bridge.sequence",
        "planck.loaded",
        "planck.interfaceAvailable",
        "planck.currentHitEventObservationOnly",
        "planck.lastHitDataProbeEnabled",
        "planck.lastHitDataBoundary",
        "planckPolicy",
    ),
    "Tools/SkyrimVR/audit_runtime_handoff.py": (
        "SkyrimTogetherVR.planck",
        "PLANCK bridge policy",
        "planck.interfaceAvailable",
        "planck.currentHitEventObservationOnly",
        "planck.lastHitDataProbeEnabled",
        "planck.lastHitDataBoundary",
        "observation_only",
    ),
    "Tools/SkyrimVR/audit_runtime_evidence_zip.py": (
        "planck_bridge",
    ),
    "Tools/SkyrimVR/audit_built_package.py": (
        '"planck"',
        "activeragdoll.dll",
        "SkyrimTogetherVRPlanckBridge.dll",
        "--require-planck",
    ),
    "BuildSkyrimTogetherVR-Windows.ps1": (
        "SkyrimTogetherVRPlanckBridge",
        "SkyrimTogetherVRPlanckBridge.dll",
    ),
    "BuildSkyrimTogetherVR-DLL-Windows.bat": (
        "SkyrimTogetherVRPlanckBridge",
        "SkyrimTogetherVRPlanckBridge.dll",
    ),
    "Tools/SkyrimVR/audit_bringup_hooks.py": (
        "BuildVRCompatibilityStatus",
        "WriteVRCompatibilityStatusFile",
        "g_IsPlanckActive",
        "activeragdoll.dll",
    ),
    "Tools/SkyrimVR/install_vr_prereqs.py": (
        "PLANCK",
        "PLANCK 0.8.0",
        "STVR_PLANCK_SOURCE",
        "--planck-source",
        "SKSE/Plugins/activeragdoll.dll",
        "Scripts/planck.pex",
        "--install",
    ),
    "Docs/SkyrimVR/planck-compatibility.md": (
        "activeragdoll.dll",
        "0x92F38745",
        "IPlanckInterface001",
        "HIGGS",
        "observation-only",
        "SkyrimTogetherVR.planck",
        "planck.currentHitEventObservationOnly",
        "planck.lastHitDataProbeEnabled",
        "planck.lastHitDataBoundary",
        "not_polled_nontrivial_return_boundary",
        "disabled_unvalidated_by_value_abi",
        "SkyrimTogetherVR.compatibility",
        "planck.loaded",
        "--planck-archive",
        "STVR_PLANCK_ARCHIVE",
    ),
    "Docs/SkyrimVR/higgs-compatibility.md": (
        "PLANCK",
        "active-ragdoll",
    ),
    "Docs/SkyrimVR/porting-status.md": (
        "PLANCK",
        "activeragdoll.dll",
    ),
    "Docs/SkyrimVR/windows-build.md": (
        "--require-planck",
        "activeragdoll.dll",
    ),
}

FORBIDDEN_DIRECT_PLANCK_TOKENS = {
    "Code/client": (
        "PlanckPluginAPI",
        "IPlanckInterface001",
        "GetPlanckInterface001",
        "PlanckHitEvent",
        "PlanckHitData",
    ),
    "Code/higgs_bridge": (
        "PlanckPluginAPI",
        "IPlanckInterface001",
        "GetPlanckInterface001",
        "PlanckHitEvent",
        "PlanckHitData",
    ),
    "Code/vrik_bridge": (
        "PlanckPluginAPI",
        "IPlanckInterface001",
        "GetPlanckInterface001",
        "PlanckHitEvent",
        "PlanckHitData",
    ),
}

FORBIDDEN_PLANCK_BRIDGE_CALL_TOKENS = (
    "->AddIgnoredActor(",
    "->RemoveIgnoredActor(",
    "->AddAggressionIgnoredActor(",
    "->RemoveAggressionIgnoredActor(",
    "->SetAggressionLowTopic(",
    "->SetAggressionHighTopic(",
    "->AddRagdollCollisionIgnoredActor(",
    "->RemoveRagdollCollisionIgnoredActor(",
    "->GetLastHitData(",
    "->SetSettingDouble(",
)


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def default_planck_archive() -> pathlib.Path | None:
    for variable in PLANCK_ARCHIVE_ENV_VARS:
        value = os.environ.get(variable)
        if value:
            return pathlib.Path(value).expanduser()
    return None


def extract_interface_block(text: str, interface_name: str) -> str:
    match = re.search(rf"struct\s+{re.escape(interface_name)}\b[^\{{;]*\{{", text)
    if not match:
        return ""

    brace_start = match.end() - 1

    depth = 0
    for index in range(brace_start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace_start + 1 : index]

    return ""


def extract_virtual_methods(text: str, interface_name: str) -> list[str]:
    block = extract_interface_block(text, interface_name)
    methods: list[str] = []
    for line in block.splitlines():
        stripped = line.strip()
        if not stripped.startswith("virtual "):
            continue

        match = re.search(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", stripped)
        if match:
            methods.append(match.group(1))

    return methods


def audit_planck_interface(root: pathlib.Path) -> tuple[list[str], int]:
    failures: list[str] = []
    upstream_path = (root / UPSTREAM_PLANCK_INTERFACE).resolve()
    source_path = (root / "../_refs/activeragdoll/src/planckinterface001.cpp").resolve()

    if not upstream_path.exists():
        return [f"upstream PLANCK reference header is missing: {upstream_path}"], 0

    text = upstream_path.read_text(encoding="utf-8", errors="replace")
    source_text = source_path.read_text(encoding="utf-8", errors="replace") if source_path.exists() else ""
    methods = extract_virtual_methods(text, "IPlanckInterface001")
    if not methods:
        failures.append("could not extract upstream IPlanckInterface001 virtual method list")
    elif methods != EXPECTED_PLANCK_METHODS:
        failures.append("upstream IPlanckInterface001 virtual method order differs from pinned PLANCK 0.8.0 API")
        failures.append(f"  expected: {', '.join(EXPECTED_PLANCK_METHODS)}")
        failures.append(f"  upstream: {', '.join(methods)}")

    for token in ("0x92F38745", '"PLANCK"', "hitEventMagicNumber = 0x59914000", "IsPlanckHit"):
        if token not in text and token not in source_text:
            failures.append(f"upstream PLANCK API is missing token `{token}`")

    return failures, len(methods)


def audit_archive(planck_archive: pathlib.Path | None, require_archive: bool) -> tuple[list[str], list[str], str]:
    failures: list[str] = []
    warnings: list[str] = []
    if planck_archive is None:
        detail = "not configured"
        message = (
            "PLANCK archive path is not configured; pass --planck-archive or set "
            "STVR_PLANCK_ARCHIVE when validating the downloaded PLANCK package contents."
        )
        if require_archive:
            failures.append(message)
        else:
            warnings.append(message)
        return failures, warnings, detail

    planck_archive = planck_archive.expanduser().resolve()
    detail = str(planck_archive)
    if not planck_archive.exists():
        failures.append(f"PLANCK archive is missing: {planck_archive}")
        return failures, warnings, detail

    try:
        with zipfile.ZipFile(planck_archive) as archive:
            entries = set(archive.namelist())
            for relative_path in REQUIRED_ARCHIVE_FILES:
                if relative_path not in entries:
                    failures.append(f"PLANCK archive is missing {relative_path}")
                else:
                    info = archive.getinfo(relative_path)
                    if info.file_size == 0:
                        failures.append(f"PLANCK archive file is empty: {relative_path}")
    except zipfile.BadZipFile:
        failures.append(f"PLANCK archive is not a readable zip: {planck_archive}")

    return failures, warnings, detail


def audit_required_tokens(root: pathlib.Path) -> list[str]:
    failures: list[str] = []
    for relative_path, tokens in REQUIRED_TOKENS.items():
        path = root / relative_path
        text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
        missing = [token for token in tokens if token not in text]
        if missing:
            failures.append(f"{relative_path}: missing PLANCK compatibility tokens: {', '.join(missing)}")

    return failures


def forbidden_token_present(text: str, token: str) -> bool:
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", token):
        return re.search(rf"(?<![A-Za-z0-9_]){re.escape(token)}(?![A-Za-z0-9_])", text) is not None
    return token in text


def audit_forbidden_direct_use(root: pathlib.Path) -> list[str]:
    failures: list[str] = []
    for relative_dir, tokens in FORBIDDEN_DIRECT_PLANCK_TOKENS.items():
        directory = root / relative_dir
        if not directory.exists():
            continue

        for path in directory.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in {".cpp", ".h", ".hpp"}:
                continue

            text = path.read_text(encoding="utf-8", errors="replace")
            present = [token for token in tokens if forbidden_token_present(text, token)]
            if present:
                failures.append(
                    f"{path.relative_to(root)}: direct PLANCK API tokens are present before a validated bridge exists: {', '.join(present)}"
                )

    return failures


def audit_planck_bridge_no_mutation(root: pathlib.Path) -> list[str]:
    failures: list[str] = []
    path = root / "Code/planck_bridge/main.cpp"
    text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
    present = [token for token in FORBIDDEN_PLANCK_BRIDGE_CALL_TOKENS if token in text]
    if present:
        failures.append(
            "Code/planck_bridge/main.cpp: forbidden PLANCK bridge call token(s) present: "
            + ", ".join(present)
        )
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--planck-archive",
        type=pathlib.Path,
        default=default_planck_archive(),
        help="PLANCK 0.8.0 zip to validate. Defaults to STVR_PLANCK_ARCHIVE or PLANCK_ARCHIVE.",
    )
    parser.add_argument(
        "--require-planck-archive",
        action="store_true",
        help="fail if no PLANCK archive path is configured",
    )
    parser.add_argument(
        "--skip-planck-archive",
        action="store_true",
        help="skip validation of the downloaded PLANCK zip while still auditing code/API compatibility",
    )
    args = parser.parse_args()

    root = repo_root()
    failures: list[str] = []
    warnings: list[str] = []

    abi_failures, method_count = audit_planck_interface(root)
    failures.extend(abi_failures)
    if args.skip_planck_archive:
        archive_detail = "skipped by --skip-planck-archive"
    else:
        archive_failures, archive_warnings, archive_detail = audit_archive(
            args.planck_archive,
            args.require_planck_archive,
        )
        failures.extend(archive_failures)
        warnings.extend(archive_warnings)
    failures.extend(audit_required_tokens(root))
    failures.extend(audit_forbidden_direct_use(root))
    failures.extend(audit_planck_bridge_no_mutation(root))

    print(f"Audited IPlanckInterface001 virtual methods: {method_count}")
    print(f"Audited PLANCK archive: {archive_detail}")
    print(f"PLANCK compatibility audit warnings: {len(warnings)}")
    for warning in warnings:
        print(f"- {warning}")
    print(f"PLANCK compatibility audit failures: {len(failures)}")
    for failure in failures:
        print(f"- {failure}")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
