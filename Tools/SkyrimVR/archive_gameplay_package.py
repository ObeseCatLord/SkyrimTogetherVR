#!/usr/bin/env python3
"""Create a deterministic ZIP from an audited gameplay package directory."""

from __future__ import annotations

import argparse
import json
import pathlib
import zipfile
from datetime import datetime, timezone


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--expected-revision", required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source = args.source.resolve()
    manifest_path = source / "SkyrimTogetherVR_BuildManifest.json"
    if not source.is_dir() or not manifest_path.is_file():
        raise FileNotFoundError(f"gameplay package manifest is missing: {manifest_path}")

    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    if (
        manifest.get("packageFlavor") != "gameplay"
        or manifest.get("gameplay") is not True
        or manifest.get("sourceRevision") != args.expected_revision
    ):
        raise ValueError("gameplay package provenance does not match the requested revision")

    generated = manifest.get("generatedAtUtc")
    try:
        timestamp = datetime.fromisoformat(str(generated).replace("Z", "+00:00")).astimezone(timezone.utc)
    except ValueError:
        timestamp = datetime(1980, 1, 1, tzinfo=timezone.utc)
    if timestamp.year < 1980:
        timestamp = datetime(1980, 1, 1, tzinfo=timezone.utc)
    zip_timestamp = timestamp.timetuple()[:6]

    files = sorted(path for path in source.rglob("*") if path.is_file())
    expected_hashes = manifest.get("packageFileSha256")
    if not isinstance(expected_hashes, dict) or len(files) != len(expected_hashes) + 1:
        raise ValueError("gameplay package file set differs from its build manifest")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + ".tmp")
    temporary.unlink(missing_ok=True)
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for path in files:
                relative = path.relative_to(source).as_posix()
                info = zipfile.ZipInfo(f"package/{relative}", zip_timestamp)
                info.create_system = 3
                info.external_attr = (0o100755 if path.stat().st_mode & 0o111 else 0o100644) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                with path.open("rb") as source_file, archive.open(info, "w") as destination:
                    for chunk in iter(lambda: source_file.read(1024 * 1024), b""):
                        destination.write(chunk)
        temporary.replace(args.output)
    finally:
        temporary.unlink(missing_ok=True)

    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
