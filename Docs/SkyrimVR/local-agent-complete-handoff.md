# Local Agent Complete Handoff

This is a private, machine-local handoff. It contains third-party mod files,
SKSEVR, installed-game overlay files, a Linux XRizer runtime/source snapshot,
and reference checkouts. Do **not** upload this archive to GitHub, Nexus, cloud
storage, or a public issue.

## Authoritative Identity

- `LOCAL-MANIFEST.json` is authoritative for the built source revision, exact
  gameplay-package and build-evidence hashes, nested build-manifest identity,
  and configured server endpoint. Do not infer identity from this document,
  an archive filename, or an older public runtime ZIP.
- `build/` contains the exact installable gameplay package and its paired
  Windows build evidence. The installers verify the manifest records and the
  package/evidence pair before writing target files.
- `dependencies/current-game-overlay/` is a filtered compatibility overlay.
  It deliberately excludes `Data/SkyrimTogetherReborn/SkyrimTogetherVR.*`
  session readout/control files. Those files are per-process state, not
  portable proof.
- The Windows installer never copies `dependencies/xrizer-runtime/`, Linux
  launch scripts, or the handoff's `openvr_api.dll` into a Windows game root.
  Windows uses its existing SteamVR/OpenXR installation.

The archive intentionally omits base-game BSA/ESM content, Steam, Proton,
Monado, Docker, raw runtime logs, build trees, PDBs, and Git object databases.

## Windows Client

Prerequisites: extract the archive normally, keep its single extracted root
intact, use an existing legal Skyrim VR 1.4.15 installation, and run the
commands from that root in Command Prompt or PowerShell. The installer requires
the exact legal
`SkyrimVR.exe` SHA-256
`6961efb4f4775a307b0fc9a3d637542c1e090be207d3b09467eab216b7f87971`.
It rejects a game root or target path containing a reparse point, so use a
direct non-junction path for the target installation.

Dry run is the default and makes no target changes:

```bat
cd /d C:\path\to\SkyrimTogetherVR-local-agent-complete-handoff-*
INSTALL-SECOND-CLIENT-WINDOWS.bat -GameDir "D:\SteamLibrary\steamapps\common\SkyrimVR"
```

Use the explicit install switch only after the dry run passes. This installs
the exact gameplay package and only portable `Data`/SKSEVR overlay data. It
never replaces `SkyrimVR.exe`.

```bat
INSTALL-SECOND-CLIENT-WINDOWS.bat -GameDir "D:\SteamLibrary\steamapps\common\SkyrimVR" -Install
```

The optional profile operation copies only the bundled `Plugins.txt` and
`loadorder.txt` to `%LOCALAPPDATA%\Skyrim VR`. It does not overwrite
`SkyrimPrefs.ini`. First review it as a dry run, then enable it explicitly:

```bat
INSTALL-SECOND-CLIENT-WINDOWS.bat -GameDir "D:\SteamLibrary\steamapps\common\SkyrimVR" -EnableProfile
INSTALL-SECOND-CLIENT-WINDOWS.bat -GameDir "D:\SteamLibrary\steamapps\common\SkyrimVR" -Install -EnableProfile
```

`INSTALL-SECOND-CLIENT-WINDOWS.bat` invokes the checked PowerShell installer
with a process-scoped execution-policy bypass; it does not change the machine's
persistent policy. The equivalent explicit PowerShell form is:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\INSTALL-SECOND-CLIENT-WINDOWS.ps1 -GameDir 'D:\SteamLibrary\steamapps\common\SkyrimVR'
```

After installation, launch the packaged VR client from the game directory:

```powershell
Set-Location 'D:\SteamLibrary\steamapps\common\SkyrimVR'
.\SkyrimTogetherVRGameplay.exe
```

Use the normal local SteamVR/OpenXR runtime. Do not copy Linux XRizer or
OpenVR runtime files from this handoff to Windows.

## Linux Client

Prerequisites: extract the archive normally, retain the single extracted root,
provide a separate legal Skyrim VR 1.4.15 install and its Steam app `611670`
Proton prefix. The included Linux installer retains its existing exact
`SkyrimVR.exe` SHA-256 gate and never launches a game or server.

```bash
cd /path/to/SkyrimTogetherVR-local-agent-complete-handoff-*/
python3 ./INSTALL-SECOND-CLIENT.py \
  --game-dir /path/to/second-steam-library/steamapps/common/SkyrimVR \
  --compatdata /path/to/second-steam-library/steamapps/compatdata/611670 \
  --dry-run

python3 ./INSTALL-SECOND-CLIENT.py \
  --game-dir /path/to/second-steam-library/steamapps/common/SkyrimVR \
  --compatdata /path/to/second-steam-library/steamapps/compatdata/611670 \
  --install
```

Without `--install`, the Linux command always performs validation only and does
not change either target. An explicit install restores the three direct-Proton
profile files, including `SkyrimPrefs.ini`; this differs from the optional
Windows profile operation above.

The refreshed installer saves every overwritten file and records every newly
created file under `.stvr-local-agent-handoff-state` in the target game
directory. Repeating the same install is idempotent. To uninstall, pass the
same target paths:

```bash
python3 ./INSTALL-SECOND-CLIENT.py \
  --game-dir /path/to/second-steam-library/steamapps/common/SkyrimVR \
  --compatdata /path/to/second-steam-library/steamapps/compatdata/611670 \
  --uninstall
```

Uninstall removes only unchanged files created by that transaction and
restores its verified backups. It refuses to overwrite or remove files changed
after installation. `--uninstall --force` deliberately discards those later
changes and should be used only after reviewing the reported paths.

Handoffs produced before this transaction support cannot be completely
reversed safely because they did not preserve overwritten files. For an older
handoff that installed launchers containing `/home/obesecatlord`, extract the
refreshed handoff and replace only the three launchers with its canonical
copies:

```bash
HANDOFF=/path/to/refreshed-handoff-root
GAME_DIR=/path/to/steam-library/steamapps/common/SkyrimVR
install -m 0755 "$HANDOFF/source/Tools/SkyrimVR/linux/launch-skyrim-together-vr.sh" "$GAME_DIR/"
install -m 0755 "$HANDOFF/source/Tools/SkyrimVR/linux/launch-skyrim-vr-offline.sh" "$GAME_DIR/"
install -m 0755 "$HANDOFF/source/Tools/SkyrimVR/linux/stvr-xrizer-input-compat.sh" "$GAME_DIR/"
STVR_GAME_DIR="$GAME_DIR" STVR_DRY_RUN=1 "$GAME_DIR/launch-skyrim-together-vr.sh"
```

Set `STVR_STEAM_ROOT`, `STVR_STEAM_LIBRARY`, `STVR_COMPATDATA`, or
`STVR_PROTONPATH` only when automatic discovery does not match that machine.
Do not use the old handoff's launcher copies for this repair.

Start or verify Monado using the helper carried in `source/`, replacing the
profile UUID when needed:

```bash
./source/Tools/SkyrimVR/linux/manage-monado-runtime.sh start simulated-qwerty-fixed
./source/Tools/SkyrimVR/linux/manage-monado-runtime.sh status
```

Launch an offline smoke test first from the installed game directory, then use
the online launcher. Set the documented override variables when the second
machine uses different Steam, Proton, XRizer, or compatdata locations.

```bash
cd /path/to/second-steam-library/steamapps/common/SkyrimVR
./launch-skyrim-vr-offline.sh
./launch-skyrim-together-vr.sh
```

## Server Match And Evidence

Read the generated manifest before connection testing. Its `serverEndpoint`
field is the currently configured endpoint for that handoff. At the time this
guide was updated it may be `incidentalstoat.xyz:26099`, but that endpoint is
operational configuration and can change. Both clients must use the same
current endpoint and must be admitted by a server compatible with the exact
manifest/package build identity; successful transport alone is not gameplay
proof.

On Linux, inspect the manifest and pass the selected endpoint to the launcher
or staged helper:

```bash
python3 -c 'import json; print(json.load(open("LOCAL-MANIFEST.json"))["serverEndpoint"])'
```

On Windows, inspect the same field before entering it in the packaged launcher:

```powershell
(Get-Content .\LOCAL-MANIFEST.json -Raw | ConvertFrom-Json).serverEndpoint
```

For each client, retain the installed package build manifest, `logs\tp_client.log`
or its Proton equivalent, relevant SKSEVR logs, and
`Data/SkyrimTogetherReborn` handoff files after the test. The packaged runtime
evidence helpers are the preferred collection path when present:

`CollectSkyrimTogetherVREvidence-Windows.bat` on Windows and
`python3 source/Tools/SkyrimVR/collect_runtime_evidence.py --help` on Linux.
Collect evidence only after both clients have matching admission, distinct
player IDs, current-cell readiness, and the required movement/pose/gameplay
checks. Follow the acceptance requirements in
`source/Docs/SkyrimVR/original-gameplay-parity-checklist.md`.
