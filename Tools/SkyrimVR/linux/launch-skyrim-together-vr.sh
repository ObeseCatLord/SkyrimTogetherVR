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
  local root="$1"
  local candidate
  if [ -n "${STVR_PROTONPATH:-}" ]; then
    [ -x "$STVR_PROTONPATH/proton" ] || die "STVR_PROTONPATH has no executable proton script"
    printf '%s\n' "$STVR_PROTONPATH"
    return 0
  fi

  local search_root
  for search_root in "$root/compatibilitytools.d" "$root/steamapps/common"; do
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

enable_plugin() {
  local file="$1"
  local entry="$2"
  mkdir -p "$(dirname -- "$file")"
  touch "$file"
  if ! grep -Fxiq -- "$entry" "$file"; then
    printf '%s\n' "$entry" >> "$file"
  fi
}

STEAM_ROOT="$(find_steam_root)" || die "could not find Steam; set STVR_STEAM_ROOT"
STEAM_LIBRARY="${STVR_STEAM_LIBRARY:-$(readlink -f -- "$GAME_DIR/../../..")}"
COMPATDATA="${STVR_COMPATDATA:-$STEAM_LIBRARY/steamapps/compatdata/$APPID}"
WINEPREFIX_DIR="${STVR_WINEPREFIX:-$COMPATDATA/pfx}"
PROTON_DIR="$(find_proton_dir "$STEAM_ROOT")" || die "could not find Proton; set STVR_PROTONPATH"

LAUNCHER="${STVR_LAUNCHER:-$GAME_DIR/SkyrimTogetherVR.exe}"
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

INPUT_HELPER="$SCRIPT_DIR/stvr-xrizer-input-compat.sh"
[ -f "$INPUT_HELPER" ] || INPUT_HELPER="$GAME_DIR/stvr-xrizer-input-compat.sh"
[ -f "$INPUT_HELPER" ] && source "$INPUT_HELPER"

MONADO_IPC_SOCKET="${STVR_MONADO_IPC_SOCKET:-${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/monado_comp_ipc}"
if [ -S "$MONADO_IPC_SOCKET" ]; then
  export PRESSURE_VESSEL_FILESYSTEMS_RW="${PRESSURE_VESSEL_FILESYSTEMS_RW:+$PRESSURE_VESSEL_FILESYSTEMS_RW:}$MONADO_IPC_SOCKET"
fi

ARGS=(--exePath "$(to_win_path "$GAME_EXE")")
if [ "${STVR_COMPANION:-0}" != "1" ]; then
  ARGS+=(--no-companion)
fi
ARGS+=("$@")

if command -v umu-run >/dev/null 2>&1 && [ "${STVR_FORCE_PROTON:-0}" != "1" ]; then
  export GAMEID="${GAMEID:-umu-$APPID}"
  export STORE="${STORE:-steam}"
  export PROTONPATH="${PROTONPATH:-$PROTON_DIR}"
  MODE="umu-run"
  COMMAND=(umu-run "$LAUNCHER" "${ARGS[@]}")
else
  PROTON_BIN="${STVR_PROTON:-$PROTON_DIR/proton}"
  require_file "$PROTON_BIN"
  export STEAM_COMPAT_DATA_PATH="${STEAM_COMPAT_DATA_PATH:-$COMPATDATA}"
  export PROTONPATH="${PROTONPATH:-$PROTON_DIR}"
  MODE="proton"
  COMMAND=("$PROTON_BIN" run "$LAUNCHER" "${ARGS[@]}")
fi

if [ "${STVR_DRY_RUN:-0}" = "1" ]; then
  printf 'Mode: %s\nGame dir: %s\nCompatdata: %s\nServer: %s\nCommand:' \
    "$MODE" "$GAME_DIR" "$COMPATDATA" "${STVR_AUTOCONNECT:-disabled}"
  printf ' %q' "${COMMAND[@]}"
  printf '\n'
  exit 0
fi

enable_plugin "$PLUGINS_FILE" "*SkyrimTogether.esp"
enable_plugin "$LOADORDER_FILE" "SkyrimTogether.esp"
if [ -d "$DISABLED_DIR" ]; then
  mkdir -p "$SKSE_PLUGIN_DIR"
  find "$DISABLED_DIR" -maxdepth 1 -type f -name 'SkyrimTogetherVR*' -exec mv -f {} "$SKSE_PLUGIN_DIR/" \;
fi

cd "$GAME_DIR"
exec "${COMMAND[@]}"
