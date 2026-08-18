#!/usr/bin/env bash
set -euo pipefail

APPID="611670"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
GAME_DIR="$(readlink -f -- "${STVR_GAME_DIR:-$SCRIPT_DIR}")"

die() {
  printf 'launch-skyrim-vr-offline: %s\n' "$*" >&2
  exit 1
}

find_steam_root() {
  local candidate
  for candidate in \
    "${STVR_STEAM_ROOT:-}" \
    "${XDG_DATA_HOME:-$HOME/.local/share}/Steam" \
    "$HOME/.steam/steam" \
    "$HOME/.steam/root"; do
    if [ -n "$candidate" ] && [ -d "$candidate" ]; then
      readlink -f -- "$candidate"
      return 0
    fi
  done
  return 1
}

find_proton_dir() {
  local root="$1" library="$2"
  local candidate
  if [ -n "${STVR_PROTONPATH:-}" ]; then
    [ -x "$STVR_PROTONPATH/proton" ] || die "STVR_PROTONPATH has no executable proton script"
    printf '%s\n' "$STVR_PROTONPATH"
    return 0
  fi
  local search_root
  for search_root in \
    "$root/compatibilitytools.d" \
    "$root/steamapps/common" \
    "$library/steamapps/common"; do
    while IFS= read -r candidate; do
      if [ -x "$candidate/proton" ]; then
        printf '%s\n' "$candidate"
        return 0
      fi
    done < <(
      find "$search_root" -mindepth 1 -maxdepth 1 -type d -iname 'GE-Proton*' 2>/dev/null | sort -Vr
    )
    while IFS= read -r candidate; do
      if [ -x "$candidate/proton" ]; then
        printf '%s\n' "$candidate"
        return 0
      fi
    done < <(
      find "$search_root" -mindepth 1 -maxdepth 1 -type d -iname 'Proton-GE*' 2>/dev/null | sort -Vr
    )
    while IFS= read -r candidate; do
      if [ -x "$candidate/proton" ]; then
        printf '%s\n' "$candidate"
        return 0
      fi
    done < <(find "$search_root" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | sort -Vr)
  done
  return 1
}

remove_exact() {
  local file="$1"
  local entry="$2"
  local temp
  [ -f "$file" ] || return 0
  temp="$(mktemp)"
  awk -v value="$entry" 'tolower($0) != tolower(value) { print }' "$file" > "$temp"
  mv -f "$temp" "$file"
}

STEAM_ROOT="$(find_steam_root)" || die "could not find Steam; set STVR_STEAM_ROOT"
STEAM_LIBRARY="${STVR_STEAM_LIBRARY:-$(readlink -f -- "$GAME_DIR/../../..")}"
COMPATDATA="${STVR_COMPATDATA:-$STEAM_LIBRARY/steamapps/compatdata/$APPID}"
WINEPREFIX_DIR="${STVR_WINEPREFIX:-$COMPATDATA/pfx}"
PROTON_DIR="$(find_proton_dir "$STEAM_ROOT" "$STEAM_LIBRARY")" || die "could not find Proton; set STVR_PROTONPATH"
LOADER="${SKYRIMVR_LAUNCHER:-$GAME_DIR/sksevr_loader.exe}"
[ -f "$LOADER" ] || die "missing SKSEVR loader: $LOADER"

LOCAL_APPDATA="$WINEPREFIX_DIR/drive_c/users/steamuser/AppData/Local/Skyrim VR"
SKSE_PLUGIN_DIR="$GAME_DIR/Data/SKSE/Plugins"
DISABLED_DIR="$SKSE_PLUGIN_DIR/stvr-disabled"
export SteamAppId="${SteamAppId:-$APPID}"
export SteamGameId="${SteamGameId:-$APPID}"
export STEAM_COMPAT_INSTALL_PATH="${STEAM_COMPAT_INSTALL_PATH:-$GAME_DIR}"
export STEAM_COMPAT_CLIENT_INSTALL_PATH="${STEAM_COMPAT_CLIENT_INSTALL_PATH:-$STEAM_ROOT}"
export WINEPREFIX="$WINEPREFIX_DIR"

append_colon_path() {
  local variable="$1" path="$2" current entry
  [[ "$path" == /* && "$path" != *:* && "$path" != *$'\n'* && "$path" != *$'\r'* ]] || die "unsafe pressure-vessel mount path: $path"
  current="${!variable:-}"
  IFS=: read -r -a _mount_entries <<< "$current"
  for entry in "${_mount_entries[@]}"; do [ "$entry" = "$path" ] && return 0; done
  printf -v "$variable" '%s' "${current:+$current:}$path"
  export "$variable"
}

stvr_append_pressure_vessel_ro() {
  append_colon_path PRESSURE_VESSEL_FILESYSTEMS_RO "$1"
}

INPUT_HELPER="$SCRIPT_DIR/stvr-xrizer-input-compat.sh"
[ -f "$INPUT_HELPER" ] || INPUT_HELPER="$GAME_DIR/stvr-xrizer-input-compat.sh"
[ -f "$INPUT_HELPER" ] || die "missing OpenVR runtime helper: $INPUT_HELPER"
source "$INPUT_HELPER"

validate_monado_manifest() {
  local prefix="$1" manifest="$2"
  python3 - "$prefix" "$manifest" <<'PY' >/dev/null
import json
import os
import sys
prefix = os.path.realpath(sys.argv[1])
manifest = os.path.realpath(sys.argv[2])
try:
    if os.path.commonpath((prefix, manifest)) != prefix or not os.path.isfile(manifest): raise ValueError("manifest outside prefix")
    with open(manifest, encoding="utf-8") as source: runtime = json.load(source)["runtime"]
    for key in ("library_path", "MND_libmonado_path"):
        value = runtime[key]
        if not isinstance(value, str) or not value: raise ValueError(key)
        library = os.path.realpath(os.path.join(os.path.dirname(manifest), value))
        if os.path.commonpath((prefix, library)) != prefix or not os.path.isfile(library): raise ValueError(key)
except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
    raise SystemExit(f"invalid Monado OpenXR manifest: {error}")
PY
}

configure_isolated_monado_runtime() {
  local host_runtime socket_parent socket_bytes library_dir mount
  MONADO_RUNTIME_DIR="${STVR_MONADO_RUNTIME_DIR:-}"
  [ -n "$MONADO_RUNTIME_DIR" ] || return 0
  [ -d "$MONADO_RUNTIME_DIR" ] && [ ! -L "$MONADO_RUNTIME_DIR" ] || die "Monado runtime directory does not exist or is unsafe: $MONADO_RUNTIME_DIR"
  MONADO_RUNTIME_DIR="$(readlink -f -- "$MONADO_RUNTIME_DIR")"
  host_runtime="${XDG_RUNTIME_DIR:-}"
  export XDG_RUNTIME_DIR="$MONADO_RUNTIME_DIR"
  MONADO_IPC_SOCKET="${STVR_MONADO_IPC_SOCKET:-$MONADO_RUNTIME_DIR/monado_comp_ipc}"
  socket_parent="$(readlink -m -- "$(dirname -- "$MONADO_IPC_SOCKET")")"
  [ "$socket_parent" = "$MONADO_RUNTIME_DIR" ] && [ "${MONADO_IPC_SOCKET##*/}" = monado_comp_ipc ] || die 'STVR_MONADO_IPC_SOCKET must be exactly STVR_MONADO_RUNTIME_DIR/monado_comp_ipc'
  socket_bytes="$(LC_ALL=C printf %s "$MONADO_IPC_SOCKET" | wc -c | tr -d '[:space:]')"
  [[ "$socket_bytes" =~ ^[0-9]+$ ]] && [ "$socket_bytes" -le 107 ] || die "Monado IPC socket path is $socket_bytes bytes; AF_UNIX permits at most 107"
  [ -n "${STVR_MONADO_PREFIX:-}" ] || die 'isolated Monado launch requires STVR_MONADO_PREFIX'
  [ -n "${STVR_MONADO_XR_RUNTIME_JSON:-}" ] || die 'isolated Monado launch requires STVR_MONADO_XR_RUNTIME_JSON'
  [ -n "${STVR_MONADO_LIBRARY_PATH:-}" ] || die 'isolated Monado launch requires STVR_MONADO_LIBRARY_PATH'
  [ -d "$STVR_MONADO_PREFIX" ] && [ ! -L "$STVR_MONADO_PREFIX" ] || die 'unsafe STVR_MONADO_PREFIX'
  MONADO_PREFIX="$(readlink -f -- "$STVR_MONADO_PREFIX")"
  XR_RUNTIME_JSON="$(readlink -f -- "$STVR_MONADO_XR_RUNTIME_JSON")"
  validate_monado_manifest "$MONADO_PREFIX" "$XR_RUNTIME_JSON" || die 'Monado OpenXR manifest does not match its prefix'
  export XR_RUNTIME_JSON
  IFS=: read -r -a _library_dirs <<< "$STVR_MONADO_LIBRARY_PATH"
  for library_dir in "${_library_dirs[@]}"; do
    [[ "$library_dir" == "$MONADO_PREFIX"/* ]] && [ -d "$library_dir" ] || die "unsafe STVR_MONADO_LIBRARY_PATH entry: $library_dir"
  done
  export LD_LIBRARY_PATH="$STVR_MONADO_LIBRARY_PATH${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  append_colon_path PRESSURE_VESSEL_FILESYSTEMS_RO "$MONADO_PREFIX"
  append_colon_path PRESSURE_VESSEL_FILESYSTEMS_RW "$MONADO_RUNTIME_DIR"
  if [ -n "$host_runtime" ] && [ -d "$host_runtime" ]; then
    host_runtime="$(readlink -f -- "$host_runtime")"
    if [ -n "${WAYLAND_DISPLAY:-}" ] && [[ "$WAYLAND_DISPLAY" != /* ]]; then export WAYLAND_DISPLAY="$host_runtime/$WAYLAND_DISPLAY"; fi
    if [[ "${DBUS_SESSION_BUS_ADDRESS:-}" == unix:path=* ]]; then
      _dbus_path="${DBUS_SESSION_BUS_ADDRESS#unix:path=}"; _dbus_suffix=""
      if [[ "$_dbus_path" == *,* ]]; then _dbus_suffix=",${_dbus_path#*,}"; _dbus_path="${_dbus_path%%,*}"; fi
      [[ "$_dbus_path" == /* ]] || _dbus_path="$host_runtime/$_dbus_path"
      export DBUS_SESSION_BUS_ADDRESS="unix:path=$_dbus_path$_dbus_suffix"
    fi
    if [[ "${PULSE_SERVER:-}" == unix:* ]]; then _pulse_path="${PULSE_SERVER#unix:}"; [[ "$_pulse_path" == /* ]] || _pulse_path="$host_runtime/$_pulse_path"; export PULSE_SERVER="unix:$_pulse_path"; fi
    if [ -n "${PIPEWIRE_REMOTE:-}" ] && [[ "$PIPEWIRE_REMOTE" != /* ]]; then export PIPEWIRE_REMOTE="$host_runtime/$PIPEWIRE_REMOTE"; fi
  fi
  IFS=: read -r -a _host_mounts <<< "${STVR_MONADO_HOST_MOUNTS:-}"
  for mount in "${_host_mounts[@]}"; do [ -n "$mount" ] && append_colon_path PRESSURE_VESSEL_FILESYSTEMS_RW "$mount"; done
  _dbus_mount="${DBUS_SESSION_BUS_ADDRESS:-}"; _dbus_mount="${_dbus_mount#unix:path=}"
  _pulse_mount="${PULSE_SERVER:-}"; _pulse_mount="${_pulse_mount#unix:}"
  for mount in "${WAYLAND_DISPLAY:-}" "$_dbus_mount" "$_pulse_mount" "${PIPEWIRE_REMOTE:-}"; do
    if [[ "$mount" == /* ]] && { [ -e "$mount" ] || [ -S "$mount" ]; }; then
      append_colon_path PRESSURE_VESSEL_FILESYSTEMS_RW "$mount"
    fi
  done
}

MONADO_RUNTIME_DIR="${STVR_MONADO_RUNTIME_DIR:-}"
configure_isolated_monado_runtime
MONADO_IPC_SOCKET="${MONADO_IPC_SOCKET:-${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/monado_comp_ipc}"
if [ -z "$MONADO_RUNTIME_DIR" ] && [ -S "$MONADO_IPC_SOCKET" ] && [ ! -L "$MONADO_IPC_SOCKET" ]; then
  append_colon_path PRESSURE_VESSEL_FILESYSTEMS_RW "$MONADO_IPC_SOCKET"
fi
export GAMEID="umu-$APPID"

if command -v umu-run >/dev/null 2>&1 && [ "${STVR_FORCE_PROTON:-0}" != "1" ]; then
  export STORE="${STORE:-steam}"
  export PROTONPATH="${PROTONPATH:-$PROTON_DIR}"
  MODE="umu-run"
  COMMAND=(umu-run "$LOADER" "$@")
else
  PROTON_BIN="${STVR_PROTON:-$PROTON_DIR/proton}"
  [ -f "$PROTON_BIN" ] || die "missing Proton executable: $PROTON_BIN"
  export STEAM_COMPAT_DATA_PATH="${STEAM_COMPAT_DATA_PATH:-$COMPATDATA}"
  export PROTONPATH="${PROTONPATH:-$PROTON_DIR}"
  MODE="proton"
  COMMAND=("$PROTON_BIN" run "$LOADER" "$@")
fi

if [ "${STVR_DRY_RUN:-0}" = "1" ]; then
  printf 'Mode: %s\nGame dir: %s\nCompatdata: %s\nOpenVR runtime: %s\nOpenVR runtime path: %s\nOpenVR pathreg: %s\nVR override: %s\nVR pathreg override: %s\nProton VR runtime: %s\nMonado runtime: %s\nXR runtime: %s\nGAMEID: %s\nPressure vessel RW: %s\nPressure vessel RO: %s\nCommand:' \
    "$MODE" "$GAME_DIR" "$COMPATDATA" "$STVR_SELECTED_OPENVR_RUNTIME" "$STVR_SELECTED_OPENVR_RUNTIME_PATH" "$STVR_SELECTED_OPENVR_PATHREG" "${VR_OVERRIDE:-none}" "${VR_PATHREG_OVERRIDE:-none}" "${PROTON_VR_RUNTIME:-none}" "${MONADO_RUNTIME_DIR:-default}" "${XR_RUNTIME_JSON:-default}" "$GAMEID" \
    "${PRESSURE_VESSEL_FILESYSTEMS_RW:-}" "${PRESSURE_VESSEL_FILESYSTEMS_RO:-}"
  printf ' %q' "${COMMAND[@]}"
  printf '\n'
  exit 0
fi

remove_exact "$LOCAL_APPDATA/Plugins.txt" "*SkyrimTogether.esp"
remove_exact "$LOCAL_APPDATA/loadorder.txt" "SkyrimTogether.esp"
mkdir -p "$DISABLED_DIR"
shopt -s nullglob
for file in "$SKSE_PLUGIN_DIR"/SkyrimTogetherVR*; do
  [ -f "$file" ] && mv -f "$file" "$DISABLED_DIR/"
done
shopt -u nullglob

cd "$GAME_DIR"
exec "${COMMAND[@]}"
