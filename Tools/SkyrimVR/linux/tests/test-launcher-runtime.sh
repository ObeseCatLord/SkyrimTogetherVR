#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
TOOLS_DIR="$SCRIPT_DIR/.."
TMPDIR_LOCAL="$(mktemp -d)"
trap 'rm -rf -- "$TMPDIR_LOCAL"' EXIT

game="$TMPDIR_LOCAL/library/steamapps/common/SkyrimVR"
compat="$TMPDIR_LOCAL/library/steamapps/compatdata/611670"
steam="$TMPDIR_LOCAL/steam"
proton="$TMPDIR_LOCAL/proton"
library_proton="$TMPDIR_LOCAL/library/steamapps/common/Proton Secondary"
runtime="$TMPDIR_LOCAL/runtime"
monado="$TMPDIR_LOCAL/monado-prefix"
host_runtime="$TMPDIR_LOCAL/host-runtime"
opencomposite="$TMPDIR_LOCAL/opencomposite"
game_without_opencomposite="$TMPDIR_LOCAL/library/steamapps/common/SkyrimVR-no-opencomposite"
steamvr="$TMPDIR_LOCAL/steamvr"
home="$TMPDIR_LOCAL/home"
config="$TMPDIR_LOCAL/config"
write_elf64_x86_64() {
  # ELF64 little-endian x86-64 ET_DYN with a 64-byte header and one 56-byte
  # PT_LOAD entry spanning the complete 120-byte file.
  printf '%s' 'f0VMRgIBAQAAAAAAAAAAAAMAPgABAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAEAAOAABAAAAAAAAAAEAAAAFAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAeAAAAAAAAAB4AAAAAAAAAAAQAAAAAAAA' | base64 -d > "$1"
}
write_build_manifest_fixture() {
  cat > "$1" <<'EOF'
{
  "schema": "skyrim_together_vr_build_package_v2",
  "platform": "windows",
  "arch": "x64",
  "packageFlavor": "gameplay",
  "buildVersion": "stvr-launcher-fixture",
  "networkVersion": "stvr-launcher-fixture",
  "sourceRevision": "0123456789abcdef0123456789abcdef01234567",
  "sourceProvenance": {
    "revision": "0123456789abcdef0123456789abcdef01234567",
    "sourceTreeSha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
    "dirty": false,
    "dirtyApproved": false
  }
}
EOF
}
mkdir -p "$game" "$compat/pfx" "$steam" "$proton" "$library_proton" "$runtime" \
  "$game/Data" \
  "$monado/lib" "$monado/share/openxr/1" "$host_runtime/pulse" \
  "$game/.stvr-openvr/xrizer/bin/linux64" "$game/.stvr-openvr/opencomposite/bin/linux64" \
  "$opencomposite/bin/linux64" \
  "$steamvr/bin/linux64" "$game_without_opencomposite/.stvr-openvr" "$home" "$config/openvr" "$config/openxr/1"
touch "$game/SkyrimVR.exe" "$game/SkyrimTogetherVRGameplay.exe" "$game/sksevr_loader.exe"
touch "$game/Data/Skyrim.esm" "$game/Data/Update.esm" "$game/Data/Dawnguard.esm" \
  "$game/Data/HearthFires.esm" "$game/Data/Dragonborn.esm" "$game/Data/SkyrimVR.esm" \
  "$game/Data/higgs_vr.esp" "$game/Data/vrik.esp" \
  "$game/Data/Realm of Lorkhan - Custom Alternate Start - Choose your own adventure.esp" \
  "$game/Data/SkyrimTogether.esp"
write_elf64_x86_64 "$game/.stvr-openvr/xrizer/bin/linux64/vrclient.so"
cp "$game/.stvr-openvr/xrizer/bin/linux64/vrclient.so" "$game/.stvr-openvr/xrizer/libxrizer.so"
write_elf64_x86_64 "$game/.stvr-openvr/opencomposite/bin/linux64/vrclient.so"
write_elf64_x86_64 "$opencomposite/bin/linux64/vrclient.so"
write_elf64_x86_64 "$steamvr/bin/linux64/vrclient.so"
touch "$steamvr/bin/linux64/vrserver"
touch "$game_without_opencomposite/SkyrimVR.exe" "$game_without_opencomposite/SkyrimTogetherVRGameplay.exe" \
  "$game_without_opencomposite/sksevr_loader.exe"
write_build_manifest_fixture "$game/SkyrimTogetherVR_BuildManifest.json"
write_build_manifest_fixture "$game_without_opencomposite/SkyrimTogetherVR_BuildManifest.json"
printf '%s\n' '{"version":1,"runtime":[]}' > "$game/.stvr-openvr/openvrpaths.vrpath"
printf '%s\n' '{"version":1,"runtime":[]}' > "$game_without_opencomposite/.stvr-openvr/openvrpaths.vrpath"
touch "$monado/lib/libopenxr_monado.so" "$monado/lib/libmonado.so"
printf '%s\n' '#!/usr/bin/env bash' '[ -z "${STVR_FAKE_PROTON_READY:-}" ] || : > "$STVR_FAKE_PROTON_READY"' '[ "${STVR_FAKE_PROTON_SLEEP:-0}" = 0 ] || sleep "$STVR_FAKE_PROTON_SLEEP"' 'exit 0' > "$proton/proton"
chmod +x "$proton/proton"
cp "$proton/proton" "$library_proton/proton"
cat > "$monado/share/openxr/1/openxr_monado.json" <<'EOF'
{"file_format_version":"1.0.0","runtime":{"library_path":"../../../lib/libopenxr_monado.so","MND_libmonado_path":"../../../lib/libmonado.so"}}
EOF

manifest="$monado/share/openxr/1/openxr_monado.json"
active_runtime="$config/openxr/1/active_runtime.json"
ln -s "$manifest" "$active_runtime"
python3 - "$host_runtime/monado_comp_ipc" <<'PY'
import socket
import sys

sock = socket.socket(socket.AF_UNIX)
sock.bind(sys.argv[1])
PY
common_env=(
  STVR_DRY_RUN=1
  STVR_GAME_DIR="$game"
  STVR_COMPATDATA="$compat"
  STVR_STEAM_ROOT="$steam"
  STVR_PROTONPATH="$proton"
  STVR_MONADO_RUNTIME_DIR="$runtime"
  STVR_MONADO_IPC_SOCKET="$runtime/monado_comp_ipc"
  STVR_MONADO_PREFIX="$monado"
  STVR_MONADO_XR_RUNTIME_JSON="$manifest"
  STVR_MONADO_LIBRARY_PATH="$monado/lib"
  STVR_MONADO_HOST_MOUNTS="$host_runtime/wayland-test:$host_runtime/bus:$host_runtime/pulse/native:$host_runtime/pipewire-0"
  XDG_RUNTIME_DIR="$host_runtime"
  WAYLAND_DISPLAY=wayland-test
  DBUS_SESSION_BUS_ADDRESS=unix:path=bus
  PULSE_SERVER=unix:pulse/native
  PIPEWIRE_REMOTE=pipewire-0
  HOME="$home"
  XDG_CONFIG_HOME="$config"
  STVR_TEST_SKIP_MONADO_CHECK=1
)

# Dry runs must not create a profile or the long-lived game-root lock.
env "${common_env[@]}" STVR_FORCE_PROTON=1 "$TOOLS_DIR/launch-skyrim-together-vr.sh" >/dev/null
[ ! -e "$game/.stvr-launch.lock" ]
[ ! -e "$compat/pfx/drive_c/users/steamuser/AppData/Local/Skyrim VR/Plugins.txt" ]

read_xrizer_keyboard_text() {
  env -u STVR_XRIZER_KEYBOARD_TEXT \
    GAME_DIR="$game" HOME="$home" XDG_CONFIG_HOME="$config" \
    bash -c '
      set -euo pipefail
      stvr_append_pressure_vessel_ro() { :; }
      source "$1"
      printf "%s" "$STVR_XRIZER_KEYBOARD_TEXT"
    ' _ "$TOOLS_DIR/stvr-xrizer-input-compat.sh"
}

[ "$(read_xrizer_keyboard_text)" = Prisoner ] || {
  printf 'XRizer helper did not provide the manual character-name fallback\n' >&2
  exit 1
}
custom_name="$(env STVR_XRIZER_KEYBOARD_TEXT=ManualIndexUser \
  GAME_DIR="$game" HOME="$home" XDG_CONFIG_HOME="$config" \
  bash -c '
    set -euo pipefail
    stvr_append_pressure_vessel_ro() { :; }
    source "$1"
    printf "%s" "$STVR_XRIZER_KEYBOARD_TEXT"
  ' _ "$TOOLS_DIR/stvr-xrizer-input-compat.sh")"
[ "$custom_name" = ManualIndexUser ] || {
  printf 'XRizer helper replaced an explicit character name\n' >&2
  exit 1
}
if env STVR_XRIZER_KEYBOARD_TEXT= \
  GAME_DIR="$game" HOME="$home" XDG_CONFIG_HOME="$config" \
  bash -c '
    set -euo pipefail
    stvr_append_pressure_vessel_ro() { :; }
    source "$1"
  ' _ "$TOOLS_DIR/stvr-xrizer-input-compat.sh" >/dev/null 2>&1; then
  printf 'XRizer helper accepted an empty automatic character name\n' >&2
  exit 1
fi

online="$(env "${common_env[@]}" STVR_FORCE_PROTON=1 GAMEID=umu-611670-alice "$TOOLS_DIR/launch-skyrim-together-vr.sh")"
offline="$(env "${common_env[@]}" STVR_FORCE_PROTON=1 GAMEID=umu-611670-alice "$TOOLS_DIR/launch-skyrim-vr-offline.sh")"
grep -Fq "Monado runtime: $runtime" <<<"$online"
grep -Fq "Monado runtime: $runtime" <<<"$offline"
grep -Fq 'OpenVR runtime: xrizer' <<<"$online"
grep -Fq "OpenVR runtime path: $game/.stvr-openvr/xrizer" <<<"$online"
grep -Fq "VR override: $game/.stvr-openvr/xrizer" <<<"$offline"
grep -Fq "VR pathreg override: $game/.stvr-openvr/openvrpaths.vrpath" <<<"$offline"
grep -Fq 'Proton VR runtime: none' <<<"$offline"
grep -Fq "XR runtime: $manifest" <<<"$online"
grep -Fq 'GAMEID: umu-611670' <<<"$online"
grep -Fq 'GAMEID: umu-611670' <<<"$offline"
grep -Fq "Pressure vessel RW: $runtime:$host_runtime/wayland-test:$host_runtime/bus:$host_runtime/pulse/native:$host_runtime/pipewire-0" <<<"$online"
grep -Fq "Pressure vessel RW: $runtime:$host_runtime/wayland-test:$host_runtime/bus:$host_runtime/pulse/native:$host_runtime/pipewire-0" <<<"$offline"
grep -Fq "Pressure vessel RO: $game/.stvr-openvr/xrizer:$game/.stvr-openvr:$monado" <<<"$online"
grep -Fq 'Server: incidentalstoat.xyz:26099' <<<"$online"
grep -Fq "$game/SkyrimTogetherVRGameplay.exe" <<<"$online"

# packageFlavor is parsed as JSON, not by matching a lookalike inside a string.
cat > "$game/SkyrimTogetherVR_BuildManifest.json" <<'EOF'
{
  "schema": "skyrim_together_vr_build_package_v2",
  "platform": "windows",
  "arch": "x64",
  "packageFlavor": "gameplay",
  "note": "\"packageFlavor\":\"avatar-sync\"",
  "buildVersion": "stvr-launcher-fixture",
  "networkVersion": "stvr-launcher-fixture",
  "sourceRevision": "0123456789abcdef0123456789abcdef01234567",
  "sourceProvenance": {
    "revision": "0123456789abcdef0123456789abcdef01234567",
    "sourceTreeSha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
    "dirty": false,
    "dirtyApproved": false
  }
}
EOF
structured_manifest_output="$(env "${common_env[@]}" STVR_FORCE_PROTON=1 "$TOOLS_DIR/launch-skyrim-together-vr.sh")"
grep -Fq "$game/SkyrimTogetherVRGameplay.exe" <<<"$structured_manifest_output"

for manifest_case in schema platform arch package_flavor empty_version long_version invalid_version \
  sentinel_version network_version source_revision source_provenance provenance_revision \
  source_tree_hash dirty dirty_approved; do
  python3 - "$game/SkyrimTogetherVR_BuildManifest.json" "$manifest_case" <<'PY'
import json
import sys

manifest = {
    "schema": "skyrim_together_vr_build_package_v2",
    "platform": "windows",
    "arch": "x64",
    "packageFlavor": "gameplay",
    "buildVersion": "stvr-launcher-fixture",
    "networkVersion": "stvr-launcher-fixture",
    "sourceRevision": "0123456789abcdef0123456789abcdef01234567",
    "sourceProvenance": {
        "revision": "0123456789abcdef0123456789abcdef01234567",
        "sourceTreeSha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "dirty": False,
        "dirtyApproved": False,
    },
}
case = sys.argv[2]
if case == "schema":
    manifest["schema"] = "skyrim_together_vr_build_package_v1"
elif case == "platform":
    manifest["platform"] = "linux"
elif case == "arch":
    manifest["arch"] = "arm64"
elif case == "package_flavor":
    manifest["packageFlavor"] = "experimental"
elif case == "empty_version":
    manifest["buildVersion"] = manifest["networkVersion"] = ""
elif case == "long_version":
    manifest["buildVersion"] = manifest["networkVersion"] = "v" * 129
elif case == "invalid_version":
    manifest["buildVersion"] = manifest["networkVersion"] = "valid\x1fversion"
elif case == "sentinel_version":
    manifest["buildVersion"] = manifest["networkVersion"] = "UnKnOwN-fixture"
elif case == "network_version":
    manifest["networkVersion"] = "different-version"
elif case == "source_revision":
    manifest["sourceRevision"] = "not-a-revision"
elif case == "source_provenance":
    manifest["sourceProvenance"] = None
elif case == "provenance_revision":
    manifest["sourceProvenance"]["revision"] = "f" * 40
elif case == "source_tree_hash":
    manifest["sourceProvenance"]["sourceTreeSha256"] = "f" * 63
elif case == "dirty":
    manifest["sourceProvenance"]["dirty"] = True
elif case == "dirty_approved":
    manifest["sourceProvenance"]["dirtyApproved"] = True
else:
    raise SystemExit(f"unknown manifest case: {case}")

with open(sys.argv[1], "w", encoding="utf-8") as destination:
    json.dump(manifest, destination)
PY
  if env "${common_env[@]}" STVR_FORCE_PROTON=1 "$TOOLS_DIR/launch-skyrim-together-vr.sh" \
    >"$TMPDIR_LOCAL/manifest-$manifest_case.out" 2>"$TMPDIR_LOCAL/manifest-$manifest_case.err"; then
    printf 'launcher accepted invalid build manifest case: %s\n' "$manifest_case" >&2
    exit 1
  fi
  grep -Fq 'invalid SkyrimTogetherVR build manifest' "$TMPDIR_LOCAL/manifest-$manifest_case.err"
done
if env "${common_env[@]}" STVR_FORCE_PROTON=1 STVR_LAUNCHER="$game/SkyrimTogetherVRGameplay.exe" \
  "$TOOLS_DIR/launch-skyrim-together-vr.sh" >/dev/null 2>&1; then
  printf 'custom launcher bypassed invalid build manifest admission\n' >&2
  exit 1
fi
write_build_manifest_fixture "$game/SkyrimTogetherVR_BuildManifest.json"

custom_launcher="$TMPDIR_LOCAL/custom-launcher.exe"
custom_game="$TMPDIR_LOCAL/custom-game.exe"
custom_proton="$TMPDIR_LOCAL/custom-proton"
ln -s "$game/SkyrimTogetherVRGameplay.exe" "$custom_launcher"
ln -s "$game/SkyrimVR.exe" "$custom_game"
ln -s "$proton/proton" "$custom_proton"
canonical_paths_output="$(env "${common_env[@]}" STVR_FORCE_PROTON=1 STVR_LAUNCHER="$custom_launcher" \
  STVR_GAME_EXE="$custom_game" STVR_PROTON="$custom_proton" "$TOOLS_DIR/launch-skyrim-together-vr.sh")"
grep -Fq "$game/SkyrimTogetherVRGameplay.exe" <<<"$canonical_paths_output"
grep -Fq "Windows game path: Z:${game//\//\\}" <<<"$canonical_paths_output"
grep -Fq "$proton/proton run" <<<"$canonical_paths_output"
nonexecutable_proton="$TMPDIR_LOCAL/nonexecutable-proton"
touch "$nonexecutable_proton"
if env "${common_env[@]}" STVR_FORCE_PROTON=1 STVR_PROTON="$nonexecutable_proton" \
  "$TOOLS_DIR/launch-skyrim-together-vr.sh" >/dev/null 2>&1; then
  printf 'launcher accepted a non-executable direct Proton path\n' >&2
  exit 1
fi
nonce='0123456789abcdef0123456789abcdef'
nonce_output="$(env "${common_env[@]}" STVR_LAUNCH_NONCE="$nonce" "$TOOLS_DIR/launch-skyrim-together-vr.sh")"
grep -Fq "Launch nonce: $nonce" <<<"$nonce_output"
grep -Fq "Windows game path: Z:${game//\//\\}" <<<"$nonce_output"
if env "${common_env[@]}" STVR_LAUNCH_NONCE=not-a-nonce "$TOOLS_DIR/launch-skyrim-together-vr.sh" >/dev/null 2>&1; then
  printf 'launcher accepted an invalid launch nonce\n' >&2
  exit 1
fi

default_online="$(env -u STVR_MONADO_RUNTIME_DIR -u STVR_MONADO_IPC_SOCKET \
  -u STVR_MONADO_PREFIX -u STVR_MONADO_XR_RUNTIME_JSON -u STVR_MONADO_LIBRARY_PATH \
  -u STVR_MONADO_HOST_MOUNTS -u PRESSURE_VESSEL_FILESYSTEMS_RW -u XR_RUNTIME_JSON \
  STVR_DRY_RUN=1 STVR_FORCE_PROTON=1 STVR_GAME_DIR="$game" STVR_COMPATDATA="$compat" \
  STVR_STEAM_ROOT="$steam" STVR_PROTONPATH="$proton" XDG_RUNTIME_DIR="$host_runtime" HOME="$home" XDG_CONFIG_HOME="$config" \
  "$TOOLS_DIR/launch-skyrim-together-vr.sh")"
default_offline="$(env -u STVR_MONADO_RUNTIME_DIR -u STVR_MONADO_IPC_SOCKET \
  -u STVR_MONADO_PREFIX -u STVR_MONADO_XR_RUNTIME_JSON -u STVR_MONADO_LIBRARY_PATH \
  -u STVR_MONADO_HOST_MOUNTS -u PRESSURE_VESSEL_FILESYSTEMS_RW -u XR_RUNTIME_JSON \
  STVR_DRY_RUN=1 STVR_FORCE_PROTON=1 STVR_GAME_DIR="$game" STVR_COMPATDATA="$compat" \
  STVR_STEAM_ROOT="$steam" STVR_PROTONPATH="$proton" XDG_RUNTIME_DIR="$host_runtime" HOME="$home" XDG_CONFIG_HOME="$config" \
  "$TOOLS_DIR/launch-skyrim-vr-offline.sh")"
grep -Fq 'Monado runtime: default' <<<"$default_online"
grep -Fq 'Monado runtime: default' <<<"$default_offline"
grep -Fq "XR runtime: $manifest" <<<"$default_online"
grep -Fq "XR runtime: $manifest" <<<"$default_offline"
grep -Fq "OpenXR library path: $monado/lib" <<<"$default_online"
grep -Fq "OpenXR library path: $monado/lib" <<<"$default_offline"
grep -Fq "Pressure vessel RW: $host_runtime/monado_comp_ipc" <<<"$default_online"
grep -Fq "Pressure vessel RW: $host_runtime/monado_comp_ipc" <<<"$default_offline"
grep -Fq "Pressure vessel RO: $game/.stvr-openvr/xrizer:$game/.stvr-openvr:$monado" <<<"$default_online"
grep -Fq "Pressure vessel RO: $game/.stvr-openvr/xrizer:$game/.stvr-openvr:$monado" <<<"$default_offline"

bad_manifest="$TMPDIR_LOCAL/bad-active-runtime.json"
printf '%s\n' '{not json' > "$bad_manifest"
ln -sfn "$bad_manifest" "$active_runtime"
explicit_runtime="$TMPDIR_LOCAL/explicit-runtime.json"
ln -s "$manifest" "$explicit_runtime"
for launcher in "$TOOLS_DIR/launch-skyrim-together-vr.sh" "$TOOLS_DIR/launch-skyrim-vr-offline.sh"; do
  explicit_output="$(env -u STVR_MONADO_RUNTIME_DIR -u STVR_MONADO_IPC_SOCKET \
    -u STVR_MONADO_PREFIX -u STVR_MONADO_XR_RUNTIME_JSON -u STVR_MONADO_LIBRARY_PATH \
    -u STVR_MONADO_HOST_MOUNTS -u PRESSURE_VESSEL_FILESYSTEMS_RW \
    STVR_DRY_RUN=1 STVR_FORCE_PROTON=1 STVR_GAME_DIR="$game" STVR_COMPATDATA="$compat" \
    STVR_STEAM_ROOT="$steam" STVR_PROTONPATH="$proton" XDG_RUNTIME_DIR="$host_runtime" HOME="$home" XDG_CONFIG_HOME="$config" \
    XR_RUNTIME_JSON="$explicit_runtime" "$launcher")"
  grep -Fq "XR runtime: $manifest" <<<"$explicit_output"
  if env -u STVR_MONADO_RUNTIME_DIR -u STVR_MONADO_IPC_SOCKET \
    -u STVR_MONADO_PREFIX -u STVR_MONADO_XR_RUNTIME_JSON -u STVR_MONADO_LIBRARY_PATH \
    -u STVR_MONADO_HOST_MOUNTS -u PRESSURE_VESSEL_FILESYSTEMS_RW -u XR_RUNTIME_JSON \
    STVR_DRY_RUN=1 STVR_FORCE_PROTON=1 STVR_GAME_DIR="$game" STVR_COMPATDATA="$compat" \
    STVR_STEAM_ROOT="$steam" STVR_PROTONPATH="$proton" XDG_RUNTIME_DIR="$host_runtime" HOME="$home" XDG_CONFIG_HOME="$config" \
    "$launcher" >/dev/null 2>&1; then
    printf 'launcher accepted a malformed active OpenXR runtime\n' >&2
    exit 1
  fi
done

nonmonado_prefix="$TMPDIR_LOCAL/nonmonado-prefix"
mkdir -p "$nonmonado_prefix/lib"
touch "$nonmonado_prefix/lib/libopenxr_other.so"
nonmonado_manifest="$nonmonado_prefix/openxr_other.json"
printf '%s\n' '{"runtime":{"library_path":"lib/libopenxr_other.so"}}' > "$nonmonado_manifest"
ln -sfn "$nonmonado_manifest" "$active_runtime"
for launcher in "$TOOLS_DIR/launch-skyrim-together-vr.sh" "$TOOLS_DIR/launch-skyrim-vr-offline.sh"; do
  nonmonado_output="$(env -u STVR_MONADO_RUNTIME_DIR -u STVR_MONADO_IPC_SOCKET \
    -u STVR_MONADO_PREFIX -u STVR_MONADO_XR_RUNTIME_JSON -u STVR_MONADO_LIBRARY_PATH \
    -u STVR_MONADO_HOST_MOUNTS -u PRESSURE_VESSEL_FILESYSTEMS_RW -u XR_RUNTIME_JSON \
    STVR_DRY_RUN=1 STVR_FORCE_PROTON=1 STVR_GAME_DIR="$game" STVR_COMPATDATA="$compat" \
    STVR_STEAM_ROOT="$steam" STVR_PROTONPATH="$proton" XDG_RUNTIME_DIR="$host_runtime" HOME="$home" XDG_CONFIG_HOME="$config" \
    "$launcher")"
  grep -Fq "XR runtime: $nonmonado_manifest" <<<"$nonmonado_output"
  grep -Fq "Pressure vessel RO: $game/.stvr-openvr/xrizer:$game/.stvr-openvr" <<<"$nonmonado_output"
done

outside_library="$TMPDIR_LOCAL/outside/libopenxr_monado.so"
mkdir -p "$(dirname -- "$outside_library")"
touch "$outside_library"
outside_manifest="$monado/share/openxr/1/outside-runtime.json"
printf '%s\n' '{"runtime":{"library_path":"../../../../outside/libopenxr_monado.so","MND_libmonado_path":"../../../lib/libmonado.so"}}' > "$outside_manifest"
ln -sfn "$outside_manifest" "$active_runtime"
for launcher in "$TOOLS_DIR/launch-skyrim-together-vr.sh" "$TOOLS_DIR/launch-skyrim-vr-offline.sh"; do
  if env -u STVR_MONADO_RUNTIME_DIR -u STVR_MONADO_IPC_SOCKET \
    -u STVR_MONADO_PREFIX -u STVR_MONADO_XR_RUNTIME_JSON -u STVR_MONADO_LIBRARY_PATH \
    -u STVR_MONADO_HOST_MOUNTS -u PRESSURE_VESSEL_FILESYSTEMS_RW -u XR_RUNTIME_JSON \
    STVR_DRY_RUN=1 STVR_FORCE_PROTON=1 STVR_GAME_DIR="$game" STVR_COMPATDATA="$compat" \
    STVR_STEAM_ROOT="$steam" STVR_PROTONPATH="$proton" XDG_RUNTIME_DIR="$host_runtime" HOME="$home" XDG_CONFIG_HOME="$config" \
    "$launcher" >/dev/null 2>&1; then
    printf 'launcher accepted an active Monado library outside its prefix\n' >&2
    exit 1
  fi
done
ln -sfn "$manifest" "$active_runtime"

opencomposite_online="$(env "${common_env[@]}" STVR_OPENVR_RUNTIME=opencomposite \
  STVR_OPENCOMPOSITE_RUNTIME="$opencomposite" PROTON_VR_RUNTIME="$game/.stvr-openvr/xrizer" \
  "$TOOLS_DIR/launch-skyrim-together-vr.sh")"
opencomposite_offline="$(env "${common_env[@]}" STVR_OPENVR_RUNTIME=opencomposite \
  STVR_OPENCOMPOSITE_RUNTIME="$opencomposite" PROTON_VR_RUNTIME="$game/.stvr-openvr/xrizer" \
  "$TOOLS_DIR/launch-skyrim-vr-offline.sh")"
grep -Fq 'OpenVR runtime: opencomposite' <<<"$opencomposite_online"
grep -Fq "OpenVR runtime path: $opencomposite" <<<"$opencomposite_online"
grep -Fq "VR override: $opencomposite" <<<"$opencomposite_offline"
grep -Fq 'Proton VR runtime: none' <<<"$opencomposite_offline"
grep -Fq "Pressure vessel RO: $opencomposite:$game/.stvr-openvr:$monado" <<<"$opencomposite_online"

opencomposite_local="$(env -u STVR_OPENCOMPOSITE_RUNTIME "${common_env[@]}" \
  STVR_OPENVR_RUNTIME=opencomposite "$TOOLS_DIR/launch-skyrim-together-vr.sh")"
grep -Fq "VR override: $game/.stvr-openvr/opencomposite" <<<"$opencomposite_local"

printf '%s\n' "{\"version\":1,\"runtime\":[\"$steamvr\"]}" > "$config/openvr/openvrpaths.vrpath"
steamvr_output="$(env "${common_env[@]}" STVR_OPENVR_RUNTIME=steamvr \
  VR_OVERRIDE="$opencomposite" PROTON_VR_RUNTIME="$game/.stvr-openvr/xrizer" \
  "$TOOLS_DIR/launch-skyrim-vr-offline.sh")"
grep -Fq 'OpenVR runtime: steamvr' <<<"$steamvr_output"
grep -Fq "VR override: $steamvr" <<<"$steamvr_output"
grep -Fq 'Proton VR runtime: none' <<<"$steamvr_output"

fake_bin="$TMPDIR_LOCAL/fake-bin"
mkdir -p "$fake_bin"
printf '%s\n' '#!/usr/bin/env bash' 'exit 0' > "$fake_bin/umu-run"
chmod +x "$fake_bin/umu-run"
real_mktemp="$(command -v mktemp)"
plugin_mktemp_log="$TMPDIR_LOCAL/plugin-mktemp.log"
cat > "$fake_bin/mktemp" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$STVR_MKTEMP_LOG"
exec "$STVR_REAL_MKTEMP" "$@"
EOF
chmod +x "$fake_bin/mktemp"
umu="$(env PATH="$fake_bin:$PATH" "${common_env[@]}" "$TOOLS_DIR/launch-skyrim-together-vr.sh")"
grep -Fq 'Mode: umu-run' <<<"$umu"
grep -Fq 'GAMEID: umu-611670' <<<"$umu"

legacy_xrizer="$TMPDIR_LOCAL/legacy-xrizer"
ordinary_xrizer="$TMPDIR_LOCAL/ordinary-xrizer"
mkdir -p "$legacy_xrizer/bin/linux64" "$ordinary_xrizer/bin/linux64"
write_elf64_x86_64 "$legacy_xrizer/libxrizer.so"
cp "$legacy_xrizer/libxrizer.so" "$legacy_xrizer/bin/linux64/vrclient.so"
write_elf64_x86_64 "$ordinary_xrizer/libxrizer.so"
cp "$ordinary_xrizer/libxrizer.so" "$ordinary_xrizer/bin/linux64/vrclient.so"
real_readelf="$(command -v readelf)"
cat > "$fake_bin/readelf" <<'EOF'
#!/usr/bin/env bash
target="${!#}"
case "$target" in
  *legacy-xrizer/*)
    printf '%s\n' '  1: 0000000000000000     0 NOTYPE  GLOBAL DEFAULT  UND _ZNSt12experimental10filesystem2v17pathC1Ev'
    ;;
  *ordinary-xrizer/*)
    printf '%s\n' '  1: 0000000000000000     0 NOTYPE  GLOBAL DEFAULT  UND _ZSt4cout'
    ;;
  *)
    exec "$STVR_REAL_READELF" "$@"
    ;;
esac
EOF
chmod +x "$fake_bin/readelf"
if env PATH="$fake_bin:$PATH" STVR_REAL_READELF="$real_readelf" "${common_env[@]}" \
  STVR_OPENVR_RUNTIME=xrizer STVR_XRIZER_RUNTIME="$legacy_xrizer" \
  "$TOOLS_DIR/launch-skyrim-together-vr.sh" >"$TMPDIR_LOCAL/legacy-xrizer.out" 2>"$TMPDIR_LOCAL/legacy-xrizer.err"; then
  printf 'launcher accepted XRizer with unresolved legacy filesystem symbols\n' >&2
  exit 1
fi
grep -Fq 'std::experimental::filesystem' "$TMPDIR_LOCAL/legacy-xrizer.err"
grep -Fq '_ZNSt12experimental10filesystem2v17pathC1Ev' "$TMPDIR_LOCAL/legacy-xrizer.err"
env PATH="$fake_bin:$PATH" STVR_REAL_READELF="$real_readelf" "${common_env[@]}" \
  STVR_OPENVR_RUNTIME=xrizer STVR_XRIZER_RUNTIME="$ordinary_xrizer" \
  "$TOOLS_DIR/launch-skyrim-together-vr.sh" >/dev/null

profile_dir="$compat/pfx/drive_c/users/steamuser/AppData/Local/Skyrim VR"
mkdir -p "$profile_dir"
printf '*Unrelated.esp\r\n*SkyrimTogether.esp\r\n' > "$profile_dir/Plugins.txt"
printf 'Unrelated.esp\r\nSkyrimTogether.esp\r\n' > "$profile_dir/loadorder.txt"
env PATH="$fake_bin:$PATH" STVR_MKTEMP_LOG="$plugin_mktemp_log" STVR_REAL_MKTEMP="$real_mktemp" STVR_REAL_READELF="$real_readelf" \
  "${common_env[@]}" STVR_DRY_RUN=0 STVR_FORCE_PROTON=1 \
  "$TOOLS_DIR/launch-skyrim-together-vr.sh"
cat > "$TMPDIR_LOCAL/expected-plugins.txt" <<'EOF'
*Unrelated.esp
*Skyrim.esm
*Update.esm
*Dawnguard.esm
*HearthFires.esm
*Dragonborn.esm
*SkyrimVR.esm
*higgs_vr.esp
*vrik.esp
*Realm of Lorkhan - Custom Alternate Start - Choose your own adventure.esp
*SkyrimTogether.esp
EOF
cat > "$TMPDIR_LOCAL/expected-loadorder.txt" <<'EOF'
Unrelated.esp
Skyrim.esm
Update.esm
Dawnguard.esm
HearthFires.esm
Dragonborn.esm
SkyrimVR.esm
higgs_vr.esp
vrik.esp
Realm of Lorkhan - Custom Alternate Start - Choose your own adventure.esp
SkyrimTogether.esp
EOF
cmp "$TMPDIR_LOCAL/expected-plugins.txt" "$profile_dir/Plugins.txt"
cmp "$TMPDIR_LOCAL/expected-loadorder.txt" "$profile_dir/loadorder.txt"
grep -Fqx "$profile_dir/Plugins.txt.tmp.XXXXXX" "$plugin_mktemp_log"
grep -Fqx "$profile_dir/loadorder.txt.tmp.XXXXXX" "$plugin_mktemp_log"
if compgen -G "$profile_dir/*.tmp.*" >/dev/null; then
  printf 'launcher left plugin-order temporary files behind\n' >&2
  exit 1
fi

# The selected XRizer/Monado route runs the non-mutating manager canary before
# Proton. Tests deliberately opt out only through the explicit test-only flag.
if env "${common_env[@]}" STVR_TEST_SKIP_MONADO_CHECK=0 STVR_DRY_RUN=0 STVR_FORCE_PROTON=1 \
  "$TOOLS_DIR/launch-skyrim-together-vr.sh" >"$TMPDIR_LOCAL/monado-check.out" 2>"$TMPDIR_LOCAL/monado-check.err"; then
  printf 'launcher accepted an unavailable Monado runtime without its canary\n' >&2
  exit 1
fi
grep -Fq 'Monado runtime listener is not ready' "$TMPDIR_LOCAL/monado-check.err"

# Required release plugins may not be silently omitted from a handoff launch.
rm -f -- "$game/Data/vrik.esp"
if env "${common_env[@]}" STVR_DRY_RUN=0 STVR_FORCE_PROTON=1 \
  "$TOOLS_DIR/launch-skyrim-together-vr.sh" >/dev/null 2>"$TMPDIR_LOCAL/missing-plugin.err"; then
  printf 'launcher accepted a missing required handoff plugin\n' >&2
  exit 1
fi
grep -Fq 'missing required handoff plugin file' "$TMPDIR_LOCAL/missing-plugin.err"
missing_plugin_compat="$TMPDIR_LOCAL/missing-plugin-compat"
if env "${common_env[@]}" STVR_DRY_RUN=0 STVR_FORCE_PROTON=1 STVR_COMPATDATA="$missing_plugin_compat" \
  "$TOOLS_DIR/launch-skyrim-together-vr.sh" >/dev/null 2>&1; then
  printf 'launcher accepted a missing required handoff plugin for a fresh profile\n' >&2
  exit 1
fi
[ ! -e "$missing_plugin_compat/pfx/drive_c/users/steamuser/AppData/Local/Skyrim VR/Plugins.txt" ]
touch "$game/Data/vrik.esp"

# Real launches contend only with the exact canonical game root. Dry runs do
# not acquire the long-lived lock, and a cloned root remains independently usable.
lock_ready="$TMPDIR_LOCAL/lock-owner-ready"
env "${common_env[@]}" STVR_DRY_RUN=0 STVR_FORCE_PROTON=1 STVR_FAKE_PROTON_SLEEP=2 STVR_FAKE_PROTON_READY="$lock_ready" \
  "$TOOLS_DIR/launch-skyrim-together-vr.sh" >"$TMPDIR_LOCAL/lock-owner.out" 2>"$TMPDIR_LOCAL/lock-owner.err" &
lock_owner=$!
for _ in {1..100}; do
  [ -e "$lock_ready" ] && break
  sleep 0.02
done
[ -e "$lock_ready" ] || {
  printf 'lock owner did not reach its Proton stub\n' >&2
  exit 1
}
contender_compat="$TMPDIR_LOCAL/contender-compat"
contender_profile="$contender_compat/pfx/drive_c/users/steamuser/AppData/Local/Skyrim VR"
mkdir -p "$contender_profile" "$game/Data/SKSE/Plugins/stvr-disabled"
printf '*ContenderOnly.esp\r\n' > "$contender_profile/Plugins.txt"
printf 'ContenderOnly.esp\r\n' > "$contender_profile/loadorder.txt"
touch "$game/Data/SKSE/Plugins/stvr-disabled/SkyrimTogetherVR-contender.dll"
if env "${common_env[@]}" STVR_COMPATDATA="$contender_compat" STVR_DRY_RUN=0 STVR_FORCE_PROTON=1 \
  "$TOOLS_DIR/launch-skyrim-together-vr.sh" >"$TMPDIR_LOCAL/lock-contender.out" 2>"$TMPDIR_LOCAL/lock-contender.err"; then
  printf 'launcher allowed a concurrent launch for one game root\n' >&2
  exit 1
fi
grep -Fq "another launch already owns game root: $game" "$TMPDIR_LOCAL/lock-contender.err"
if grep -Fq '.stvr-launch.lock' "$TMPDIR_LOCAL/lock-contender.err"; then
  printf 'launcher lock failure exposed an internal lock path\n' >&2
  exit 1
fi
printf '*ContenderOnly.esp\r\n' | cmp - "$contender_profile/Plugins.txt"
printf 'ContenderOnly.esp\r\n' | cmp - "$contender_profile/loadorder.txt"
[ -f "$game/Data/SKSE/Plugins/stvr-disabled/SkyrimTogetherVR-contender.dll" ]
env "${common_env[@]}" STVR_DRY_RUN=1 STVR_FORCE_PROTON=1 "$TOOLS_DIR/launch-skyrim-together-vr.sh" >/dev/null
clone_game="$TMPDIR_LOCAL/library/steamapps/common/SkyrimVR-clone"
clone_compat="$TMPDIR_LOCAL/library/steamapps/compatdata/611670-clone"
cp -a -- "$game" "$clone_game"
mkdir -p "$clone_compat/pfx"
env "${common_env[@]}" STVR_DRY_RUN=0 STVR_FORCE_PROTON=1 STVR_GAME_DIR="$clone_game" STVR_COMPATDATA="$clone_compat" \
  "$TOOLS_DIR/launch-skyrim-together-vr.sh" >/dev/null
wait "$lock_owner"

auto_proton="$(env -u PROTONPATH -u STVR_PROTONPATH \
  STVR_DRY_RUN=1 STVR_FORCE_PROTON=1 STVR_GAME_DIR="$game" STVR_COMPATDATA="$compat" \
  STVR_STEAM_ROOT="$steam" STVR_STEAM_LIBRARY="$TMPDIR_LOCAL/library" \
  XDG_RUNTIME_DIR="$host_runtime" HOME="$home" XDG_CONFIG_HOME="$config" "$TOOLS_DIR/launch-skyrim-together-vr.sh")"
printf -v escaped_library_proton '%q' "$library_proton/proton"
grep -Fq "$escaped_library_proton run" <<<"$auto_proton"

offline_auto_proton="$(env -u PROTONPATH -u STVR_PROTONPATH \
  STVR_DRY_RUN=1 STVR_FORCE_PROTON=1 STVR_GAME_DIR="$game" STVR_COMPATDATA="$compat" \
  STVR_STEAM_ROOT="$steam" STVR_STEAM_LIBRARY="$TMPDIR_LOCAL/library" \
  XDG_RUNTIME_DIR="$host_runtime" HOME="$home" XDG_CONFIG_HOME="$config" "$TOOLS_DIR/launch-skyrim-vr-offline.sh")"
grep -Fq "$escaped_library_proton run" <<<"$offline_auto_proton"

if env "${common_env[@]}" STVR_MONADO_IPC_SOCKET="$TMPDIR_LOCAL/wrong/monado_comp_ipc" \
  "$TOOLS_DIR/launch-skyrim-together-vr.sh" >/dev/null 2>&1; then
  printf 'launcher accepted a socket outside its selected Monado runtime\n' >&2
  exit 1
fi
if env "${common_env[@]}" STVR_MONADO_XR_RUNTIME_JSON="$TMPDIR_LOCAL/outside.json" \
  "$TOOLS_DIR/launch-skyrim-together-vr.sh" >/dev/null 2>&1; then
  printf 'launcher accepted a manifest outside its selected Monado prefix\n' >&2
  exit 1
fi

mkdir -p "$TMPDIR_LOCAL/empty-xrizer" "$TMPDIR_LOCAL/windows-only/bin/linux64"
touch "$TMPDIR_LOCAL/windows-only/bin/linux64/vrclient_x64.dll"
for launcher in "$TOOLS_DIR/launch-skyrim-together-vr.sh" "$TOOLS_DIR/launch-skyrim-vr-offline.sh"; do
  if env "${common_env[@]}" STVR_OPENVR_RUNTIME=invalid "$launcher" >/dev/null 2>&1; then
    printf 'launcher accepted an invalid OpenVR runtime selector\n' >&2
    exit 1
  fi
  if env -u STVR_OPENCOMPOSITE_RUNTIME "${common_env[@]}" STVR_GAME_DIR="$game_without_opencomposite" \
    STVR_OPENVR_RUNTIME=opencomposite "$launcher" >/dev/null 2>&1; then
    printf 'launcher accepted OpenComposite without an explicit or bundled runtime\n' >&2
    exit 1
  fi
  if env "${common_env[@]}" STVR_OPENVR_RUNTIME=xrizer STVR_XRIZER_RUNTIME="$TMPDIR_LOCAL/empty-xrizer" \
    "$launcher" >/dev/null 2>&1; then
    printf 'launcher accepted an empty explicit XRizer runtime\n' >&2
    exit 1
  fi
  rm -f "$game/.stvr-openvr/xrizer/libxrizer.so"
  if env "${common_env[@]}" STVR_OPENVR_RUNTIME=xrizer "$launcher" >/dev/null 2>&1; then
    printf 'launcher accepted bundled XRizer without its runtime-root library\n' >&2
    exit 1
  fi
  cp "$game/.stvr-openvr/xrizer/bin/linux64/vrclient.so" "$game/.stvr-openvr/xrizer/libxrizer.so"
  printf 'different XRizer payload' >> "$game/.stvr-openvr/xrizer/libxrizer.so"
  if env "${common_env[@]}" STVR_OPENVR_RUNTIME=xrizer "$launcher" >/dev/null 2>&1; then
    printf 'launcher accepted bundled XRizer with mismatched root and loader payloads\n' >&2
    exit 1
  fi
  cp "$game/.stvr-openvr/xrizer/bin/linux64/vrclient.so" "$game/.stvr-openvr/xrizer/libxrizer.so"
  if env "${common_env[@]}" STVR_OPENVR_RUNTIME=opencomposite STVR_OPENCOMPOSITE_RUNTIME="$TMPDIR_LOCAL/windows-only" \
    "$launcher" >/dev/null 2>&1; then
    printf 'launcher accepted a Windows-only OpenComposite payload\n' >&2
    exit 1
  fi
done

mkdir -p "$TMPDIR_LOCAL/symlink-escape/bin/linux64" "$TMPDIR_LOCAL/symlink-contained/bin/linux64"
write_elf64_x86_64 "$TMPDIR_LOCAL/outside-loader"
ln -s "$TMPDIR_LOCAL/outside-loader" "$TMPDIR_LOCAL/symlink-escape/bin/linux64/vrclient.so"
write_elf64_x86_64 "$TMPDIR_LOCAL/symlink-contained/loader"
ln -s ../../loader "$TMPDIR_LOCAL/symlink-contained/bin/linux64/vrclient.so"

if env "${common_env[@]}" STVR_OPENVR_RUNTIME=opencomposite STVR_OPENCOMPOSITE_RUNTIME="$TMPDIR_LOCAL/symlink-escape" \
  "$TOOLS_DIR/launch-skyrim-together-vr.sh" >/dev/null 2>&1; then
  printf 'launcher accepted an escaping loader symlink\n' >&2
  exit 1
fi
env "${common_env[@]}" STVR_OPENVR_RUNTIME=opencomposite STVR_OPENCOMPOSITE_RUNTIME="$TMPDIR_LOCAL/symlink-contained" \
  "$TOOLS_DIR/launch-skyrim-vr-offline.sh" >/dev/null

for kind in bad-magic bad-class bad-machine bad-type bad-header-version truncated-header bad-header-size bad-phdr-table no-load bad-load-span; do
  mkdir -p "$TMPDIR_LOCAL/$kind/bin/linux64"
  write_elf64_x86_64 "$TMPDIR_LOCAL/$kind/bin/linux64/vrclient.so"
done
printf 'not an ELF' > "$TMPDIR_LOCAL/bad-magic/bin/linux64/vrclient.so"
printf '%s' 'AQ==' | base64 -d | dd of="$TMPDIR_LOCAL/bad-class/bin/linux64/vrclient.so" bs=1 seek=4 conv=notrunc status=none
printf '%s' 'Aw==' | base64 -d | dd of="$TMPDIR_LOCAL/bad-machine/bin/linux64/vrclient.so" bs=1 seek=18 conv=notrunc status=none
printf '%s' 'Ag==' | base64 -d | dd of="$TMPDIR_LOCAL/bad-type/bin/linux64/vrclient.so" bs=1 seek=16 conv=notrunc status=none
printf '%s' 'Ag==' | base64 -d | dd of="$TMPDIR_LOCAL/bad-header-version/bin/linux64/vrclient.so" bs=1 seek=20 conv=notrunc status=none
dd if="$TMPDIR_LOCAL/truncated-header/bin/linux64/vrclient.so" of="$TMPDIR_LOCAL/truncated-header/short" bs=1 count=20 status=none
mv "$TMPDIR_LOCAL/truncated-header/short" "$TMPDIR_LOCAL/truncated-header/bin/linux64/vrclient.so"
printf '%s' 'Pw==' | base64 -d | dd of="$TMPDIR_LOCAL/bad-header-size/bin/linux64/vrclient.so" bs=1 seek=52 conv=notrunc status=none
printf '%s' 'Ag==' | base64 -d | dd of="$TMPDIR_LOCAL/bad-phdr-table/bin/linux64/vrclient.so" bs=1 seek=56 conv=notrunc status=none
printf '%s' 'Ag==' | base64 -d | dd of="$TMPDIR_LOCAL/no-load/bin/linux64/vrclient.so" bs=1 seek=64 conv=notrunc status=none
printf '%s' 'eQ==' | base64 -d | dd of="$TMPDIR_LOCAL/bad-load-span/bin/linux64/vrclient.so" bs=1 seek=96 conv=notrunc status=none
for kind in bad-magic bad-class bad-machine bad-type bad-header-version truncated-header bad-header-size bad-phdr-table no-load bad-load-span; do
  if env "${common_env[@]}" STVR_OPENVR_RUNTIME=opencomposite STVR_OPENCOMPOSITE_RUNTIME="$TMPDIR_LOCAL/$kind" \
    "$TOOLS_DIR/launch-skyrim-together-vr.sh" >/dev/null 2>&1; then
    printf 'launcher accepted %s loader\n' "$kind" >&2
    exit 1
  fi
done

printf '%s\n' "{\"version\":1,\"runtime\":[\"$opencomposite\"]}" > "$config/openvr/openvrpaths.vrpath"
if env "${common_env[@]}" STVR_GAME_DIR="$game_without_opencomposite" \
  "$TOOLS_DIR/launch-skyrim-together-vr.sh" >/dev/null 2>&1; then
  printf 'launcher misclassified an OpenComposite registry entry as XRizer\n' >&2
  exit 1
fi
if env "${common_env[@]}" STVR_OPENVR_RUNTIME=steamvr "$TOOLS_DIR/launch-skyrim-vr-offline.sh" >/dev/null 2>&1; then
  printf 'launcher accepted OpenComposite as SteamVR from the host registry\n' >&2
  exit 1
fi
env "${common_env[@]}" STVR_OPENVR_RUNTIME=steamvr STVR_STEAMVR_RUNTIME="$steamvr" \
  "$TOOLS_DIR/launch-skyrim-together-vr.sh" >/dev/null

printf 'launcher runtime tests passed\n'
