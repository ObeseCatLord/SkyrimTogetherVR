#!/usr/bin/env python3
"""Validate the paired gameplay artifacts embedded in a local agent handoff."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import zipfile


BUILD_MANIFEST_SCHEMA = "skyrim_together_vr_build_package_v2"
SHA256_RE = re.compile(r"[0-9a-f]{64}")
REQUIRED_EVIDENCE_COMMANDS = {
    "python-version",
    "xmake-version",
    "xmake-targets",
    "windows-build-audit",
    "built-package-audit-gameplay",
}


def _sha256_entry(archive: zipfile.ZipFile, name: str) -> str:
    digest = hashlib.sha256()
    with archive.open(name) as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _normalized_members(archive: zipfile.ZipFile) -> dict[str, str]:
    members: dict[str, str] = {}
    for raw_name in archive.namelist():
        if raw_name.endswith(("/", "\\")):
            continue
        name = raw_name.replace("\\", "/")
        canonical = pathlib.PurePosixPath(name).as_posix()
        if (
            name.startswith("/")
            or ".." in pathlib.PurePosixPath(name).parts
            or canonical != name
        ):
            raise ValueError(f"unsafe nested ZIP path: {raw_name}")
        if name in members:
            raise ValueError(f"duplicate normalized nested ZIP path: {name}")
        members[name] = raw_name
    return members


def _read_json(archive: zipfile.ZipFile, raw_name: str) -> dict[str, object]:
    try:
        value = json.loads(archive.read(raw_name).decode("utf-8-sig"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid JSON in nested ZIP entry {raw_name}: {error}") from error
    if not isinstance(value, dict):
        raise ValueError(f"nested ZIP entry {raw_name} is not a JSON object")
    return value


def _validate_build_manifest(manifest: dict[str, object], expected_revision: str) -> None:
    if manifest.get("schema") != BUILD_MANIFEST_SCHEMA:
        raise ValueError(f"unexpected gameplay build manifest schema: {manifest.get('schema')!r}")
    if manifest.get("packageFlavor") != "gameplay" or manifest.get("gameplay") is not True:
        raise ValueError("build manifest is not a gameplay package")
    revision = manifest.get("sourceRevision")
    if revision != expected_revision:
        raise ValueError(
            f"gameplay artifact source revision {revision!r} does not match {expected_revision}"
        )
    provenance = manifest.get("sourceProvenance")
    if not isinstance(provenance, dict) or provenance.get("dirty") is not False:
        raise ValueError("gameplay artifact does not report clean source provenance")
    if provenance.get("sourceRevision") != expected_revision:
        raise ValueError("gameplay artifact source provenance revision is inconsistent")


def validate_artifact_pair(
    package_path: pathlib.Path,
    evidence_path: pathlib.Path,
    expected_revision: str,
) -> dict[str, str]:
    """Validate ZIP integrity, package hashes, and shared build provenance."""

    if not zipfile.is_zipfile(package_path):
        raise ValueError(f"gameplay package is not a ZIP archive: {package_path}")
    if not zipfile.is_zipfile(evidence_path):
        raise ValueError(f"build evidence is not a ZIP archive: {evidence_path}")

    with zipfile.ZipFile(package_path) as package:
        bad_entry = package.testzip()
        if bad_entry is not None:
            raise ValueError(f"gameplay package CRC failure: {bad_entry}")
        package_members = _normalized_members(package)
        manifest_names = [
            name for name in package_members if name.endswith("/SkyrimTogetherVR_BuildManifest.json")
        ]
        if len(manifest_names) != 1:
            raise ValueError("gameplay package must contain exactly one build manifest")
        package_manifest_name = manifest_names[0]
        package_manifest = _read_json(package, package_members[package_manifest_name])
        _validate_build_manifest(package_manifest, expected_revision)

        package_root = package_manifest_name.removesuffix("SkyrimTogetherVR_BuildManifest.json")
        expected_hashes = package_manifest.get("packageFileSha256")
        if not isinstance(expected_hashes, dict) or not expected_hashes:
            raise ValueError("gameplay package manifest has no package file hash map")
        expected_members = {package_manifest_name}
        for relative, expected_hash in expected_hashes.items():
            if (
                not isinstance(relative, str)
                or not isinstance(expected_hash, str)
                or SHA256_RE.fullmatch(expected_hash) is None
            ):
                raise ValueError("gameplay package manifest has a malformed file hash entry")
            normalized = package_root + relative.replace("\\", "/")
            if pathlib.PurePosixPath(normalized).as_posix() != normalized:
                raise ValueError(f"gameplay package manifest has a non-canonical file path: {relative}")
            expected_members.add(normalized)
            raw_name = package_members.get(normalized)
            if raw_name is None:
                raise ValueError(f"gameplay package is missing manifest file: {relative}")
            if _sha256_entry(package, raw_name) != expected_hash:
                raise ValueError(f"gameplay package file hash mismatch: {relative}")
        extra_members = sorted(set(package_members) - expected_members)
        missing_members = sorted(expected_members - set(package_members))
        if extra_members or missing_members:
            raise ValueError(
                "gameplay package member set differs from its manifest"
                f" (extra={extra_members[:5]}, missing={missing_members[:5]})"
            )

    with zipfile.ZipFile(evidence_path) as evidence:
        bad_entry = evidence.testzip()
        if bad_entry is not None:
            raise ValueError(f"build evidence CRC failure: {bad_entry}")
        evidence_members = _normalized_members(evidence)
        for required in ("manifest.json", "package/SkyrimTogetherVR_BuildManifest.json"):
            if required not in evidence_members:
                raise ValueError(f"build evidence is missing {required}")
        evidence_manifest = _read_json(evidence, evidence_members["manifest.json"])
        if evidence_manifest.get("mode") != "gameplay" or evidence_manifest.get("packageExists") is not True:
            raise ValueError("build evidence does not describe a completed gameplay package")
        commands = evidence_manifest.get("commands")
        if not isinstance(commands, list) or not commands or any(
            not isinstance(command, dict) or command.get("exitCode") != 0 for command in commands
        ):
            raise ValueError("build evidence contains a missing or failed command result")
        command_names = {
            command.get("name") for command in commands if isinstance(command.get("name"), str)
        }
        missing_commands = sorted(REQUIRED_EVIDENCE_COMMANDS - command_names)
        if missing_commands:
            raise ValueError(f"build evidence is missing command results: {missing_commands}")
        evidence_package_manifest = _read_json(
            evidence, evidence_members["package/SkyrimTogetherVR_BuildManifest.json"]
        )
        _validate_build_manifest(evidence_package_manifest, expected_revision)
        canonical_package_manifest = json.dumps(
            package_manifest, sort_keys=True, separators=(",", ":"), ensure_ascii=True
        ).encode("ascii")
        canonical_evidence_manifest = json.dumps(
            evidence_package_manifest, sort_keys=True, separators=(",", ":"), ensure_ascii=True
        ).encode("ascii")
        if canonical_evidence_manifest != canonical_package_manifest:
            raise ValueError("gameplay package and build evidence contain different build manifests")

    return {
        "sourceRevision": expected_revision,
        "buildManifestSha256": hashlib.sha256(canonical_package_manifest).hexdigest(),
        "generatedAtUtc": str(package_manifest.get("generatedAtUtc", "")),
    }
