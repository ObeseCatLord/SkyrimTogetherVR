#!/usr/bin/env python3
"""Audit SkyrimTogetherVR prerelease runtime and review-handoff archives."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

PREFIX = "SkyrimTogetherVR"
BUILD_MANIFEST_NAME = "SkyrimTogetherVR_BuildManifest.json"
RUNTIME_EXCLUDE_EXTS = {".pdb", ".lib", ".exp", ".log"}
SOURCE_EXCLUDE_EXTS = {".pdb", ".lib", ".exp", ".exe", ".dll", ".o", ".obj", ".a", ".so", ".dylib"}

FORBIDDEN_SOURCE_STRINGS = (
    "skyrimvr.exe",
    "skyrimvr.dll",
)
FORBIDDEN_TEXT_EXTS = {
    ".md",
    ".txt",
    ".json",
    ".yaml",
    ".yml",
    ".ini",
    ".cfg",
    ".toml",
    ".toml",
    ".ps1",
    ".bat",
    ".sh",
    ".cpp",
    ".h",
}

SECRET_FILENAME_HINTS = (
    ".env",
    "credentials",
    "secrets",
    "private-key",
    "private_key",
    "id_rsa",
    ".pem",
    ".pfx",
)

SECRET_PATTERNS = (
    re.compile(rb"(?i)api[_-]?key\s*[:=]\s*[\"'][A-Za-z0-9_\-]{20,}[\"']"),
    re.compile(rb"(?i)secret\s*[:=]\s*[\"'][A-Za-z0-9_\-]{20,}[\"']"),
    re.compile(rb"(?i)bearer\s+[A-Za-z0-9_\-\.]+"),
    re.compile(rb"AKIA[0-9A-Z]{16}"),
    re.compile(rb"ghp_[A-Za-z0-9]{36}"),
    re.compile(rb"-----BEGIN (?:[A-Z ]+ )?PRIVATE KEY-----"),
)

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Audit SkyrimTogetherVR prerelease bundle outputs.")
    parser.add_argument("--asset-dir", type=Path, default=Path.cwd())
    parser.add_argument("--runtime-archive", type=Path)
    parser.add_argument("--review-archive", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--expect-version", default="")
    parser.add_argument("--require-server-endpoint")
    return parser.parse_args()


def read_json(path: Path) -> object:
    return json.loads(path.read_text(encoding="utf-8"))


def hash_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def hash_zip_entry(zf: zipfile.ZipFile, name: str) -> tuple[str, int]:
    digest = hashlib.sha256()
    size = 0
    with zf.open(name, "r") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
            size += len(chunk)
    return digest.hexdigest(), size


def hash_file_path(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def check_zip_name_safety(names: list[str], label: str, failures: list[str]) -> set[str]:
    seen: set[str] = set()
    for name in names:
        if name in seen:
            failures.append(f"{label}: duplicate zip entry {name}")
        seen.add(name)
        if name.startswith("/") or name.startswith("\\"):
            failures.append(f"{label}: absolute zip entry {name}")
            continue
        normalized = name.replace("\\", "/")
        if normalized != name:
            failures.append(f"{label}: backslash path separator in {name}")
        if ".." in normalized.split("/"):
            failures.append(f"{label}: traversal path in {name}")
        if ":" in normalized[:3] and normalized[1:3] == ":/":
            failures.append(f"{label}: windows absolute path {name}")
    return seen


def locate_default_archives(asset_dir: Path, version: str) -> tuple[Path | None, Path | None]:
    runtime_glob = f"{PREFIX}-*-runtime.zip" if not version else f"{PREFIX}-{version}-*-runtime.zip"
    review_glob = f"{PREFIX}-*-review-handoff.zip" if not version else f"{PREFIX}-{version}-review-handoff.zip"
    runtimes = sorted(asset_dir.glob(runtime_glob))
    reviews = sorted(asset_dir.glob(review_glob))
    if len(runtimes) == 1 and len(reviews) == 1:
        return runtimes[0], reviews[0]
    return (runtimes[0] if len(runtimes) == 1 else None), (reviews[0] if len(reviews) == 1 else None)


def inspect_archive(path: Path, failures: list[str], label: str) -> tuple[zipfile.ZipFile, list[str], list[str]]:
    if not path.is_file():
        failures.append(f"{label}: missing archive {path}")
        return None, [], []
    try:
        zf = zipfile.ZipFile(path, "r")
    except OSError as exc:
        failures.append(f"{label}: cannot open zip {path}: {exc}")
        return None, [], []

    names = zf.namelist()
    names = [name for name in names if not name.endswith("/")]
    check_zip_name_safety(names, label, failures)
    return zf, names, [name for name in names if name.startswith(PREFIX + "-")]

def collect_manifest_from_archive(zf: zipfile.ZipFile, names: list[str]) -> dict | None:
    for candidate in (name for name in names if name.endswith("/" + BUILD_MANIFEST_NAME) or name == BUILD_MANIFEST_NAME):
        try:
            return json.loads(zf.read(candidate).decode("utf-8"))
        except Exception:
            return None
    return None


def find_bundle_ref(refs: set[str], expected: str) -> bool:
    short = expected.split("/", 2)[-1]
    return expected in refs or short in refs


def scan_secrets(path: Path | None, name: str, data: bytes, failures: list[str]) -> None:
    lower_name = name.lower()
    if any(hit in lower_name for hit in SECRET_FILENAME_HINTS):
        failures.append(f"{path if path else 'zip'}: suspicious secret-like filename {name}")
        return
    if not any(lower_name.endswith(ext) for ext in FORBIDDEN_TEXT_EXTS):
        return
    if b"\x00" in data:
        return
    for pattern in SECRET_PATTERNS:
        if pattern.search(data):
            failures.append(f"{path if path else 'zip'}: secret pattern in {name}")
            return
def check_runtime_archive(runtime_zip: Path, manifest: dict, failures: list[str]) -> dict | None:
    zf, names, _ = inspect_archive(runtime_zip, failures, "runtime")
    if zf is None:
        return None

    runtime_root = next((name.split("/", 1)[0] for name in names if name.startswith(PREFIX + "-") and "-" in name), None)
    if runtime_root is None and names:
        runtime_root = names[0].split("/")[0]

    for name in names:
        norm = name.lower()
        if name.startswith((".git", "__pycache__")):
            failures.append(f"runtime: forbidden path {name}")
        if Path(name).suffix.lower() in RUNTIME_EXCLUDE_EXTS and Path(name).name != "SkyrimTogetherVR.exe":
            failures.append(f"runtime: developer artifact {name}")
        if Path(norm).name in {"skyrimvr.exe", "sksevr_loader.exe", "sksevr_steam_loader.dll"}:
            failures.append(f"runtime: proprietary or third-party prerequisite included: {name}")
        if "/data/skse/plugins/" in norm and norm.endswith(".dll"):
            if not Path(norm).name.startswith("skyrimtogethervr"):
                failures.append(f"runtime: non-project SKSE plugin included: {name}")
        if any(hit in norm for hit in SECRET_FILENAME_HINTS) or any(
            norm.endswith(ext) for ext in FORBIDDEN_TEXT_EXTS
        ):
            scan_secrets(runtime_zip, name, zf.read(name), failures)

    runtime_payload = manifest.get("runtime", {})
    required = set(runtime_payload.get("requiredRuntimeFiles", []))
    required |= set(runtime_payload.get("requiredCefFiles", []))
    required |= set(runtime_payload.get("requiredLicenseFiles", []))
    required |= set(runtime_payload.get("requiredDocFiles", []))
    required |= set(runtime_payload.get("requiredScriptFiles", []))
    required |= set(runtime_payload.get("requiredPatchFiles", []))
    required |= set(runtime_payload.get("requiredManifestFiles", []))
    for req in sorted(required):
        candidates = [name for name in names if name == req]
        if not candidates:
            failures.append(f"runtime: required file missing in runtime archive: {req}")

    for record in runtime_payload.get("records", []):
        path = record.get("path", "")
        if path not in names:
            failures.append(f"runtime: manifest record missing from archive: {path}")
            continue
        payload_hash, payload_size = hash_zip_entry(zf, path)
        if record.get("size") != payload_size:
            failures.append(f"runtime: size mismatch for {path}")
        if record.get("sha256") != payload_hash:
            failures.append(f"runtime: hash mismatch for {path}")
    recorded_paths = {record.get("path", "") for record in runtime_payload.get("records", [])}
    unrecorded_paths = sorted(set(names) - recorded_paths)
    for path in unrecorded_paths:
        failures.append(f"runtime: archive entry missing from manifest records: {path}")

    # optional review-handoff/runtime mismatch check
    observed = hash_file_path(str(runtime_zip))
    if runtime_payload.get("sha256") and runtime_payload["sha256"] != observed:
        failures.append("runtime: manifest sha mismatch for runtime archive")
    if runtime_payload.get("size") and runtime_payload["size"] != runtime_zip.stat().st_size:
        failures.append("runtime: manifest size mismatch for runtime archive")
    zf.close()
    return {
        "hash": observed,
        "name": runtime_zip.name,
        "root": runtime_root,
    }


def check_review_archive(
    review_zip: Path,
    runtime_meta: dict | None,
    manifest: dict,
    require_server_endpoint: str | None,
    failures: list[str],
) -> None:
    zf, names, _ = inspect_archive(review_zip, failures, "review")
    if zf is None:
        return

    manifest_review = manifest.get("reviewHandoff", {})
    expected_runtime = manifest.get("runtime", {}).get("path")
    runtime_candidates = [name for name in names if name.endswith("-runtime.zip")]
    if expected_runtime:
        runtime_candidates = [name for name in runtime_candidates if name == expected_runtime or name.endswith("/" + expected_runtime)]

    if not runtime_candidates:
        failures.append("review: runtime zip nested in review archive is missing")
    elif len(runtime_candidates) > 1:
        failures.append("review: runtime zip appears more than once in review archive")
    else:
        runtime_entry = runtime_candidates[0]
        review_runtime_hash, _ = hash_zip_entry(zf, runtime_entry)
        if runtime_meta and runtime_meta.get("hash") and runtime_meta["hash"] != review_runtime_hash:
            failures.append("review: nested runtime hash does not match standalone runtime archive")
        expected_hash = manifest.get("runtime", {}).get("sha256")
        if expected_hash and expected_hash != review_runtime_hash:
            failures.append("review: nested runtime hash does not match manifest")

    source_bundle_entry = manifest_review.get("sourceBundle", {}).get("path")
    if source_bundle_entry and source_bundle_entry not in names:
        failures.append(f"review: source bundle missing at {source_bundle_entry}")
    elif source_bundle_entry is None:
        source_bundle_entry = next((name for name in names if name.endswith("/source.bundle") or name.endswith("source.bundle")), None)
        if source_bundle_entry is None:
            failures.append("review: source bundle missing")

    if source_bundle_entry and source_bundle_entry in names:
        source_bundle_data = zf.read(source_bundle_entry)
        source_bundle_hash = hash_bytes(source_bundle_data)
        bundle_meta = manifest_review.get("sourceBundle", {})
        if bundle_meta.get("sha256") and bundle_meta["sha256"] != source_bundle_hash:
            failures.append("review: source bundle hash mismatch with manifest")
        if bundle_meta.get("size") and bundle_meta["size"] != len(source_bundle_data):
            failures.append("review: source bundle size mismatch with manifest")
        if bundle_meta.get("refs"):
            refs = set(bundle_meta.get("refs", []))
        else:
            refs = set()
        with tempfile.TemporaryDirectory() as tmpdir:
            bundle_path = Path(tmpdir) / "source.bundle"
            bundle_path.write_bytes(source_bundle_data)
            verify = subprocess.run(
                ["git", "bundle", "verify", str(bundle_path)],
                check=False,
                capture_output=True,
                text=True,
            )
            if verify.returncode != 0:
                failures.append(f"review: git bundle verify failed ({verify.stderr.strip()})")
            else:
                listed = subprocess.run(
                    ["git", "bundle", "list-heads", str(bundle_path)],
                    check=False,
                    capture_output=True,
                    text=True,
                )
                found_refs = {line.rsplit(" ", 1)[-1] for line in listed.stdout.splitlines() if line}
                if listed.returncode != 0:
                    failures.append("review: git bundle list-heads failed")
                else:
                    if not find_bundle_ref(found_refs, "refs/heads/main"):
                        failures.append("review: bundle missing main branch")
                    if not find_bundle_ref(found_refs, "refs/heads/original-skyrim-together"):
                        failures.append("review: bundle missing original-skyrim-together branch")
                    version = str(manifest.get("version", ""))
                    if version and not find_bundle_ref(found_refs, f"refs/tags/{version}"):
                        failures.append(f"review: bundle missing release tag {version}")
                if refs:
                    if not find_bundle_ref(refs, "main"):
                        failures.append("manifest: sourceBundle.refs missing main")
                    if not find_bundle_ref(refs, "original-skyrim-together"):
                        failures.append("manifest: sourceBundle.refs missing original-skyrim-together")
                    version = str(manifest.get("version", ""))
                    if version and not find_bundle_ref(refs, version):
                        failures.append(f"manifest: sourceBundle.refs missing release tag {version}")

    source_root_candidates = [name for name in names if "source/" in name or "source\\" in name]
    source_files = [name for name in names if "/source/" in name or "\\source\\" in name]
    if not source_files:
        failures.append("review: source snapshot appears missing")
    for name in source_files:
        lower = name.lower().replace("\\", "/")
        lower_name = lower.rsplit("/", 1)[-1]
        suffix = Path(lower).suffix
        if suffix == ".exe" and lower_name != "skyrimvr.exe":
            failures.append(f"review: prohibited source executable {name}")
        elif suffix in SOURCE_EXCLUDE_EXTS:
            failures.append(f"review: forbidden source extension {name}")
        if any(forbidden in lower for forbidden in FORBIDDEN_SOURCE_STRINGS):
            failures.append(f"review: prohibited source payload {name}")
        if "/data/skse/plugins/" in lower and lower.endswith(".dll"):
            failures.append(f"review: prohibited source SKSE DLL {name}")

        if any(hit in lower for hit in SECRET_FILENAME_HINTS):
            failures.append(f"review: suspicious filename in source snapshot: {name}")
        data = zf.read(name)
        scan_secrets(review_zip, name, data, failures)

    runtime_entry_payload = manifest.get("runtime", {})
    for expected in ("path", "size", "sha256"):
        if expected not in runtime_entry_payload:
            failures.append(f"review: manifest missing runtime.{expected}")

    build_manifest = collect_manifest_from_archive(zf, names)
    if build_manifest is None:
        failures.append("review: missing embedded build manifest")
    else:
        release_source_revision = manifest.get("binarySourceRevision", "")
        payload_rev = str(
            build_manifest.get("sourceRevision")
            or build_manifest.get("sourceProvenance", {}).get("sourceRevision", "")
        )
        if release_source_revision and payload_rev and release_source_revision != payload_rev:
            failures.append("review: build manifest sourceRevision mismatch with RELEASE-MANIFEST")

    embedded_release_entries = [name for name in names if name.endswith("/RELEASE-MANIFEST.json")]
    if len(embedded_release_entries) != 1:
        failures.append("review: expected exactly one embedded RELEASE-MANIFEST.json")
    else:
        try:
            embedded_release = json.loads(zf.read(embedded_release_entries[0]).decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            failures.append("review: embedded RELEASE-MANIFEST.json is invalid")
        else:
            for key in ("version", "sourceHead", "binarySourceRevision", "testedScope"):
                if embedded_release.get(key) != manifest.get(key):
                    failures.append(f"review: embedded manifest {key} differs from standalone manifest")
            if embedded_release.get("runtime", {}).get("sha256") != manifest.get("runtime", {}).get("sha256"):
                failures.append("review: embedded runtime hash differs from standalone manifest")

    for name in names:
        if source_root_candidates:
            if name == "" or name.endswith("/"):
                continue
            data = zf.read(name)
            scan_secrets(review_zip, name, data, failures)

    if manifest.get("testedScope") != "connection-only":
        failures.append("manifest: testedScope is not connection-only")
    if require_server_endpoint is not None and manifest.get("publicFacts", {}).get("serverEndpoint") != require_server_endpoint:
        failures.append("manifest: public server endpoint does not match expectation")

    if manifest.get("sourceHead", "") == "" or manifest.get("binarySourceRevision", "") == "":
        failures.append("manifest: missing sourceHead/source manifest revision")
    zf.close()


def ensure_manifest(path: Path, failures: list[str]) -> dict:
    if not path.is_file():
        failures.append(f"manifest missing at {path}")
        return {}
    try:
        data = read_json(path)
        if not isinstance(data, dict):
            failures.append(f"manifest not a JSON object: {path}")
            return {}
        return data
    except Exception as exc:
        failures.append(f"manifest parse failed: {exc}")
        return {}


def main() -> int:
    global args
    args = parse_args()
    failures: list[str] = []

    version = args.expect_version.strip()
    runtime_path = args.runtime_archive
    review_path = args.review_archive
    if runtime_path is None or review_path is None:
        auto_runtime, auto_review = locate_default_archives(args.asset_dir, version)
        if runtime_path is None:
            runtime_path = auto_runtime
        if review_path is None:
            review_path = auto_review
        if runtime_path is None:
            failures.append("could not auto-detect runtime archive")
        if review_path is None:
            failures.append("could not auto-detect review handoff archive")

    manifest_path = args.manifest or (args.asset_dir / "RELEASE-MANIFEST.json")
    manifest = ensure_manifest(manifest_path, failures)
    if manifest and manifest.get("schema") != "skyrim_together_vr_prerelease_manifest_v1":
        failures.append("manifest: unexpected schema")
    if manifest and version and manifest.get("version") != version:
        failures.append(f"manifest: version is {manifest.get('version')!r}, expected {version!r}")
    if not failures:
        runtime_meta = check_runtime_archive(runtime_path, manifest, failures)
        check_review_archive(review_path, runtime_meta, manifest, args.require_server_endpoint, failures)

    if failures:
        print("audit failed:", file=sys.stderr)
        for issue in failures:
            print(f" - {issue}", file=sys.stderr)
        return 1

    print("audit passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
