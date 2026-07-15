#!/usr/bin/env python3
"""Audit the private machine-local Skyrim Together VR agent handoff."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import zipfile


REQUIRED_FRAGMENTS = (
    "/START-HERE.md",
    "/LOCAL-MANIFEST.json",
    "/source.bundle",
    "/bundles/SkyrimTogetherVR-stvr-v0.1.0-alpha.1-linux-monado-runtime.zip",
    "/bundles/SkyrimTogetherVR-stvr-v0.1.0-alpha.1-review-handoff.zip",
    "/dependencies/current-game-overlay/SkyrimVR.exe",
    "/dependencies/current-game-overlay/sksevr_loader.exe",
    "/dependencies/current-game-overlay/Data/SKSE/Plugins/devbench.dll",
    "/dependencies/current-game-overlay/Data/SKSE/Plugins/devbench/config.json",
    "/dependencies/current-game-overlay/Data/SKSE/Plugins/devbench/recordings/GuardianStonesToWhiterun.json",
    "/dependencies/current-game-overlay/Data/SKSE/Plugins/SkyrimVR-FBT.dll",
    "/dependencies/current-game-overlay/Data/SKSE/Plugins/higgs_vr.dll",
    "/dependencies/current-game-overlay/Data/SKSE/Plugins/activeragdoll.dll",
    "/dependencies/current-game-overlay/Data/SKSE/Plugins/VRIK.dll",
    "/dependencies/current-game-overlay/Data/SKSE/Plugins/version-1-4-15-0.csv",
    "/dependencies/xrizer-runtime/libxrizer.so",
    "/dependencies/source-references/devbench/src/",
    "/dependencies/source-references/XRizer/src/",
    "/dependencies/source-references/HIGGS/src/",
    "/dependencies/source-references/PLANCK-activeragdoll/src/",
    "/dependencies/source-references/CommonLibSSE-NG/include/",
    "/dependencies/source-references/CommonLibSSE-sample-plugin/src/",
    "/dependencies/source-references/SKSE-Menu-Framework-3/",
    "/dependencies/source-references/PapyrusTweaks/",
    "/dependencies/fus-mods/HIGGS - Hand Interaction and Gravity Gloves for Skyrim VR/",
    "/dependencies/fus-mods/PLANCK - Physical Animation and Character Kinetics/",
    "/dependencies/fus-mods/VRIK Player Avatar/",
    "/dependencies/fus-mods/Controller Fix VR/",
    "/dependencies/fus-mods/Realm of Lorkhan - Freeform Alternate Start/",
    "/profiles/direct-proton/Plugins.txt",
    "/dependencies/download-archives/ClibDT (SOURCE)-",
    "/dependencies/download-archives/ClibDT (EXE)-",
    "/dependencies/download-archives/SkyrimVR FBT Source 185070",
    "/dependencies/download-archives/SkyrimVRTools-27782-",
    "/review-notes/REVIEW-AGENT-HANDOFF.md",
    "/review-notes/REVIEW-FRIEND-SUMMARY.md",
    "/review-notes/deep-research-report-1.md",
)


def hash_entry(archive: zipfile.ZipFile, name: str) -> tuple[str, int]:
    digest = hashlib.sha256()
    size = 0
    with archive.open(name) as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
            size += len(chunk)
    return digest.hexdigest(), size


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=pathlib.Path)
    args = parser.parse_args()
    failures: list[str] = []

    with zipfile.ZipFile(args.archive) as archive:
        names = [name for name in archive.namelist() if not name.endswith("/")]
        for name in names:
            normalized = name.replace("\\", "/")
            if normalized != name or name.startswith("/") or ".." in normalized.split("/"):
                failures.append(f"unsafe archive path: {name}")
            if pathlib.PurePosixPath(name).suffix.lower() in {".pdb", ".log", ".dmp", ".lib", ".exp"}:
                failures.append(f"excluded developer/runtime artifact present: {name}")
        if archive.testzip() is not None:
            failures.append("ZIP CRC test failed")

        manifest_names = [name for name in names if name.endswith("/LOCAL-MANIFEST.json")]
        if len(manifest_names) != 1:
            failures.append("expected exactly one LOCAL-MANIFEST.json")
            manifest = {}
        else:
            manifest = json.loads(archive.read(manifest_names[0]).decode("utf-8"))
        if manifest.get("schema") != "skyrim_together_vr_local_agent_handoff_v1" or manifest.get("localOnly") is not True:
            failures.append("invalid local handoff manifest")

        records = manifest.get("records", [])
        record_paths = {record.get("path") for record in records}
        payload_names = set(names) - set(manifest_names)
        if record_paths != payload_names:
            failures.append("manifest record set does not match archive payload")
        for record in records:
            path = record.get("path", "")
            if path not in payload_names:
                continue
            digest, size = hash_entry(archive, path)
            if digest != record.get("sha256") or size != record.get("size"):
                failures.append(f"manifest mismatch: {path}")

        for fragment in REQUIRED_FRAGMENTS:
            if not any(fragment in name for name in names):
                failures.append(f"missing required payload: {fragment}")

    if failures:
        print("audit failed")
        for failure in failures:
            print(f" - {failure}")
        return 1
    print(f"audit passed: {len(records)} payload files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
