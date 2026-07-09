# SkyrimTogetherVR Code Review Handoff Letter

Date: July 8, 2026

This package is a source-review handoff for the SkyrimTogether VR port. The goal is to port Tilted Online / Skyrim Together Reborn to Skyrim VR as a separate VR-only repository and build target. Skyrim SE support is intentionally out of scope for this fork. The target runtime is Skyrim VR with SKSEVR, VR Address Library, VRIK, HIGGS, and PLANCK compatibility.

## Goal

The requested end state is a SkyrimTogetherVR package that can be built on Windows/MSVC or through a Windows toolchain under Wine, installed into Skyrim VR, and tested in-game. Mandatory VR functionality includes VRIK IK pose replication so remote players can see each other's HMD, hands, weapon/spell/projectile origins, and VRIK-derived body/hand state. The port should keep dangerous flat-Skyrim gameplay hooks disabled until each hook and layout has been validated against Skyrim VR.

## Approach Taken

The port has been structured as a staged VR bring-up rather than a direct enablement of all Skyrim Together gameplay systems:

- Added separate SkyrimTogetherVR xmake targets and launcher modes for default connection-only, avatar-sync validation, gameplay validation, and DLL-only bridge builds.
- Added SKSEVR-aware loading and a launcher path-rerouting path that can start the VR client without relying on `StartSKSE` as the primary user entry point.
- Added VR Address Library support and AE-to-SE-to-VR address translation/audit data, including generated address audit reports.
- Added a CommonLibSSE-NG-informed runtime layout layer in `Code/client/Games/Skyrim/RuntimeLayout.h`, then routed staged actor/object/player/menu/inventory/projectile access through semantic accessors instead of raw shifted SE member reads where practical.
- Disabled dangerous gameplay hooks by default. The default package is connection-only and writes telemetry/readout files rather than mutating remote actors or objects.
- Added startup/main-loop/VM/render logging hooks for initial VR bring-up.
- Added file-based VR handoff under `Data/SkyrimTogetherReborn`, plus a desktop/browser companion control surface to replace the flat CEF/D3D overlay for initial VR testing.
- Added staged VR network lanes for pose, movement, equipment, activation, magic, combat, projectile, grab, HIGGS state, PLANCK compatibility, player-cell status, and save/load telemetry.
- Added VRIK, HIGGS, and PLANCK SKSEVR bridge DLL targets. VRIK sync is mandatory; HIGGS and PLANCK remain observation-only until ownership, physics mutation, and ABI boundaries are validated.
- Added Windows handoff scripts for build, package audit, build evidence collection, install dry-run, runtime evidence collection, and final evidence audit.

## Current State

This is not a release-ready runtime package yet. It is ready for code review and then a Windows/MSVC build attempt.

Important constraints:

- The code has not been compiled in the final current state.
- Skyrim VR has not been launched by the porting agent.
- Runtime validation has not happened.
- Gameplay hooks and inline patches remain intentionally gated unless explicit validation targets/configuration are used.
- The local worktree contains many uncommitted and untracked files that are part of the VR port. Review the packaged source tree as the authoritative handoff state, not only `git status`.

## Review Priorities

Please focus review on these areas first:

- Windows/MSVC build correctness for all VR targets, especially untracked VR source files and xmake target graph integration.
- `RuntimeLayout.h` offsets and semantic accessors against CommonLibSSE-NG VR headers.
- Remaining raw member access to `TESObjectREFR`, `Actor`, `PlayerCharacter`, `TESObjectCELL`, inventory, magic, combat, and save/load structures.
- SKSEVR loader behavior and launcher path handling, especially `STVR_GAME_PATH` propagation into the client and bridge DLL handoff paths.
- VR Address Library translation/audit data and whether generated helper CSVs match the target Skyrim VR runtime.
- VRIK/HIGGS/PLANCK bridge boundaries. HIGGS and PLANCK should remain observation-only unless the review validates safe mutating APIs and ownership rules.
- Avatar-sync validation target behavior. The current remote avatar application path is deliberately narrow and should be treated as provisional until two-client VR evidence confirms it.
- Inline patch manifest and disassembly evidence. Do not enable inline patches broadly until every target site has been validated against the protected Skyrim VR executable image.
- Package scripts and install dry-run behavior. The package should stage files under the Skyrim VR root and `Data`, not stale root-level mod folders.

## Local Binary Policy

The review package intentionally does not include `SkyrimVR.exe` or the installed game binaries. Those files are proprietary game content and should not be redistributed by default. Instead, the package includes metadata and SHA-256 hashes for the local Skyrim VR executable and SKSEVR loader DLLs so a reviewer working on the same machine can locate and verify the local install.

Expected local paths:

- Skyrim VR: `/home/obesecatlord/FasterGames/SteamLibrary/steamapps/common/SkyrimVR`
- Skyrim SE reference install: `/home/obesecatlord/LargeGames/SteamLibrary/steamapps/common/Skyrim Special Edition`
- Reference sources: `/home/obesecatlord/Documents/SkyrimModding/_refs`
- FUS mod list: `/home/obesecatlord/Games/FUS/mods`

## Before Building

Before treating this as ready for in-game testing, the next agent should:

- Review the current dirty worktree and package contents.
- Make any obvious source fixes found during review.
- Run the Windows handoff preflight on a Windows/MSVC machine.
- Build all handoff packages.
- Run the built-package readiness checks against the real Skyrim VR install with SKSEVR, VR Address Library, VRIK, HIGGS, and PLANCK present.
- Run install dry-runs before copying files into Skyrim VR.

The recommended first full Windows handoff command is:

```bat
PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --all --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites
```

Then verify snapshots:

```bat
VerifySkyrimTogetherVRWindowsPackages-Windows.bat --include-fus --planck-archive "C:\Downloads\PLANCK.zip" --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
```

Only after those pass should a package be installed and launched in Skyrim VR.

## Runtime Test Order

Use this runtime order:

1. Default package: connection, startup logging, VRIK/HIGGS/PLANCK installed-state readouts, local pose, command/status handoff.
2. Avatar-sync package: two clients, remote player proxy, VRIK IK state, HMD/hand pose, weapon/magic/projectile pose context, HIGGS-aware avatar readiness.
3. Gameplay package: deliberate one-lane validation for movement, equipment, activation, magic, combat, projectile, grab, HIGGS, PLANCK, and save/load.

Runtime evidence should be collected with the packaged `CollectSkyrimTogetherVREvidence-Windows.bat`, `CollectSkyrimTogetherVRAvatarSyncEvidence-Windows.bat`, and `CollectSkyrimTogetherVRGameplayEvidence-Windows.bat` wrappers after the user runs Skyrim VR.
