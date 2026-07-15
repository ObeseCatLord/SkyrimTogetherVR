# Linux and Monado Prerelease Guide

This guide covers the connection-only `stvr-v0.1.0-alpha.1` package. It has
reached Skyrim VR, finalized a player, connected to the test server, received a
nonzero player ID, and published the current cell. It is not a claim that
remote avatars, combat, quests, inventory, PLANCK/HIGGS interaction, VRIK pose,
or full-body tracking are release-ready.

## Prerequisites

- A legal Steam installation of Skyrim VR 1.4.15 (App ID 611670).
- SKSEVR 2.0.12, installed into the Skyrim VR directory.
- VR Address Library for Skyrim VR.
- VRIK, HIGGS, and PLANCK installed according to their own instructions.
- Monado or Envision with a working OpenXR session and
  `$XDG_RUNTIME_DIR/monado_comp_ipc` socket.
- XRizer built from the tested base and project patch described below.
- Proton; GE-Proton 10-34 is the currently verified version. `umu-run` is
  supported and preferred when installed.

Do not copy SkyrimVR.exe, SKSEVR, Address Library, or FUS files from this
archive. Obtain those components from their owners. Back up the game and Proton
prefix before changing an established mod list.

## Install

Open the runtime archive and extract the contents of its single top-level
`SkyrimTogetherVR-stvr-*` directory into the Skyrim VR directory. The result
must include:

```text
SkyrimTogetherVR.exe
Data/SkyrimTogether.esp
Data/SKSE/Plugins/SkyrimTogetherVRTickBridge.dll
launch-skyrim-together-vr.sh
launch-skyrim-vr-offline.sh
stvr-xrizer-input-compat.sh
```

Make the launchers executable:

```bash
chmod +x launch-skyrim-together-vr.sh launch-skyrim-vr-offline.sh \
  stvr-xrizer-input-compat.sh
```

The launch helper derives the Steam library from a standard
`steamapps/common/SkyrimVR` layout. Override discovery when necessary:

```bash
export STVR_STEAM_ROOT="$HOME/.local/share/Steam"
export STVR_STEAM_LIBRARY="/path/to/SteamLibrary"
export STVR_PROTONPATH="$HOME/.local/share/Steam/compatibilitytools.d/GE-Proton10-34"
export STVR_COMPATDATA="$STVR_STEAM_LIBRARY/steamapps/compatdata/611670"
```

Inspect the command without starting VR:

```bash
STVR_DRY_RUN=1 ./launch-skyrim-together-vr.sh
```

The helper uses Proton's stable `Z:` host mapping for `--exePath`; a transient
custom drive mapping can disappear while Proton initializes the prefix and
cause launcher error 161.

## XRizer Compatibility Patch

The package includes `Tools/SkyrimVR/xrizer-skyrimvr-monado.patch`. It is based
on XRizer commit `31319560c1bd0f1e5c16936a946bb1c7295dbfd9` and provides the
legacy Index-to-Oculus facade, trigger mapping, safe compositor timing query,
and the overlay keyboard completion Skyrim VR needs during character creation.

```bash
git clone https://github.com/Supreeeme/xrizer.git
cd xrizer
git checkout 31319560c1bd0f1e5c16936a946bb1c7295dbfd9
git apply /path/to/SkyrimTogetherVR/Tools/SkyrimVR/xrizer-skyrimvr-monado.patch
cargo build --release
export STVR_XRIZER_RUNTIME="$PWD/target/release"
```

The default XRizer feature is Monado. The launcher also checks
`PROTON_VR_RUNTIME` and the first valid runtime in
`~/.config/openvr/openvrpaths.vrpath`. Index controllers default to
`XRIZER_OPENVR_KNUCKLES_AS_OCULUS_TOUCH=1` because that is the mapping verified
with the FUS control map; set it to `0` to keep native Knuckles identifiers.

## Launch and Connect

Start Monado first and verify its socket:

```bash
pgrep -af 'monado-service|envision'
test -S "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/monado_comp_ipc"
```

The public test endpoint is the default:

```bash
./launch-skyrim-together-vr.sh
```

Override it for another server:

```bash
STVR_AUTOCONNECT=example.net:26099 ./launch-skyrim-together-vr.sh
```

Set `STVR_DISABLE_AUTOCONNECT=1` to launch the Together client without an
automatic connection. The current test server is password-protected; supply
it through the normal connection UI or a private `STVR_PASSWORD` environment
value and never place it in logs, scripts, or bug reports.

Use `./launch-skyrim-vr-offline.sh` for a control run. It removes only the exact
`SkyrimTogether.esp` profile entries and moves project bridge files into
`Data/SKSE/Plugins/stvr-disabled`; it does not disable VRIK, HIGGS, or PLANCK.

## Character Creation and Automation

Normal headset/controller input is the release path. For deterministic CI-like
testing, see `Docs/SkyrimVR/devbench-new-game.md`. That flow additionally needs
DevBench 1.9.1, `kdotool`, `ydotool`/`ydotoold`, writable `/dev/uinput`, the
project Monado Qwerty patch, and the patched XRizer overlay keyboard.

Never force-close `RaceSex Menu`. The valid automated sequence is Monado `N`
to request Done, verify `Finish and name your character?`, then hold Monado `P`
to accept. XRizer must emit the overlay-specific `KeyboardDone` event. Hiding
the menu bypasses Skyrim's finalization transaction and has reproduced a crash.

## Test Gates

Record the package version, binary manifest revision, SkyrimVR.exe hash, SKSEVR
version, Proton version, XRizer revision, headset/controller type, mod list, and
server endpoint. Test in this order:

1. Offline launch reaches a finalized player with VRIK/HIGGS/PLANCK loaded.
2. Together launch reaches a finalized player without `Main Menu`,
   `RaceSex Menu`, loading/fader menus, or a message box remaining open.
3. `SkyrimTogether.esp` is active and lifecycle reaches `state=ready`.
4. Online status is fresh, `online=1`, and `playerId` is nonzero.
5. A newer current-cell request has a nonzero server cell ID and no translation
   failure.
6. The server log admits the same client while exactly one server instance is
   running.
7. A normal exit reaches the launcher shutdown hook without a new crash dump,
   owner mismatch, starvation, or reentrancy warning.

Connection and current-cell success have been observed. Clean normal exit with
the newest crash handler and multiplayer gameplay synchronization remain open
prerelease gates. Do not report those areas as passed without fresh evidence.
