#!/usr/bin/env python3
"""Shared path resolution helpers for SkyrimTogetherVR no-launch tools."""

from __future__ import annotations

import os
import pathlib


SKYRIM_VR_ENV_VARS = ("SKYRIMVR_PATH", "STVR_SKYRIM_VR")


def env_path(names: tuple[str, ...]) -> pathlib.Path | None:
    for name in names:
        value = os.environ.get(name)
        if value:
            return pathlib.Path(value).expanduser()
    return None


def default_skyrim_vr_path() -> pathlib.Path:
    configured = env_path(SKYRIM_VR_ENV_VARS)
    if configured is not None:
        return configured

    return pathlib.Path.cwd()


def default_skyrim_vr_exe_path() -> pathlib.Path:
    return default_skyrim_vr_path() / "SkyrimVR.exe"


def skyrim_vr_help(default_usage: str = "--skyrim-vr") -> str:
    return (
        "Skyrim VR install path. Prefer passing "
        f"{default_usage} or setting SKYRIMVR_PATH/STVR_SKYRIM_VR; "
        "the current working directory is used as a neutral fallback."
    )


def skyrim_vr_exe_help(default_usage: str = "--exe") -> str:
    return (
        "SkyrimVR.exe path. Prefer passing "
        f"{default_usage} or setting SKYRIMVR_PATH/STVR_SKYRIM_VR; "
        "the current working directory is used as a neutral fallback."
    )


def require_existing_skyrim_vr(path: pathlib.Path, *, label: str = "Skyrim VR root") -> pathlib.Path:
    resolved = path.expanduser().resolve()
    if not resolved.exists():
        raise SystemExit(
            f"{label} does not exist: {resolved}\n"
            "Pass the path explicitly or set SKYRIMVR_PATH/STVR_SKYRIM_VR."
        )
    return resolved
