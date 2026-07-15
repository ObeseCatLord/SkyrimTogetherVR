#!/usr/bin/env python3
"""Create the private machine-local Skyrim Together VR agent handoff."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import tempfile
import zipfile
from datetime import datetime, timezone


MOD_NAMES = (
    "SKSE files",
    "VR Address Library for SKSEVR",
    "HIGGS - Hand Interaction and Gravity Gloves for Skyrim VR",
    "PLANCK - Physical Animation and Character Kinetics",
    "VRIK Player Avatar",
    "Arctal's VRIK Tweaks",
    "Neutral VR Animations for VRIK and PCEA2",
    "Controller Fix VR",
    "Realm of Lorkhan - Freeform Alternate Start",
    "Skyrim VR Tools",
    "SkyUI (REQUIRES Skyrim VR Tools!)",
    "OpenComposite_SkyUIVR_Fix_No_Search",
    "VRIK Default Controller Bindings",
    "Controller Bindings - Kvite",
)

ROOT_GAME_FILES = (
    "SkyrimVR.exe",
    "SkyrimSE.exe",
    "sksevr_loader.exe",
    "sksevr_1_4_15.dll",
    "sksevr_steam_loader.dll",
    "openvr_api.dll",
    "launch-skyrim-together-vr.sh",
    "launch-skyrim-vr-offline.sh",
    "stvr-xrizer-input-compat.sh",
    "SkyrimTogetherVR_BuildManifest.json",
)

DOWNLOAD_PATTERNS = (
    "HIGGS 1.10.10-*.zip",
    "PLANCK 0.8.0*.zip",
    "SkyrimVR FBT 185070*.zip",
    "SkyrimVR FBT Source 185070*.zip",
    "SkyrimVRTools-27782-*.zip",
    "ClibDT-154240-*.zip",
    "ClibDT (EXE)-154240-*.zip",
    "ClibDT (SOURCE)-154240-*.zip",
    "Visual Studio Import-154240-*.zip",
)

REFERENCE_SOURCES = {
    "devbench": "devbench",
    "SkyrimVRTools": "_refs/SkyrimVRTools",
    "VRCustomQuickslots": "_refs/VRCustomQuickslots",
    "PLANCK-activeragdoll": "_refs/activeragdoll",
    "HIGGS": "_refs/higgs",
    "CommonLibSSE-NG": "_refs/CommonLibSSE-NG",
    "CommonLibSSE-legacy": "_refs/CommonLibSSE-old",
    "CommonLibSSE-sample-plugin": "_refs/commonlibsse-sample-plugin",
    "SKSE-Menu-Framework-3": "_refs/SKSE-Menu-Framework-3",
    "PapyrusTweaks": "_refs/PapyrusTweaks",
}

EXTERNAL_REVIEW_NOTES = {
    "REVIEW-AGENT-HANDOFF.md": "REVIEW-AGENT-HANDOFF.md",
    "REVIEW-FRIEND-SUMMARY.md": "REVIEW-FRIEND-SUMMARY.md",
    "deep-research-report-1.md": "Backup/Downloads/deep-research-report(1).md",
}

EXCLUDED_DIRS = {
    ".git",
    "artifacts",
    "build",
    "target",
    "__pycache__",
    ".vs",
    ".vscode",
    "node_modules",
    "stvr-backups",
    "codex-disabled-connection-test",
    "codex-disabled-menu-mouse-fix",
}
EXCLUDED_SUFFIXES = {".pdb", ".lib", ".exp", ".log", ".dmp", ".pyc", ".obj", ".o"}
CORE_DATA_NAMES = {
    "skyrim.esm",
    "skyrimvr.esm",
    "update.esm",
    "dawnguard.esm",
    "hearthfires.esm",
    "dragonborn.esm",
}


def run_git(repo: pathlib.Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo), *args],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip())
    return result.stdout


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def skip(path: pathlib.Path, root: pathlib.Path) -> bool:
    parts = {part.lower() for part in path.relative_to(root).parts}
    return bool(parts & EXCLUDED_DIRS) or path.suffix.lower() in EXCLUDED_SUFFIXES


def iter_tree(root: pathlib.Path, *, devbench: bool = False) -> list[pathlib.Path]:
    files: list[pathlib.Path] = []
    for path in root.rglob("*"):
        if not path.is_file() or skip(path, root):
            continue
        if devbench and "lib" in {part.lower() for part in path.relative_to(root).parts}:
            continue
        files.append(path)
    return sorted(files, key=lambda item: item.relative_to(root).as_posix())


def iter_game_overlay(game_dir: pathlib.Path) -> list[pathlib.Path]:
    files: list[pathlib.Path] = []
    for name in ROOT_GAME_FILES:
        path = game_dir / name
        if not path.is_file():
            raise FileNotFoundError(f"missing required game-root file: {path}")
        files.append(path)

    data = game_dir / "Data"
    for path in data.rglob("*"):
        if not path.is_file() or skip(path, data):
            continue
        rel = path.relative_to(data)
        if len(rel.parts) == 1:
            lower = path.name.lower()
            if lower in CORE_DATA_NAMES or lower.endswith(".bsa") or lower.startswith("cc") or lower.startswith("_resourcepack"):
                continue
        files.append(path)
    return sorted(files, key=lambda item: item.as_posix())


class Writer:
    def __init__(self, archive: zipfile.ZipFile, timestamp: tuple[int, int, int, int, int, int]):
        self.archive = archive
        self.timestamp = timestamp
        self.records: list[dict[str, object]] = []
        self.names: set[str] = set()

    def add(self, source: pathlib.Path, name: str) -> None:
        name = name.replace(os.sep, "/")
        if name in self.names:
            raise ValueError(f"duplicate archive path: {name}")
        self.names.add(name)
        info = zipfile.ZipInfo(name, self.timestamp)
        info.create_system = 3
        info.external_attr = (0o100755 if source.stat().st_mode & 0o111 else 0o100644) << 16
        info.compress_type = zipfile.ZIP_STORED if source.suffix.lower() == ".zip" else zipfile.ZIP_DEFLATED
        digest = hashlib.sha256()
        size = 0
        with source.open("rb") as inp, self.archive.open(info, "w") as out:
            for chunk in iter(lambda: inp.read(1024 * 1024), b""):
                out.write(chunk)
                digest.update(chunk)
                size += len(chunk)
        self.records.append({"path": name, "size": size, "sha256": digest.hexdigest()})

    def add_bytes(self, payload: bytes, name: str) -> None:
        info = zipfile.ZipInfo(name, self.timestamp)
        info.create_system = 3
        info.external_attr = 0o100644 << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        self.archive.writestr(info, payload)


def parse_args() -> argparse.Namespace:
    home = pathlib.Path.home()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=pathlib.Path, default=pathlib.Path.cwd())
    parser.add_argument("--game-dir", type=pathlib.Path, default=home / "FasterGames/SteamLibrary/steamapps/common/SkyrimVR")
    parser.add_argument("--compatdata", type=pathlib.Path, default=home / "FasterGames/SteamLibrary/steamapps/compatdata/611670")
    parser.add_argument("--fus-root", type=pathlib.Path, default=home / "LargeGames/FUS")
    parser.add_argument("--downloads", type=pathlib.Path, default=home / "Backup/Downloads")
    parser.add_argument("--xrizer-root", type=pathlib.Path, default=home / ".local/share/envision/ovr_comp")
    parser.add_argument("--reference-root", type=pathlib.Path, default=home / "Documents/SkyrimModding")
    parser.add_argument("--public-assets", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo = pathlib.Path(run_git(args.repo.resolve(), "rev-parse", "--show-toplevel").strip())
    status = run_git(repo, "status", "--porcelain=v1", "--untracked-files=all").strip()
    if status:
        raise RuntimeError("commit or remove all repository changes before creating the local handoff")

    head = run_git(repo, "rev-parse", "HEAD").strip()
    epoch = int(run_git(repo, "show", "-s", "--format=%ct", head).strip())
    timestamp = datetime.fromtimestamp(epoch, timezone.utc).timetuple()[:6]
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%SZ")
    public_assets = args.public_assets or repo / "artifacts/SkyrimTogetherVR/prerelease/stvr-v0.1.0-alpha.1"
    output = args.output or repo / f"artifacts/SkyrimTogetherVR/review-handoff/SkyrimTogetherVR-local-agent-complete-handoff-{stamp}.zip"
    output.parent.mkdir(parents=True, exist_ok=True)
    root = output.stem

    public_runtime = public_assets / "SkyrimTogetherVR-stvr-v0.1.0-alpha.1-linux-monado-runtime.zip"
    public_handoff = public_assets / "SkyrimTogetherVR-stvr-v0.1.0-alpha.1-review-handoff.zip"
    for path in (public_runtime, public_handoff):
        if not path.is_file():
            raise FileNotFoundError(path)

    with tempfile.TemporaryDirectory(prefix="stvr-local-handoff-") as temp_dir:
        bundle = pathlib.Path(temp_dir) / "source.bundle"
        run_git(
            repo,
            "bundle",
            "create",
            str(bundle),
            "HEAD",
            "refs/heads/main",
            "refs/heads/original-skyrim-together",
            "refs/tags/stvr-v0.1.0-alpha.1",
        )

        with zipfile.ZipFile(output, "w", allowZip64=True) as archive:
            writer = Writer(archive, timestamp)
            writer.add(public_runtime, f"{root}/bundles/{public_runtime.name}")
            writer.add(public_handoff, f"{root}/bundles/{public_handoff.name}")
            writer.add(bundle, f"{root}/source.bundle")
            writer.add(repo / "Docs/SkyrimVR/local-agent-complete-handoff.md", f"{root}/START-HERE.md")

            tracked = run_git(repo, "ls-files", "--cached", "--recurse-submodules", "-z")
            for rel in sorted(item for item in tracked.split("\0") if item):
                path = repo / rel
                if path.is_file() and not skip(path, repo):
                    writer.add(path, f"{root}/source/{rel}")

            for mod_name in MOD_NAMES:
                mod_dir = args.fus_root / "mods" / mod_name
                if not mod_dir.is_dir():
                    raise FileNotFoundError(f"missing FUS dependency: {mod_dir}")
                for path in iter_tree(mod_dir):
                    rel = path.relative_to(mod_dir).as_posix()
                    writer.add(path, f"{root}/dependencies/fus-mods/{mod_name}/{rel}")

            for path in iter_game_overlay(args.game_dir):
                if path.is_relative_to(args.game_dir / "Data"):
                    rel = pathlib.Path("Data") / path.relative_to(args.game_dir / "Data")
                else:
                    rel = path.relative_to(args.game_dir)
                writer.add(path, f"{root}/dependencies/current-game-overlay/{rel.as_posix()}")

            xrizer_runtime = args.xrizer_root / "target/release"
            for name in ("libxrizer.so", "openvrpaths.vrpath"):
                writer.add(xrizer_runtime / name, f"{root}/dependencies/xrizer-runtime/{name}")
            for path in iter_tree(args.xrizer_root):
                rel = path.relative_to(args.xrizer_root).as_posix()
                writer.add(path, f"{root}/dependencies/source-references/XRizer/{rel}")

            for label, rel_root in REFERENCE_SOURCES.items():
                source_root = args.reference_root / rel_root
                if not source_root.is_dir():
                    raise FileNotFoundError(source_root)
                for path in iter_tree(source_root, devbench=label == "devbench"):
                    rel = path.relative_to(source_root).as_posix()
                    writer.add(path, f"{root}/dependencies/source-references/{label}/{rel}")

            for name, rel_path in EXTERNAL_REVIEW_NOTES.items():
                path = pathlib.Path.home() / rel_path
                if not path.is_file():
                    raise FileNotFoundError(path)
                writer.add(path, f"{root}/review-notes/{name}")

            for pattern in DOWNLOAD_PATTERNS:
                matches = sorted(args.downloads.glob(pattern))
                if not matches:
                    raise FileNotFoundError(f"missing supplied dependency archive matching {pattern}")
                for path in matches:
                    writer.add(path, f"{root}/dependencies/download-archives/{path.name}")

            prefix = args.compatdata / "pfx/drive_c/users/steamuser"
            direct_profile = {
                "Plugins.txt": prefix / "AppData/Local/Skyrim VR/Plugins.txt",
                "loadorder.txt": prefix / "AppData/Local/Skyrim VR/loadorder.txt",
                "SkyrimPrefs.ini": prefix / "Documents/My Games/Skyrim VR/SkyrimPrefs.ini",
            }
            for name, path in direct_profile.items():
                writer.add(path, f"{root}/profiles/direct-proton/{name}")
            fus_profile = args.fus_root / "profiles/FUS (Basic)"
            for path in iter_tree(fus_profile):
                writer.add(path, f"{root}/profiles/FUS (Basic)/{path.relative_to(fus_profile).as_posix()}")

            manifest = {
                "schema": "skyrim_together_vr_local_agent_handoff_v1",
                "generatedAtUtc": datetime.now(timezone.utc).isoformat(),
                "sourceHead": head,
                "localOnly": True,
                "serverEndpoint": "incidentalstoat.xyz:26099",
                "machinePaths": {
                    "repo": str(repo),
                    "game": str(args.game_dir),
                    "compatdata": str(args.compatdata),
                    "fus": str(args.fus_root),
                    "xrizer": str(args.xrizer_root),
                },
                "records": writer.records,
            }
            writer.add_bytes(
                (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8"),
                f"{root}/LOCAL-MANIFEST.json",
            )

    checksum = output.with_suffix(output.suffix + ".sha256.txt")
    checksum.write_text(f"{sha256(output)}  {output.name}\n", encoding="ascii")
    print(output)
    print(checksum)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
