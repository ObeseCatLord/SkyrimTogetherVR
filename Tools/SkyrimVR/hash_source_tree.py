#!/usr/bin/env python3
"""Hash a source tree using the WinBoat PLANCK dependency manifest format."""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import re
import sys


SHA256_RE = re.compile(r"[0-9a-f]{64}")


def file_sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def source_tree_sha256(root: pathlib.Path) -> tuple[str, int]:
    if not root.is_dir() or root.is_symlink():
        raise ValueError(f"source-tree root must be a regular directory: {root}")

    files: list[tuple[str, pathlib.Path]] = []
    casefold_paths: set[str] = set()
    for path in root.rglob("*"):
        if path.is_symlink():
            raise ValueError(f"source tree contains a symbolic link: {path}")
        if not path.is_file():
            continue
        relative = path.relative_to(root).as_posix()
        try:
            relative.encode("ascii")
        except UnicodeEncodeError as error:
            raise ValueError(f"source-tree path is not ASCII: {relative!r}") from error
        if any(character in relative for character in ("\0", "\r", "\n")):
            raise ValueError(f"source-tree path contains a control character: {relative!r}")
        folded = relative.casefold()
        if folded in casefold_paths:
            raise ValueError(f"source tree contains a case-aliased path: {relative}")
        casefold_paths.add(folded)
        files.append((relative, path))

    if not files:
        raise ValueError(f"source tree is empty: {root}")

    digest = hashlib.sha256()
    for relative, path in sorted(files, key=lambda item: item[0]):
        digest.update(f"{file_sha256(path)}  {relative}\n".encode("ascii"))
    return digest.hexdigest(), len(files)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="hash sorted '<file-sha256>  <POSIX-relative-path>\\n' records"
    )
    parser.add_argument("root", type=pathlib.Path)
    parser.add_argument("--expect", help="fail unless the tree matches this lowercase SHA-256")
    args = parser.parse_args()

    if args.expect is not None and SHA256_RE.fullmatch(args.expect) is None:
        parser.error("--expect must be a lowercase SHA-256")
    try:
        digest, count = source_tree_sha256(args.root)
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 2
    if args.expect is not None and digest != args.expect:
        print(
            f"source-tree SHA-256 mismatch: expected {args.expect}, got {digest} ({count} files)",
            file=sys.stderr,
        )
        return 1
    print(f"{digest}  {count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
