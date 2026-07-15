#!/usr/bin/env python3
"""Verify the exact Skyrim VR gameplay-adapter dependency baseline."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_LOCK = REPO_ROOT / "Dependencies" / "SkyrimVR.lock.json"
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_lock(path: pathlib.Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        data = json.load(stream)
    if data.get("schemaVersion") != 1:
        raise ValueError(f"unsupported lock schema in {path}")
    return data


def verify_hash(label: str, path: pathlib.Path, expected: str, failures: list[str]) -> None:
    if not SHA256_PATTERN.fullmatch(expected):
        failures.append(f"{label}: malformed locked SHA-256")
        return
    if not path.is_file():
        failures.append(f"{label}: missing {path}")
        return
    actual = sha256(path)
    if actual != expected:
        failures.append(f"{label}: SHA-256 mismatch (expected {expected}, got {actual})")


def git_head(path: pathlib.Path) -> str | None:
    result = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "HEAD"],
        check=False,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip().lower() if result.returncode == 0 else None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=pathlib.Path, help="Skyrim VR installation root")
    parser.add_argument("--lock", type=pathlib.Path, default=DEFAULT_LOCK)
    parser.add_argument(
        "--source-only",
        action="store_true",
        help="verify the CommonLib submodule without requiring an installed game",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    lock = load_lock(args.lock.resolve())
    failures: list[str] = []

    commonlib = lock["commonLibSseNg"]
    commonlib_path = REPO_ROOT / commonlib["submodulePath"]
    expected_commit = commonlib["commit"].lower()
    observed_commit = git_head(commonlib_path)
    if observed_commit != expected_commit:
        failures.append(
            "CommonLibSSE-NG: commit mismatch "
            f"(expected {expected_commit}, got {observed_commit or 'unavailable'})"
        )

    if not args.source_only:
        if args.game is None:
            failures.append("runtime: --game is required unless --source-only is used")
        else:
            game = args.game.expanduser().resolve()
            runtime = lock["runtime"]
            verify_hash(
                "Skyrim VR runtime",
                game / runtime["executable"],
                runtime["sha256"],
                failures,
            )
            for relative, expected in lock["sksevr"]["runtimeFiles"].items():
                verify_hash(f"SKSEVR {relative}", game / relative, expected, failures)
            address_library = lock["vrAddressLibrary"]
            verify_hash(
                "VR Address Library",
                game / pathlib.PurePosixPath(address_library["file"]),
                address_library["sha256"],
                failures,
            )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    scope = "source dependency" if args.source_only else "source and runtime dependencies"
    print(f"PASS: verified locked Skyrim Together VR {scope}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
