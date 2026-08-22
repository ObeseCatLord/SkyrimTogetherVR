#!/usr/bin/env python3
"""Audit the optional negotiated PLANCK interface002 relay contract."""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import sys
import zipfile


PLANCK_INTERFACE001 = pathlib.Path("Libraries/activeragdoll/include/planckinterface001.h")
PLANCK_INTERFACE002 = pathlib.Path("Libraries/activeragdoll/include/planckinterface002.h")
PLANCK_ARCHIVE_ENV_VARS = ("STVR_PLANCK_ARCHIVE", "PLANCK_ARCHIVE")

EXPECTED_INTERFACE001_METHODS = (
    "GetBuildNumber", "Deprecated1", "Deprecated2", "AddIgnoredActor", "RemoveIgnoredActor",
    "AddAggressionIgnoredActor", "RemoveAggressionIgnoredActor", "SetAggressionLowTopic",
    "SetAggressionHighTopic", "AddRagdollCollisionIgnoredActor", "RemoveRagdollCollisionIgnoredActor",
    "GetLastHitData", "GetCurrentHitEvent", "GetSettingDouble", "SetSettingDouble",
)
EXPECTED_INTERFACE002_METHODS = (
    "GetCapabilities", "SubmitRemoteHitImpulse", "SubmitRemoteRagdoll", "SubmitRemoteRagdollExit",
    "BeginRemoteGrip", "UpdateRemoteGrip", "EndRemoteGrip", "ClearRemoteSession",
    "DequeueLocalPhysicalEvent", "DiscardLocalPhysicalEvents",
)
REQUIRED_ARCHIVE_FILES = (
    "Scripts/planck.pex", "SKSE/Plugins/activeragdoll.dll", "SKSE/Plugins/activeragdoll.ini",
    "Source/Scripts/planck.psc",
)

REQUIRED_TOKENS = {
    "Code/client/VRCompatibilityStatus.cpp": (
        'L"activeragdoll.dll"', "planck.installed", "planck.loaded",
        "planckPolicy=optional_negotiated_interface002_not_core_readiness",
    ),
    "Code/planck_bridge/main.cpp": (
        "kPlanckMessageGetInterface", "sizeof(PlanckMessage*)", "kPlanckInterfaceRevision = 2",
        "IPlanckInterface002", "GetCapabilities",
        "SkyrimTogetherVR_Planck002_GetCapabilities", "SkyrimTogetherVR_Planck002_DequeueLocalEvent",
        "SkyrimTogetherVR_Planck002_SubmitRemoteEvent", "SkyrimTogetherVR_Planck002_ClearRemoteSession",
        "planck.interfaceRevision", "planck.features", "planck.interface002RequiredFeatures",
        "planck.damageAuthority", "planck.remotePhysicsReplay", "kRequiredFeatures",
    ),
    "Code/vr_common/VRPlanckPhysicsBridge.h": (
        "kRequiredFeatures", "kGetCapabilitiesExport", "kDequeueLocalEventExport",
        "kSubmitRemoteEventExport", "kClearRemoteSessionExport",
    ),
    "Code/encoding/Structs/GameplayCapabilities.h": (
        "kGameplayProtocolRevision = 21", "PlanckPhysicsInterface002",
        "part of the base gameplay profile",
    ),
    "Code/encoding/Opcodes.h": ("kRequestVRPlanckPhysicsEvent", "kNotifyVRPlanckPhysicsEvent"),
    "Code/encoding/Messages/RequestVRPlanckPhysicsEvent.h": ("RequestVRPlanckPhysicsEvent", "VRPlanckPhysicsEvent"),
    "Code/encoding/Messages/NotifyVRPlanckPhysicsEvent.h": ("NotifyVRPlanckPhysicsEvent", "VRPlanckPhysicsEvent"),
    "Code/encoding/Messages/ClientMessageFactory.h": ("RequestVRPlanckPhysicsEvent",),
    "Code/encoding/Messages/ServerMessageFactory.h": ("NotifyVRPlanckPhysicsEvent",),
    "Code/client/Services/Generic/VRActorReplicationService.cpp": (
        "VRPlanckPhysicsBridge.h", "PlanckPhysicsInterface002", "DrainLocalPlanckPhysics",
        "OnVrPlanckPhysics", "RequestVRPlanckPhysicsEvent", "NotifyVRPlanckPhysicsEvent",
    ),
    "Code/client/Services/Generic/TransportService.cpp": (
        "PreparePlanckInterface002PhysicsBridge", "kMaximumPreSessionPlanckEvents",
        "pre-session PLANCK physics events", "PlanckPhysicsInterface002",
    ),
    "Code/server/Services/VRPlanckPhysicsRelayService.cpp": (
        "VRPlanckPhysicsRelayService", "RequestVRPlanckPhysicsEvent", "NotifyVRPlanckPhysicsEvent",
        "PlanckPhysicsInterface002",
    ),
    "Code/server/World.cpp": ("VRPlanckPhysicsRelayService", "ctx().emplace<VRPlanckPhysicsRelayService>"),
    "Code/client/Games/PapyrusFunctions.cpp": (
        "AppendPlanckSummary", "AppendPlanckTelemetry", "planck.interfaceRevision",
        "planck.features", "planck.interface002RequiredFeatures", "planck.damageAuthority",
        "planck.remotePhysicsReplay",
    ),
    "Tools/SkyrimVR/collect_runtime_evidence.py": (
        "optional_negotiated_interface002_not_core_readiness", "planck.interfaceRevision",
        "planck.features", "planck.interface002RequiredFeatures", "planck.damageAuthority",
        "planck.remotePhysicsReplay",
    ),
    "Tools/SkyrimVR/audit_runtime_handoff.py": (
        "optional_negotiated_interface002_not_core_readiness", "planck.interfaceRevision",
        "planck.features", "planck.interface002RequiredFeatures", "planck.damageAuthority",
        "planck.remotePhysicsReplay",
    ),
    "Tools/SkyrimVR/audit_runtime_evidence_zip.py": (
        "planck_bridge", "optional_negotiated_interface002_not_core_readiness",
        "planck.interfaceRevision", "planck.remotePhysicsReplay",
    ),
}

STALE_PLANCK_CLAIMS = {
    "Code/client/VRCompatibilityStatus.cpp": (
        "unsupported_no_remote_physical_replay",
    ),
    "Code/planck_bridge/main.cpp": (
        "IPlanckInterface001", "planck.currentHitEventObservationOnly",
        "planck.lastHitDataProbeEnabled", "planck.policy=observation_only",
    ),
    "Code/client/Games/PapyrusFunctions.cpp": (
        "planck.buildNumber", "planck.currentHitEvent", "planck.lastHitData", "planck.policy",
    ),
    "Tools/SkyrimVR/collect_runtime_evidence.py": (
        "planck.currentHitEventObservationOnly", "planck.lastHitDataProbeEnabled",
        'planck.get("planck.policy") == "observation_only"',
    ),
    "Tools/SkyrimVR/audit_runtime_handoff.py": (
        "planck.currentHitEventObservationOnly", "planck.lastHitDataProbeEnabled",
        'planck.get("planck.policy") == "observation_only"',
    ),
    "Tools/SkyrimVR/audit_runtime_evidence_zip.py": (
        "planck.currentHitEventObservationOnly", "planck.lastHitDataProbeEnabled",
        "planck.policy=observation_only",
    ),
}


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def default_planck_archive() -> pathlib.Path | None:
    for variable in PLANCK_ARCHIVE_ENV_VARS:
        if value := os.environ.get(variable):
            return pathlib.Path(value).expanduser()
    return None


def extract_interface_block(text: str, interface_name: str) -> str:
    match = re.search(rf"struct\s+{re.escape(interface_name)}\b[^{{;]*{{", text)
    if not match:
        return ""
    depth = 0
    for index in range(match.end() - 1, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[match.end() : index]
    return ""


def extract_virtual_methods(text: str, interface_name: str) -> list[str]:
    return re.findall(
        r"^\s*virtual\s+.*?\b([A-Za-z_][A-Za-z0-9_]*)\s*\(",
        extract_interface_block(text, interface_name),
        flags=re.MULTILINE,
    )


def audit_interfaces(root: pathlib.Path) -> tuple[list[str], dict[str, int]]:
    failures: list[str] = []
    counts: dict[str, int] = {}
    for path, name, expected, required_tokens in (
        (PLANCK_INTERFACE001, "IPlanckInterface001", EXPECTED_INTERFACE001_METHODS,
         ("GetPlanckInterface001", "hitEventMagicNumber")),
        (PLANCK_INTERFACE002, "IPlanckInterface002", EXPECTED_INTERFACE002_METHODS,
         ("kPlanckInterface002Revision = 2", "GetPlanckInterface002", "kPlanckFeature002_LocalPhysicalEvents")),
    ):
        header = root / path
        if not header.exists():
            failures.append(f"PLANCK submodule header is missing: {path}")
            continue
        text = header.read_text(encoding="utf-8", errors="replace")
        methods = extract_virtual_methods(text, name)
        counts[name] = len(methods)
        if methods != list(expected):
            failures.append(f"{path}: {name} virtual method order differs from the pinned contract")
        for token in required_tokens:
            if token not in text:
                failures.append(f"{path}: missing `{token}`")
    return failures, counts


def audit_archive(planck_archive: pathlib.Path | None, require_archive: bool) -> tuple[list[str], list[str], str]:
    failures: list[str] = []
    warnings: list[str] = []
    if planck_archive is None:
        message = "PLANCK archive not configured; pass --planck-archive or set STVR_PLANCK_ARCHIVE to validate package contents"
        (failures if require_archive else warnings).append(message)
        return failures, warnings, "not configured"
    archive_path = planck_archive.expanduser().resolve()
    if not archive_path.exists():
        return [f"PLANCK archive is missing: {archive_path}"], warnings, str(archive_path)
    try:
        with zipfile.ZipFile(archive_path) as archive:
            entries = set(archive.namelist())
            for relative_path in REQUIRED_ARCHIVE_FILES:
                if relative_path not in entries:
                    failures.append(f"PLANCK archive is missing {relative_path}")
                elif archive.getinfo(relative_path).file_size == 0:
                    failures.append(f"PLANCK archive file is empty: {relative_path}")
    except zipfile.BadZipFile:
        failures.append(f"PLANCK archive is not a readable zip: {archive_path}")
    return failures, warnings, str(archive_path)


def audit_source_contract(root: pathlib.Path) -> list[str]:
    failures: list[str] = []
    for relative_path, tokens in REQUIRED_TOKENS.items():
        path = root / relative_path
        text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
        missing = [token for token in tokens if token not in text]
        if missing:
            failures.append(f"{relative_path}: missing interface002 relay tokens: {', '.join(missing)}")
    for relative_path, tokens in STALE_PLANCK_CLAIMS.items():
        path = root / relative_path
        text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
        stale = [token for token in tokens if token in text]
        if stale:
            failures.append(f"{relative_path}: stale interface001/observation-only PLANCK claim(s): {', '.join(stale)}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--planck-archive", type=pathlib.Path, default=default_planck_archive())
    parser.add_argument("--require-planck-archive", action="store_true")
    parser.add_argument("--skip-planck-archive", action="store_true")
    args = parser.parse_args()

    root = repo_root()
    failures, counts = audit_interfaces(root)
    warnings: list[str] = []
    archive_detail = "skipped by --skip-planck-archive"
    if not args.skip_planck_archive:
        archive_failures, archive_warnings, archive_detail = audit_archive(args.planck_archive, args.require_planck_archive)
        failures.extend(archive_failures)
        warnings.extend(archive_warnings)
    failures.extend(audit_source_contract(root))

    print("Audited PLANCK submodule interfaces: " + ", ".join(f"{name}={count}" for name, count in counts.items()))
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
