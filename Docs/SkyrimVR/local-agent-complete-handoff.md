# Local Agent Complete Handoff

This is the start-here document for the private, machine-local handoff. Unlike
the public GitHub prerelease, the local archive contains third-party mod files,
SKSEVR, the installed Skyrim VR executables, the active test overlay, DevBench,
the patched XRizer runtime and source, FUS configuration sources, and reference
source checkouts used during the port. Do not upload this archive to GitHub,
Nexus, cloud storage, or a public issue.

## Archive Layout

- `bundles/`: the audited public runtime and review handoff.
- `build/`: the newest exact gameplay package and its audited Windows build
  evidence archive. Use these for the next runtime test; the older public alpha
  bundle is retained only as release/history context.
  `LOCAL-MANIFEST.json` binds both archives to one build manifest and records
  the built source revision separately from the newer documentation HEAD. The
  current pair is the `a7b71d90` gameplay build: 503 packaged files, zero
  package/evidence audit failures, embedded network build version
  `stvr-v0.1.0-alpha.1-54-ga7b71d90`, and Linux/Monado proof of new-character
  finalization, exact-version server admission, and interior-cell sync. The
  remaining shutdown and event-subscription findings are recorded in
  `source/Docs/SkyrimVR/runtime-connection-result-20260716-a7b71d90.md`.
- `dependencies/fus-mods/`: pristine FUS mod directories for SKSE scripts, VR
  Address Library, VRIK, HIGGS, PLANCK, Controller Fix VR, SkyUI/VR Tools,
  Realm of Lorkhan, controller bindings, and related configuration fixes.
- `dependencies/current-game-overlay/`: the filtered, currently working direct
  Skyrim VR overlay. It includes SkyrimVR.exe/SkyrimSE.exe, SKSEVR loaders,
  project DLLs, active mod DLLs/configuration, DevBench, FBT, scripts, meshes,
  plugins, and launcher helpers. Bethesda BSA/ESM content, logs, PDBs, stale
  backups, and disabled experiments are omitted.
- `dependencies/xrizer-runtime/`: the active release `libxrizer.so` and runtime
  registration file.
- `dependencies/source-references/`: XRizer, DevBench, HIGGS, PLANCK,
  SkyrimVRTools, VRCustomQuickslots, CommonLibSSE-NG, legacy CommonLibSSE, the
  CommonLib sample, SKSE Menu Framework, and PapyrusTweaks source used for
  compatibility analysis.
- `dependencies/download-archives/`: the exact small dependency archives that
  were supplied for PLANCK, HIGGS, FBT, SkyrimVRTools, and ClibDT setup.
- `review-notes/`: the external agent handoff, friend summary, and supplied
  deep-research code review. Project-authored senior-review briefs and
  dispositions are under `source/Docs/SkyrimVR/`.
- `profiles/`: the current direct Proton plugin/load-order/preferences files and
  the FUS Basic profile configuration.
- `source/`: the newest SkyrimTogetherVR tracked source and initialized
  submodule working trees.
- `source/Docs/SkyrimVR/original-gameplay-parity-checklist.md`: the living
  original-branch parity matrix and per-domain definition of done.
- `source.bundle`: Git history for `main`, `original-skyrim-together`, and the
  alpha release tag.
- `LOCAL-MANIFEST.json`: SHA-256 and size for every payload file.

The archive intentionally does not contain the large base-game BSA/ESM files,
Steam, Proton, Monado, or Docker. Those already exist on this machine or are
system services. The existing test server has no configured password. No raw
runtime/session logs are included.

DevBench's own generated `lib/` checkout is omitted because the same filtered
CommonLibSSE-NG source is included once under `source-references/`. Build trees,
Git object databases, PDBs, and logs are likewise omitted; these are derived
artifacts rather than inputs.

## Existing Machine Paths

```text
Repository: /home/obesecatlord/Documents/SkyrimModding/SkyrimTogetherVR
Skyrim VR: /home/obesecatlord/FasterGames/SteamLibrary/steamapps/common/SkyrimVR
Proton prefix: /home/obesecatlord/FasterGames/SteamLibrary/steamapps/compatdata/611670/pfx
FUS: /home/obesecatlord/LargeGames/FUS
XRizer: /home/obesecatlord/.local/share/envision/ovr_comp
Server: incidentalstoat.xyz:26099/udp
```

Prefer running the existing installation rather than reinstalling it from the
archive. Verify Monado, then launch:

```bash
test -S "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/monado_comp_ipc"
cd /home/obesecatlord/FasterGames/SteamLibrary/steamapps/common/SkyrimVR
./launch-skyrim-together-vr.sh
```

The offline control is `./launch-skyrim-vr-offline.sh`. It disables only the
Skyrim Together plugin and bridge files; it retains VRIK, HIGGS, PLANCK,
Controller Fix VR, and the XRizer controller facade.

## Fresh Legal Install Recovery

For a separate legal Skyrim VR 1.4.15 installation on this same machine:

1. Copy `dependencies/current-game-overlay/` into the new game root.
2. Extract the contents of the public runtime ZIP's single top-level directory
   into that game root, overwriting the project files with the audited build.
3. Restore `profiles/direct-proton/` into the matching Proton prefix paths.
4. Set `STVR_GAME_DIR`, `STVR_STEAM_LIBRARY`, `STVR_COMPATDATA`, and
   `STVR_XRIZER_RUNTIME` if its layout differs from the documented paths.
5. Start Monado first. Use GE-Proton 10-34 or explicitly set
   `STVR_PROTONPATH`.

Do not install multiple controller-binding alternatives over each other. The
working direct overlay is authoritative; the separate FUS binding folders are
included for comparison and recovery.

## DevBench Automation

DevBench 1.9.1 is installed as
`Data/SKSE/Plugins/devbench.dll`; its config and Guardian Stones-to-Whiterun
recording are included. Automation source and detailed input rules live in
`source/Tools/SkyrimVR/devbench_new_game.py`,
`source/Docs/SkyrimVR/devbench-new-game.md`, and `source/AGENTS.md`.

```bash
cd /home/obesecatlord/Documents/SkyrimModding/SkyrimTogetherVR
python3 Tools/SkyrimVR/devbench_new_game.py \
  --launch-game \
  --skyrim-vr /home/obesecatlord/FasterGames/SteamLibrary/steamapps/common/SkyrimVR \
  --vm-update-mode active \
  --connect incidentalstoat.xyz:26099 \
  --timeout 180
```

Never force-close RaceSex Menu. The valid synthetic flow is Monado `N`, verify
the finish/name confirmation, then hold Monado `P`; patched XRizer must deliver
the overlay-specific `KeyboardDone` event.

## Server

Exactly one server container is expected: `skyrim-together-vr`, image
`skyrim-together-vr-server:a7b71d90`, Linux ARM64, host network, UDP 26099,
restart policy `unless-stopped`. Its gameplay/protocol binary revision is
`a7b71d900a72c44e8e31436a245fe448b97d0daa`, matching the packaged client. See
`source/Docs/SkyrimVR/server-deployment.md` for the exact image ID and binary
hashes plus build, Compose, firewall, configuration, and verification
instructions. No password is currently configured for the existing test
server. Leave `STVR_PASSWORD` empty unless the server configuration is changed.

## Test Standard

Connection-only success requires finalized character state, no blocking menu,
active `SkyrimTogether.esp`, lifecycle `ready`, fresh `online=1`, nonzero player
ID, a newer valid current-cell request, and matching server admission. Full
remote gameplay, pose/FBT, HIGGS/PLANCK interactions, and clean shutdown remain
separate evidence gates; do not infer them from connection success.

For the current movement/animation stage, use two clients and retain evidence
for one interior/exterior retained-handle transfer, one deliberate stale-tick
rejection, a complete local and remote graph snapshot, applied graph
acknowledgement, and zero spatial/animation rejection or ring-drop counters.
The complete ordered matrix and per-domain definition of done are in
`source/Docs/SkyrimVR/original-gameplay-parity-checklist.md`.
