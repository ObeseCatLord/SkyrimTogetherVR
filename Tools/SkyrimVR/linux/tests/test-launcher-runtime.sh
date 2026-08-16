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
mkdir -p "$game" "$compat/pfx" "$steam" "$proton" "$library_proton" "$runtime" \
  "$monado/lib" "$monado/share/openxr/1" "$host_runtime/pulse"
touch "$game/SkyrimVR.exe" "$game/SkyrimTogetherVRGameplay.exe" "$game/sksevr_loader.exe"
printf '%s\n' '{"packageFlavor":"gameplay"}' > "$game/SkyrimTogetherVR_BuildManifest.json"
touch "$monado/lib/libopenxr_monado.so" "$monado/lib/libmonado.so"
printf '%s\n' '#!/usr/bin/env bash' 'exit 0' > "$proton/proton"
chmod +x "$proton/proton"
cp "$proton/proton" "$library_proton/proton"
cat > "$monado/share/openxr/1/openxr_monado.json" <<'EOF'
{"file_format_version":"1.0.0","runtime":{"library_path":"../../../lib/libopenxr_monado.so","MND_libmonado_path":"../../../lib/libmonado.so"}}
EOF

manifest="$monado/share/openxr/1/openxr_monado.json"
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
)

online="$(env "${common_env[@]}" STVR_FORCE_PROTON=1 GAMEID=umu-611670-alice "$TOOLS_DIR/launch-skyrim-together-vr.sh")"
offline="$(env "${common_env[@]}" STVR_FORCE_PROTON=1 GAMEID=umu-611670-alice "$TOOLS_DIR/launch-skyrim-vr-offline.sh")"
grep -Fq "Monado runtime: $runtime" <<<"$online"
grep -Fq "Monado runtime: $runtime" <<<"$offline"
grep -Fq "XR runtime: $manifest" <<<"$online"
grep -Fq 'GAMEID: umu-611670' <<<"$online"
grep -Fq 'GAMEID: umu-611670' <<<"$offline"
grep -Fq "Pressure vessel RW: $runtime:$host_runtime/wayland-test:$host_runtime/bus:$host_runtime/pulse/native:$host_runtime/pipewire-0" <<<"$online"
grep -Fq "Pressure vessel RW: $runtime:$host_runtime/wayland-test:$host_runtime/bus:$host_runtime/pulse/native:$host_runtime/pipewire-0" <<<"$offline"
grep -Fq "Pressure vessel RO: $monado" <<<"$online"
grep -Fq 'Server: incidentalstoat.xyz:26099' <<<"$online"
grep -Fq "$game/SkyrimTogetherVRGameplay.exe" <<<"$online"

default_online="$(env -u STVR_MONADO_RUNTIME_DIR -u STVR_MONADO_IPC_SOCKET \
  -u STVR_MONADO_PREFIX -u STVR_MONADO_XR_RUNTIME_JSON -u STVR_MONADO_LIBRARY_PATH \
  -u STVR_MONADO_HOST_MOUNTS -u PRESSURE_VESSEL_FILESYSTEMS_RW \
  STVR_DRY_RUN=1 STVR_FORCE_PROTON=1 STVR_GAME_DIR="$game" STVR_COMPATDATA="$compat" \
  STVR_STEAM_ROOT="$steam" STVR_PROTONPATH="$proton" XDG_RUNTIME_DIR="$host_runtime" \
  "$TOOLS_DIR/launch-skyrim-together-vr.sh")"
default_offline="$(env -u STVR_MONADO_RUNTIME_DIR -u STVR_MONADO_IPC_SOCKET \
  -u STVR_MONADO_PREFIX -u STVR_MONADO_XR_RUNTIME_JSON -u STVR_MONADO_LIBRARY_PATH \
  -u STVR_MONADO_HOST_MOUNTS -u PRESSURE_VESSEL_FILESYSTEMS_RW \
  STVR_DRY_RUN=1 STVR_FORCE_PROTON=1 STVR_GAME_DIR="$game" STVR_COMPATDATA="$compat" \
  STVR_STEAM_ROOT="$steam" STVR_PROTONPATH="$proton" XDG_RUNTIME_DIR="$host_runtime" \
  "$TOOLS_DIR/launch-skyrim-vr-offline.sh")"
grep -Fq 'Monado runtime: default' <<<"$default_online"
grep -Fq 'Monado runtime: default' <<<"$default_offline"
grep -Fq "Pressure vessel RW: $host_runtime/monado_comp_ipc" <<<"$default_online"
grep -Fq "Pressure vessel RW: $host_runtime/monado_comp_ipc" <<<"$default_offline"

fake_bin="$TMPDIR_LOCAL/fake-bin"
mkdir -p "$fake_bin"
printf '%s\n' '#!/usr/bin/env bash' 'exit 0' > "$fake_bin/umu-run"
chmod +x "$fake_bin/umu-run"
umu="$(env PATH="$fake_bin:$PATH" "${common_env[@]}" "$TOOLS_DIR/launch-skyrim-together-vr.sh")"
grep -Fq 'Mode: umu-run' <<<"$umu"
grep -Fq 'GAMEID: umu-611670' <<<"$umu"

auto_proton="$(env -u PROTONPATH -u STVR_PROTONPATH \
  STVR_DRY_RUN=1 STVR_FORCE_PROTON=1 STVR_GAME_DIR="$game" STVR_COMPATDATA="$compat" \
  STVR_STEAM_ROOT="$steam" STVR_STEAM_LIBRARY="$TMPDIR_LOCAL/library" \
  XDG_RUNTIME_DIR="$host_runtime" "$TOOLS_DIR/launch-skyrim-together-vr.sh")"
printf -v escaped_library_proton '%q' "$library_proton/proton"
grep -Fq "$escaped_library_proton run" <<<"$auto_proton"

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

printf 'launcher runtime tests passed\n'
