#!/usr/bin/env python3
"""Audit or install Skyrim VR runtime prerequisites used by SkyrimTogetherVR."""

from __future__ import annotations

import argparse
import dataclasses
import os
import pathlib
import shutil
import sys
import zipfile

import vr_paths

DEFAULT_MO2_MOD_DIRS = (
    pathlib.Path("/home/obesecatlord/LargeGames/Games/Skyrim VR MO2/mods"),
    pathlib.Path.home() / "LargeGames" / "Games" / "Skyrim VR MO2" / "mods",
)
DEFAULT_BACKUP_DOWNLOAD_DIRS = (
    pathlib.Path("/home/obesecatlord/Backup/Downloads"),
    pathlib.Path.home() / "Backup" / "Downloads",
    pathlib.Path.home() / "Downloads",
)
DEFAULT_NORDIC_MOD_DIRS = (
    pathlib.Path("/home/obesecatlord/Games/Nordic Adventures - High Fantasy Beta 2.01/mods"),
    pathlib.Path.home() / "Games" / "Nordic Adventures - High Fantasy Beta 2.01" / "mods",
)


def unique_paths(paths: tuple[pathlib.Path, ...] | list[pathlib.Path]) -> tuple[pathlib.Path, ...]:
    seen: set[str] = set()
    result: list[pathlib.Path] = []
    for path in paths:
        expanded = path.expanduser()
        key = str(expanded)
        if key in seen:
            continue
        seen.add(key)
        result.append(expanded)
    return tuple(result)


def env_path_list(*names: str) -> tuple[pathlib.Path, ...]:
    paths: list[pathlib.Path] = []
    for name in names:
        value = os.environ.get(name)
        if not value:
            continue
        for part in value.split(os.pathsep):
            part = part.strip()
            if part:
                paths.append(pathlib.Path(part))
    return unique_paths(paths)


def prepend_env_paths(defaults: tuple[pathlib.Path, ...], *env_names: str) -> tuple[pathlib.Path, ...]:
    return unique_paths((*env_path_list(*env_names), *defaults))


MO2_MOD_DIRS = prepend_env_paths(DEFAULT_MO2_MOD_DIRS, "STVR_MO2_MODS", "STVR_MO2_MOD_DIRS")
BACKUP_DOWNLOAD_DIRS = prepend_env_paths(DEFAULT_BACKUP_DOWNLOAD_DIRS, "STVR_BACKUP_DOWNLOADS", "STVR_DOWNLOADS")
NORDIC_MOD_DIRS = prepend_env_paths(DEFAULT_NORDIC_MOD_DIRS, "STVR_NORDIC_MODS")


def mod_dir_sources(*names: str) -> tuple[pathlib.Path, ...]:
    return tuple(root / name for root in (*MO2_MOD_DIRS, *NORDIC_MOD_DIRS) for name in names)


def download_sources(*names: str) -> tuple[pathlib.Path, ...]:
    return tuple(root / name for root in BACKUP_DOWNLOAD_DIRS for name in names)


@dataclasses.dataclass(frozen=True)
class ModSpec:
    key: str
    display_name: str
    sources: tuple[pathlib.Path, ...]
    required_files: tuple[str, ...]
    plugin_files: tuple[str, ...] = ()


MODS: tuple[ModSpec, ...] = (
    ModSpec(
        key="vrik",
        display_name="VRIK Player Avatar",
        sources=(
            *mod_dir_sources("VRIK", "VRIK Player Avatar"),
        ),
        required_files=(
            "SKSE/Plugins/VRIK.dll",
            "SKSE/Plugins/vrik.ini",
            "SKSE/Plugins/vrikslots.ini",
            "Scripts/VRIK.pex",
            "Scripts/_vrik_qust_system.pex",
            "meshes/vrik/torchoff.nif",
            "vrik.esp",
        ),
        plugin_files=("vrik.esp",),
    ),
    ModSpec(
        key="higgs",
        display_name="HIGGS",
        sources=(
            *download_sources("HIGGS 1.10.10-43930-1-10-10-1768263289.zip"),
            *mod_dir_sources("HIGGS - Hand Interaction and Gravity Gloves for Skyrim VR"),
        ),
        required_files=(
            "SKSE/Plugins/higgs_vr.dll",
            "SKSE/Plugins/higgs_vr.ini",
            "Scripts/HiggsVR.pex",
            "higgs_vr.esp",
        ),
        plugin_files=("higgs_vr.esp",),
    ),
    ModSpec(
        key="planck",
        display_name="PLANCK",
        sources=(
            *download_sources("PLANCK 0.8.0 66025 0.8.0 2026-06-13T23-29Z s6Og0dE6Y.zip"),
            *mod_dir_sources("PLANCK - Physical Animation and Character Kinetics"),
        ),
        required_files=(
            "SKSE/Plugins/activeragdoll.dll",
            "SKSE/Plugins/activeragdoll.ini",
            "Scripts/planck.pex",
        ),
    ),
)


def normalize_relative(path: pathlib.PurePath | str) -> str:
    return pathlib.PurePath(path).as_posix().lstrip("/")


def iter_source_files(source: pathlib.Path) -> dict[str, pathlib.Path | zipfile.ZipInfo]:
    if source.is_dir():
        result: dict[str, pathlib.Path | zipfile.ZipInfo] = {}
        for path in source.rglob("*"):
            if not path.is_file():
                continue
            relative = normalize_relative(path.relative_to(source))
            if relative.lower() == "meta.ini":
                continue
            result[relative] = path
        return result

    if source.is_file() and zipfile.is_zipfile(source):
        with zipfile.ZipFile(source) as archive:
            result = {}
            for info in archive.infolist():
                if info.is_dir():
                    continue
                relative = normalize_relative(info.filename)
                if relative.lower() == "meta.ini":
                    continue
                if ".." in pathlib.PurePath(relative).parts:
                    continue
                result[relative] = info
            return result

    return {}


def find_source(spec: ModSpec) -> tuple[pathlib.Path | None, dict[str, pathlib.Path | zipfile.ZipInfo]]:
    for source in spec.sources:
        files = iter_source_files(source)
        if not files:
            continue

        lower_files = {name.lower() for name in files}
        if all(required.lower() in lower_files for required in spec.required_files):
            return source, files

    return None, {}


def find_case_insensitive(root: pathlib.Path, relative_path: str) -> pathlib.Path | None:
    current = root
    for part in pathlib.PurePath(relative_path).parts:
        if not current.exists() or not current.is_dir():
            return None

        wanted = part.lower()
        match = next((child for child in current.iterdir() if child.name.lower() == wanted), None)
        if match is None:
            return None
        current = match

    return current if current.exists() else None


def installed_status(data_dir: pathlib.Path, spec: ModSpec) -> tuple[list[str], list[str]]:
    present: list[str] = []
    missing: list[str] = []
    for relative in spec.required_files:
        if find_case_insensitive(data_dir, relative) is None:
            missing.append(relative)
        else:
            present.append(relative)
    return present, missing


def copy_from_directory(
    source_root: pathlib.Path,
    source_files: dict[str, pathlib.Path | zipfile.ZipInfo],
    data_dir: pathlib.Path,
    overwrite: bool,
) -> tuple[int, int]:
    copied = 0
    skipped = 0
    for relative, source in sorted(source_files.items()):
        if not isinstance(source, pathlib.Path):
            continue
        destination = data_dir / relative
        if destination.exists() and not overwrite:
            skipped += 1
            continue
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        copied += 1

    return copied, skipped


def copy_from_zip(
    source_archive: pathlib.Path,
    source_files: dict[str, pathlib.Path | zipfile.ZipInfo],
    data_dir: pathlib.Path,
    overwrite: bool,
) -> tuple[int, int]:
    copied = 0
    skipped = 0
    with zipfile.ZipFile(source_archive) as archive:
        for relative, info in sorted(source_files.items()):
            if not isinstance(info, zipfile.ZipInfo):
                continue
            destination = data_dir / relative
            if destination.exists() and not overwrite:
                skipped += 1
                continue
            destination.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(info) as src, destination.open("wb") as dst:
                shutil.copyfileobj(src, dst)
            copied += 1

    return copied, skipped


def default_plugin_list_candidates(skyrim_vr: pathlib.Path) -> list[pathlib.Path]:
    steamapps = skyrim_vr.parents[1] if len(skyrim_vr.parents) >= 2 else None
    candidates: list[pathlib.Path] = []
    if steamapps:
        candidates.append(
            steamapps
            / "compatdata"
            / "611670"
            / "pfx"
            / "drive_c"
            / "users"
            / "steamuser"
            / "AppData"
            / "Local"
            / "Skyrim VR"
            / "plugins.txt"
        )
    candidates.extend(
        [
            pathlib.Path.home()
            / ".steam"
            / "steam"
            / "steamapps"
            / "compatdata"
            / "611670"
            / "pfx"
            / "drive_c"
            / "users"
            / "steamuser"
            / "AppData"
            / "Local"
            / "Skyrim VR"
            / "plugins.txt",
            pathlib.Path.home()
            / ".local"
            / "share"
            / "Steam"
            / "steamapps"
            / "compatdata"
            / "611670"
            / "pfx"
            / "drive_c"
            / "users"
            / "steamuser"
            / "AppData"
            / "Local"
            / "Skyrim VR"
            / "plugins.txt",
        ]
    )
    return candidates


def resolve_plugin_list(skyrim_vr: pathlib.Path, explicit_path: pathlib.Path | None) -> pathlib.Path | None:
    if explicit_path is not None:
        return explicit_path

    candidates = default_plugin_list_candidates(skyrim_vr)
    for candidate in candidates:
        if candidate.exists():
            return candidate

    return candidates[0] if candidates else None


def update_plugin_list(plugin_list: pathlib.Path, plugins: list[str], install: bool) -> tuple[list[str], list[str]]:
    existing_lines: list[str] = []
    if plugin_list.exists():
        existing_lines = plugin_list.read_text(encoding="utf-8", errors="replace").splitlines()

    enabled = {line.strip().lstrip("*").lower() for line in existing_lines if line.strip()}
    missing = [plugin for plugin in plugins if plugin.lower() not in enabled]
    if install and missing:
        plugin_list.parent.mkdir(parents=True, exist_ok=True)
        lines = existing_lines[:]
        if not lines or lines[0].strip().lower() != "# this file is used by skyrim to keep track of your downloaded content.":
            lines.insert(0, "# This file is used by Skyrim to keep track of your downloaded content.")
        lines.extend(f"*{plugin}" for plugin in missing)
        plugin_list.write_text("\n".join(lines) + "\n", encoding="utf-8")
        existing_lines = lines

    enabled = {line.strip().lstrip("*").lower() for line in existing_lines if line.strip()}
    missing = [plugin for plugin in plugins if plugin.lower() not in enabled]
    return [plugin for plugin in plugins if plugin.lower() in enabled], missing


def parse_mods(value: str) -> set[str]:
    if value == "all":
        return {spec.key for spec in MODS}
    return {part.strip().lower() for part in value.split(",") if part.strip()}


def env_source_for(key: str) -> pathlib.Path | None:
    value = os.environ.get(f"STVR_{key.upper()}_SOURCE")
    if value:
        return pathlib.Path(value).expanduser()
    if key == "planck":
        archive = os.environ.get("STVR_PLANCK_ARCHIVE") or os.environ.get("PLANCK_ARCHIVE")
        if archive:
            return pathlib.Path(archive).expanduser()
    return None


def with_source_override(spec: ModSpec, explicit_source: pathlib.Path | None) -> ModSpec:
    extra_sources = []
    if explicit_source is not None:
        extra_sources.append(explicit_source.expanduser())
    env_source = env_source_for(spec.key)
    if env_source is not None:
        extra_sources.append(env_source)
    if not extra_sources:
        return spec

    seen = set()
    sources = []
    for source in (*extra_sources, *spec.sources):
        key = str(source)
        if key in seen:
            continue
        seen.add(key)
        sources.append(source)

    return dataclasses.replace(spec, sources=tuple(sources))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--skyrim-vr",
        type=pathlib.Path,
        default=vr_paths.default_skyrim_vr_path(),
        help=vr_paths.skyrim_vr_help(),
    )
    parser.add_argument("--mods", default="all", help="comma-separated subset: vrik,higgs,planck")
    parser.add_argument("--vrik-source", type=pathlib.Path, help="VRIK source folder or archive. Also supports STVR_VRIK_SOURCE.")
    parser.add_argument("--higgs-source", type=pathlib.Path, help="HIGGS source folder or archive. Also supports STVR_HIGGS_SOURCE.")
    parser.add_argument("--planck-source", type=pathlib.Path, help="PLANCK source folder or archive. Also supports STVR_PLANCK_SOURCE or STVR_PLANCK_ARCHIVE.")
    parser.add_argument("--install", action="store_true", help="copy prerequisite files into Skyrim VR/Data")
    parser.add_argument("--overwrite", action="store_true", help="overwrite existing prerequisite files")
    parser.add_argument("--require", action="store_true", help="fail if requested prerequisites are missing after the operation")
    parser.add_argument("--enable-plugins", action="store_true", help="enable VRIK/HIGGS ESPs in plugins.txt")
    parser.add_argument("--plugin-list", type=pathlib.Path, help="explicit plugins.txt path to update")
    args = parser.parse_args()

    selected = parse_mods(args.mods)
    known = {spec.key for spec in MODS}
    unknown = selected - known
    if unknown:
        print(f"Unknown mod key(s): {', '.join(sorted(unknown))}")
        return 2

    skyrim_vr = args.skyrim_vr.resolve()
    data_dir = skyrim_vr / "Data"
    failures: list[str] = []
    all_plugin_files: list[str] = []

    print(f"Skyrim VR root: {skyrim_vr}")
    print(f"Data root: {data_dir}")
    print(f"Mode: {'install' if args.install else 'audit-only'}")
    print(
        "Source roots: "
        f"MO2={len(MO2_MOD_DIRS)} download={len(BACKUP_DOWNLOAD_DIRS)} nordic={len(NORDIC_MOD_DIRS)} "
        "(override with STVR_MO2_MODS, STVR_BACKUP_DOWNLOADS, or per-mod source flags)"
    )

    source_overrides = {
        "vrik": args.vrik_source,
        "higgs": args.higgs_source,
        "planck": args.planck_source,
    }

    for spec in MODS:
        if spec.key not in selected:
            continue

        spec = with_source_override(spec, source_overrides.get(spec.key))
        source, source_files = find_source(spec)
        present_before, missing_before = installed_status(data_dir, spec)
        print(f"\n{spec.display_name}:")
        print(f"  source: {source if source else 'missing'}")
        print(f"  installed required files: {len(present_before)}/{len(spec.required_files)}")
        if missing_before:
            print("  missing:")
            for relative in missing_before:
                print(f"    - {relative}")

        if source is None:
            if missing_before or args.install:
                failures.append(f"{spec.display_name}: no usable source found")
            else:
                print("  source missing, but all required files are already installed")
            continue

        if args.install:
            if source.is_dir():
                copied, skipped = copy_from_directory(source, source_files, data_dir, args.overwrite)
            else:
                copied, skipped = copy_from_zip(source, source_files, data_dir, args.overwrite)
            print(f"  copied files: {copied}")
            print(f"  skipped existing files: {skipped}")

        present_after, missing_after = installed_status(data_dir, spec)
        if args.require and missing_after:
            failures.append(f"{spec.display_name}: missing after operation: {', '.join(missing_after)}")

        all_plugin_files.extend(spec.plugin_files)

    if args.enable_plugins:
        plugin_list = resolve_plugin_list(skyrim_vr, args.plugin_list)
        if plugin_list is None:
            failures.append("could not resolve plugins.txt path")
        else:
            enabled, missing = update_plugin_list(plugin_list, sorted(set(all_plugin_files)), args.install)
            print(f"\nPlugin list: {plugin_list}")
            print(f"  already enabled: {', '.join(enabled) if enabled else 'none'}")
            print(f"  missing enable entries: {', '.join(missing) if missing else 'none'}")
            if args.require and missing and not args.install:
                failures.append(f"plugins.txt missing enabled entries: {', '.join(missing)}")

    print(f"\nPrerequisite installer failures: {len(failures)}")
    for failure in failures:
        print(f"- {failure}")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
