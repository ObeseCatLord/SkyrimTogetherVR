#!/usr/bin/env bash
set -euo pipefail

APPID="611670"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
GAME_DIR="${STVR_GAME_DIR:-$SCRIPT_DIR}"
GAME_DIR="$(readlink -f -- "$GAME_DIR")"

die() {
  printf 'launch-skyrim-together-vr: %s\n' "$*" >&2
  exit 1
}

require_file() {
  [ -f "$1" ] || die "missing required file: $1"
}

default_launcher() {
  local manifest="$GAME_DIR/SkyrimTogetherVR_BuildManifest.json"
  local package_flavor=""

  if [ -f "$manifest" ]; then
    package_flavor="$(sed -nE 's/.*"packageFlavor"[[:space:]]*:[[:space:]]*"([^"]+)".*/\1/p' "$manifest" | head -n 1)"
  fi
  case "$package_flavor" in
    gameplay)
      printf '%s\n' "$GAME_DIR/SkyrimTogetherVRGameplay.exe"
      ;;
    avatar-sync)
      printf '%s\n' "$GAME_DIR/SkyrimTogetherVRAvatarSync.exe"
      ;;
    default|'')
      printf '%s\n' "$GAME_DIR/SkyrimTogetherVR.exe"
      ;;
    *)
      die "unsupported SkyrimTogetherVR package flavor in manifest: $package_flavor"
      ;;
  esac
}

find_steam_root() {
  local candidate
  if [ -n "${STVR_STEAM_ROOT:-}" ]; then
    printf '%s\n' "$STVR_STEAM_ROOT"
    return 0
  fi
  for candidate in \
    "${XDG_DATA_HOME:-$HOME/.local/share}/Steam" \
    "$HOME/.steam/steam" \
    "$HOME/.steam/root"; do
    if [ -d "$candidate" ]; then
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

to_win_path() {
  local path
  path="$(readlink -f -- "$1")"
  printf 'Z:%s' "${path//\//\\}"
}

seed_handoff_plugin_order() {
  local plugins_file="$1"
  local loadorder_file="$2"
  local data_dir="$3"
  local temp_plugins temp_loadorder plugin
  local -a plugins=(
    "Skyrim.esm"
    "Update.esm"
    "Dawnguard.esm"
    "HearthFires.esm"
    "Dragonborn.esm"
    "SkyrimVR.esm"
    "higgs_vr.esp"
    "vrik.esp"
    "Realm of Lorkhan - Custom Alternate Start - Choose your own adventure.esp"
    "SkyrimTogether.esp"
  )

  mkdir -p "$(dirname -- "$plugins_file")"
  touch "$plugins_file" "$loadorder_file"
  temp_plugins="$(mktemp)"
  temp_loadorder="$(mktemp)"

  # Preserve user-supplied entries, but rewrite the handoff-owned set in its
  # tested order. This also repairs fresh prefixes and previously partial files.
  awk '
    BEGIN {
      managed["skyrim.esm"] = 1
      managed["update.esm"] = 1
      managed["dawnguard.esm"] = 1
      managed["hearthfires.esm"] = 1
      managed["dragonborn.esm"] = 1
      managed["skyrimvr.esm"] = 1
      managed["higgs_vr.esp"] = 1
      managed["vrik.esp"] = 1
      managed["realm of lorkhan - custom alternate start - choose your own adventure.esp"] = 1
      managed["skyrimtogether.esp"] = 1
    }
    {
      key = tolower($0)
      sub(/^\*/, "", key)
      if (!managed[key]) print
    }
  ' "$plugins_file" > "$temp_plugins"
  awk '
    BEGIN {
      managed["skyrim.esm"] = 1
      managed["update.esm"] = 1
      managed["dawnguard.esm"] = 1
      managed["hearthfires.esm"] = 1
      managed["dragonborn.esm"] = 1
      managed["skyrimvr.esm"] = 1
      managed["higgs_vr.esp"] = 1
      managed["vrik.esp"] = 1
      managed["realm of lorkhan - custom alternate start - choose your own adventure.esp"] = 1
      managed["skyrimtogether.esp"] = 1
    }
    {
      key = tolower($0)
      sub(/^\*/, "", key)
      if (!managed[key]) print
    }
  ' "$loadorder_file" > "$temp_loadorder"

  for plugin in "${plugins[@]}"; do
    [ -f "$data_dir/$plugin" ] || continue
    printf '*%s\n' "$plugin" >> "$temp_plugins"
    printf '%s\n' "$plugin" >> "$temp_loadorder"
  done
  mv -f "$temp_plugins" "$plugins_file"
  mv -f "$temp_loadorder" "$loadorder_file"
}

STEAM_ROOT="$(find_steam_root)" || die "could not find Steam; set STVR_STEAM_ROOT"
STEAM_LIBRARY="${STVR_STEAM_LIBRARY:-$(
  readlink -f -- "$GAME_DIR/../../.."
)}"
COMPATDATA="${STVR_COMPATDATA:-$STEAM_LIBRARY/steamapps/compatdata/$APPID}"
WINEPREFIX_DIR="${STVR_WINEPREFIX:-$COMPATDATA/pfx}"
PROTON_DIR="$(find_proton_dir "$STEAM_ROOT" "$STEAM_LIBRARY")" || die "could not find Proton; set STVR_PROTONPATH"

LAUNCHER="${STVR_LAUNCHER:-}"
[ -n "$LAUNCHER" ] || LAUNCHER="$(default_launcher)"
GAME_EXE="${STVR_GAME_EXE:-$GAME_DIR/SkyrimVR.exe}"
require_file "$LAUNCHER"
require_file "$GAME_EXE"

LOCAL_APPDATA="$WINEPREFIX_DIR/drive_c/users/steamuser/AppData/Local/Skyrim VR"
PLUGINS_FILE="$LOCAL_APPDATA/Plugins.txt"
LOADORDER_FILE="$LOCAL_APPDATA/loadorder.txt"
SKSE_PLUGIN_DIR="$GAME_DIR/Data/SKSE/Plugins"
DISABLED_DIR="$SKSE_PLUGIN_DIR/stvr-disabled"

if [ "${STVR_DISABLE_AUTOCONNECT:-0}" = "1" ]; then
  unset STVR_AUTOCONNECT
else
  export STVR_AUTOCONNECT="${STVR_AUTOCONNECT:-incidentalstoat.xyz:26099}"
fi
export STVR_VM_UPDATE_MODE="${STVR_VM_UPDATE_MODE:-active}"

export SteamAppId="${SteamAppId:-$APPID}"
export SteamGameId="${SteamGameId:-$APPID}"
export STEAM_COMPAT_INSTALL_PATH="${STEAM_COMPAT_INSTALL_PATH:-$GAME_DIR}"
export STEAM_COMPAT_CLIENT_INSTALL_PATH="${STEAM_COMPAT_CLIENT_INSTALL_PATH:-$STEAM_ROOT}"
export WINEPREFIX="$WINEPREFIX_DIR"
export STEAM_COMPAT_DATA_PATH="${STEAM_COMPAT_DATA_PATH:-$COMPATDATA}"

append_colon_path() {
  local variable="$1" path="$2" current entry
  [[ "$path" == /* && "$path" != *:* && "$path" != *$'\n'* && "$path" != *$'\r'* ]] || \
    die "unsafe pressure-vessel mount path: $path"
  current="${!variable:-}"
  IFS=: read -r -a _mount_entries <<< "$current"
  for entry in "${_mount_entries[@]}"; do
    [ "$entry" = "$path" ] && return 0
  done
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
    if os.path.commonpath((prefix, manifest)) != prefix or not os.path.isfile(manifest):
        raise ValueError("manifest outside prefix")
    with open(manifest, encoding="utf-8") as source:
        runtime = json.load(source)["runtime"]
    for key in ("library_path", "MND_libmonado_path"):
        value = runtime[key]
        if not isinstance(value, str) or not value:
            raise ValueError(key)
        library = os.path.realpath(os.path.join(os.path.dirname(manifest), value))
        if os.path.commonpath((prefix, library)) != prefix or not os.path.isfile(library):
            raise ValueError(key)
except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
    raise SystemExit(f"invalid Monado OpenXR manifest: {error}")
PY
}

configure_host_monado_runtime() {
  local candidate result runtime_kind runtime_manifest runtime_prefix library_dir
  local -a candidates runtime_lines

  if [ -n "${XR_RUNTIME_JSON:-}" ]; then
    candidates=("$XR_RUNTIME_JSON")
  else
    if [ -n "${XDG_CONFIG_HOME:-}" ]; then
      candidates+=("$XDG_CONFIG_HOME/openxr/1/active_runtime.json")
    fi
    if [ -n "${HOME:-}" ]; then
      candidates+=("$HOME/.config/openxr/1/active_runtime.json")
    fi
  fi
  for candidate in "${candidates[@]}"; do
    { [ -e "$candidate" ] || [ -L "$candidate" ]; } && break
    candidate=""
  done
  [ -n "${candidate:-}" ] || die 'a selected host Monado socket requires XR_RUNTIME_JSON or an OpenXR active_runtime.json'

  result="$(python3 - "$candidate" <<'PY'
import json
import os
import sys

candidate = sys.argv[1]

def below(child, parent):
    try:
        return os.path.commonpath((child, parent)) == parent
    except ValueError:
        return False

try:
    manifest = os.path.realpath(candidate)
    if not manifest.endswith(".json") or not os.path.isfile(manifest):
        raise ValueError("active runtime is not a regular JSON manifest")
    with open(manifest, encoding="utf-8") as source:
        runtime = json.load(source)["runtime"]
    if not isinstance(runtime, dict):
        raise ValueError("runtime")
    libraries = []
    for key in ("library_path", "MND_libmonado_path"):
        if key not in runtime:
            continue
        value = runtime[key]
        if not isinstance(value, str) or not value:
            raise ValueError(key)
        library = os.path.realpath(os.path.join(os.path.dirname(manifest), value))
        if not os.path.isfile(library):
            raise ValueError(key)
        libraries.append(library)
    if not libraries or "library_path" not in runtime:
        raise ValueError("library_path")

    is_monado = "MND_libmonado_path" in runtime or "monado" in os.path.basename(libraries[0]).lower()
    if not is_monado:
        print("other")
        print(manifest)
        raise SystemExit(0)
    if "MND_libmonado_path" not in runtime:
        raise ValueError("Monado manifest is missing MND_libmonado_path")

    manifest_dir = os.path.dirname(manifest)
    prefix = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(manifest))))
    if (prefix == os.path.sep or os.path.basename(os.path.dirname(manifest_dir)) != "openxr"
            or os.path.basename(manifest_dir) != "1"
            or os.path.basename(os.path.dirname(os.path.dirname(manifest_dir))) != "share"):
        raise ValueError("Monado manifest is not under PREFIX/share/openxr/1")
    if any(not below(library, prefix) for library in libraries):
        raise ValueError("Monado library outside manifest prefix")

    print("monado")
    print(manifest)
    print(prefix)
    for directory in dict.fromkeys([os.path.dirname(library) for library in libraries] +
                                   [os.path.join(prefix, "lib"), os.path.join(prefix, "lib64")]):
        if os.path.isdir(directory):
            print(directory)
except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
    raise SystemExit(f"invalid active OpenXR runtime: {error}")
PY
)" || die "could not validate active OpenXR runtime: $candidate"
  mapfile -t runtime_lines <<<"$result"
  [ "${#runtime_lines[@]}" -ge 2 ] || die 'active OpenXR runtime did not resolve a manifest'
  runtime_kind="${runtime_lines[0]}"
  runtime_manifest="${runtime_lines[1]}"
  export XR_RUNTIME_JSON="$runtime_manifest"
  [ "$runtime_kind" = monado ] || return 0
  [ "${#runtime_lines[@]}" -ge 3 ] || die 'active Monado runtime did not resolve a prefix'
  runtime_prefix="${runtime_lines[2]}"
  append_colon_path PRESSURE_VESSEL_FILESYSTEMS_RO "$runtime_prefix"
  for library_dir in "${runtime_lines[@]:3}"; do
    append_colon_path LD_LIBRARY_PATH "$library_dir"
  done
}

configure_isolated_monado_runtime() {
  local host_runtime socket_parent socket_bytes library_dir mount
  MONADO_RUNTIME_DIR="${STVR_MONADO_RUNTIME_DIR:-}"
  [ -n "$MONADO_RUNTIME_DIR" ] || return 0
  [ -d "$MONADO_RUNTIME_DIR" ] && [ ! -L "$MONADO_RUNTIME_DIR" ] || \
    die "Monado runtime directory does not exist or is unsafe: $MONADO_RUNTIME_DIR"
  MONADO_RUNTIME_DIR="$(readlink -f -- "$MONADO_RUNTIME_DIR")"
  host_runtime="${XDG_RUNTIME_DIR:-}"
  export XDG_RUNTIME_DIR="$MONADO_RUNTIME_DIR"
  MONADO_IPC_SOCKET="${STVR_MONADO_IPC_SOCKET:-$MONADO_RUNTIME_DIR/monado_comp_ipc}"
  socket_parent="$(readlink -m -- "$(dirname -- "$MONADO_IPC_SOCKET")")"
  [ "$socket_parent" = "$MONADO_RUNTIME_DIR" ] && [ "${MONADO_IPC_SOCKET##*/}" = monado_comp_ipc ] || \
    die 'STVR_MONADO_IPC_SOCKET must be exactly STVR_MONADO_RUNTIME_DIR/monado_comp_ipc'
  socket_bytes="$(LC_ALL=C printf %s "$MONADO_IPC_SOCKET" | wc -c | tr -d '[:space:]')"
  [[ "$socket_bytes" =~ ^[0-9]+$ ]] && [ "$socket_bytes" -le 107 ] || \
    die "Monado IPC socket path is $socket_bytes bytes; AF_UNIX permits at most 107"

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
    [[ "$library_dir" == "$MONADO_PREFIX"/* ]] && [ -d "$library_dir" ] || \
      die "unsafe STVR_MONADO_LIBRARY_PATH entry: $library_dir"
  done
  export LD_LIBRARY_PATH="$STVR_MONADO_LIBRARY_PATH${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  append_colon_path PRESSURE_VESSEL_FILESYSTEMS_RO "$MONADO_PREFIX"
  append_colon_path PRESSURE_VESSEL_FILESYSTEMS_RW "$MONADO_RUNTIME_DIR"

  # Manager-provided values are absolute host endpoints. For direct calls,
  # resolve relative values before replacing XDG_RUNTIME_DIR as well.
  if [ -n "$host_runtime" ] && [ -d "$host_runtime" ]; then
    host_runtime="$(readlink -f -- "$host_runtime")"
    if [ -n "${WAYLAND_DISPLAY:-}" ] && [[ "$WAYLAND_DISPLAY" != /* ]]; then
      export WAYLAND_DISPLAY="$host_runtime/$WAYLAND_DISPLAY"
    fi
    if [[ "${DBUS_SESSION_BUS_ADDRESS:-}" == unix:path=* ]]; then
      _dbus_path="${DBUS_SESSION_BUS_ADDRESS#unix:path=}"
      _dbus_suffix=""
      if [[ "$_dbus_path" == *,* ]]; then _dbus_suffix=",${_dbus_path#*,}"; _dbus_path="${_dbus_path%%,*}"; fi
      [[ "$_dbus_path" == /* ]] || _dbus_path="$host_runtime/$_dbus_path"
      export DBUS_SESSION_BUS_ADDRESS="unix:path=$_dbus_path$_dbus_suffix"
    fi
    if [[ "${PULSE_SERVER:-}" == unix:* ]]; then
      _pulse_path="${PULSE_SERVER#unix:}"
      [[ "$_pulse_path" == /* ]] || _pulse_path="$host_runtime/$_pulse_path"
      export PULSE_SERVER="unix:$_pulse_path"
    fi
    if [ -n "${PIPEWIRE_REMOTE:-}" ] && [[ "$PIPEWIRE_REMOTE" != /* ]]; then
      export PIPEWIRE_REMOTE="$host_runtime/$PIPEWIRE_REMOTE"
    fi
  fi
  IFS=: read -r -a _host_mounts <<< "${STVR_MONADO_HOST_MOUNTS:-}"
  for mount in "${_host_mounts[@]}"; do
    [ -n "$mount" ] || continue
    append_colon_path PRESSURE_VESSEL_FILESYSTEMS_RW "$mount"
  done
  _dbus_mount="${DBUS_SESSION_BUS_ADDRESS:-}"
  _dbus_mount="${_dbus_mount#unix:path=}"
  _pulse_mount="${PULSE_SERVER:-}"
  _pulse_mount="${_pulse_mount#unix:}"
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
  configure_host_monado_runtime
  append_colon_path PRESSURE_VESSEL_FILESYSTEMS_RW "$MONADO_IPC_SOCKET"
fi
export GAMEID="umu-$APPID"

ARGS=(--exePath "$(to_win_path "$GAME_EXE")")
if [ "${STVR_COMPANION:-0}" != "1" ]; then
  ARGS+=(--no-companion)
fi
ARGS+=("$@")

if command -v umu-run >/dev/null 2>&1 && [ "${STVR_FORCE_PROTON:-0}" != "1" ]; then
  export STORE="${STORE:-steam}"
  export PROTONPATH="${PROTONPATH:-$PROTON_DIR}"
  MODE="umu-run"
  COMMAND=(umu-run "$LAUNCHER" "${ARGS[@]}")
else
  PROTON_BIN="${STVR_PROTON:-$PROTON_DIR/proton}"
  require_file "$PROTON_BIN"
  export PROTONPATH="${PROTONPATH:-$PROTON_DIR}"
  MODE="proton"
  COMMAND=("$PROTON_BIN" run "$LAUNCHER" "${ARGS[@]}")
fi

if [ "${STVR_DRY_RUN:-0}" = "1" ]; then
  printf 'Mode: %s\nGame dir: %s\nCompatdata: %s\nOpenVR runtime: %s\nOpenVR runtime path: %s\nOpenVR pathreg: %s\nVR override: %s\nVR pathreg override: %s\nProton VR runtime: %s\nMonado runtime: %s\nXR runtime: %s\nOpenXR library path: %s\nGAMEID: %s\nPressure vessel RW: %s\nPressure vessel RO: %s\nServer: %s\nCommand:' \
    "$MODE" "$GAME_DIR" "$COMPATDATA" "$STVR_SELECTED_OPENVR_RUNTIME" "$STVR_SELECTED_OPENVR_RUNTIME_PATH" "$STVR_SELECTED_OPENVR_PATHREG" "${VR_OVERRIDE:-none}" "${VR_PATHREG_OVERRIDE:-none}" "${PROTON_VR_RUNTIME:-none}" "${MONADO_RUNTIME_DIR:-default}" "${XR_RUNTIME_JSON:-default}" "${LD_LIBRARY_PATH:-default}" "$GAMEID" \
    "${PRESSURE_VESSEL_FILESYSTEMS_RW:-}" "${PRESSURE_VESSEL_FILESYSTEMS_RO:-}" "${STVR_AUTOCONNECT:-disabled}"
  printf ' %q' "${COMMAND[@]}"
  printf '\n'
  exit 0
fi

seed_handoff_plugin_order "$PLUGINS_FILE" "$LOADORDER_FILE" "$GAME_DIR/Data"
if [ -d "$DISABLED_DIR" ]; then
  mkdir -p "$SKSE_PLUGIN_DIR"
  find "$DISABLED_DIR" -maxdepth 1 -type f -name 'SkyrimTogetherVR*' -exec mv -f {} "$SKSE_PLUGIN_DIR/" \;
fi

cd "$GAME_DIR"
exec "${COMMAND[@]}"
