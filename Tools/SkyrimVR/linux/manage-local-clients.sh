#!/usr/bin/env bash
# Prepare and run isolated local Skyrim Together VR clients. This script never
# removes a completed client directory; use a new instance name for a fresh copy.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LAUNCHER="${STVR_CLIENT_LAUNCHER:-$SCRIPT_DIR/launch-skyrim-together-vr.sh}"
MONADO_MANAGER="${STVR_MONADO_MANAGER:-$SCRIPT_DIR/manage-monado-instance.sh}"
DEFAULT_SERVER="${STVR_SERVER:-incidentalstoat.xyz:26099}"

ROOT="${STVR_CLIENT_ROOT:-}"
BASE_GAME_DIR="${STVR_BASE_GAME_DIR:-${STVR_GAME_DIR:-}}"
BASE_COMPATDATA="${STVR_BASE_COMPATDATA:-${STVR_COMPATDATA:-}}"
SERVER="$DEFAULT_SERVER"
DRY_RUN=0
MANAGE_MONADO="${STVR_MANAGE_MONADO:-1}"
CLIENT_STOP_TIMEOUT_SECONDS=""
CLIENT_LOCK_TIMEOUT_SECONDS=""
SYSTEMCTL_QUERY_TIMEOUT_SECONDS=""

die() {
  printf 'manage-local-clients: %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage:
  manage-local-clients.sh [options] prepare NAME
  manage-local-clients.sh [options] status NAME
  manage-local-clients.sh [options] launch NAME [SERVER]
  manage-local-clients.sh [options] stop NAME
  manage-local-clients.sh self-test

Options:
  --root PATH              client root (default: beside --base-game)
  --base-game PATH         installed, configured Skyrim VR game directory
  --base-compatdata PATH   initialized Steam compatdata directory (contains pfx)
  --server HOST:PORT       default server for launch
  --no-manage-monado       do not start/stop the matching Monado instance
  --dry-run                print the requested operation without requiring a game
EOF
}

is_valid_name() {
  [[ "$1" =~ ^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$ ]]
}

validate_name() {
  is_valid_name "$1" || die "invalid instance name '$1' (use 1-64 letters, digits, _ or -, beginning with a letter or digit)"
}

validate_server() {
  local server="$1" host port
  [[ "$server" =~ ^[A-Za-z0-9._-]+:[0-9]{1,5}$ ]] || die "invalid server '$server' (expected HOST:PORT)"
  host="${server%:*}"
  port="${server##*:}"
  [ -n "$host" ] && [ "$port" -ge 1 ] && [ "$port" -le 65535 ] || die "invalid server port in '$server'"
}

validate_absolute_path() {
  local path="$1" label="$2"
  [ -n "$path" ] || die "$label is required"
  [[ "$path" == /* && "$path" != *$'\n'* && "$path" != *$'\r'* ]] || die "$label must be a newline-free absolute path"
}

parse_seconds() {
  local variable="$1" default_value="$2" minimum="$3" maximum="$4" value
  value="${!variable:-$default_value}"
  value="${value%s}"
  [[ "$value" =~ ^[0-9]+$ ]] || die "$variable must be an integer number of seconds"
  [ "$value" -ge "$minimum" ] && [ "$value" -le "$maximum" ] || \
    die "$variable must be between $minimum and $maximum seconds"
  printf '%s\n' "$value"
}

configure_timeouts() {
  CLIENT_STOP_TIMEOUT_SECONDS="$(parse_seconds STVR_CLIENT_STOP_TIMEOUT 30 1 120)"
  CLIENT_LOCK_TIMEOUT_SECONDS="$(parse_seconds STVR_CLIENT_LOCK_TIMEOUT 30 1 120)"
  SYSTEMCTL_QUERY_TIMEOUT_SECONDS="$(parse_seconds STVR_CLIENT_SYSTEMCTL_QUERY_TIMEOUT 5 1 30)"
}

path_is_below() {
  local child="$1" parent="$2"
  [[ "$child" == "$parent" || "$child" == "$parent"/* ]]
}

normalize_root() {
  if [ -z "$ROOT" ]; then
    [ -n "$BASE_GAME_DIR" ] || die "client root is required when no base game path is available"
    ROOT="$(dirname -- "$(readlink -m -- "$BASE_GAME_DIR")")/SkyrimTogetherVR-local-clients"
  fi
  validate_absolute_path "$ROOT" 'client root'
  ROOT="$(readlink -m -- "$ROOT")"
  [ "$ROOT" != / ] || die 'client root must not be /'
  if [ "$DRY_RUN" -eq 0 ]; then
    if [ -e "$ROOT" ] || [ -L "$ROOT" ]; then
      [ -d "$ROOT" ] && [ ! -L "$ROOT" ] || die "client root is not a safe directory: $ROOT"
    else
      mkdir -p -- "$ROOT"
    fi
    local resolved
    resolved="$(readlink -f -- "$ROOT")"
    [ "$resolved" = "$ROOT" ] || die "client root resolves through a symlink: $ROOT"
  fi
}

require_source_dir() {
  local path="$1" label="$2"
  validate_absolute_path "$path" "$label"
  [ -d "$path" ] && [ ! -L "$path" ] || die "$label is not a safe directory: $path"
  readlink -f -- "$path"
}

client_dir() {
  printf '%s/%s\n' "$ROOT" "$1"
}

client_unit() {
  printf 'stvr-local-client-%s.service\n' "$1"
}

file_has_exact_lines() {
  local file="$1"
  shift
  [ -f "$file" ] && [ ! -L "$file" ] || return 1
  [ "$(wc -l < "$file" | tr -d '[:space:]')" = "$#" ] || return 1
  local line expected
  while IFS= read -r line || [ -n "$line" ]; do
    expected="$1"
    shift
    [ "$line" = "$expected" ] || return 1
  done < "$file"
  [ "$#" -eq 0 ]
}

atomic_write_lines() {
  local target="$1" directory temporary
  shift
  directory="$(dirname -- "$target")"
  [ -d "$directory" ] && [ ! -L "$directory" ] || die "unsafe state directory: $directory"
  if [ -e "$target" ] || [ -L "$target" ]; then
    [ -f "$target" ] && [ ! -L "$target" ] || die "refusing to replace unsafe state file: $target"
  fi
  temporary="$(mktemp "$directory/.stvr-write.XXXXXX")"
  (
    umask 077
    printf '%s\n' "$@" > "$temporary"
    chmod 600 -- "$temporary"
    mv -f -- "$temporary" "$target"
  ) || {
    rm -f -- "$temporary"
    die "could not write state file: $target"
  }
}

client_marker_path() { printf '%s/.stvr-local-client\n' "$1"; }
client_unit_marker_path() { printf '%s/.stvr-local-client-unit\n' "$1"; }

client_marker_is_owned() {
  local name="$1" client="$2" marker unit
  marker="$(client_marker_path "$client")"
  unit="$(client_unit "$name")"
  [ -d "$client" ] && [ ! -L "$client" ] && \
    [ "$(readlink -f -- "$client")" = "$client" ] && \
    file_has_exact_lines "$marker" \
      'format=1' \
      'kind=stvr-local-client' \
      "name=$name" \
      "root=$ROOT" \
      "client_dir=$client" \
      "unit=$unit"
}

require_client_dir() {
  local name="$1" client
  client="$(client_dir "$name")"
  client_marker_is_owned "$name" "$client" || die "client '$name' has not been prepared by this manager or is unsafe"
}

require_regular_metadata() {
  local path="$1/metadata"
  [ -f "$path" ] && [ ! -L "$path" ] || die "client metadata is missing or unsafe: $path"
}

metadata_value() {
  local file="$1" key="$2"
  sed -n "s/^${key}=//p" "$file" | head -n 1
}

copy_tree() {
  local source="$1" destination="$2"
  # Preserve source hard-link topology so du's one-copy accounting remains a
  # valid upper bound even on a filesystem without reflink support.
  mkdir -p -- "$destination"
  cp -R --reflink=auto --preserve=mode,timestamps,links -- "$source"/. "$destination"/
}

source_overlap_is_safe() {
  local source="$1"
  ! path_is_below "$ROOT" "$source" && ! path_is_below "$source" "$ROOT"
}

check_prepare_space() {
  local game_source="$1" compat_source="$2" game_bytes compat_bytes reserve required available
  reserve="${STVR_PREPARE_FREE_RESERVE_BYTES:-2147483648}"
  [[ "$reserve" =~ ^[0-9]+$ ]] || die 'STVR_PREPARE_FREE_RESERVE_BYTES must be an integer'
  game_bytes="$(du -sx --block-size=1 -- "$game_source" | awk '{print $1}')"
  compat_bytes="$(du -sx --block-size=1 -- "$compat_source" | awk '{print $1}')"
  [[ "$game_bytes" =~ ^[0-9]+$ && "$compat_bytes" =~ ^[0-9]+$ ]] || die 'could not determine source sizes'
  required=$((game_bytes + compat_bytes + reserve))
  available="$(df -B1 --output=avail "$ROOT" | tail -n 1 | tr -d '[:space:]')"
  [[ "$available" =~ ^[0-9]+$ ]] || die "could not determine free space below $ROOT"
  [ "$available" -ge "$required" ] || die "insufficient free space below $ROOT (need at most $required bytes including reserve; $available available)"
  printf 'Prepare space check: at most %s bytes needed; %s available.\n' "$required" "$available"
}

monado_runtime_dir() {
  local name="$1" parent
  parent="${STVR_MONADO_PARENT_RUNTIME:-${XDG_RUNTIME_DIR:-/run/user/$(id -u)}}"
  printf '%s/stvr-monado-instances/%s\n' "$(readlink -m -- "$parent")" "$name"
}

check_socket_length() {
  local path="$1" bytes
  bytes="$(LC_ALL=C printf %s "$path" | wc -c | tr -d '[:space:]')"
  [[ "$bytes" =~ ^[0-9]+$ ]] && [ "$bytes" -le 107 ] || die "Monado IPC socket path is $bytes bytes; AF_UNIX permits at most 107: $path"
}

stage_marker_is_owned() {
  local stage="$1" name="$2" destination="$3"
  [ -d "$stage" ] && [ ! -L "$stage" ] && \
    file_has_exact_lines "$stage/.stvr-local-client-stage" \
      'format=1' \
      'kind=stvr-local-client-stage' \
      "name=$name" \
      "root=$ROOT" \
      "destination=$destination"
}

prepare() {
  local name="$1" game_source compat_source destination runtime
  validate_name "$name"
  destination="$(client_dir "$name")"
  if [ "$DRY_RUN" -eq 1 ]; then
    printf 'Would prepare client: %s\nRoot: %s\nGame source: %s\nCompatdata source: %s\n' \
      "$name" "$ROOT" "${BASE_GAME_DIR:-<required for real prepare>}" "${BASE_COMPATDATA:-<required for real prepare>}"
    return 0
  fi
  [ ! -e "$destination" ] && [ ! -L "$destination" ] || die "client '$name' already exists; this tool never replaces client data"
  game_source="$(require_source_dir "$BASE_GAME_DIR" 'base game directory')"
  compat_source="$(require_source_dir "$BASE_COMPATDATA" 'base compatdata directory')"
  [ -d "$compat_source/pfx" ] && [ ! -L "$compat_source/pfx" ] || die "base compatdata has no safe pfx directory: $compat_source/pfx"
  source_overlap_is_safe "$game_source" || die 'base game directory overlaps the client root; refusing recursive staging copy'
  source_overlap_is_safe "$compat_source" || die 'base compatdata overlaps the client root; refusing recursive staging copy'
  ! path_is_below "$game_source" "$compat_source" && ! path_is_below "$compat_source" "$game_source" || \
    die 'base game and compatdata directories must not be ancestors of one another'
  runtime="$(monado_runtime_dir "$name")"
  check_socket_length "$runtime/monado_comp_ipc"
  check_prepare_space "$game_source" "$compat_source"

  (
    set -euo pipefail
    local stage stale_seed
    stage="$(mktemp -d "$ROOT/.stvr-local-client-stage-$name.XXXXXX")"
    cleanup_stage() {
      if stage_marker_is_owned "$stage" "$name" "$destination"; then
        rm -rf --one-file-system -- "$stage"
      fi
    }
    trap cleanup_stage EXIT HUP INT TERM
    chmod 700 -- "$stage"
    atomic_write_lines "$stage/.stvr-local-client-stage" \
      'format=1' \
      'kind=stvr-local-client-stage' \
      "name=$name" \
      "root=$ROOT" \
      "destination=$destination"
    copy_tree "$game_source" "$stage/game"
    copy_tree "$compat_source" "$stage/compatdata"
    mkdir -p -- "$stage/state/dxvk" "$stage/state/vkd3d" "$stage/logs"
    stale_seed="$stage/stale-runtime-seed"
    if [ -d "$stage/game/Data/SkyrimTogetherReborn" ] && [ ! -L "$stage/game/Data/SkyrimTogetherReborn" ]; then
      shopt -s nullglob
      local stale_files=("$stage/game/Data/SkyrimTogetherReborn"/SkyrimTogetherVR.*)
      shopt -u nullglob
      if [ "${#stale_files[@]}" -gt 0 ]; then
        mkdir -p -- "$stale_seed"
        mv -- "${stale_files[@]}" "$stale_seed/"
      fi
    fi
    atomic_write_lines "$(client_marker_path "$stage")" \
      'format=1' \
      'kind=stvr-local-client' \
      "name=$name" \
      "root=$ROOT" \
      "client_dir=$destination" \
      "unit=$(client_unit "$name")"
    atomic_write_lines "$stage/metadata" \
      "name=$name" \
      "game_dir=$destination/game" \
      "compatdata=$destination/compatdata" \
      "wineprefix=$destination/compatdata/pfx" \
      "runtime_dir=$runtime" \
      "state_dir=$destination/state" \
      "log_dir=$destination/logs" \
      "monado_ipc_socket=$runtime/monado_comp_ipc" \
      "prepared_at=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    [ ! -e "$destination" ] && [ ! -L "$destination" ] || die "client '$name' appeared during staging; refusing to replace it"
    mv -- "$stage" "$destination"
    trap - EXIT HUP INT TERM
  )
  printf 'Prepared client %s at %s\n' "$name" "$destination"
}

client_unit_marker_value() {
  local client="$1" key="$2"
  sed -n "s/^${key}=//p" "$(client_unit_marker_path "$client")" | head -n 1
}

client_unit_marker_is_valid() {
  local name="$1" client="$2" marker unit attempt invocation
  client_marker_is_owned "$name" "$client" || return 1
  marker="$(client_unit_marker_path "$client")"
  unit="$(client_unit "$name")"
  [ -f "$marker" ] && [ ! -L "$marker" ] || return 1
  attempt="$(client_unit_marker_value "$client" attempt_id)"
  invocation="$(client_unit_marker_value "$client" invocation_id)"
  [[ "$attempt" =~ ^[A-Za-z0-9_-]{16,128}$ ]] || return 1
  [[ "$invocation" = pending || "$invocation" =~ ^[A-Fa-f0-9]{32}$ ]] || return 1
  file_has_exact_lines "$marker" \
    'format=1' \
    'kind=stvr-local-client-unit' \
    "name=$name" \
    "unit=$unit" \
    "attempt_id=$attempt" \
    "invocation_id=$invocation"
}

write_client_unit_marker() {
  local name="$1" client="$2" attempt="$3" invocation="$4"
  [[ "$attempt" =~ ^[A-Za-z0-9_-]{16,128}$ ]] || die 'invalid internal client attempt identifier'
  [[ "$invocation" = pending || "$invocation" =~ ^[A-Fa-f0-9]{32}$ ]] || die 'invalid systemd InvocationID'
  atomic_write_lines "$(client_unit_marker_path "$client")" \
    'format=1' \
    'kind=stvr-local-client-unit' \
    "name=$name" \
    "unit=$(client_unit "$name")" \
    "attempt_id=$attempt" \
    "invocation_id=$invocation"
}

generate_attempt_id() {
  local value
  value="$(od -An -N16 -tx1 /dev/urandom | tr -d '[:space:]')"
  [[ "$value" =~ ^[a-f0-9]{32}$ ]] || die 'could not generate a client ownership identifier'
  printf '%s\n' "$value"
}

client_unit_property() {
  local name="$1" property="$2" value unit
  unit="$(client_unit "$name")"
  value="$(timeout --foreground "${SYSTEMCTL_QUERY_TIMEOUT_SECONDS}s" \
    systemctl --user show --property="$property" --value "$unit" 2>/dev/null)" || return 1
  printf '%s\n' "$value"
}

client_unit_load_state() {
  local name="$1" value
  value="$(client_unit_property "$name" LoadState)" || return 1
  [ -n "$value" ] || return 1
  printf '%s\n' "$value"
}

client_unit_environment_matches_marker() {
  local name="$1" client="$2" environment attempt
  client_unit_marker_is_valid "$name" "$client" || return 1
  attempt="$(client_unit_marker_value "$client" attempt_id)"
  environment="$(client_unit_property "$name" Environment)" || return 1
  [[ " $environment " == *" STVR_LOCAL_CLIENT_MANAGER=1 "* ]] && \
    [[ " $environment " == *" STVR_LOCAL_CLIENT_NAME=$name "* ]] && \
    [[ " $environment " == *" STVR_LOCAL_CLIENT_ATTEMPT=$attempt "* ]]
}

client_unit_is_owned() {
  local name="$1" client="$2" expected actual
  client_unit_marker_is_valid "$name" "$client" || return 1
  expected="$(client_unit_marker_value "$client" invocation_id)"
  [ "$expected" != pending ] || return 1
  client_unit_environment_matches_marker "$name" "$client" || return 1
  actual="$(client_unit_property "$name" InvocationID)" || return 1
  [ "$actual" = "$expected" ]
}

client_unit_active() {
  systemctl --user is-active --quiet "$(client_unit "$1")"
}

remove_owned_client_unit_marker() {
  local name="$1" client="$2" marker
  marker="$(client_unit_marker_path "$client")"
  [ -e "$marker" ] || [ -L "$marker" ] || return 0
  client_unit_marker_is_valid "$name" "$client" || die "refusing to remove unsafe client unit marker: $marker"
  unlink -- "$marker"
}

runtime_state_value() {
  local runtime="$1" key="$2"
  sed -n "s/^${key}=//p" "$runtime/.stvr-monado-runtime" | head -n 1
}

load_runtime_state() {
  local name="$1" runtime="$2" state
  state="$runtime/.stvr-monado-runtime"
  [ -f "$state" ] && [ ! -L "$state" ] || die "matching Monado runtime state is missing or unsafe: $state"
  RUNTIME_MONADO_PREFIX="$(runtime_state_value "$runtime" monado_prefix)"
  RUNTIME_XR_JSON="$(runtime_state_value "$runtime" xr_runtime_json)"
  RUNTIME_LIBRARY_PATH="$(runtime_state_value "$runtime" monado_library_path)"
  RUNTIME_HOST_MOUNTS="$(runtime_state_value "$runtime" host_mounts)"
  RUNTIME_WAYLAND_DISPLAY="$(runtime_state_value "$runtime" wayland_display)"
  RUNTIME_DBUS_ADDRESS="$(runtime_state_value "$runtime" dbus_session_bus_address)"
  RUNTIME_PULSE_SERVER="$(runtime_state_value "$runtime" pulse_server)"
  RUNTIME_PIPEWIRE_REMOTE="$(runtime_state_value "$runtime" pipewire_remote)"
  [ -d "$RUNTIME_MONADO_PREFIX" ] && [ ! -L "$RUNTIME_MONADO_PREFIX" ] || die 'Monado runtime state has an unsafe prefix'
  [ -f "$RUNTIME_XR_JSON" ] && [ ! -L "$RUNTIME_XR_JSON" ] || die 'Monado runtime state has an unsafe OpenXR manifest'
  [ -n "$RUNTIME_LIBRARY_PATH" ] || die 'Monado runtime state has no library path'
  [ "$(runtime_state_value "$runtime" instance)" = "$name" ] || die 'Monado runtime state belongs to another instance'
  [ "$(runtime_state_value "$runtime" runtime_dir)" = "$runtime" ] || die 'Monado runtime state has a mismatched runtime directory'
}

write_metadata_after_launch() {
  local client="$1" name="$2" unit="$3" invocation="$4" log="$5" now="$6" metadata temporary
  metadata="$client/metadata"
  temporary="$(mktemp "$client/.metadata.XXXXXX")"
  {
    sed '/^unit=/d; /^unit_invocation_id=/d; /^last_launch=/d; /^last_log=/d' "$metadata"
    printf 'unit=%s\nunit_invocation_id=%s\nlast_launch=%s\nlast_log=%s\n' "$unit" "$invocation" "$now" "$log"
  } > "$temporary"
  chmod 600 -- "$temporary"
  mv -f -- "$temporary" "$metadata"
}

cleanup_failed_client_start() {
  local name="$1" client="$2" load_state unit
  unit="$(client_unit "$name")"
  load_state="$(client_unit_load_state "$name" 2>/dev/null || true)"
  if [ "$load_state" != not-found ] && client_unit_environment_matches_marker "$name" "$client"; then
    systemctl --user stop "$unit" >/dev/null 2>&1 || true
  fi
  remove_owned_client_unit_marker "$name" "$client" || true
}

wait_for_client_unit_removal() {
  local name="$1" elapsed=0 state
  while :; do
    state="$(client_unit_load_state "$name")" || return 1
    [ "$state" = not-found ] && return 0
    [ "$elapsed" -lt "$CLIENT_STOP_TIMEOUT_SECONDS" ] || return 1
    sleep 1
    elapsed=$((elapsed + 1))
  done
}

launch() {
  local name="$1" server="$2" client game compat prefix runtime state logs socket log now attempt invocation load_state unit
  validate_name "$name"
  validate_server "$server"
  client="$(client_dir "$name")"
  if [ "$DRY_RUN" -eq 1 ]; then
    printf 'Would launch client: %s\nGame: %s/game\nCompatdata: %s/compatdata\nWineprefix: %s/compatdata/pfx\nMonado runtime: %s\nServer: %s\n' \
      "$name" "$client" "$client" "$client" "$(monado_runtime_dir "$name")" "$server"
    return 0
  fi
  [ -x "$LAUNCHER" ] || die "required launcher is not executable: $LAUNCHER"
  [ -x "$MONADO_MANAGER" ] || die "Monado instance manager is not executable: $MONADO_MANAGER"
  require_client_dir "$name"
  require_regular_metadata "$client"
  game="$client/game"
  compat="$client/compatdata"
  prefix="$compat/pfx"
  runtime="$(monado_runtime_dir "$name")"
  state="$client/state"
  logs="$client/logs"
  socket="$runtime/monado_comp_ipc"
  check_socket_length "$socket"
  [ -d "$game" ] && [ ! -L "$game" ] || die 'client game directory is missing or unsafe'
  [ -d "$compat" ] && [ ! -L "$compat" ] || die 'client compatdata directory is missing or unsafe'
  [ -d "$prefix" ] && [ ! -L "$prefix" ] || die 'client Wine prefix is missing or unsafe'
  mkdir -p -- "$state/dxvk" "$state/vkd3d" "$logs"

  unit="$(client_unit "$name")"
  load_state="$(client_unit_load_state "$name")" || die "failed to query client transient unit: $unit"
  if [ "$load_state" != not-found ]; then
    if client_unit_is_owned "$name" "$client" && client_unit_active "$name"; then
      die "client '$name' is already running in owned cgroup $unit"
    fi
    die "refusing to manage pre-existing client unit that does not match its marker/environment/InvocationID: $unit"
  fi
  remove_owned_client_unit_marker "$name" "$client"

  if [ "$MANAGE_MONADO" = 1 ]; then
    "$MONADO_MANAGER" start "$name"
  fi
  # status independently verifies a non-symlink exact socket endpoint, live
  # listener, marker, environment, and InvocationID before UMU is invoked.
  "$MONADO_MANAGER" status "$name" >/dev/null
  load_runtime_state "$name" "$runtime"

  now="$(date -u +%Y%m%dT%H%M%SZ)"
  log="$logs/launch-$now.log"
  attempt="$(generate_attempt_id)"
  write_client_unit_marker "$name" "$client" "$attempt" pending
  if ! systemd-run --user --unit="${unit%.service}" --collect --service-type=exec \
    --property=KillMode=control-group --property=SendSIGKILL=no \
    --property="TimeoutStopSec=${CLIENT_STOP_TIMEOUT_SECONDS}s" \
    --property="StandardOutput=append:$log" --property="StandardError=append:$log" \
    --description="STVR local client: $name" \
    --setenv=STVR_LOCAL_CLIENT_MANAGER=1 \
    --setenv=STVR_LOCAL_CLIENT_NAME="$name" \
    --setenv=STVR_LOCAL_CLIENT_ATTEMPT="$attempt" \
    --setenv=STVR_GAME_DIR="$game" \
    --setenv=STVR_COMPATDATA="$compat" \
    --setenv=STVR_WINEPREFIX="$prefix" \
    --setenv=STVR_MONADO_RUNTIME_DIR="$runtime" \
    --setenv=STVR_MONADO_IPC_SOCKET="$socket" \
    --setenv=STVR_MONADO_PREFIX="$RUNTIME_MONADO_PREFIX" \
    --setenv=STVR_MONADO_XR_RUNTIME_JSON="$RUNTIME_XR_JSON" \
    --setenv=STVR_MONADO_LIBRARY_PATH="$RUNTIME_LIBRARY_PATH" \
    --setenv=STVR_MONADO_HOST_MOUNTS="$RUNTIME_HOST_MOUNTS" \
    --setenv=STVR_AUTOCONNECT="$server" \
    --setenv=STVR_CLIENT_RUNTIME_DIR="$state" \
    --setenv=STVR_LOG_DIR="$logs" \
    --setenv=STEAM_COMPAT_DATA_PATH="$compat" \
    --setenv=WINEPREFIX="$prefix" \
    --setenv=PROTON_LOG_DIR="$logs" \
    --setenv=DXVK_STATE_CACHE_PATH="$state/dxvk" \
    --setenv=VKD3D_SHADER_CACHE_PATH="$state/vkd3d" \
    --setenv=DBUS_SESSION_BUS_ADDRESS="$RUNTIME_DBUS_ADDRESS" \
    --setenv=PULSE_SERVER="$RUNTIME_PULSE_SERVER" \
    --setenv=PIPEWIRE_REMOTE="$RUNTIME_PIPEWIRE_REMOTE" \
    ${RUNTIME_WAYLAND_DISPLAY:+"--setenv=WAYLAND_DISPLAY=$RUNTIME_WAYLAND_DISPLAY"} \
    "$LAUNCHER"; then
    cleanup_failed_client_start "$name" "$client"
    die "could not start client transient unit: $unit"
  fi
  invocation="$(client_unit_property "$name" InvocationID)" || {
    cleanup_failed_client_start "$name" "$client"
    die "could not read InvocationID for client unit: $unit"
  }
  if ! [[ "$invocation" =~ ^[A-Fa-f0-9]{32}$ ]]; then
    cleanup_failed_client_start "$name" "$client"
    die "systemd returned an invalid InvocationID for $unit"
  fi
  write_client_unit_marker "$name" "$client" "$attempt" "$invocation"
  if ! client_unit_active "$name" || ! client_unit_is_owned "$name" "$client"; then
    cleanup_failed_client_start "$name" "$client"
    die "client unit failed before owned cgroup status could be established: $unit"
  fi
  write_metadata_after_launch "$client" "$name" "$unit" "$invocation" "$log" "$now"
  printf 'Launched client %s in cgroup %s\nLog: %s\n' "$name" "$unit" "$log"
}

status() {
  local name="$1" client metadata unit state
  validate_name "$name"
  client="$(client_dir "$name")"
  require_client_dir "$name"
  require_regular_metadata "$client"
  metadata="$client/metadata"
  unit="$(client_unit "$name")"
  printf 'Client: %s\nGame: %s\nCompatdata: %s\nLogs: %s\nUnit: %s\n' \
    "$name" "$client/game" "$client/compatdata" "$client/logs" "$unit"
  state="$(client_unit_load_state "$name" 2>/dev/null || true)"
  if [ "$state" = not-found ] || [ -z "$state" ]; then
    printf 'State: prepared or stopped\n'
  elif client_unit_is_owned "$name" "$client"; then
    printf 'State: %s (owned cgroup; leader PID is not used)\n' "$(systemctl --user is-active "$unit" 2>/dev/null || true)"
  else
    printf 'State: conflict (unit marker, environment, or InvocationID mismatch)\n'
    return 1
  fi
}

stop() {
  local name="$1" client unit load_state
  validate_name "$name"
  client="$(client_dir "$name")"
  require_client_dir "$name"
  require_regular_metadata "$client"
  unit="$(client_unit "$name")"
  load_state="$(client_unit_load_state "$name")" || die "failed to query client transient unit: $unit"
  if [ "$load_state" != not-found ]; then
    client_unit_is_owned "$name" "$client" || \
      die "refusing to stop client unit without matching marker, environment, and InvocationID: $unit"
    systemctl --user stop "$unit"
    wait_for_client_unit_removal "$name" || \
      die "client '$name' did not stop within ${CLIENT_STOP_TIMEOUT_SECONDS}s; leaving its Monado instance running"
  fi
  remove_owned_client_unit_marker "$name" "$client"
  if [ "$MANAGE_MONADO" = 1 ] && [ -x "$MONADO_MANAGER" ]; then
    "$MONADO_MANAGER" stop "$name"
  fi
  printf 'Stopped client %s.\n' "$name"
}

with_client_lock() {
  local name="$1" operation="$2"
  shift 2
  local lock_file="$ROOT/.stvr-local-client-$name.lock" lock_fd
  [ ! -L "$lock_file" ] || die "refusing to lock through symlink: $lock_file"
  exec {lock_fd}>"$lock_file"
  flock -w "$CLIENT_LOCK_TIMEOUT_SECONDS" "$lock_fd" || die "timed out waiting for client lock: $name"
  "$operation" "$name" "$@"
  flock -u "$lock_fd"
  exec {lock_fd}>&-
}

self_test() {
  is_valid_name 'client_01' || die 'instance-name self-test failed'
  ! is_valid_name '../escape' || die 'instance-name rejection self-test failed'
  validate_server '127.0.0.1:26099'
  [ "$(parse_seconds STVR_CLIENT_SELF_TEST_TIMEOUT 1 1 2)" = 1 ] || die 'timeout self-test failed'
  printf 'Self-test passed: validation works and no game installation is required.\n'
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --root) [ "$#" -ge 2 ] || die '--root needs a path'; ROOT="$2"; shift 2 ;;
    --base-game) [ "$#" -ge 2 ] || die '--base-game needs a path'; BASE_GAME_DIR="$2"; shift 2 ;;
    --base-compatdata) [ "$#" -ge 2 ] || die '--base-compatdata needs a path'; BASE_COMPATDATA="$2"; shift 2 ;;
    --server) [ "$#" -ge 2 ] || die '--server needs HOST:PORT'; SERVER="$2"; shift 2 ;;
    --no-manage-monado) MANAGE_MONADO=0; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    --) shift; break ;;
    -*) die "unknown option: $1" ;;
    *) break ;;
  esac
done

[ "$#" -ge 1 ] || { usage >&2; exit 2; }
COMMAND="$1"
shift

if [ "$COMMAND" = self-test ]; then
  [ "$#" -eq 0 ] || die 'self-test takes no arguments'
  self_test
  exit 0
fi

configure_timeouts
normalize_root
case "$COMMAND" in
  prepare)
    [ "$#" -eq 1 ] || die 'prepare needs exactly one NAME'
    validate_name "$1"
    with_client_lock "$1" prepare
    ;;
  status)
    [ "$#" -eq 1 ] || die 'status needs exactly one NAME'
    status "$1"
    ;;
  launch)
    [ "$#" -ge 1 ] && [ "$#" -le 2 ] || die 'launch needs NAME and an optional SERVER'
    validate_name "$1"
    launch_name="$1"
    launch_server="${2:-$SERVER}"
    with_client_lock "$launch_name" launch "$launch_server"
    ;;
  stop)
    [ "$#" -eq 1 ] || die 'stop needs exactly one NAME'
    validate_name "$1"
    with_client_lock "$1" stop
    ;;
  *) die "unknown command: $COMMAND" ;;
esac
