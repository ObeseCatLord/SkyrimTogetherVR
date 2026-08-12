# Local Agent Complete Handoff

This is a private, machine-local handoff. It contains third-party mod files,
SKSEVR, installed-game overlay files, the patched XRizer runtime/source, and
reference checkouts. Do **not** upload this archive to GitHub, Nexus, cloud
storage, or a public issue.

## What Is Authoritative

- `build/` holds the installable, audited gameplay package and its paired
  Windows build evidence. Install the gameplay ZIP, not the older public alpha
  runtime ZIP in `bundles/`; that public ZIP is release/history context only.
- `LOCAL-MANIFEST.json` binds every handoff payload and records both the built
  revision and the later source/documentation HEAD. The source HEAD may be
  newer than the build; that is expected, not a reason to substitute artifacts.
- The current pair is built revision `a4b90e01`
  (`a4b90e0129197039f2a7f94170caf618c8ab8965`), network version
  `stvr-v0.1.0-alpha.1-68-ga4b90e01`. Its matching server image is
  `skyrim-together-vr-server:a4b90e01-arm64`.
- `dependencies/current-game-overlay/` is a filtered compatibility overlay.
  It deliberately excludes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.*`
  readout/control files: those are per-process/session state, not portable
  proof. Package code and configuration remain supplied by the authoritative
  gameplay package.

The archive intentionally omits base-game BSA/ESM content, Steam, Proton,
Monado, Docker, raw runtime logs, build trees, PDBs, and Git object databases.

## Install a Second Legal Linux + Monado Client

Extract the handoff ZIP normally, enter its single extracted root, and run the
included installer. It requires a separate legal Skyrim VR 1.4.15 install and
checks that its `SkyrimVR.exe` SHA-256 is
`6961efb4f4775a307b0fc9a3d637542c1e090be207d3b09467eab216b7f87971` before
writing anything. It never launches a game/server or deletes target data.

```bash
cd /path/to/SkyrimTogetherVR-local-agent-complete-handoff-*/
python3 ./INSTALL-SECOND-CLIENT.py \
  --game-dir /path/to/second-steam-library/steamapps/common/SkyrimVR \
  --compatdata /path/to/second-steam-library/steamapps/compatdata/611670
```

Use `--dry-run` first to validate the legal executable, nested gameplay
package/evidence pair, package manifest, and build-to-source ancestry without
mutating either target. The installer then copies the filtered overlay without
replacing the target `SkyrimVR.exe`, extracts `build/`'s gameplay package over
it, restores the three direct-Proton profile files at their exact `pfx` paths,
and marks the launcher helpers executable. It rejects traversal, duplicate, and
symlink ZIP entries.

The restored plugin/load order is exactly:

1. `Skyrim.esm`
2. `Update.esm`
3. `Dawnguard.esm`
4. `HearthFires.esm`
5. `Dragonborn.esm`
6. `SkyrimVR.esm`
7. `higgs_vr.esp`
8. `vrik.esp`
9. `Realm of Lorkhan - Custom Alternate Start - Choose your own adventure.esp`
10. `SkyrimTogether.esp`

Do not overlay multiple controller-binding alternatives. The direct overlay is
the selected working configuration; the FUS folders are recovery/reference
material only.

## Monado and Launch Order

Start or verify Monado using the helper carried in `source/`. Replace the
profile name with the second machine's Envision profile UUID when it does not
have the local `simulated-qwerty-fixed` profile:

```bash
./source/Tools/SkyrimVR/linux/manage-monado-runtime.sh start simulated-qwerty-fixed
./source/Tools/SkyrimVR/linux/manage-monado-runtime.sh status
```

For the first smoke test, launch offline first from the newly installed game
directory. This disables only Skyrim Together files and preserves the VR
compatibility stack.

```bash
cd /path/to/second-steam-library/steamapps/common/SkyrimVR
./launch-skyrim-vr-offline.sh
```

For layouts other than the original machine, set the launch-script overrides
before launching online: `STVR_STEAM_ROOT`, `STVR_STEAM_LIBRARY`,
`STVR_COMPATDATA`, `STVR_WINEPREFIX`, `STVR_XRIZER_RUNTIME`, `STVR_PROTONPATH`,
or `STVR_LAUNCHER`. The launchers derive the game directory from their own
location, so invoke the copy installed in the desired game directory.

```bash
STVR_STEAM_LIBRARY=/path/to/second-steam-library \
STVR_COMPATDATA=/path/to/second-steam-library/steamapps/compatdata/611670 \
STVR_XRIZER_RUNTIME=/path/to/extracted-handoff/dependencies/xrizer-runtime \
STVR_PROTONPATH=/path/to/GE-Proton10-34 \
./launch-skyrim-together-vr.sh
```

Use endpoint `incidentalstoat.xyz:26099`. The server currently has no password;
leave `STVR_PASSWORD` empty unless its configuration changes.

## Two-Client Staged Checks

1. With client two alone, complete the offline-first launch and confirm the
   base VR/mod stack is healthy before enabling Skyrim Together.
2. Start client one online, finalize its character with no blocking menu, and
   confirm the expected `a4b90e01` admission at `incidentalstoat.xyz:26099`.
3. Start client two online with the same endpoint. Confirm distinct player IDs,
   current-cell readiness, and matching server admission before testing remote
   movement, poses, HIGGS/PLANCK, or animation.

Connection success is not complete gameplay proof. Retain evidence for the
interior/exterior transfer, stale-tick rejection, graph acknowledgement, and
zero spatial/animation rejection or ring-drop counters as defined in
`source/Docs/SkyrimVR/original-gameplay-parity-checklist.md`.
