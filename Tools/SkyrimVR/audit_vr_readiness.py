#!/usr/bin/env python3
"""Run the SkyrimTogetherVR static and runtime-prerequisite readiness gates."""

from __future__ import annotations

import argparse
import copy
import json
import pathlib
import subprocess
import sys
import tempfile
from dataclasses import dataclass


DEFAULT_PACKAGE = pathlib.Path("artifacts/SkyrimTogetherVR/releasedbg")

SOURCE_AUDITS: tuple[tuple[str, tuple[str, ...]], ...] = (
    ("VR-only target graph", ("Tools/SkyrimVR/audit_vr_only.py",)),
    ("Windows/Wine build script", ("Tools/SkyrimVR/audit_windows_build.py",)),
    ("Address Library coverage", ("Tools/SkyrimVR/audit_addresses.py",)),
    ("Inline patch guard policy", ("Tools/SkyrimVR/audit_inline_patches.py",)),
    ("CommonLib-informed layouts", ("Tools/SkyrimVR/audit_commonlib_layout.py",)),
    ("ESP/Papyrus/behavior files", ("Tools/SkyrimVR/audit_gamefiles.py",)),
    ("Smoke package source manifest", ("Tools/SkyrimVR/audit_smoke_package.py", "--require-installed-prerequisites")),
    ("Built package audit self-test", ("Tools/SkyrimVR/audit_built_package.py", "--self-test")),
    ("Crash diagnostics", ("Tools/SkyrimVR/audit_crash_diagnostics.py",)),
    ("Built package installer self-test", ("Tools/SkyrimVR/install_built_package.py", "--self-test")),
    ("Bring-up hook guard", ("Tools/SkyrimVR/audit_bringup_hooks.py",)),
    ("SKSEVR task tick bridge", ("Tools/SkyrimVR/audit_tick_bridge.py",)),
    ("VRIK IK lane", ("Tools/SkyrimVR/audit_vrik_ik.py",)),
    ("HIGGS bridge", ("Tools/SkyrimVR/audit_higgs_bridge.py",)),
    ("SkyrimVR-FBT compatibility", ("Tools/SkyrimVR/audit_fbt_compat.py",)),
    ("HIGGS relay", ("Tools/SkyrimVR/audit_vr_higgs.py",)),
    ("PLANCK compatibility", ("Tools/SkyrimVR/audit_planck_compat.py",)),
    ("FUS native DLL compatibility", ("Tools/SkyrimVR/audit_fus_dll_compat.py",)),
    ("VR gameplay observation services", ("Tools/SkyrimVR/audit_vr_services.py",)),
    ("VR grab relay", ("Tools/SkyrimVR/audit_vr_grab.py",)),
    ("Remote-player proxy schema", ("Tools/SkyrimVR/audit_remote_player_proxy.py",)),
    ("VR overlay/input boundary", ("Tools/SkyrimVR/audit_vr_overlay_boundary.py",)),
    ("VR handoff/companion", ("Tools/SkyrimVR/audit_vr_handoff.py",)),
    ("Runtime evidence collector self-test", ("Tools/SkyrimVR/collect_runtime_evidence.py", "--self-test")),
    ("Runtime evidence zip audit self-test", ("Tools/SkyrimVR/audit_runtime_evidence_zip.py", "--self-test")),
    ("Build evidence collector self-test", ("Tools/SkyrimVR/collect_build_evidence.py", "--self-test")),
    ("Build evidence zip audit self-test", ("Tools/SkyrimVR/audit_build_evidence_zip.py", "--self-test")),
    ("Final handoff audit self-test", ("Tools/SkyrimVR/audit_final_handoff.py", "--self-test")),
)

RUNTIME_AUDITS: tuple[tuple[str, tuple[str, ...]], ...] = (
    ("Installed VRIK/HIGGS/PLANCK prerequisites", ("Tools/SkyrimVR/install_vr_prereqs.py", "--require", "--enable-plugins")),
)


@dataclass(frozen=True)
class CommittedReportSpec:
    path: pathlib.Path
    generator: str
    comparator: str
    required_inputs: tuple[pathlib.Path, ...] = ()


ADDRESS_AUDIT_INPUTS = (
    pathlib.Path("../_refs/skyrim_vr_address_library/database.csv"),
    pathlib.Path("../_refs/VRAddressLibraryRelease/SKSE/Plugins/version-1-4-15-0.csv"),
)

# Only reports with a deterministic producer and CI-available inputs are authoritative here.
COMMITTED_REPORT_MANIFEST: tuple[CommittedReportSpec, ...] = (
    CommittedReportSpec(
        pathlib.Path("Docs/SkyrimVR/address-audit.md"),
        "address-audit",
        "bytes",
        ADDRESS_AUDIT_INPUTS,
    ),
    CommittedReportSpec(
        pathlib.Path("Docs/SkyrimVR/address-audit.json"),
        "address-audit",
        "address-json",
        ADDRESS_AUDIT_INPUTS,
    ),
    CommittedReportSpec(
        pathlib.Path("Docs/SkyrimVR/vr-services-audit.md"),
        "vr-services-audit",
        "bytes",
    ),
)

# These committed files are intentionally not treated as generated reports:
# they are prose, or require a local game/package/executable unavailable in CI.
NON_AUTHORITATIVE_REPORT_LIMITATIONS: tuple[tuple[pathlib.Path, str], ...] = (
    (
        pathlib.Path("Docs/SkyrimVR/commonlib-layout-audit.md"),
        "prose/analysis document with no deterministic report producer",
    ),
    (
        pathlib.Path("Docs/SkyrimVR/gamefiles-audit.md"),
        "depends on an installed Skyrim VR root and environment-specific paths",
    ),
    (
        pathlib.Path("Docs/SkyrimVR/inline-patch-audit.md"),
        "depends on executable byte/disassembly analysis unavailable in CI",
    ),
    (
        pathlib.Path("Docs/SkyrimVR/inline-patch-manifest.json"),
        "depends on executable byte/disassembly analysis unavailable in CI",
    ),
)


def build_source_readiness_commands(report_dir: pathlib.Path) -> tuple[tuple[str, tuple[str, ...]], ...]:
    """Build the complete repository-only readiness suite with isolated reports."""
    return (
        ("VR-only target graph", ("Tools/SkyrimVR/audit_vr_only.py",)),
        ("Windows/Wine build script", ("Tools/SkyrimVR/audit_windows_build.py",)),
        (
            "Address Library coverage",
            (
                "Tools/SkyrimVR/audit_addresses.py",
                "--report",
                str(report_dir / "address-audit.md"),
                "--json-report",
                str(report_dir / "address-audit.json"),
                "--runtime-csv-dir",
                str(report_dir / "address-runtime-csv"),
                "--refs",
                str(ADDRESS_AUDIT_INPUTS[0].parent),
                "--release",
                str(ADDRESS_AUDIT_INPUTS[1]),
            ),
        ),
        ("Inline patch guard policy", ("Tools/SkyrimVR/audit_inline_patches.py", "--source-only")),
        ("CommonLib-informed layouts", ("Tools/SkyrimVR/audit_commonlib_layout.py",)),
        (
            "ESP/Papyrus/behavior files",
            (
                "Tools/SkyrimVR/audit_gamefiles.py",
                "--skyrim-vr",
                str(report_dir / "no-installed-skyrim-vr"),
                "--report",
                str(report_dir / "gamefiles-audit.md"),
            ),
        ),
        ("Smoke package source manifest", ("Tools/SkyrimVR/audit_smoke_package.py",)),
        ("Built package audit self-test", ("Tools/SkyrimVR/audit_built_package.py", "--self-test")),
        ("Crash diagnostics", ("Tools/SkyrimVR/audit_crash_diagnostics.py",)),
        ("Built package installer self-test", ("Tools/SkyrimVR/install_built_package.py", "--self-test")),
        ("Bring-up hook guard", ("Tools/SkyrimVR/audit_bringup_hooks.py",)),
        ("SKSEVR task tick bridge", ("Tools/SkyrimVR/audit_tick_bridge.py",)),
        ("VRIK IK lane", ("Tools/SkyrimVR/audit_vrik_ik.py",)),
        ("HIGGS bridge", ("Tools/SkyrimVR/audit_higgs_bridge.py",)),
        ("SkyrimVR-FBT compatibility", ("Tools/SkyrimVR/audit_fbt_compat.py",)),
        ("HIGGS relay", ("Tools/SkyrimVR/audit_vr_higgs.py",)),
        ("PLANCK compatibility", ("Tools/SkyrimVR/audit_planck_compat.py", "--skip-planck-archive")),
        (
            "FUS native DLL compatibility",
            (
                "Tools/SkyrimVR/audit_fus_dll_compat.py",
                "--fus",
                str(report_dir / "empty-fus"),
                "--all-mods",
                "--skip-installed-root",
            ),
        ),
        (
            "VR gameplay observation services",
            ("Tools/SkyrimVR/audit_vr_services.py", "--report", str(report_dir / "vr-services-audit.md")),
        ),
        ("VR grab relay", ("Tools/SkyrimVR/audit_vr_grab.py",)),
        ("Remote-player proxy schema", ("Tools/SkyrimVR/audit_remote_player_proxy.py",)),
        ("VR overlay/input boundary", ("Tools/SkyrimVR/audit_vr_overlay_boundary.py",)),
        ("VR handoff/companion", ("Tools/SkyrimVR/audit_vr_handoff.py",)),
        ("Runtime evidence collector self-test", ("Tools/SkyrimVR/collect_runtime_evidence.py", "--self-test")),
        ("Runtime evidence zip audit self-test", ("Tools/SkyrimVR/audit_runtime_evidence_zip.py", "--self-test")),
        ("Build evidence collector self-test", ("Tools/SkyrimVR/collect_build_evidence.py", "--self-test")),
        ("Build evidence zip audit self-test", ("Tools/SkyrimVR/audit_build_evidence_zip.py", "--self-test")),
        ("Final handoff audit self-test", ("Tools/SkyrimVR/audit_final_handoff.py", "--self-test")),
    )


def build_committed_report_commands(report_dir: pathlib.Path) -> dict[str, tuple[str, ...]]:
    return {
        "address-audit": (
            "Tools/SkyrimVR/audit_addresses.py",
            "--report",
            str(report_dir / "address-audit.md"),
            "--json-report",
            str(report_dir / "address-audit.json"),
            "--runtime-csv-dir",
            str(report_dir / "address-runtime-csv"),
            "--refs",
            str(ADDRESS_AUDIT_INPUTS[0].parent),
            "--release",
            str(ADDRESS_AUDIT_INPUTS[1]),
        ),
        "vr-services-audit": (
            "Tools/SkyrimVR/audit_vr_services.py",
            "--report",
            str(report_dir / "vr-services-audit.md"),
        ),
    }


def _normalize_address_report(value: object) -> object:
    """Ignore only absolute input locations; preserve all audit findings."""
    normalized = copy.deepcopy(value)
    if not isinstance(normalized, dict):
        return normalized
    inputs = normalized.get("inputs")
    if isinstance(inputs, dict):
        for key in ("refsRoot", "releaseCsv", "runtimeCsvDir"):
            if key in inputs:
                inputs[key] = "<environment-dependent-path>"
    return normalized


def compare_report_files(
    committed: pathlib.Path,
    generated: pathlib.Path,
    comparator: str,
) -> tuple[bool, str]:
    if not committed.is_file():
        return False, f"committed report is missing: {committed}"
    if not generated.is_file():
        return False, f"generated report is missing: {generated}"

    if comparator == "bytes":
        if committed.read_bytes() == generated.read_bytes():
            return True, "byte-identical"
        return False, "report bytes differ"

    if comparator == "address-json":
        try:
            committed_value = json.loads(committed.read_text(encoding="utf-8"))
            generated_value = json.loads(generated.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as error:
            return False, f"invalid JSON report: {error}"
        if _normalize_address_report(committed_value) == _normalize_address_report(generated_value):
            return True, "semantic JSON match (environment paths normalized)"
        return False, "semantic JSON report content differs"

    return False, f"unknown report comparator: {comparator}"


def committed_report_consistency_self_test() -> list[str]:
    failures: list[str] = []
    with tempfile.TemporaryDirectory(prefix="stvr-report-consistency-self-test-") as temp:
        root = pathlib.Path(temp)
        committed_text = root / "committed.md"
        generated_text = root / "generated.md"
        committed_text.write_bytes(b"stable report\n")
        generated_text.write_bytes(b"stable report\n")
        equal, _ = compare_report_files(committed_text, generated_text, "bytes")
        if not equal:
            failures.append("byte comparator rejected identical reports")
        generated_text.write_bytes(b"drifted report\n")
        equal, _ = compare_report_files(committed_text, generated_text, "bytes")
        if equal:
            failures.append("byte comparator missed report drift")

        committed_json = root / "committed.json"
        generated_json = root / "generated.json"
        committed_json.write_text(
            json.dumps(
                {
                    "inputs": {
                        "refsRoot": "/checkout-a/_refs/skyrim_vr_address_library",
                        "releaseCsv": "/checkout-a/_refs/version-1-4-15-0.csv",
                        "runtimeCsvDir": "/checkout-a/GameFiles/Plugins",
                    },
                    "summary": {"missingNonRttiIds": 0},
                }
            ),
            encoding="utf-8",
        )
        generated_json.write_text(
            json.dumps(
                {
                    "inputs": {
                        "refsRoot": "/runner/_refs/skyrim_vr_address_library",
                        "releaseCsv": "/runner/_refs/version-1-4-15-0.csv",
                        "runtimeCsvDir": "/tmp/reports/plugins",
                    },
                    "summary": {"missingNonRttiIds": 0},
                }
            ),
            encoding="utf-8",
        )
        equal, _ = compare_report_files(committed_json, generated_json, "address-json")
        if not equal:
            failures.append("JSON comparator rejected equivalent environment paths")
        generated_payload = json.loads(generated_json.read_text(encoding="utf-8"))
        generated_payload["summary"]["missingNonRttiIds"] = 1
        generated_json.write_text(json.dumps(generated_payload), encoding="utf-8")
        equal, _ = compare_report_files(committed_json, generated_json, "address-json")
        if equal:
            failures.append("JSON comparator missed semantic report drift")
    return failures


def source_readiness_commands_self_test() -> list[str]:
    with tempfile.TemporaryDirectory(prefix="stvr-source-readiness-args-") as temp:
        report_dir = pathlib.Path(temp)
        commands = build_source_readiness_commands(report_dir)

    failures: list[str] = []
    names = [name for name, _ in commands]
    if len(names) != len(set(names)):
        failures.append("source readiness command names must be unique")

    expected_names = {name for name, _ in SOURCE_AUDITS}
    missing_names = sorted(expected_names - set(names))
    unexpected_names = sorted(set(names) - expected_names)
    if missing_names:
        failures.append("source readiness is missing source audit(s): " + ", ".join(missing_names))
    if unexpected_names:
        failures.append("source readiness has untracked audit(s): " + ", ".join(unexpected_names))

    by_name = {name: command for name, command in commands}
    expected_commands = {
        "Address Library coverage": ("--report", "--json-report", "--runtime-csv-dir"),
        "Inline patch guard policy": ("--source-only",),
        "ESP/Papyrus/behavior files": ("--skyrim-vr", "--report"),
        "PLANCK compatibility": ("--skip-planck-archive",),
        "FUS native DLL compatibility": ("--fus", "--all-mods", "--skip-installed-root"),
        "VR gameplay observation services": ("--report",),
    }
    for name, expected_args in expected_commands.items():
        command = by_name.get(name)
        if command is None:
            failures.append(f"missing source readiness command: {name}")
            continue
        for expected_arg in expected_args:
            if expected_arg not in command:
                failures.append(f"{name} is missing {expected_arg}")

    forbidden_args = {
        "--require-installed-prerequisites",
        "--require-installed",
        "--require-built-package",
        "--require-planck-archive",
    }
    for name, command in commands:
        present = sorted(forbidden_args & set(command))
        if present:
            failures.append(f"{name} is not source-only: {', '.join(present)}")
        if any("Docs/SkyrimVR/" in argument for argument in command):
            failures.append(f"{name} writes a tracked documentation report")
    return failures


def repo_root() -> pathlib.Path:
    cwd = pathlib.Path.cwd()
    if (cwd / "xmake.lua").exists() and (cwd / "Tools" / "SkyrimVR" / "audit_vr_readiness.py").exists():
        return cwd
    return pathlib.Path(__file__).absolute().parents[2]


def run_python(root: pathlib.Path, args: tuple[str, ...]) -> subprocess.CompletedProcess[str]:
    command = [sys.executable, *args]
    return subprocess.run(
        command,
        cwd=root,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def summarize_output(output: str, max_lines: int) -> str:
    lines = [line.rstrip() for line in output.splitlines() if line.strip()]
    if len(lines) <= max_lines:
        return "\n".join(lines)
    head = lines[: max_lines // 2]
    tail = lines[-(max_lines - len(head)) :]
    return "\n".join([*head, f"... truncated {len(lines) - len(head) - len(tail)} line(s) ...", *tail])


def run_gate(root: pathlib.Path, name: str, args: tuple[str, ...], verbose: bool) -> tuple[bool, str]:
    result = run_python(root, args)
    status = "PASS" if result.returncode == 0 else "FAIL"
    command = " ".join((sys.executable, *args))
    print(f"[{status}] {name}")
    print(f"  {command}")
    if verbose or result.returncode != 0:
        summary = summarize_output(result.stdout, 40)
        if summary:
            for line in summary.splitlines():
                print(f"    {line}")
    return result.returncode == 0, result.stdout


def command_for_environment(command: tuple[str, ...], skyrim_vr: pathlib.Path | None) -> tuple[str, ...]:
    if skyrim_vr is None:
        return command

    if not command:
        return command

    script = command[0]
    if script in {
        "Tools/SkyrimVR/audit_gamefiles.py",
        "Tools/SkyrimVR/audit_smoke_package.py",
        "Tools/SkyrimVR/install_vr_prereqs.py",
        "Tools/SkyrimVR/audit_built_package.py",
    }:
        return (*command, "--skyrim-vr", str(skyrim_vr))

    if script == "Tools/SkyrimVR/audit_inline_patches.py":
        return (*command, "--exe", str(skyrim_vr / "SkyrimVR.exe"))

    return command


def xmake_targets_visible(root: pathlib.Path) -> tuple[bool, str]:
    expected = {
        "SkyrimTogetherVRClient",
        "SkyrimTogetherVRVrikBridge",
        "SkyrimTogetherVRHiggsBridge",
        "SkyrimTogetherVRPlanckBridge",
        "SkyrimTogetherVRTickBridge",
        "SkyrimVRImmersiveLauncher",
        "SkyrimTogetherVRClientAvatarSync",
        "SkyrimVRImmersiveLauncherAvatarSync",
        "SkyrimTogetherVRGameplayClient",
        "SkyrimVRImmersiveLauncherGameplay",
        "ImmersiveElf",
        "TPProcess",
    }
    result = subprocess.run(
        ["xmake", "show", "-l", "targets"],
        cwd=root,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if result.returncode != 0:
        return False, result.stdout
    missing = sorted(target for target in expected if target not in result.stdout)
    if missing:
        return False, "missing target(s): " + ", ".join(missing)
    return True, "all expected VR targets are visible"


def run_committed_report_consistency(root: pathlib.Path, verbose: bool) -> int:
    failures: list[str] = []
    print("Committed-report consistency checks only explicitly authoritative generated reports.")
    for path, limitation in NON_AUTHORITATIVE_REPORT_LIMITATIONS:
        print(f"[INFO] Not authoritative: {path} ({limitation})")

    with tempfile.TemporaryDirectory(prefix="stvr-committed-report-check-") as temp:
        report_dir = pathlib.Path(temp)
        commands = build_committed_report_commands(report_dir)
        specs_by_generator: dict[str, list[CommittedReportSpec]] = {}
        for spec in COMMITTED_REPORT_MANIFEST:
            specs_by_generator.setdefault(spec.generator, []).append(spec)

        for generator, specs in specs_by_generator.items():
            missing_inputs = sorted(
                {
                    str(root / required_input)
                    for spec in specs
                    for required_input in spec.required_inputs
                    if not (root / required_input).is_file()
                }
            )
            if missing_inputs:
                print(f"[SKIP] {generator} committed reports")
                print("  deterministic CI input(s) unavailable: " + ", ".join(missing_inputs))
                print("  no consistency claim is made for this report group")
                continue

            command = commands[generator]
            ok, _ = run_gate(root, f"Generate {generator} report(s)", command, verbose)
            if not ok:
                failures.append(generator)
                continue

            for spec in specs:
                committed = root / spec.path
                generated = report_dir / spec.path.name
                equal, detail = compare_report_files(committed, generated, spec.comparator)
                status = "PASS" if equal else "FAIL"
                print(f"[{status}] {spec.path}")
                print(f"  {detail}")
                if not equal:
                    failures.append(str(spec.path))

    print(f"Committed-report consistency failures: {len(failures)}")
    for failure in failures:
        print(f"- {failure}")
    return 1 if failures else 0


def run_source_readiness(root: pathlib.Path, verbose: bool, check_committed_reports: bool) -> int:
    failures: list[str] = []
    print(f"Repository root: {root}")
    print("Source readiness runs only checkout-backed audits and temporary self-test fixtures.")

    target_ok, target_detail = xmake_targets_visible(root)
    print(f"[{'PASS' if target_ok else 'FAIL'}] VR xmake targets")
    print(f"  {target_detail}")
    if not target_ok:
        failures.append("VR xmake targets are not all visible")

    with tempfile.TemporaryDirectory(prefix="stvr-source-readiness-reports-") as temp:
        report_dir = pathlib.Path(temp)
        (report_dir / "empty-fus" / "mods").mkdir(parents=True)
        for name, command in build_source_readiness_commands(report_dir):
            ok, _ = run_gate(root, name, command, verbose)
            if not ok:
                failures.append(name)

    if check_committed_reports:
        report_status = run_committed_report_consistency(root, verbose)
        if report_status:
            failures.append("Committed-report consistency")
    else:
        print("Committed-report consistency: not requested")
    print(f"Source readiness failures: {len(failures)}")
    for failure in failures:
        print(f"- {failure}")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package", type=pathlib.Path, default=DEFAULT_PACKAGE)
    parser.add_argument(
        "--require-built-package",
        action="store_true",
        help="fail if the Windows-built artifacts package is absent or invalid",
    )
    parser.add_argument(
        "--avatar-sync",
        action="store_true",
        help="audit the explicit remote-avatar validation package executable",
    )
    parser.add_argument(
        "--gameplay",
        action="store_true",
        help="audit the full gameplay package executable",
    )
    parser.add_argument(
        "--skyrim-vr",
        type=pathlib.Path,
        help="target Skyrim VR root for installed-prerequisite and built-package audits",
    )
    parser.add_argument(
        "--skip-fus",
        action="store_true",
        help="skip the local FUS native DLL compatibility audit when the local FUS modlist is unavailable",
    )
    parser.add_argument(
        "--planck-archive",
        type=pathlib.Path,
        help="PLANCK 0.8.0 zip path to pass through to audit_planck_compat.py",
    )
    parser.add_argument(
        "--require-planck-archive",
        action="store_true",
        help="fail the PLANCK source audit if no PLANCK archive is configured",
    )
    parser.add_argument(
        "--skip-planck-archive",
        action="store_true",
        help="skip PLANCK zip validation while still auditing PLANCK code/API compatibility",
    )
    parser.add_argument(
        "--source-only",
        action="store_true",
        help="run the complete non-mutating checkout-backed readiness suite",
    )
    parser.add_argument(
        "--check-committed-reports",
        action="store_true",
        help="regenerate explicitly authoritative reports into temporary paths and compare them",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="verify source-readiness command construction without running the suite",
    )
    parser.add_argument("--verbose", action="store_true", help="print summarized output for passing gates")
    args = parser.parse_args()
    if args.avatar_sync and args.gameplay:
        parser.error("--avatar-sync and --gameplay cannot be combined")

    if args.self_test:
        failures = source_readiness_commands_self_test()
        failures.extend(committed_report_consistency_self_test())
        print(f"Source readiness self-test failures: {len(failures)}")
        for failure in failures:
            print(f"- {failure}")
        return 1 if failures else 0

    root = repo_root()
    if args.source_only:
        return run_source_readiness(root, args.verbose, args.check_committed_reports)
    if args.check_committed_reports:
        return run_committed_report_consistency(root, args.verbose)

    skyrim_vr = args.skyrim_vr.expanduser().resolve() if args.skyrim_vr else None
    failures: list[str] = []
    warnings: list[str] = []

    print(f"Repository root: {root}")
    print("SkyrimTogetherVR readiness audit does not launch Skyrim or mutate the game install.")
    if skyrim_vr:
        print(f"Skyrim VR root: {skyrim_vr}")

    target_ok, target_detail = xmake_targets_visible(root)
    print(f"[{'PASS' if target_ok else 'FAIL'}] VR xmake targets")
    print(f"  {target_detail}")
    if not target_ok:
        failures.append("VR xmake targets are not all visible")

    for name, command in SOURCE_AUDITS:
        if args.skip_fus and name == "FUS native DLL compatibility":
            print("[SKIP] FUS native DLL compatibility")
            print("  skipped by --skip-fus")
            continue
        gate_command = command_for_environment(command, skyrim_vr)
        if name == "PLANCK compatibility":
            if args.skip_planck_archive:
                gate_command = (*gate_command, "--skip-planck-archive")
            elif args.planck_archive:
                gate_command = (*gate_command, "--planck-archive", str(args.planck_archive.expanduser().resolve()))
            if args.require_planck_archive:
                gate_command = (*gate_command, "--require-planck-archive")
        ok, _ = run_gate(root, name, gate_command, args.verbose)
        if not ok:
            failures.append(name)

    for name, command in RUNTIME_AUDITS:
        ok, _ = run_gate(root, name, command_for_environment(command, skyrim_vr), args.verbose)
        if not ok:
            failures.append(name)

    package = (root / args.package).resolve() if not args.package.is_absolute() else args.package.resolve()
    built_package_command = [
        "Tools/SkyrimVR/audit_built_package.py",
        "--package",
        str(package),
        "--require-installed-prerequisites",
        "--require-vrik",
        "--require-higgs",
        "--require-planck",
    ]
    if args.avatar_sync:
        built_package_command.append("--avatar-sync")
    if args.gameplay:
        built_package_command.append("--gameplay")
    if skyrim_vr:
        built_package_command.extend(["--skyrim-vr", str(skyrim_vr)])

    if package.exists() or args.require_built_package:
        ok, _ = run_gate(root, "Windows-built package", tuple(built_package_command), args.verbose)
        if not ok:
            failures.append("Windows-built package")
    else:
        warning = f"Windows-built package not present: {package}"
        warnings.append(warning)
        print("[WARN] Windows-built package")
        print(f"  {warning}")
        if args.avatar_sync:
            print("  Run PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --avatar-sync-only on Windows/MSVC for the VRIK/HIGGS remote-avatar validation package.")
            print('  Then rerun this gate with --package "artifacts\\SkyrimTogetherVR\\packages\\avatar-sync" --avatar-sync --require-built-package.')
            print("  After it passes, dry-run InstallSkyrimTogetherVR-Windows.bat --avatar-sync before using --install.")
        elif args.gameplay:
            print("  Run PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --gameplay-only on Windows/MSVC for the full gameplay package.")
            print('  Then rerun this gate with --package "artifacts\\SkyrimTogetherVR\\packages\\gameplay" --gameplay --require-built-package.')
            print("  After it passes, dry-run InstallSkyrimTogetherVR-Windows.bat --gameplay before using --install.")
        else:
            print("  Run PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --all on Windows/MSVC for default, avatar-sync, gameplay, and DLL-only packages.")
            print("  Then run VerifySkyrimTogetherVRWindowsPackages-Windows.bat to audit the stable package snapshots and install dry-runs.")
            print('  Default snapshot: artifacts\\SkyrimTogetherVR\\packages\\default')
            print('  Avatar-sync snapshot: artifacts\\SkyrimTogetherVR\\packages\\avatar-sync')
            print('  Gameplay snapshot: artifacts\\SkyrimTogetherVR\\packages\\gameplay')
            print('  DLL-only snapshot: artifacts\\SkyrimTogetherVR\\packages\\dll-only')

    print(f"Readiness warnings: {len(warnings)}")
    for warning in warnings:
        print(f"- {warning}")
    print(f"Readiness failures: {len(failures)}")
    for failure in failures:
        print(f"- {failure}")

    return 1 if failures or (warnings and args.require_built_package) else 0


if __name__ == "__main__":
    sys.exit(main())
