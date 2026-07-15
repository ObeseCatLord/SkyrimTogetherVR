# Skyrim VR Runtime Version Recovery Review Brief

## Goal and scope

Port Skyrim Together's launcher to Skyrim VR 1.4.15.0 without retaining heap-backed version strings across manual PE mapping. This is a focused, read-only Sol-max review of runtime-version recovery and propagation before one corrective build. It must also identify any directly adjacent startup assumption that would cause another rebuild.

## Verified evidence

| Claim | Status | Evidence |
| --- | --- | --- |
| The reviewed source is commit `af0ca0f109b8f800a020e0525c97c77325d36fba`. | verified | `git rev-parse HEAD`; clean worktree before this brief. |
| A clean WinBoat build and package audit completed successfully. | verified | `windows-build-af0ca0f1.log`, exit 0; all launcher/client/bridge targets built. |
| The exact package passed strict Linux target-prerequisite audit and post-install Papyrus verification. | verified | `audit_built_package.py` against the installed Skyrim VR root, zero failures. |
| Observer startup reaches `RunTiltedInit` without an exception. | verified | deployed `logs/tp_client.log` reports address loading rather than a crash. |
| The propagated runtime is incorrectly `1.0.0.0`. | verified | `tp_client.log`: `Loading Skyrim address data for runtime 1.0.0.0`, then an intentional unsupported-runtime refusal. |
| The installed `SkyrimVR.exe` fixed root version is `1.0.0.0`. | verified | current `QueryFileVersion` reads `VS_FIXEDFILEINFO::dwFileVersion*`; PE resource strings also expose a root `VersionNumber versionNumber="1.0.0.0"`. |
| The installed executable's string-table `FileVersion` and `ProductVersion` are both `1.4.15.0`. | verified | `strings -el SkyrimVR.exe` finds `FileVersion`, `ProductVersion`, and their `1.4.15.0` values. |
| The former implementation queried string-table ProductVersion first, then FileVersion, but returned a heap-backed project string retained in `LaunchContext`. | verified | parent commit implementation of `utils/FileVersion.inl` and `Launcher.cpp`. |
| The current implementation queries the fixed root before `ExeLoader::Load` and stores four `uint16_t` fields in a stack POD. | verified | `Launcher.cpp`, `Launcher.h`, and `utils/FileVersion.inl`. |
| The client consumes four integers and rejects any runtime other than Skyrim VR 1.4.15.0. | verified | `RunTiltedInit` in `Code/client/main.cpp` and `VersionDb::Load` in `Code/client/VersionDb.h`. |
| `VersionDb` already contains a separate ProductVersion/FileVersion string parser, but that function operates after launcher mapping and uses a hard-coded translation. | verified | `VersionDb::GetExecutableVersion` in `Code/client/VersionDb.h`. |

## Environment facts

| Item | Value |
| --- | --- |
| Host runtime | Linux + GE-Proton 10-34 + Monado + XRizer |
| Target executable | Bethesda Skyrim VR 1.4.15.0, Steam CEG image |
| Manual mapper | `Code/immersive_launcher/loader/ExeLoader.*` |
| Version capture point | Before `ExeLoader::Load`, while normal process CRT/heap state is trustworthy |
| Required output representation | Fixed-width POD surviving mapped-image initialization; no retained pointer/string into version buffer |
| Supported runtime policy | Exact 1.4.15.0 fail-closed |
| Current failure boundary | Before address CSV loading and before hook installation |

## Current lean

Replace fixed-root extraction with a strict string-table query that:

1. Reads the resource before mapping.
2. Enumerates `\VarFileInfo\Translation` language/codepage pairs.
3. Queries `ProductVersion` first, then `FileVersion` for each translation, with a narrow documented fallback only if the translation block is absent.
4. Parses exactly four bounded unsigned components into `RuntimeVersion` without retaining heap or resource-buffer references.
5. Rejects malformed, partial, overflowed, or trailing-junk values.
6. Logs the recovered runtime before mapping if the existing logger is available; otherwise preserves the current post-map diagnostic.

Rejected alternative: use `VS_FIXEDFILEINFO::dwProductVersion*`; the executable's fixed resource is demonstrably not the supported runtime. Rejected alternative: restore `TiltedPhoques::String` in `LaunchContext`; the prior mapped-CRT lifetime risk remains.

## Questions for the reviewer

1. Verify the root cause and rank the safest correction.
2. Specify the translation enumeration/fallback policy and strict parsing contract.
3. Audit the complete value path from resource bytes through `LoadProgram`, `RunTiltedInit`, `VersionDb::Load`, diagnostics, and exact-runtime enforcement.
4. Identify any other pre-map metadata or process-global state in this immediate path that can be invalidated by manual mapping.
5. Recommend focused source tests/audits that catch `1.0.0.0` versus `1.4.15.0` without requiring Skyrim execution.
6. Flag any issue that should be fixed before the next build. Do not broaden into gameplay-address coverage already handled by `full-runtime-senior-brief-20260714.md` and its disposition.

## Output contract

Return a prioritized review with file/line evidence, a concrete parser/query policy, and a short must-fix-before-build list. Read-only: do not edit files. If evidence is missing, state that rather than assuming.
