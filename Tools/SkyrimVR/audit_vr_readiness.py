#!/usr/bin/env python3
"""Run the SkyrimTogetherVR static and runtime-prerequisite readiness gates."""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys


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
    parser.add_argument("--verbose", action="store_true", help="print summarized output for passing gates")
    args = parser.parse_args()
    if args.avatar_sync and args.gameplay:
        parser.error("--avatar-sync and --gameplay cannot be combined")

    root = repo_root()
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
