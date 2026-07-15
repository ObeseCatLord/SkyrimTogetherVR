#!/usr/bin/env python3
"""Audit the private machine-local Skyrim Together VR agent handoff."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import tempfile
import zipfile

from local_handoff_artifacts import validate_artifact_pair


REQUIRED_PATHS = (
    "START-HERE.md",
    "source.bundle",
    "bundles/SkyrimTogetherVR-stvr-v0.1.0-alpha.1-linux-monado-runtime.zip",
    "bundles/SkyrimTogetherVR-stvr-v0.1.0-alpha.1-review-handoff.zip",
    "source/Docs/SkyrimVR/original-gameplay-parity-checklist.md",
    "dependencies/current-game-overlay/SkyrimVR.exe",
    "dependencies/current-game-overlay/sksevr_loader.exe",
    "dependencies/current-game-overlay/Data/SKSE/Plugins/devbench.dll",
    "dependencies/current-game-overlay/Data/SKSE/Plugins/devbench/config.json",
    "dependencies/current-game-overlay/Data/SKSE/Plugins/devbench/recordings/GuardianStonesToWhiterun.json",
    "dependencies/current-game-overlay/Data/SKSE/Plugins/SkyrimVR-FBT.dll",
    "dependencies/current-game-overlay/Data/SKSE/Plugins/higgs_vr.dll",
    "dependencies/current-game-overlay/Data/SKSE/Plugins/activeragdoll.dll",
    "dependencies/current-game-overlay/Data/SKSE/Plugins/VRIK.dll",
    "dependencies/current-game-overlay/Data/SKSE/Plugins/version-1-4-15-0.csv",
    "dependencies/xrizer-runtime/libxrizer.so",
    "profiles/direct-proton/Plugins.txt",
    "review-notes/REVIEW-AGENT-HANDOFF.md",
    "review-notes/REVIEW-FRIEND-SUMMARY.md",
    "review-notes/deep-research-report-1.md",
)

REQUIRED_PREFIXES = (
    "dependencies/source-references/devbench/src/",
    "dependencies/source-references/XRizer/src/",
    "dependencies/source-references/HIGGS/src/",
    "dependencies/source-references/PLANCK-activeragdoll/src/",
    "dependencies/source-references/CommonLibSSE-NG/include/",
    "dependencies/source-references/CommonLibSSE-sample-plugin/src/",
    "dependencies/source-references/SKSE-Menu-Framework-3/",
    "dependencies/source-references/PapyrusTweaks/",
    "dependencies/fus-mods/HIGGS - Hand Interaction and Gravity Gloves for Skyrim VR/",
    "dependencies/fus-mods/PLANCK - Physical Animation and Character Kinetics/",
    "dependencies/fus-mods/VRIK Player Avatar/",
    "dependencies/fus-mods/Controller Fix VR/",
    "dependencies/fus-mods/Realm of Lorkhan - Freeform Alternate Start/",
    "dependencies/download-archives/ClibDT (SOURCE)-",
    "dependencies/download-archives/ClibDT (EXE)-",
    "dependencies/download-archives/SkyrimVR FBT Source 185070",
    "dependencies/download-archives/SkyrimVRTools-27782-",
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

    try:
        archive = zipfile.ZipFile(args.archive)
    except (OSError, zipfile.BadZipFile) as error:
        print(f"audit failed\n - could not open handoff ZIP: {error}")
        return 1

    with archive:
        names = [name for name in archive.namelist() if not name.endswith("/")]
        for name in names:
            normalized = name.replace("\\", "/")
            canonical = pathlib.PurePosixPath(normalized).as_posix()
            if (
                normalized != name
                or name.startswith("/")
                or ".." in normalized.split("/")
                or canonical != normalized
            ):
                failures.append(f"unsafe archive path: {name}")
            if pathlib.PurePosixPath(name).suffix.lower() in {".pdb", ".log", ".dmp", ".lib", ".exp"}:
                failures.append(f"excluded developer/runtime artifact present: {name}")
        if archive.testzip() is not None:
            failures.append("ZIP CRC test failed")

        manifest_names = [name for name in names if name.endswith("/LOCAL-MANIFEST.json")]
        if len(manifest_names) != 1:
            failures.append("expected exactly one LOCAL-MANIFEST.json")
            manifest = {}
            root = ""
        else:
            root = manifest_names[0].removesuffix("/LOCAL-MANIFEST.json")
            try:
                manifest = json.loads(archive.read(manifest_names[0]).decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as error:
                failures.append(f"invalid local handoff manifest JSON: {error}")
                manifest = {}
            if not isinstance(manifest, dict):
                failures.append("local handoff manifest is not a JSON object")
                manifest = {}
        if root and any(not name.startswith(root + "/") for name in names):
            failures.append("archive payload is not contained under one root directory")
        if manifest.get("schema") != "skyrim_together_vr_local_agent_handoff_v1" or manifest.get("localOnly") is not True:
            failures.append("invalid local handoff manifest")

        records = manifest.get("records", [])
        if not isinstance(records, list) or any(not isinstance(record, dict) for record in records):
            failures.append("manifest records field is not a list of objects")
            records = []
        record_paths = [record.get("path") for record in records]
        if len(set(record_paths)) != len(record_paths):
            failures.append("manifest contains duplicate record paths")
        records_by_path = {record.get("path"): record for record in records}
        payload_names = set(names) - set(manifest_names)
        if set(record_paths) != payload_names:
            failures.append("manifest record set does not match archive payload")
        for record in records:
            path = record.get("path", "")
            if path not in payload_names:
                continue
            digest, size = hash_entry(archive, path)
            if digest != record.get("sha256") or size != record.get("size"):
                failures.append(f"manifest mismatch: {path}")

        relative_names = {name.removeprefix(root + "/") for name in names} if root else set()
        for required in REQUIRED_PATHS:
            if required not in relative_names:
                failures.append(f"missing required payload: {required}")
        for prefix in REQUIRED_PREFIXES:
            if not any(name.startswith(prefix) for name in relative_names):
                failures.append(f"missing required payload prefix: {prefix}")

        artifact_paths: dict[str, str] = {}
        for metadata_key in ("gameplayPackage", "buildEvidence"):
            metadata = manifest.get(metadata_key, {})
            if not isinstance(metadata, dict):
                failures.append(f"{metadata_key} metadata is not an object")
                continue
            artifact_name = metadata.get("name")
            artifact_hash = metadata.get("sha256")
            relative_path = f"build/{artifact_name}" if isinstance(artifact_name, str) else ""
            matching = f"{root}/{relative_path}" if root and relative_path in relative_names else ""
            if not artifact_name or not matching:
                failures.append(f"invalid {metadata_key} artifact reference")
                continue
            artifact_paths[metadata_key] = matching
            if records_by_path.get(matching, {}).get("sha256") != artifact_hash:
                failures.append(f"{metadata_key} hash does not match its payload record")

        build_revision = manifest.get("buildSourceRevision")
        source_head = manifest.get("sourceHead")
        if not isinstance(build_revision, str) or not isinstance(source_head, str):
            failures.append("manifest source/build revisions are missing")
        elif len(artifact_paths) == 2:
            with tempfile.TemporaryDirectory(prefix="stvr-handoff-audit-") as temp_dir:
                temp = pathlib.Path(temp_dir)
                package_path = temp / pathlib.PurePosixPath(artifact_paths["gameplayPackage"]).name
                evidence_path = temp / pathlib.PurePosixPath(artifact_paths["buildEvidence"]).name
                bundle_path = temp / "source.bundle"
                for archive_name, output_path in (
                    (artifact_paths["gameplayPackage"], package_path),
                    (artifact_paths["buildEvidence"], evidence_path),
                    (f"{root}/source.bundle", bundle_path),
                ):
                    with archive.open(archive_name) as source, output_path.open("wb") as output:
                        shutil.copyfileobj(source, output, length=1024 * 1024)
                try:
                    identity = validate_artifact_pair(package_path, evidence_path, build_revision)
                    for metadata_key in ("gameplayPackage", "buildEvidence"):
                        metadata = manifest[metadata_key]
                        for key, value in identity.items():
                            if metadata.get(key) != value:
                                failures.append(f"{metadata_key} {key} does not match nested artifacts")
                except (OSError, ValueError, zipfile.BadZipFile) as error:
                    failures.append(f"nested gameplay artifact audit failed: {error}")

                bare_repo = temp / "bundle.git"
                init = subprocess.run(
                    ["git", "init", "--bare", str(bare_repo)], capture_output=True, text=True, check=False
                )
                unbundle = subprocess.run(
                    ["git", "-C", str(bare_repo), "bundle", "unbundle", str(bundle_path)],
                    capture_output=True,
                    text=True,
                    check=False,
                ) if init.returncode == 0 else init
                relation = subprocess.run(
                    ["git", "-C", str(bare_repo), "merge-base", "--is-ancestor", build_revision, source_head],
                    capture_output=True,
                    text=True,
                    check=False,
                ) if unbundle.returncode == 0 else unbundle
                if init.returncode or unbundle.returncode or relation.returncode:
                    failures.append("source bundle does not contain the declared build-to-handoff revision ancestry")

    if failures:
        print("audit failed")
        for failure in failures:
            print(f" - {failure}")
        return 1
    print(f"audit passed: {len(records)} payload files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
