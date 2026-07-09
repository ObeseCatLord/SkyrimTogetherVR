#!/usr/bin/env python3
"""Audit FUS native DLL mods against the SkyrimTogetherVR launcher policy."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import sys
from dataclasses import dataclass
from typing import Iterable


DEFAULT_FUS = pathlib.Path(
    os.environ.get("STVR_FUS_ROOT")
    or os.environ.get("FUS_ROOT")
    or "/home/obesecatlord/Games/FUS"
)

HARD_BLOCKED_DLLS = {
    "enginefixes.dll": "SE Engine Fixes is blocked/greylisted by SkyrimTogether; FUS should use EngineFixesVR.dll.",
    "crashhandler64.dll": "Known to break the SkyrimTogether heap/crash path.",
    "fraps64.dll": "Known to break the Tilted UI/rendering path.",
    "specialk64.dll": "Known rendering hook conflict.",
    "reshade64_specialk64.dll": "Known rendering hook conflict.",
    "nvcamera64.dll": "Known NVIDIA hook conflict.",
}

ROOT_PROXY_DLLS = {
    "d3d11.dll",
    "d3dx9_42.dll",
    "dinput8.dll",
    "dxgi.dll",
    "openvr_api.dll",
    "x3daudio1_7.dll",
}

IGNORED_INSTALLED_ROOT_DIR_PREFIXES = (
    "codex-disabled-",
)

EXPECTED_VR_STACK = {
    "vrik.dll": "required VR IK dependency; SkyrimTogetherVR reads VRIK state through its bridge.",
    "higgs_vr.dll": "required/expected hand-physics dependency; SkyrimTogetherVR keeps HIGGS in an observation bridge.",
    "higgs.dll": "alternate HIGGS plugin filename; SkyrimTogetherVR treats it like higgs_vr.dll.",
    "activeragdoll.dll": "PLANCK dependency; SkyrimTogetherVR keeps actor ragdoll mutation gated.",
    "skyrimvrtools.dll": "FUS/SkyUI VR dependency; allowed, not directly required by SkyrimTogetherVR.",
    "skyui-vr.dll": "FUS UI dependency; allowed.",
    "skyrimvresl.dll": "FUS ESL support dependency; allowed.",
    "enginefixesvr.dll": "FUS Engine Fixes VR dependency; allowed with SkyrimTogether-safe allocator settings.",
}

ENGINE_FIXES_VR_REQUIRED = {
    "MemoryManager": "false",
    "MaxStdio": "8192",
}

LAUNCHER_POLICY_TOKENS = {
    "Code/immersive_launcher/stubs/DllBlocklist.cpp": (
        'L"EngineFixes.dll"',
        'L"crashhandler64.dll"',
        'L"fraps64.dll"',
        'L"SpecialK64.dll"',
        'L"ReShade64_SpecialK64.dll"',
        'L"NvCamera64.dll"',
        'L"EngineFixesVR.dll"',
        'L"Data\\\\SKSE\\\\Plugins\\\\EngineFixesVR.ini"',
        "MemoryManager = false",
        "MaxStdio = 8192",
        'L"higgs_vr.dll"',
        'L"higgs.dll"',
        'L"activeragdoll.dll"',
    ),
    "Code/client/main.cpp": (
        "BuildVRCompatibilityStatus",
        "WriteVRCompatibilityStatusFile",
        "VRPhysicsCompatibilityModInstalled",
        "HIGGS or PLANCK is installed; refusing to install unvalidated SkyrimTogetherVR gameplay hooks",
    ),
    "Code/client/VRCompatibilityStatus.cpp": (
        'L"higgs_vr.dll"',
        'L"higgs.dll"',
        'L"activeragdoll.dll"',
        "SkyrimTogetherVR.compatibility",
    ),
}


@dataclass(frozen=True)
class DllRecord:
    mod: str
    mod_order: int | None
    path: pathlib.Path
    rel: pathlib.PurePosixPath
    deployed: pathlib.PurePosixPath
    category: str

    @property
    def name_lower(self) -> str:
        return self.path.name.lower()

    @property
    def deployed_key(self) -> str:
        return self.deployed.as_posix().lower()


def read_text(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8-sig", errors="replace")


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def resolve_fus_root(fus: pathlib.Path) -> pathlib.Path:
    fus = fus.expanduser().resolve()
    if (fus / "mods").is_dir() and (fus / "profiles").is_dir():
        return fus

    for child in ("MO2", "Modlist"):
        candidate = (fus / child).resolve()
        if (candidate / "mods").is_dir() and (candidate / "profiles").is_dir():
            return candidate

    return fus


def infer_installed_root(requested_fus: pathlib.Path, fus: pathlib.Path) -> pathlib.Path | None:
    candidates = [
        requested_fus / "SkyrimVR",
        fus.parent / "SkyrimVR",
    ]
    if requested_fus.name.casefold() == "skyrimvr":
        candidates.insert(0, requested_fus)

    seen: set[pathlib.Path] = set()
    for candidate in candidates:
        candidate = candidate.expanduser().resolve()
        if candidate in seen:
            continue
        seen.add(candidate)
        if (candidate / "Data" / "SKSE" / "Plugins").is_dir() or (candidate / "SkyrimVR.exe").exists():
            return candidate
    return None


def selected_profile(fus: pathlib.Path) -> str | None:
    ini = fus / "ModOrganizer.ini"
    if not ini.exists():
        return None

    match = re.search(r"^selected_profile\s*=\s*@ByteArray\((.*?)\)\s*$", read_text(ini), re.MULTILINE)
    return match.group(1) if match else None


def parse_modlist(profile_dir: pathlib.Path) -> tuple[list[str], dict[str, int]]:
    modlist = profile_dir / "modlist.txt"
    enabled: list[str] = []
    order: dict[str, int] = {}
    if not modlist.exists():
        raise SystemExit(f"Missing FUS profile modlist: {modlist}")

    for index, raw_line in enumerate(read_text(modlist).splitlines()):
        line = raw_line.strip()
        if not line or line[0] not in "+-":
            continue
        if line[0] == "+":
            name = line[1:].strip()
            enabled.append(name)
            order[name] = index

    return enabled, order


def find_mod_dirs(fus: pathlib.Path, mods: Iterable[str] | None) -> list[tuple[str, pathlib.Path, int | None]]:
    mods_dir = fus / "mods"
    if not mods_dir.exists():
        raise SystemExit(f"Missing FUS mods directory: {mods_dir}")

    if mods is None:
        return sorted(
            ((path.name, path, None) for path in mods_dir.iterdir() if path.is_dir()),
            key=lambda item: item[0].casefold(),
        )

    result = []
    for index, name in enumerate(mods):
        path = mods_dir / name
        if path.exists() and path.is_dir():
            result.append((name, path, index))
    return result


def classify_path(rel: pathlib.PurePosixPath) -> tuple[pathlib.PurePosixPath, str]:
    parts = tuple(part.casefold() for part in rel.parts)
    if parts and parts[0] == "root":
        deployed = pathlib.PurePosixPath(*rel.parts[1:]) if len(rel.parts) > 1 else pathlib.PurePosixPath(rel.name)
        return deployed, "game-root"

    lowered = rel.as_posix().casefold()
    if "/skse/plugins/" in f"/{lowered}/":
        return pathlib.PurePosixPath("Data") / rel, "skse-plugin"
    if lowered.startswith("upscalerbaseplugin/"):
        return pathlib.PurePosixPath("Data") / rel, "upscaler-runtime"
    if lowered.startswith("plugins/"):
        return pathlib.PurePosixPath("Data") / rel, "data-plugin"
    return pathlib.PurePosixPath("Data") / rel, "data-runtime"


def classify_installed_path(rel: pathlib.PurePosixPath) -> tuple[pathlib.PurePosixPath, str]:
    lowered = rel.as_posix().casefold()
    if "/skse/plugins/" in f"/{lowered}/":
        return rel, "skse-plugin"
    if lowered.startswith("data/"):
        return rel, "data-runtime"
    return rel, "game-root"


def scan_dlls(mod_dirs: Iterable[tuple[str, pathlib.Path, int | None]]) -> list[DllRecord]:
    records: list[DllRecord] = []
    for mod_name, mod_dir, order in mod_dirs:
        for path in sorted(mod_dir.rglob("*")):
            if not path.is_file() or path.suffix.casefold() != ".dll":
                continue
            rel = pathlib.PurePosixPath(path.relative_to(mod_dir).as_posix())
            deployed, category = classify_path(rel)
            records.append(DllRecord(mod_name, order, path, rel, deployed, category))
    return records


def is_ignored_installed_root_path(path: pathlib.Path, root: pathlib.Path) -> bool:
    try:
        rel = path.relative_to(root)
    except ValueError:
        return True

    for part in rel.parts:
        folded = part.casefold()
        if any(folded.startswith(prefix) for prefix in IGNORED_INSTALLED_ROOT_DIR_PREFIXES):
            return True
    return False


def scan_installed_root_dlls(installed_root: pathlib.Path) -> list[DllRecord]:
    records: list[DllRecord] = []
    for path in sorted(installed_root.rglob("*")):
        if not path.is_file() or path.suffix.casefold() != ".dll":
            continue
        if is_ignored_installed_root_path(path, installed_root):
            continue
        rel = pathlib.PurePosixPath(path.relative_to(installed_root).as_posix())
        deployed, category = classify_installed_path(rel)
        records.append(DllRecord("installed SkyrimVR root", None, path, rel, deployed, category))
    return records


def case_insensitive_child(parent: pathlib.Path, rel: str) -> pathlib.Path | None:
    current = parent
    for part in pathlib.PurePosixPath(rel).parts:
        if not current.exists():
            return None
        matches = [child for child in current.iterdir() if child.name.casefold() == part.casefold()]
        if not matches:
            return None
        current = matches[0]
    return current


def record_mod_root(record: DllRecord) -> pathlib.Path:
    root = record.path
    for _ in record.rel.parts:
        root = root.parent
    return root


def find_engine_fixes_vr_config(record: DllRecord) -> pathlib.Path | None:
    same_dir = case_insensitive_child(record.path.parent, "EngineFixesVR.ini")
    if same_dir is not None:
        return same_dir

    mod_root = record_mod_root(record)
    for relative_path in (
        "SKSE/Plugins/EngineFixesVR.ini",
        "Data/SKSE/Plugins/EngineFixesVR.ini",
    ):
        config = case_insensitive_child(mod_root, relative_path)
        if config is not None:
            return config

    return None


def read_ini_value(path: pathlib.Path, key: str) -> str | None:
    pattern = re.compile(rf"^\s*{re.escape(key)}\s*=\s*([^;\s]+)", re.IGNORECASE)
    for line in read_text(path).splitlines():
        match = pattern.match(line)
        if match:
            return match.group(1).strip()
    return None


def engine_fixes_vr_warnings(record: DllRecord) -> list[str]:
    config = find_engine_fixes_vr_config(record)
    if config is None:
        return ["EngineFixesVR.ini was not found next to EngineFixesVR.dll; launcher greylist cannot validate settings."]

    warnings = []
    for key, expected in ENGINE_FIXES_VR_REQUIRED.items():
        actual = read_ini_value(config, key)
        if actual is None:
            warnings.append(f"EngineFixesVR.ini missing {key}; launcher greylist can rewrite known releases.")
        elif actual.casefold() != expected.casefold():
            warnings.append(f"EngineFixesVR.ini has {key}={actual}; SkyrimTogetherVR launcher will prompt to set {key}={expected}.")
    return warnings


def record_notes(record: DllRecord) -> tuple[list[str], list[str]]:
    failures: list[str] = []
    warnings: list[str] = []

    blocked = HARD_BLOCKED_DLLS.get(record.name_lower)
    if blocked:
        failures.append(f"{record.path.name}: {blocked}")

    if record.name_lower == "skyrimsoulsre.dll":
        warnings.append("Skyrim Souls RE is detected; use the SkyrimTogetherVR-compatible SkyrimSoulsRE build only.")

    if record.name_lower == "enginefixesvr.dll":
        warnings.extend(engine_fixes_vr_warnings(record))

    if record.category == "game-root" and record.name_lower in ROOT_PROXY_DLLS:
        if record.name_lower == "openvr_api.dll":
            warnings.append("OpenComposite root openvr_api.dll changes the VR runtime and disables SteamVR overlays/keyboard in FUS.")
        elif record.name_lower in {"dxgi.dll", "d3d11.dll"}:
            warnings.append(f"{record.path.name} is a root rendering proxy; avoid stacking multiple ReShade/upscaler/VRPerfKit proxies.")

    return failures, warnings


def duplicate_groups(records: Iterable[DllRecord]) -> dict[str, list[DllRecord]]:
    groups: dict[str, list[DllRecord]] = {}
    for record in records:
        groups.setdefault(record.deployed_key, []).append(record)
    return {key: values for key, values in groups.items() if len(values) > 1}


def ordered(records: Iterable[DllRecord]) -> list[DllRecord]:
    return sorted(records, key=lambda record: (record.mod_order if record.mod_order is not None else 999999, record.mod.casefold(), record.rel.as_posix().casefold()))


def validate_launcher_policy(root: pathlib.Path) -> list[str]:
    failures: list[str] = []
    for relative_path, tokens in LAUNCHER_POLICY_TOKENS.items():
        path = root / relative_path
        if not path.exists():
            failures.append(f"launcher policy source is missing: {relative_path}")
            continue

        text = read_text(path)
        for token in tokens:
            if token not in text:
                failures.append(f"{relative_path}: missing launcher policy token {token!r}")
    return failures


def record_to_report(record: DllRecord) -> dict[str, object]:
    failures, warnings = record_notes(record)
    return {
        "mod": record.mod,
        "modOrder": record.mod_order,
        "name": record.path.name,
        "relativePath": record.rel.as_posix(),
        "deployedPath": record.deployed.as_posix(),
        "category": record.category,
        "failures": failures,
        "warnings": warnings,
    }


def build_json_report(
    *,
    requested_fus: pathlib.Path,
    fus: pathlib.Path,
    installed_root: pathlib.Path | None,
    profile_name: str | None,
    scan_mode: str,
    records: list[DllRecord],
    installed_root_records: list[DllRecord],
    duplicate_records: dict[str, list[DllRecord]],
    active_expected: list[str],
    root_proxies: list[DllRecord],
    skse_plugins: list[DllRecord],
    warnings: list[str],
    failures: list[str],
    launcher_policy_failures: list[str],
) -> dict[str, object]:
    return {
        "schema": "skyrim_together_vr_fus_dll_compat_v1",
        "fusPath": str(requested_fus),
        "resolvedMo2Root": str(fus),
        "installedRoot": str(installed_root) if installed_root else None,
        "scanMode": scan_mode,
        "profile": profile_name,
        "counts": {
            "dllRecords": len(records),
            "installedRootDllRecords": len(installed_root_records),
            "sksePluginDlls": len(skse_plugins),
            "gameRootProxyDlls": len(root_proxies),
            "duplicateDeployedDllPaths": len(duplicate_records),
            "expectedVrStackActive": len(active_expected),
            "compatibilityWarnings": len(warnings),
            "compatibilityFailures": len(failures),
            "launcherPolicyFailures": len(launcher_policy_failures),
        },
        "expectedVrStack": {
            name: {
                "active": name in active_expected,
                "description": EXPECTED_VR_STACK[name],
            }
            for name in sorted(EXPECTED_VR_STACK)
        },
        "rootProxyDlls": [record_to_report(record) for record in ordered(root_proxies)],
        "installedRootRecords": [record_to_report(record) for record in ordered(installed_root_records)],
        "duplicateDeployedDllPaths": [
            {
                "deployedPath": values[0].deployed.as_posix(),
                "providers": [record_to_report(record) for record in ordered(values)],
            }
            for values in sorted(duplicate_records.values(), key=lambda group: group[0].deployed_key)
        ],
        "records": [record_to_report(record) for record in ordered(records)],
        "warnings": warnings,
        "failures": failures,
        "launcherPolicyFailures": launcher_policy_failures,
    }


def write_json_report(path: pathlib.Path, report: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def print_records(records: list[DllRecord], limit: int) -> None:
    for record in ordered(records)[:limit]:
        print(f"- {record.category}: {record.mod} -> {record.rel} => {record.deployed}")
    if len(records) > limit:
        print(f"- ... {len(records) - limit} more DLL record(s)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fus", type=pathlib.Path, default=DEFAULT_FUS)
    parser.add_argument("--profile", help="FUS profile name. Defaults to ModOrganizer.ini selected_profile.")
    parser.add_argument("--all-mods", action="store_true", help="Scan every mod folder instead of an enabled profile.")
    parser.add_argument(
        "--installed-root",
        type=pathlib.Path,
        help="Installed/deployed Skyrim VR root to scan for active FUS root and SKSE DLLs. Defaults to FUS/SkyrimVR when present.",
    )
    parser.add_argument(
        "--skip-installed-root",
        action="store_true",
        help="Only scan the MO2 modlist/mod folders and skip the deployed SkyrimVR root DLL surface.",
    )
    parser.add_argument("--list", action="store_true", help="Print DLL records.")
    parser.add_argument("--limit", type=int, default=80, help="Maximum DLL records to print with --list.")
    parser.add_argument("--fail-on-warnings", action="store_true", help="Treat compatibility warnings as failures.")
    parser.add_argument("--json-report", type=pathlib.Path, help="Write a machine-readable report with every scanned native DLL.")
    parser.add_argument("--repo", type=pathlib.Path, default=repo_root(), help="Repository root used for launcher policy validation.")
    parser.add_argument("--skip-launcher-policy", action="store_true", help="Skip launcher-side blocklist/greylist source validation.")
    args = parser.parse_args()

    requested_fus = args.fus.expanduser().resolve()
    fus = resolve_fus_root(requested_fus)
    installed_root = None
    installed_root_records: list[DllRecord] = []
    if not args.skip_installed_root:
        installed_root = (
            args.installed_root.expanduser().resolve()
            if args.installed_root
            else infer_installed_root(requested_fus, fus)
        )
        if installed_root and installed_root.exists():
            installed_root_records = scan_installed_root_dlls(installed_root)
        else:
            installed_root = None

    if args.all_mods:
        profile_name = None
        mod_dirs = find_mod_dirs(fus, None)
    else:
        profile_name = args.profile or selected_profile(fus)
        if not profile_name:
            raise SystemExit("No FUS profile selected. Pass --profile or --all-mods.")
        enabled, order = parse_modlist(fus / "profiles" / profile_name)
        mod_dirs = []
        for name in enabled:
            path = fus / "mods" / name
            if path.exists() and path.is_dir():
                mod_dirs.append((name, path, order.get(name)))

    records = scan_dlls(mod_dirs)
    failures: list[str] = []
    warnings: list[str] = []
    launcher_policy_failures: list[str] = []

    for record in [*records, *installed_root_records]:
        record_failures, record_warnings = record_notes(record)
        failures.extend(f"{record.mod}: {message}" for message in record_failures)
        warnings.extend(f"{record.mod}: {message}" for message in record_warnings)

    if not args.skip_launcher_policy:
        launcher_policy_failures = validate_launcher_policy(args.repo.expanduser().resolve())
        failures.extend(f"launcher-policy: {failure}" for failure in launcher_policy_failures)

    duplicates = duplicate_groups(records)
    duplicate_warnings = []
    for values in duplicates.values():
        values = ordered(values)
        first = values[0]
        others = values[1:]
        duplicate_warnings.append(
            f"{first.deployed}: {first.mod} is the first profile occurrence in this static scan; "
            f"{len(others)} other enabled mod(s) also provide this DLL path: "
            + ", ".join(record.mod for record in others)
        )
    warnings.extend(duplicate_warnings)

    active_names = {record.name_lower for record in [*records, *installed_root_records]}
    active_expected = sorted(name for name in EXPECTED_VR_STACK if name in active_names)
    missing_expected = [
        name for name in ("vrik.dll", "activeragdoll.dll")
        if name not in active_names
    ]
    for name in missing_expected:
        warnings.append(f"Expected FUS VR dependency not active in scanned set: {name}")
    if "higgs_vr.dll" not in active_names and "higgs.dll" not in active_names:
        warnings.append("Expected FUS VR dependency not active in scanned set: higgs_vr.dll or higgs.dll")

    all_scanned_records = [*records, *installed_root_records]
    root_proxies = [record for record in all_scanned_records if record.category == "game-root" and record.name_lower in ROOT_PROXY_DLLS]
    skse_plugins = [record for record in all_scanned_records if record.category == "skse-plugin"]

    print(f"FUS path: {requested_fus}")
    if fus != requested_fus:
        print(f"Resolved MO2 root: {fus}")
    if installed_root:
        print(f"Installed SkyrimVR root: {installed_root}")
    else:
        print("Installed SkyrimVR root: not scanned")
    print(f"Scan mode: {'all mod folders' if args.all_mods else 'enabled profile'}")
    if profile_name:
        print(f"Profile: {profile_name}")
    print(f"DLL records scanned: {len(records)}")
    print(f"Installed root DLL records scanned: {len(installed_root_records)}")
    print(f"SKSE plugin DLLs: {len(skse_plugins)}")
    print(f"Game-root/proxy DLLs: {len(root_proxies)}")
    print(f"Duplicate deployed DLL paths: {len(duplicates)}")
    print(f"Expected VR stack DLLs active: {len(active_expected)}")
    print(f"Launcher policy failures: {len(launcher_policy_failures)}")
    print(f"Compatibility warnings: {len(warnings)}")
    print(f"Compatibility failures: {len(failures)}")

    if active_expected:
        print("\nExpected VR stack DLLs:")
        for name in active_expected:
            print(f"- {name}: {EXPECTED_VR_STACK[name]}")

    if args.list:
        print("\nDLL records:")
        print_records(records, args.limit)
        if installed_root_records:
            print("\nInstalled root DLL records:")
            print_records(installed_root_records, args.limit)

    if root_proxies:
        print("\nGame-root/proxy DLLs:")
        for record in ordered(root_proxies):
            print(f"- {record.mod}: {record.rel} => {record.deployed}")

    if duplicate_warnings:
        print("\nDuplicate deployed DLL path warnings:")
        for warning in duplicate_warnings:
            print(f"- {warning}")

    if warnings:
        print("\nWarnings:")
        for warning in warnings:
            print(f"- {warning}")

    if failures:
        print("\nFailures:")
        for failure in failures:
            print(f"- {failure}")

    if args.json_report:
        report_path = args.json_report.expanduser()
        if not report_path.is_absolute():
            report_path = (pathlib.Path.cwd() / report_path).resolve()
        report = build_json_report(
            requested_fus=requested_fus,
            fus=fus,
            installed_root=installed_root,
            profile_name=profile_name,
            scan_mode="all mod folders" if args.all_mods else "enabled profile",
            records=records,
            installed_root_records=installed_root_records,
            duplicate_records=duplicates,
            active_expected=active_expected,
            root_proxies=root_proxies,
            skse_plugins=skse_plugins,
            warnings=warnings,
            failures=failures,
            launcher_policy_failures=launcher_policy_failures,
        )
        write_json_report(report_path, report)
        print(f"\nWrote JSON report: {report_path}")

    if failures or (warnings and args.fail_on_warnings):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
