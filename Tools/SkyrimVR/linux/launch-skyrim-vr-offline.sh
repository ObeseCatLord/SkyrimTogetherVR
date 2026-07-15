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
  local root="$1"
  local candidate
  if [ -n "${STVR_PROTONPATH:-}" ]; then
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
PROTON_DIR="$(find_proton_dir "$STEAM_ROOT")" || die "could not find Proton; set STVR_PROTONPATH"
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

INPUT_HELPER="$SCRIPT_DIR/stvr-xrizer-input-compat.sh"
[ -f "$INPUT_HELPER" ] || INPUT_HELPER="$GAME_DIR/stvr-xrizer-input-compat.sh"
[ -f "$INPUT_HELPER" ] && source "$INPUT_HELPER"

MONADO_IPC_SOCKET="${STVR_MONADO_IPC_SOCKET:-${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/monado_comp_ipc}"
if [ -S "$MONADO_IPC_SOCKET" ]; then
  export PRESSURE_VESSEL_FILESYSTEMS_RW="${PRESSURE_VESSEL_FILESYSTEMS_RW:+$PRESSURE_VESSEL_FILESYSTEMS_RW:}$MONADO_IPC_SOCKET"
fi

if command -v umu-run >/dev/null 2>&1 && [ "${STVR_FORCE_PROTON:-0}" != "1" ]; then
  export GAMEID="${GAMEID:-umu-$APPID}"
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
  printf 'Mode: %s\nGame dir: %s\nCompatdata: %s\nCommand:' "$MODE" "$GAME_DIR" "$COMPATDATA"
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
