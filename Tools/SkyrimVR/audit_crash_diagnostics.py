#!/usr/bin/env python3
"""Keep the VR crash diagnostics and hook-failure guard from regressing."""

from __future__ import annotations

import pathlib

def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def require_tokens(root: pathlib.Path, path: pathlib.Path, tokens: tuple[str, ...]) -> list[str]:
    text = path.read_text(encoding="utf-8")
    return [f"{path.relative_to(root)}: missing {token}" for token in tokens if token not in text]


def main() -> int:
    root = repo_root()
    failures = []
    failures.extend(
        require_tokens(
            root,
            root / "Code" / "client" / "CrashHandler.cpp",
            (
                "PruneCrashDumps",
                "kRetainedCrashDumpCount = 5",
                "MiniDumpWithUnloadedModules",
                "MiniDumpWithFullMemoryInfo",
                "MiniDumpWithCodeSegs",
                "dumpWritten = MiniDumpWriteDump",
                "coredump write failed",
                "(IS_MASTER) && (!defined(TP_SKYRIM_VR) || TP_SKYRIM_VR != 1)",
            ),
        )
    )
    failures.extend(
        require_tokens(
            root,
            root / "Libraries" / "TiltedReverse" / "Code" / "reverse" / "src" / "FunctionHook.cpp",
            (
                "HookInstallSummary FunctionHookManager::InstallDelayedHooks",
                "m_ownsHook",
                "if (createStatus != MH_OK)",
                "if (enableStatus != MH_OK)",
                "MH_RemoveHook(hook.m_pSystemFunction)",
            ),
        )
    )
    failures.extend(
        require_tokens(
            root,
            root / "Code" / "client" / "main.cpp",
            (
                "hookSummary.Failures != 0",
                "refused to enter Skyrim VR",
                "hook install verified",
            ),
        )
    )
    failures.extend(
        require_tokens(
            root,
            root / "Code" / "immersive_launcher" / "Launcher.cpp",
            (
                "if (!RunTiltedInit(LC->gamePath, LC->Version))",
                "could not install its VR hook set",
            ),
        )
    )

    print(f"Crash diagnostic audit failures: {len(failures)}")
    for failure in failures:
        print(f"- {failure}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
