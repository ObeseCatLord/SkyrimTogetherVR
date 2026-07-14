#!/usr/bin/env python3
"""Compile the Skyrim VR Papyrus scripts that differ from the SE package."""

from __future__ import annotations

import argparse
import os
import pathlib
import platform
import shutil
import subprocess
import sys
import tempfile


TARGET_SCRIPTS = (
    "SkyrimTogetherVRTickBridge.psc",
    "SkyrimTogetherVerifyLaunchScript.psc",
    "SkyrimTogetherPlayerAliasScript.psc",
    "SkyrimTogetherVRMigrationXScript.psc",
    "SkyrimTogetherVRMigrationScript.psc",
    "SkyrimTogetherUtils.psc",
    "SkyrimTogetherVRConnectionMenu.psc",
    "SkyrimTogetherVRConnectionSpellEffect.psc",
)


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def default_caprica(root: pathlib.Path) -> pathlib.Path | None:
    candidates = (
        root.parent / "_refs" / "Caprica-release" / "extracted" / "Caprica.exe",
        root.parent / "_refs" / "Caprica" / "build" / "Caprica" / "Caprica",
        root.parent / "_refs" / "Caprica" / "build" / "Caprica" / "Caprica.exe",
    )
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def resolve_compiler(root: pathlib.Path, requested: str | None) -> pathlib.Path:
    if requested:
        compiler = pathlib.Path(requested).expanduser().resolve()
    elif os.environ.get("CAPRICA"):
        compiler = pathlib.Path(os.environ["CAPRICA"]).expanduser().resolve()
    else:
        compiler = default_caprica(root)
        if compiler is None:
            raise SystemExit(
                "Caprica was not found. Pass --caprica /path/to/Caprica.exe "
                "or set CAPRICA=/path/to/Caprica.exe."
            )

    if not compiler.exists():
        raise SystemExit(f"Caprica compiler does not exist: {compiler}")

    return compiler


def command_for_compiler(compiler: pathlib.Path, wine: str | None, no_wine: bool) -> list[str]:
    is_windows_host = platform.system().lower().startswith("win")
    if compiler.suffix.lower() == ".exe" and not is_windows_host and not no_wine:
        return [wine or "wine", str(compiler)]
    return [str(compiler)]


def copy_sources(source_dir: pathlib.Path, temp_source: pathlib.Path) -> None:
    for script in TARGET_SCRIPTS:
        source = source_dir / script
        if not source.exists():
            raise SystemExit(f"Missing source script: {source}")
        shutil.copy2(source, temp_source / script)


def compile_script(
    command_prefix: list[str],
    flags_file: pathlib.Path,
    import_dir: pathlib.Path,
    temp_source: pathlib.Path,
    output_dir: pathlib.Path,
    script: str,
) -> None:
    command = [
        *command_prefix,
        "--game",
        "skyrim",
        "--flags",
        str(flags_file),
        "--import",
        str(import_dir),
        "--import",
        str(temp_source),
        "--output",
        str(output_dir),
        script,
    ]

    env = os.environ.copy()
    env.setdefault("WINEDEBUG", "-all")
    result = subprocess.run(command, cwd=temp_source, env=env)
    if result.returncode != 0:
        raise SystemExit(f"Papyrus compile failed for {script} with exit code {result.returncode}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--caprica", help="Path to Caprica or Caprica.exe.")
    parser.add_argument("--wine", default=os.environ.get("WINE", "wine"), help="Wine executable for Windows Caprica builds.")
    parser.add_argument("--no-wine", action="store_true", help="Run Caprica.exe directly even on non-Windows hosts.")
    parser.add_argument("--output", type=pathlib.Path, help="Override PEX output directory.")
    args = parser.parse_args()

    root = repo_root()
    source_dir = root / "GameFiles" / "SkyrimVR" / "Scripts" / "source"
    output_dir = (args.output or (root / "GameFiles" / "SkyrimVR" / "Scripts")).resolve()
    flags_file = root / "Tools" / "SkyrimVR" / "TESV_Papyrus_Flags.flg"
    import_dir = root / "Tools" / "SkyrimVR" / "PapyrusImports"

    compiler = resolve_compiler(root, args.caprica)
    command_prefix = command_for_compiler(compiler, args.wine, args.no_wine)

    output_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="stvr-papyrus-") as temp:
        temp_source = pathlib.Path(temp)
        copy_sources(source_dir, temp_source)

        for script in TARGET_SCRIPTS:
            compile_script(command_prefix, flags_file, import_dir, temp_source, output_dir, script)

    missing = []
    for script in TARGET_SCRIPTS:
        pex = output_dir / f"{pathlib.Path(script).stem}.pex"
        if not pex.exists():
            missing.append(pex)

    if missing:
        for path in missing:
            print(f"Missing compiled output: {path}", file=sys.stderr)
        return 1

    print(f"Compiled {len(TARGET_SCRIPTS)} Papyrus scripts to {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
