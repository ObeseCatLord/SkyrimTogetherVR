#!/usr/bin/env python3
"""Create reproducible SkyrimTogetherVR prerelease runtime and review-handoff assets."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import zipfile
from datetime import datetime, timezone


DEFAULT_VERSION = "stvr-v0.1.0-alpha.1"
PREFIX = "SkyrimTogetherVR"
BUILD_MANIFEST_NAME = "SkyrimTogetherVR_BuildManifest.json"
MANIFEST_SCHEMA = "skyrim_together_vr_prerelease_manifest_v1"
DEFAULT_SERVER_ENDPOINT = "incidentalstoat.xyz:26099"

RUNTIME_EXCLUDE_DIR_PARTS = {
    ".git",
    "build",
    "artifacts",
    "build-evidence",
    "runtime-evidence",
    "review-handoff",
    "__pycache__",
    ".venv",
}
RUNTIME_EXCLUDE_EXTS = {".pdb", ".lib", ".exp", ".log"}

SOURCE_EXCLUDE_DIR_PARTS = {
    ".git",
    "artifacts",
    "build",
    "runtime-evidence",
    "review-handoff",
    "__pycache__",
    "compiled",
    ".venv",
}
SOURCE_EXCLUDE_EXTS = {
    ".pdb",
    ".lib",
    ".exp",
    ".log",
    ".exe",
    ".dll",
    ".so",
    ".dylib",
    ".a",
    ".o",
    ".obj",
    ".msi",
    ".zip",
    ".7z",
}

REQUIRED_RUNTIME_FILES = (
    "SkyrimTogetherVR.exe",
    "EarlyLoad.dll",
    "TPProcess.exe",
    "Data/SKSE/Plugins/SkyrimTogetherVRVrikBridge.dll",
    "Data/SKSE/Plugins/SkyrimTogetherVRHiggsBridge.dll",
    "Data/SKSE/Plugins/SkyrimTogetherVRPlanckBridge.dll",
    "Data/SKSE/Plugins/SkyrimTogetherVRTickBridge.dll",
    "Data/SKSE/Plugins/SkyrimTogetherVRGameplayBridge.dll",
)

REQUIRED_CEF_FILES = (
    "chrome_elf.dll",
    "d3dcompiler_47.dll",
    "dxcompiler.dll",
    "dxil.dll",
    "icudtl.dat",
    "libcef.dll",
    "libEGL.dll",
    "libGLESv2.dll",
    "locales/en-US.pak",
    "resources.pak",
    "chrome_100_percent.pak",
    "chrome_200_percent.pak",
    "vk_swiftshader.dll",
    "vulkan-1.dll",
)

RUNTIME_DOCS = (
    "Docs/SkyrimVR/connection-only-mode.md",
    "Docs/SkyrimVR/linux-monado-prerelease-guide.md",
    "Docs/SkyrimVR/prerelease-versioning.md",
    "Docs/SkyrimVR/server-deployment.md",
    "Docs/SkyrimVR/devbench-new-game.md",
    "Docs/SkyrimVR/releases/stvr-v0.1.0-alpha.1.md",
    "Docs/SkyrimVR/windows-agent-build-guide.md",
    "Docs/SkyrimVR/windows-build.md",
)

RUNTIME_SCRIPTS = (
    "Tools/SkyrimVR/linux/launch-skyrim-together-vr.sh",
    "Tools/SkyrimVR/linux/launch-skyrim-vr-offline.sh",
    "Tools/SkyrimVR/linux/stvr-xrizer-input-compat.sh",
)

RUNTIME_PATCHES = (
    "Tools/SkyrimVR/monado-qwerty-automation.patch",
    "Tools/SkyrimVR/xrizer-skyrimvr-monado.patch",
)

REVIEW_DOCS = (
    "Docs/SkyrimVR/connection-only-mode.md",
    "Docs/SkyrimVR/linux-monado-prerelease-guide.md",
    "Docs/SkyrimVR/prerelease-versioning.md",
    "Docs/SkyrimVR/review-handoff-introduction.md",
    "Docs/SkyrimVR/server-deployment.md",
    "Docs/SkyrimVR/releases/stvr-v0.1.0-alpha.1.md",
    "Docs/SkyrimVR/windows-agent-build-guide.md",
)


def run_git(repo: pathlib.Path, args: list[str]) -> str:
    cp = subprocess.run(
        ["git", "-C", str(repo), *args],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if cp.returncode != 0:
        detail = cp.stderr.strip() or cp.stdout.strip()
        raise RuntimeError(f"git {' '.join(args)} failed: {detail}")
    return cp.stdout


def resolve_repo(repo: pathlib.Path | None) -> pathlib.Path:
    candidate = (repo or pathlib.Path.cwd()).expanduser().resolve()
    try:
        return pathlib.Path(run_git(candidate, ["rev-parse", "--show-toplevel"]).strip())
    except RuntimeError:
        return candidate


def source_head(repo: pathlib.Path) -> str:
    return run_git(repo, ["rev-parse", "HEAD"]).strip()


def source_epoch(repo: pathlib.Path, head: str) -> int:
    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    if epoch is not None:
        return max(0, int(epoch))
    return int(run_git(repo, ["show", "-s", "--format=%ct", head]).strip())


def zip_timestamp(epoch: int) -> tuple[int, int, int, int, int, int]:
    dt = datetime.fromtimestamp(epoch, tz=timezone.utc)
    if dt.year < 1980:
        dt = datetime(1980, 1, 1, tzinfo=timezone.utc)
    return dt.timetuple()[:6]


def should_skip(path: pathlib.Path, root: pathlib.Path, skip_dirs: set[str], skip_exts: set[str]) -> bool:
    rel_parts = path.relative_to(root).parts
    lowered_parts = [part.lower() for part in rel_parts]
    if any(part == "evidence" for part in lowered_parts):
        return True
    if any(part in skip_dirs for part in lowered_parts):
        return True
    if path.suffix.lower() in skip_exts:
        return True
    return False


def hash_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def list_file_records(repo: pathlib.Path) -> list[pathlib.Path]:
    try:
        data = run_git(repo, ["ls-files", "--cached", "--recurse-submodules", "-z"])
        values = [path for path in data.split("\x00") if path]
        return [repo / path for path in values]
    except RuntimeError:
        return []


def iter_build_files(build_dir: pathlib.Path, build_manifest: dict) -> list[pathlib.Path]:
    declared = build_manifest.get("packageFileSha256")
    if not isinstance(declared, dict) or not declared:
        raise ValueError("BuildManifest packageFileSha256 is missing or empty")

    forbidden_names = {"skyrimvr.exe", "sksevr_loader.exe", "sksevr_steam_loader.dll"}
    items: list[pathlib.Path] = []
    included_relpaths: set[str] = set()
    for rel, expected_hash in sorted(declared.items()):
        if not isinstance(rel, str) or not isinstance(expected_hash, str):
            raise ValueError("BuildManifest packageFileSha256 contains a malformed entry")
        path = build_dir / pathlib.PurePosixPath(rel)
        if should_skip(path, build_dir, RUNTIME_EXCLUDE_DIR_PARTS, RUNTIME_EXCLUDE_EXTS):
            continue
        if path.name.lower() in forbidden_names:
            raise ValueError(f"proprietary or third-party prerequisite declared in build package: {rel}")
        if not path.is_file():
            raise FileNotFoundError(f"declared build package file is missing: {path}")
        actual_hash = hash_file(path)
        if actual_hash.lower() != expected_hash.lower():
            raise ValueError(f"build package hash mismatch: {rel}")
        items.append(path)
        included_relpaths.add(path.relative_to(build_dir).as_posix())

    for path in build_dir.rglob("*"):
        if not path.is_file() or path.name == BUILD_MANIFEST_NAME:
            continue
        if should_skip(path, build_dir, RUNTIME_EXCLUDE_DIR_PARTS, RUNTIME_EXCLUDE_EXTS):
            continue
        rel = path.relative_to(build_dir).as_posix()
        if rel not in included_relpaths:
            raise ValueError(f"undeclared file in build package: {rel}")

    return sorted(items, key=lambda item: item.as_posix())


def sanitize_build_manifest(value: object) -> object:
    if isinstance(value, dict):
        return {key: sanitize_build_manifest(item) for key, item in value.items()}
    if isinstance(value, list):
        return [sanitize_build_manifest(item) for item in value]
    if isinstance(value, str):
        value = re.sub(r"(?i)[A-Z]:\\Users\\[^\\]+", r"C:\\Users\\USER", value)
        value = re.sub(r"/home/[^/]+", "/home/USER", value)
    return value


def require_clean_tagged_tree(repo: pathlib.Path, version: str) -> None:
    status = run_git(repo, ["status", "--porcelain=v1", "--untracked-files=all"]).strip()
    if status:
        raise RuntimeError("release packaging requires a clean, fully tracked working tree")
    head = source_head(repo)
    tag_commit = run_git(repo, ["rev-parse", f"refs/tags/{version}^{{commit}}"]).strip()
    if tag_commit != head:
        raise RuntimeError(f"release tag {version} does not point to HEAD")


def collect_runtime_aux(repo: pathlib.Path, build_dir: pathlib.Path, cef_license: pathlib.Path | None) -> list[pathlib.Path]:
    candidates: list[pathlib.Path] = []
    for rel in (*RUNTIME_DOCS, *RUNTIME_SCRIPTS, *RUNTIME_PATCHES):
        path = repo / rel
        if path.is_file():
            candidates.append(path)

    tracked = list_file_records(repo)
    for path in tracked:
        rel = path.relative_to(repo).as_posix()
        lower = rel.lower()
        if not path.is_file():
            continue
        if lower.startswith("tools/skyrimvr/linux/") and path.suffix.lower() == ".sh":
            candidates.append(path)
        elif "linux" in path.name.lower() and path.suffix.lower() in {".sh", ".bash"}:
            candidates.append(path)

    for fixed in (repo / "LICENSE", repo / "THIRD-PARTY-NOTICES.md"):
        if fixed.is_file():
            candidates.append(fixed)

    if cef_license is not None:
        resolved = cef_license.expanduser().resolve()
        if not resolved.is_file():
            raise FileNotFoundError(f"--cef-license path not found: {resolved}")
        candidates.append(resolved)

    unique: list[pathlib.Path] = []
    seen: set[pathlib.Path] = set()
    for path in candidates:
        if path not in seen and path.is_file():
            seen.add(path)
            unique.append(path)
    return sorted(unique, key=lambda item: item.as_posix())


def add_file_entry(
    zf: zipfile.ZipFile,
    path: pathlib.Path,
    arcname: str,
    dt: tuple[int, int, int, int, int, int],
) -> dict[str, object]:
    info = zipfile.ZipInfo(arcname, date_time=dt)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 3
    mode = 0o100755 if path.stat().st_mode & 0o111 else 0o100644
    info.external_attr = mode << 16
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as inp, zf.open(info, "w") as out:
        while True:
            chunk = inp.read(1024 * 1024)
            if not chunk:
                break
            out.write(chunk)
            digest.update(chunk)
            size += len(chunk)
    return {"path": arcname, "size": size, "sha256": digest.hexdigest()}


def add_bytes_entry(zf: zipfile.ZipFile, payload: bytes, arcname: str, dt: tuple[int, int, int, int, int, int]) -> dict[str, object]:
    info = zipfile.ZipInfo(arcname, date_time=dt)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    with zf.open(info, "w") as out:
        out.write(payload)
    return {"path": arcname, "size": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}


def read_build_manifest(build_dir: pathlib.Path) -> dict:
    path = build_dir / BUILD_MANIFEST_NAME
    if not path.is_file():
        raise FileNotFoundError(f"missing build manifest: {path}")
    return json.loads(path.read_text(encoding="utf-8-sig", errors="replace"))


def source_revision_from_manifest(manifest: dict) -> str:
    return str(
        manifest.get("sourceRevision")
        or manifest.get("sourceProvenance", {}).get("sourceRevision", "")
        or manifest.get("source_provenance", {}).get("sourceRevision", "")
    ).strip()


def gather_reference_names(repo: pathlib.Path, version: str) -> list[str]:
    refs: list[str] = ["HEAD"]

    def has_ref(ref: str) -> bool:
        try:
            run_git(repo, ["show-ref", "--verify", "--quiet", ref])
            return True
        except RuntimeError:
            return False

    if has_ref("refs/heads/main"):
        refs.append("refs/heads/main")
    elif has_ref("refs/remotes/origin/main"):
        refs.append("refs/remotes/origin/main")
    if has_ref("refs/heads/original-skyrim-together"):
        refs.append("refs/heads/original-skyrim-together")
    elif has_ref("refs/remotes/origin/original-skyrim-together"):
        refs.append("refs/remotes/origin/original-skyrim-together")

    tag_ref = f"refs/tags/{version}"
    if not has_ref(tag_ref):
        raise RuntimeError(f"release tag must exist before packaging: {version}")
    refs.append(tag_ref)

    dedup = []
    for ref in refs:
        if ref not in dedup:
            dedup.append(ref)
    return dedup


def init_submodules(repo: pathlib.Path) -> None:
    try:
        status = run_git(repo, ["submodule", "status", "--recursive"]).strip()
    except RuntimeError:
        return
    if status:
        run_git(repo, ["submodule", "update", "--init", "--recursive"])


def create_source_bundle(repo: pathlib.Path, temp_dir: pathlib.Path, version: str) -> tuple[pathlib.Path, list[str]]:
    init_submodules(repo)
    refs = gather_reference_names(repo, version)
    bundle_path = temp_dir / "source.bundle"
    run_git(repo, ["bundle", "create", str(bundle_path), *refs])

    normalized_refs = [ref.split("/", 2)[-1] if ref.startswith("refs/") else ref for ref in refs]
    return bundle_path, sorted(set(normalized_refs))


def create_runtime_zip(
    version: str,
    repo: pathlib.Path,
    build_dir: pathlib.Path,
    output_dir: pathlib.Path,
    dt: tuple[int, int, int, int, int, int],
    cef_license: pathlib.Path | None,
    build_manifest: dict,
    build_manifest_text: str,
) -> tuple[pathlib.Path, dict[str, object]]:
    root = f"{PREFIX}-{version}"
    runtime_name = f"{root}-linux-monado-runtime.zip"
    runtime_path = output_dir / runtime_name
    build_files = iter_build_files(build_dir, build_manifest)
    aux_files = collect_runtime_aux(repo, build_dir, cef_license)

    runtime_records: list[dict[str, object]] = []
    candidates: list[tuple[pathlib.Path, str]] = []
    runtime_seen: set[pathlib.Path] = set()
    cef_path = cef_license.expanduser().resolve() if cef_license is not None else None

    for path in [*build_files, *aux_files]:
        if path in runtime_seen or not path.is_file():
            continue
        runtime_seen.add(path)
        if path.is_relative_to(build_dir):
            rel = path.relative_to(build_dir).as_posix()
        elif cef_path is not None and path == cef_path:
            rel = "THIRD-PARTY/CEF/LICENSE.txt"
        else:
            rel = path.relative_to(repo).as_posix()
            if rel in RUNTIME_SCRIPTS:
                rel = path.name
        candidates.append((path, f"{root}/{rel}"))

    with zipfile.ZipFile(runtime_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for path, arc in sorted(candidates, key=lambda item: item[1]):
            runtime_records.append(add_file_entry(zf, path, arc, dt))
        runtime_records.append(
            add_bytes_entry(
                zf,
                build_manifest_text.encode("utf-8"),
                f"{root}/{BUILD_MANIFEST_NAME}",
                dt,
            )
        )

    runtime_meta = {
        "name": runtime_name,
        "path": runtime_name,
        "size": runtime_path.stat().st_size,
        "sha256": hash_file(runtime_path),
        "requiredRuntimeFiles": [f"{root}/{path}" for path in REQUIRED_RUNTIME_FILES],
        "requiredCefFiles": [f"{root}/{path}" for path in REQUIRED_CEF_FILES],
        "requiredDocFiles": [f"{root}/{path}" for path in RUNTIME_DOCS],
        "requiredScriptFiles": [f"{root}/{pathlib.Path(path).name}" for path in RUNTIME_SCRIPTS],
        "requiredPatchFiles": [f"{root}/{path}" for path in RUNTIME_PATCHES],
        "requiredManifestFiles": [f"{root}/{BUILD_MANIFEST_NAME}"],
        "requiredLicenseFiles": [
            f"{root}/LICENSE",
            f"{root}/THIRD-PARTY-NOTICES.md",
            f"{root}/THIRD-PARTY/CEF/LICENSE.txt",
        ],
        "records": runtime_records,
    }
    return runtime_path, runtime_meta


def add_nested_runtime(
    zf: zipfile.ZipFile,
    runtime_path: pathlib.Path,
    runtime_name: str,
    root: str,
    dt: tuple[int, int, int, int, int, int],
) -> None:
    info = zipfile.ZipInfo(f"{root}/{runtime_name}", date_time=dt)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    with runtime_path.open("rb") as inp, zf.open(info, "w") as out:
        while True:
            chunk = inp.read(1024 * 1024)
            if not chunk:
                break
            out.write(chunk)


def list_source_snapshot(repo: pathlib.Path) -> list[pathlib.Path]:
    files = list_file_records(repo)
    if files:
        candidates = [path for path in files if path.is_file()]
    else:
        candidates = [path for path in repo.rglob("*") if path.is_file()]

    selected: list[pathlib.Path] = []
    for path in candidates:
        if should_skip(path, repo, SOURCE_EXCLUDE_DIR_PARTS, SOURCE_EXCLUDE_EXTS):
            continue
        selected.append(path)
    return sorted(selected, key=lambda item: item.as_posix())


def create_review_handoff_zip(
    version: str,
    repo: pathlib.Path,
    build_dir: pathlib.Path,
    output_dir: pathlib.Path,
    runtime_name: str,
    source_bundle: pathlib.Path,
    source_bundle_refs: list[str],
    dt: tuple[int, int, int, int, int, int],
    build_manifest_text: str,
    embedded_manifest: dict[str, object],
) -> tuple[pathlib.Path, dict[str, object]]:
    root = f"{PREFIX}-{version}"
    review_name = f"{root}-review-handoff.zip"
    review_path = output_dir / review_name
    source_root = f"{root}/source"

    source_files = list_source_snapshot(repo)
    source_records: list[dict[str, object]] = []

    source_bundle_meta = {
        "name": "source.bundle",
        "path": f"{root}/source.bundle",
        "size": source_bundle.stat().st_size,
        "sha256": hash_file(source_bundle),
        "refs": source_bundle_refs,
    }

    with zipfile.ZipFile(review_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        add_nested_runtime(zf, output_dir / runtime_name, runtime_name, root, dt)

        # top-level git bundle: branch + optional tag.
        add_file_entry(zf, source_bundle, f"{root}/source.bundle", dt)

        add_bytes_entry(
            zf,
            build_manifest_text.encode("utf-8"),
            f"{root}/{BUILD_MANIFEST_NAME}",
            dt,
        )
        add_bytes_entry(
            zf,
            (json.dumps(embedded_manifest, indent=2, sort_keys=True) + "\n").encode("utf-8"),
            f"{root}/RELEASE-MANIFEST.json",
            dt,
        )

        # selected docs/manifest for current run
        for rel in REVIEW_DOCS:
            path = repo / rel
            if path.is_file():
                add_file_entry(zf, path, f"{root}/{rel}", dt)

        for rel in ("LICENSE", "THIRD-PARTY-NOTICES.md"):
            path = repo / rel
            if path.is_file():
                add_file_entry(zf, path, f"{root}/{rel}", dt)

        for path in source_files:
            rel = path.relative_to(repo).as_posix()
            source_records.append(add_file_entry(zf, path, f"{source_root}/{rel}", dt))

        readme = "\n".join(
            [
                "# SkyrimTogetherVR prerelease handoff",
                f"version={version}",
                f"runtime={runtime_name}",
                f"source_bundle_refs={','.join(source_bundle_refs)}",
                "",
                "This archive contains the runtime zip, a deterministic source snapshot with initialized submodule",
                "working trees, a release manifest, and a top-level git bundle (main, original-skyrim-together, and tag).",
                "It contains no game executable, SKSEVR distribution, VR Address Library, FUS payload, or server credentials.",
            ]
        ).strip()
        add_bytes_entry(zf, (readme + "\n").encode("utf-8"), f"{root}/README.md", dt)

    review_meta = {
        "name": review_name,
        "path": review_name,
        "size": review_path.stat().st_size,
        "sha256": hash_file(review_path),
        "sourceFileCount": len(source_records),
        "sourceBundle": source_bundle_meta,
        "sourceRecords": source_records,
    }
    return review_path, review_meta


def parse_public_facts(values: list[str] | None) -> dict[str, str]:
    if not values:
        return {}
    facts: dict[str, str] = {}
    for entry in values:
        if "=" not in entry:
            raise argparse.ArgumentTypeError(f"invalid --public-fact value: {entry} (expected key=value)")
        key, value = entry.split("=", 1)
        if not key:
            raise argparse.ArgumentTypeError(f"invalid --public-fact key in: {entry}")
        facts[key.strip()] = value.strip()
    return facts


def create_manifest(
    output_dir: pathlib.Path,
    version: str,
    source_head_val: str,
    binary_source_revision: str,
    runtime_meta: dict[str, object],
    review_meta: dict[str, object],
    source_bundle_refs: list[str],
    public_facts: dict[str, str],
    dt_iso: str,
) -> pathlib.Path:
    payload = {
        "schema": MANIFEST_SCHEMA,
        "version": version,
        "generatedAt": dt_iso,
        "sourceHead": source_head_val,
        "binarySourceRevision": binary_source_revision,
        "testedScope": "connection-only",
        "publicFacts": {
            "serverEndpoint": public_facts.get("serverEndpoint", DEFAULT_SERVER_ENDPOINT),
            **public_facts,
        },
        "runtime": runtime_meta,
        "reviewHandoff": {
            "path": review_meta["path"],
            "size": review_meta["size"],
            "sha256": review_meta["sha256"],
            "sourceBundle": review_meta["sourceBundle"],
            "sourceFileCount": review_meta["sourceFileCount"],
        },
        "sourceBundleRefs": source_bundle_refs,
        "buildManifestPath": BUILD_MANIFEST_NAME,
    }
    manifest_path = output_dir / "RELEASE-MANIFEST.json"
    manifest_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return manifest_path


def write_checksums(output_dir: pathlib.Path, files: list[pathlib.Path]) -> pathlib.Path:
    lines = []
    for path in sorted(files, key=lambda item: item.name):
        lines.append(f"{hash_file(path)}  {path.name}\n")
    checksum_path = output_dir / "SHA256SUMS.txt"
    checksum_path.write_text("".join(lines), encoding="utf-8")
    return checksum_path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Create SkyrimTogetherVR prerelease runtime and review handoff bundles.")
    parser.add_argument("--version", default=DEFAULT_VERSION)
    parser.add_argument("--build-dir", type=pathlib.Path)
    parser.add_argument("--output-dir", type=pathlib.Path)
    parser.add_argument("--repo", type=pathlib.Path)
    parser.add_argument("--cef-license", type=pathlib.Path)
    parser.add_argument("--server-endpoint", default=DEFAULT_SERVER_ENDPOINT)
    parser.add_argument(
        "--public-fact",
        action="append",
        default=[],
        help="Repeatable key=value pair included in publicFacts.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        repo = resolve_repo(args.repo)
        build_dir = args.build_dir or (repo / "artifacts" / "SkyrimTogetherVR" / "releasedbg")
        output_dir = args.output_dir or (repo / "artifacts" / "SkyrimTogetherVR" / "prerelease")
        output_dir.mkdir(parents=True, exist_ok=True)

        require_clean_tagged_tree(repo, args.version)
        build_manifest = read_build_manifest(build_dir)
        binary_source_revision = source_revision_from_manifest(build_manifest)
        if not binary_source_revision:
            raise ValueError("BuildManifest missing source revision")

        head = source_head(repo)
        epoch = source_epoch(repo, head)
        dt = zip_timestamp(epoch)
        dt_iso = datetime.fromtimestamp(epoch, tz=timezone.utc).isoformat()
        sanitized_build_manifest = sanitize_build_manifest(build_manifest)
        build_manifest_text = json.dumps(sanitized_build_manifest, indent=2, sort_keys=True) + "\n"
        cef_license = args.cef_license or (repo / "ThirdParty" / "CEF-LICENSE.txt")
        if not cef_license.expanduser().resolve().is_file():
            raise FileNotFoundError(f"CEF license is required: {cef_license}")

        runtime_path, runtime_meta = create_runtime_zip(
            version=args.version,
            repo=repo,
            build_dir=build_dir,
            output_dir=output_dir,
            dt=dt,
            cef_license=cef_license,
            build_manifest=build_manifest,
            build_manifest_text=build_manifest_text,
        )

        with tempfile.TemporaryDirectory(prefix="stvr-source-bundle-") as tmpdir:
            tmp = pathlib.Path(tmpdir)
            source_bundle_path, source_bundle_refs = create_source_bundle(repo, tmp, args.version)

            embedded_manifest = {
                "schema": MANIFEST_SCHEMA,
                "version": args.version,
                "generatedAt": dt_iso,
                "sourceHead": head,
                "binarySourceRevision": binary_source_revision,
                "testedScope": "connection-only",
                "publicFacts": {
                    "serverEndpoint": args.server_endpoint,
                    **parse_public_facts(args.public_fact),
                },
                "runtime": runtime_meta,
                "sourceBundle": {
                    "path": f"{PREFIX}-{args.version}/source.bundle",
                    "size": source_bundle_path.stat().st_size,
                    "sha256": hash_file(source_bundle_path),
                    "refs": source_bundle_refs,
                },
                "buildManifestPath": BUILD_MANIFEST_NAME,
            }

            review_path, review_meta = create_review_handoff_zip(
                version=args.version,
                repo=repo,
                build_dir=build_dir,
                output_dir=output_dir,
                runtime_name=runtime_path.name,
                source_bundle=source_bundle_path,
                source_bundle_refs=source_bundle_refs,
                dt=dt,
                build_manifest_text=build_manifest_text,
                embedded_manifest=embedded_manifest,
            )

        manifest_path = create_manifest(
            output_dir=output_dir,
            version=args.version,
            source_head_val=head,
            binary_source_revision=binary_source_revision,
            runtime_meta=runtime_meta,
            review_meta=review_meta,
            source_bundle_refs=source_bundle_refs,
            public_facts={"serverEndpoint": args.server_endpoint, **parse_public_facts(args.public_fact)},
            dt_iso=dt_iso,
        )

        checksums = write_checksums(output_dir, [runtime_path, review_path, manifest_path])
        print(f"runtime zip: {runtime_path}")
        print(f"review handoff zip: {review_path}")
        print(f"manifest: {manifest_path}")
        print(f"checksums: {checksums}")
        return 0
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
