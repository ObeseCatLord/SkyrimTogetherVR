#!/usr/bin/env bash
# Run independent Monado services without changing the caller's runtime directory.
set -euo pipefail

SCRIPT_NAME="${0##*/}"
DEFAULT_INSTANCE="${STVR_MONADO_INSTANCE:-${STVR_MONADO_PROFILE:-simulated-qwerty-fixed}}"
COMMAND="${1:-status}"
INSTANCE="${2:-$DEFAULT_INSTANCE}"

MONADO_SERVICE=""
MONADO_PREFIX=""
MONADO_MANIFEST=""
MONADO_LIBRARY_PATH=""
PARENT_RUNTIME=""
INSTANCE_ROOT=""
INSTANCE_DIR=""
SOCKET=""
MARKER=""
UNIT_MARKER=""
RUNTIME_STATE=""
UNIT=""
ATTEMPT_ID=""
SYSTEMD_ENV=()
HOST_MOUNTS=()

die() {
  printf '%s: %s\n' "$SCRIPT_NAME" "$*" >&2
  exit 1
}

usage() {
  cat <<EOF
usage: $SCRIPT_NAME {start|status|stop|restart} [instance-name]
       $SCRIPT_NAME --self-test

The Envision simulated_default monado-service is preferred when installed. Set
STVR_MONADO_SERVICE to override it. Instance transitions are serialized by name.
EOF
}

valid_instance_name() {
  [[ "$1" =~ ^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$ ]]
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || die "required command is not available: $1"
}

parse_seconds() {
  local variable="$1" default_value="$2" minimum="$3" maximum="$4" value
  value="${!variable:-$default_value}"
  value="${value%s}"
  [[ "$value" =~ ^[0-9]+$ ]] || die "$variable must be an integer number of seconds"
  # Decimal shell arithmetic is bounded here before it reaches a deadline calculation.
  [ "$value" -ge "$minimum" ] && [ "$value" -le "$maximum" ] || \
    die "$variable must be between $minimum and $maximum seconds"
  printf '%s\n' "$value"
}

configure_timeouts() {
  START_TIMEOUT_SECONDS="$(parse_seconds STVR_MONADO_START_TIMEOUT 45 1 300)"
  STABILITY_SECONDS="$(parse_seconds STVR_MONADO_STABILITY_SECONDS 2 1 30)"
  STOP_TIMEOUT_SECONDS="$(parse_seconds STVR_MONADO_STOP_TIMEOUT 10 1 60)"
  LOCK_TIMEOUT_SECONDS="$(parse_seconds STVR_MONADO_LOCK_TIMEOUT 30 1 120)"
  SYSTEMCTL_QUERY_TIMEOUT_SECONDS="$(parse_seconds STVR_MONADO_SYSTEMCTL_QUERY_TIMEOUT 5 1 30)"
}

resolve_monado_service() {
  local candidate envision_service resolved
  envision_service="${HOME:-}/.local/share/envision/prefixes/simulated_default/bin/monado-service"
  if [ -n "${STVR_MONADO_SERVICE:-}" ]; then
    candidate="$STVR_MONADO_SERVICE"
  elif [ -x "$envision_service" ]; then
    candidate="$envision_service"
  else
    candidate='monado-service'
  fi

  if [[ "$candidate" == */* ]]; then
    [ -x "$candidate" ] || die "Monado service is not executable: $candidate"
    resolved="$candidate"
  else
    resolved="$(command -v -- "$candidate")" || die "Monado service is not available: $candidate"
  fi
  MONADO_SERVICE="$(readlink -f -- "$resolved")"
  [ -x "$MONADO_SERVICE" ] || die "could not resolve Monado service executable: $candidate"
  MONADO_PREFIX="$(dirname -- "$(dirname -- "$MONADO_SERVICE")")"
  [ -d "$MONADO_PREFIX" ] && [ ! -L "$MONADO_PREFIX" ] || die "unsafe Monado prefix: $MONADO_PREFIX"
}

path_is_below() {
  local child="$1" parent="$2"
  [[ "$child" == "$parent" || "$child" == "$parent"/* ]]
}

resolve_monado_manifest() {
  local candidate result
  candidate="${STVR_MONADO_XR_RUNTIME_JSON:-$MONADO_PREFIX/share/openxr/1/openxr_monado.json}"
  [ -f "$candidate" ] || die "Monado OpenXR manifest is not a regular file: $candidate"
  result="$(python3 - "$MONADO_PREFIX" "$candidate" <<'PY'
import json
import os
import sys

prefix = os.path.realpath(sys.argv[1])
manifest = os.path.realpath(sys.argv[2])

def below(path, root):
    try:
        return os.path.commonpath((path, root)) == root
    except ValueError:
        return False

if not below(manifest, prefix) or not os.path.isfile(manifest):
    raise SystemExit("manifest must resolve inside the selected Monado prefix")
try:
    with open(manifest, encoding="utf-8") as source:
        runtime = json.load(source)["runtime"]
    libraries = []
    for key in ("library_path", "MND_libmonado_path"):
        value = runtime.get(key)
        if not isinstance(value, str) or not value:
            raise ValueError(key)
        library = os.path.realpath(os.path.join(os.path.dirname(manifest), value))
        if not below(library, prefix) or not os.path.isfile(library):
            raise ValueError(key)
        libraries.append(library)
except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
    raise SystemExit(f"invalid Monado OpenXR manifest: {error}")
print(manifest)
print(*libraries, sep="\n")
PY
)" || die "could not validate Monado OpenXR manifest: $candidate"
  mapfile -t _manifest_lines <<<"$result"
  [ "${#_manifest_lines[@]}" -eq 3 ] || die "Monado OpenXR manifest did not resolve both required libraries"
  MONADO_MANIFEST="${_manifest_lines[0]}"

  local library_dir
  MONADO_LIBRARY_PATH=""
  for library_dir in "$MONADO_PREFIX/lib" "$MONADO_PREFIX/lib64"; do
    [ -d "$library_dir" ] || continue
    MONADO_LIBRARY_PATH+="${MONADO_LIBRARY_PATH:+:}$library_dir"
  done
  [ -n "$MONADO_LIBRARY_PATH" ] || die "Monado prefix has no library directory: $MONADO_PREFIX"
}

configure_paths() {
  PARENT_RUNTIME="${STVR_MONADO_PARENT_RUNTIME:-${XDG_RUNTIME_DIR:-/run/user/$(id -u)}}"
  [ -d "$PARENT_RUNTIME" ] || die "parent user runtime directory does not exist: $PARENT_RUNTIME"
  PARENT_RUNTIME="$(readlink -f -- "$PARENT_RUNTIME")"
  [ -d "$PARENT_RUNTIME" ] && [ ! -L "$PARENT_RUNTIME" ] || die "unsafe parent user runtime directory: $PARENT_RUNTIME"

  INSTANCE_ROOT="$PARENT_RUNTIME/stvr-monado-instances"
  INSTANCE_DIR="$INSTANCE_ROOT/$INSTANCE"
  SOCKET="$INSTANCE_DIR/monado_comp_ipc"
  MARKER="$INSTANCE_DIR/.stvr-monado-instance"
  UNIT_MARKER="$INSTANCE_DIR/.stvr-monado-unit"
  RUNTIME_STATE="$INSTANCE_DIR/.stvr-monado-runtime"
  UNIT="stvr-monado-instance-$INSTANCE.service"

  local socket_bytes
  socket_bytes="$(LC_ALL=C printf %s "$SOCKET" | wc -c | tr -d '[:space:]')"
  [[ "$socket_bytes" =~ ^[0-9]+$ ]] && [ "$socket_bytes" -le 107 ] || \
    die "Monado IPC socket path is $socket_bytes bytes; AF_UNIX permits at most 107: $SOCKET"
}

prepare_instance_root() {
  if [ -e "$INSTANCE_ROOT" ] || [ -L "$INSTANCE_ROOT" ]; then
    [ -d "$INSTANCE_ROOT" ] && [ ! -L "$INSTANCE_ROOT" ] || \
      die "refusing to use unsafe instance root: $INSTANCE_ROOT"
  else
    mkdir -p -- "$INSTANCE_ROOT" || die "could not create instance root: $INSTANCE_ROOT"
    [ -d "$INSTANCE_ROOT" ] && [ ! -L "$INSTANCE_ROOT" ] || \
      die "instance root became unsafe while creating it: $INSTANCE_ROOT"
  fi
  chmod 700 -- "$INSTANCE_ROOT"
}

file_has_exact_lines() {
  local file="$1"
  shift
  [ -f "$file" ] && [ ! -L "$file" ] || return 1
  local expected_count="$#" actual_count line expected
  actual_count="$(wc -l < "$file" | tr -d '[:space:]')"
  [ "$actual_count" = "$expected_count" ] || return 1
  while IFS= read -r line || [ -n "$line" ]; do
    expected="$1"
    shift
    [ "$line" = "$expected" ] || return 1
  done < "$file"
  [ "$#" -eq 0 ]
}

marker_is_owned() {
  [ -d "$INSTANCE_ROOT" ] && [ ! -L "$INSTANCE_ROOT" ] && \
    [ -d "$INSTANCE_DIR" ] && [ ! -L "$INSTANCE_DIR" ] && \
    file_has_exact_lines "$MARKER" \
      'format=1' \
      'kind=stvr-monado-instance' \
      "instance=$INSTANCE" \
      "unit=$UNIT" \
      "runtime_dir=$INSTANCE_DIR"
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

prepare_instance_dir() {
  prepare_instance_root
  if [ -e "$INSTANCE_DIR" ] || [ -L "$INSTANCE_DIR" ]; then
    marker_is_owned || die "refusing to use unowned instance directory: $INSTANCE_DIR"
    return
  fi
  mkdir -- "$INSTANCE_DIR" || die "could not create instance directory: $INSTANCE_DIR"
  chmod 700 -- "$INSTANCE_DIR"
  atomic_write_lines "$MARKER" \
    'format=1' \
    'kind=stvr-monado-instance' \
    "instance=$INSTANCE" \
    "unit=$UNIT" \
    "runtime_dir=$INSTANCE_DIR"
}

unit_marker_value() {
  local key="$1"
  sed -n "s/^${key}=//p" "$UNIT_MARKER" | head -n 1
}

unit_marker_is_valid() {
  marker_is_owned || return 1
  [ -f "$UNIT_MARKER" ] && [ ! -L "$UNIT_MARKER" ] || return 1
  local attempt invocation
  attempt="$(unit_marker_value attempt_id)"
  invocation="$(unit_marker_value invocation_id)"
  [[ "$attempt" =~ ^[A-Za-z0-9_-]{16,128}$ ]] || return 1
  [[ "$invocation" = pending || "$invocation" =~ ^[A-Fa-f0-9]{32}$ ]] || return 1
  file_has_exact_lines "$UNIT_MARKER" \
    'format=1' \
    'kind=stvr-monado-unit' \
    "instance=$INSTANCE" \
    "unit=$UNIT" \
    "attempt_id=$attempt" \
    "invocation_id=$invocation"
}

write_unit_marker() {
  local attempt="$1" invocation="$2"
  [[ "$attempt" =~ ^[A-Za-z0-9_-]{16,128}$ ]] || die 'invalid internal Monado attempt identifier'
  [[ "$invocation" = pending || "$invocation" =~ ^[A-Fa-f0-9]{32}$ ]] || die 'invalid systemd InvocationID'
  atomic_write_lines "$UNIT_MARKER" \
    'format=1' \
    'kind=stvr-monado-unit' \
    "instance=$INSTANCE" \
    "unit=$UNIT" \
    "attempt_id=$attempt" \
    "invocation_id=$invocation"
}

generate_attempt_id() {
  local value
  value="$(od -An -N16 -tx1 /dev/urandom | tr -d '[:space:]')"
  [[ "$value" =~ ^[a-f0-9]{32}$ ]] || die 'could not generate a Monado ownership identifier'
  printf '%s\n' "$value"
}

socket_is_exact() {
  [ -S "$SOCKET" ] && [ ! -L "$SOCKET" ]
}

socket_listener() {
  socket_is_exact || return 1
  local listener
  listener="$(ss -xlpnH 2>/dev/null | awk -v socket="$SOCKET" '
    $2 == "LISTEN" {
      for (field = 1; field <= NF; field++) {
        if ($field == socket && !found) { print; found = 1 }
      }
    }
  ')"
  [ -n "$listener" ] || return 1
  printf '%s\n' "$listener"
}

runtime_listener_ready() {
  local listener
  listener="$(socket_listener)" || return 1
  [ -n "$listener" ]
}

unit_property() {
  local property="$1" value
  value="$(timeout --foreground "${SYSTEMCTL_QUERY_TIMEOUT_SECONDS}s" \
    systemctl --user show --property="$property" --value "$UNIT" 2>/dev/null)" || return 1
  printf '%s\n' "$value"
}

unit_load_state() {
  local value
  value="$(unit_property LoadState)" || return 1
  [ -n "$value" ] || return 1
  printf '%s\n' "$value"
}

unit_environment_matches_marker() {
  unit_marker_is_valid || return 1
  local environment attempt
  attempt="$(unit_marker_value attempt_id)"
  environment="$(unit_property Environment)" || return 1
  [[ " $environment " == *" STVR_MONADO_INSTANCE_MANAGER=1 "* ]] && \
    [[ " $environment " == *" STVR_MONADO_INSTANCE=$INSTANCE "* ]] && \
    [[ " $environment " == *" STVR_MONADO_ATTEMPT=$attempt "* ]]
}

unit_matches_owned_invocation() {
  unit_marker_is_valid || return 1
  local expected actual
  expected="$(unit_marker_value invocation_id)"
  [ "$expected" != pending ] || return 1
  unit_environment_matches_marker || return 1
  actual="$(unit_property InvocationID)" || return 1
  [ "$actual" = "$expected" ]
}

runtime_state_value() {
  local key="$1"
  sed -n "s/^${key}=//p" "$RUNTIME_STATE" | head -n 1
}

runtime_state_is_valid() {
  marker_is_owned || return 1
  [ -f "$RUNTIME_STATE" ] && [ ! -L "$RUNTIME_STATE" ] || return 1
  local invocation prefix manifest library_path mounts wayland dbus pulse pipewire
  invocation="$(runtime_state_value invocation_id)"
  prefix="$(runtime_state_value monado_prefix)"
  manifest="$(runtime_state_value xr_runtime_json)"
  library_path="$(runtime_state_value monado_library_path)"
  mounts="$(runtime_state_value host_mounts)"
  wayland="$(runtime_state_value wayland_display)"
  dbus="$(runtime_state_value dbus_session_bus_address)"
  pulse="$(runtime_state_value pulse_server)"
  pipewire="$(runtime_state_value pipewire_remote)"
  [[ "$invocation" =~ ^[A-Fa-f0-9]{32}$ ]] || return 1
  [ -d "$prefix" ] && [ ! -L "$prefix" ] || return 1
  [ -f "$manifest" ] && [ ! -L "$manifest" ] || return 1
  path_is_below "$(readlink -f -- "$manifest")" "$(readlink -f -- "$prefix")" || return 1
  [ -n "$library_path" ] || return 1
  file_has_exact_lines "$RUNTIME_STATE" \
    'format=1' \
    'kind=stvr-monado-runtime' \
    "instance=$INSTANCE" \
    "unit=$UNIT" \
    "invocation_id=$invocation" \
    "runtime_dir=$INSTANCE_DIR" \
    "monado_prefix=$prefix" \
    "xr_runtime_json=$manifest" \
    "monado_library_path=$library_path" \
    "host_mounts=$mounts" \
    "wayland_display=$wayland" \
    "dbus_session_bus_address=$dbus" \
    "pulse_server=$pulse" \
    "pipewire_remote=$pipewire"
}

runtime_matches_owned_unit() {
  runtime_state_is_valid && unit_matches_owned_invocation && \
    [ "$(runtime_state_value invocation_id)" = "$(unit_marker_value invocation_id)" ]
}

runtime_state_matches_resolved_monado() {
  [ "$(runtime_state_value monado_prefix)" = "$MONADO_PREFIX" ] && \
    [ "$(runtime_state_value xr_runtime_json)" = "$MONADO_MANIFEST" ] && \
    [ "$(runtime_state_value monado_library_path)" = "$MONADO_LIBRARY_PATH" ]
}

remove_owned_state_file() {
  local file="$1"
  [ -e "$file" ] || [ -L "$file" ] || return 0
  [ -f "$file" ] && [ ! -L "$file" ] || die "refusing to remove unsafe state file: $file"
  unlink -- "$file"
}

cleanup_owned_orphan_socket() {
  marker_is_owned || return 0
  if socket_is_exact && ! runtime_listener_ready; then
    unlink -- "$SOCKET"
    printf 'Removed orphan Monado IPC socket from instance %s.\n' "$INSTANCE"
  fi
}

normalize_path_endpoint() {
  local value="$1"
  if [[ "$value" == /* ]]; then
    printf '%s\n' "$value"
  else
    printf '%s/%s\n' "$PARENT_RUNTIME" "$value"
  fi
}

add_host_mount() {
  local endpoint="$1" existing
  [ -n "$endpoint" ] && [[ "$endpoint" == /* ]] || return 0
  [[ "$endpoint" != *:* && "$endpoint" != *$'\n'* && "$endpoint" != *$'\r'* ]] || \
    die "unsafe host runtime endpoint: $endpoint"
  [ -e "$endpoint" ] || [ -S "$endpoint" ] || return 0
  for existing in "${HOST_MOUNTS[@]}"; do
    [ "$existing" = "$endpoint" ] && return 0
  done
  HOST_MOUNTS+=("$endpoint")
}

normalize_dbus_address() {
  local address="${DBUS_SESSION_BUS_ADDRESS:-unix:path=$PARENT_RUNTIME/bus}" path suffix
  if [[ "$address" == unix:path=* ]]; then
    path="${address#unix:path=}"
    suffix=""
    if [[ "$path" == *,* ]]; then
      suffix=",${path#*,}"
      path="${path%%,*}"
    fi
    path="$(normalize_path_endpoint "$path")"
    DBUS_ENDPOINT="$path"
    DBUS_ADDRESS="unix:path=$path$suffix"
  else
    DBUS_ENDPOINT=""
    DBUS_ADDRESS="$address"
  fi
}

normalize_pulse_server() {
  local server="${PULSE_SERVER:-unix:$PARENT_RUNTIME/pulse/native}" path
  if [[ "$server" == unix:* ]]; then
    path="$(normalize_path_endpoint "${server#unix:}")"
    PULSE_ENDPOINT="$path"
    PULSE_ADDRESS="unix:$path"
  else
    PULSE_ENDPOINT=""
    PULSE_ADDRESS="$server"
  fi
}

forward_graphics_environment() {
  local wayland_raw pipewire_raw mounts variable
  SYSTEMD_ENV=(
    '--setenv=STVR_MONADO_INSTANCE_MANAGER=1'
    "--setenv=STVR_MONADO_INSTANCE=$INSTANCE"
    "--setenv=STVR_MONADO_ATTEMPT=$ATTEMPT_ID"
    "--setenv=XDG_RUNTIME_DIR=$INSTANCE_DIR"
    "--setenv=XR_RUNTIME_JSON=$MONADO_MANIFEST"
    "--setenv=LD_LIBRARY_PATH=$MONADO_LIBRARY_PATH${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  )

  append_default_environment QWERTY_ENABLE 1
  append_default_environment XRT_DEBUG_GUI 1
  append_default_environment XRT_CURATED_GUI 1
  append_default_environment XRT_COMPOSITOR_COMPUTE 0
  append_default_environment XRT_NO_STDIN 1
  if [ "${MONADO_QWERTY_NATIVE_WAYLAND:-0}" != 1 ]; then
    append_default_environment XRT_COMPOSITOR_FORCE_XCB 1
  else
    append_default_environment XRT_COMPOSITOR_FORCE_XCB 0
  fi
  append_default_environment SDL_VIDEODRIVER x11
  append_default_environment U_PACING_APP_USE_MIN_FRAME_PERIOD 1
  append_default_environment XRT_COMPOSITOR_SCALE_PERCENTAGE \
    "${STVR_MONADO_COMPOSITOR_SCALE_PERCENTAGE:-140}"

  for variable in DISPLAY XAUTHORITY XDG_CURRENT_DESKTOP; do
    if [ -n "${!variable:-}" ]; then
      SYSTEMD_ENV+=("--setenv=$variable=${!variable}")
    fi
  done

  HOST_MOUNTS=()
  WAYLAND_ADDRESS=""
  if [ -n "${WAYLAND_DISPLAY:-}" ]; then
    wayland_raw="$(normalize_path_endpoint "$WAYLAND_DISPLAY")"
    WAYLAND_ADDRESS="$wayland_raw"
    SYSTEMD_ENV+=("--setenv=WAYLAND_DISPLAY=$WAYLAND_ADDRESS")
    add_host_mount "$wayland_raw"
  fi

  normalize_dbus_address
  normalize_pulse_server
  pipewire_raw="${PIPEWIRE_REMOTE:-pipewire-0}"
  PIPEWIRE_ADDRESS="$(normalize_path_endpoint "$pipewire_raw")"
  add_host_mount "$DBUS_ENDPOINT"
  add_host_mount "$PULSE_ENDPOINT"
  add_host_mount "$PIPEWIRE_ADDRESS"
  SYSTEMD_ENV+=(
    "--setenv=DBUS_SESSION_BUS_ADDRESS=$DBUS_ADDRESS"
    "--setenv=PULSE_SERVER=$PULSE_ADDRESS"
    "--setenv=PIPEWIRE_REMOTE=$PIPEWIRE_ADDRESS"
  )
}

append_default_environment() {
  local variable default_value value
  variable="$1"
  default_value="$2"
  value="$default_value"
  if [[ -v $variable ]]; then
    value="${!variable}"
  fi
  SYSTEMD_ENV+=("--setenv=$variable=$value")
}

write_runtime_state() {
  local invocation mounts
  invocation="$(unit_marker_value invocation_id)"
  mounts="$(IFS=:; printf '%s' "${HOST_MOUNTS[*]-}")"
  atomic_write_lines "$RUNTIME_STATE" \
    'format=1' \
    'kind=stvr-monado-runtime' \
    "instance=$INSTANCE" \
    "unit=$UNIT" \
    "invocation_id=$invocation" \
    "runtime_dir=$INSTANCE_DIR" \
    "monado_prefix=$MONADO_PREFIX" \
    "xr_runtime_json=$MONADO_MANIFEST" \
    "monado_library_path=$MONADO_LIBRARY_PATH" \
    "host_mounts=$mounts" \
    "wayland_display=$WAYLAND_ADDRESS" \
    "dbus_session_bus_address=$DBUS_ADDRESS" \
    "pulse_server=$PULSE_ADDRESS" \
    "pipewire_remote=$PIPEWIRE_ADDRESS"
}

unit_active() {
  systemctl --user is-active --quiet "$UNIT"
}

wait_for_listener() {
  local elapsed=0
  while ! runtime_listener_ready; do
    unit_active || return 1
    [ "$elapsed" -lt "$START_TIMEOUT_SECONDS" ] || return 1
    sleep 1
    elapsed=$((elapsed + 1))
  done
}

wait_for_stable_listener() {
  local elapsed=0
  while [ "$elapsed" -lt "$STABILITY_SECONDS" ]; do
    unit_active && runtime_listener_ready || return 1
    sleep 1
    elapsed=$((elapsed + 1))
  done
}

wait_for_unit_removal() {
  local elapsed=0 load_state
  while :; do
    load_state="$(unit_load_state)" || return 1
    [ "$load_state" = not-found ] && return 0
    [ "$elapsed" -lt "$STOP_TIMEOUT_SECONDS" ] || return 1
    sleep 1
    elapsed=$((elapsed + 1))
  done
}

cleanup_failed_start() {
  # The pending marker and matching environment are the only authority used
  # here; this path never guesses at a PID or touches an unrelated unit/socket.
  local load_state
  load_state="$(unit_load_state 2>/dev/null || true)"
  if [ "$load_state" != not-found ] && unit_environment_matches_marker; then
    systemctl --user stop "$UNIT" >/dev/null 2>&1 || true
    wait_for_unit_removal >/dev/null 2>&1 || true
  fi
  cleanup_owned_orphan_socket
  remove_owned_state_file "$RUNTIME_STATE" || true
  remove_owned_state_file "$UNIT_MARKER" || true
}

stop_managed_instance() {
  local load_state
  load_state="$(unit_load_state)" || die "failed to query transient unit: $UNIT"
  if [ "$load_state" != not-found ]; then
    unit_matches_owned_invocation || die "refusing to stop non-instance transient unit: $UNIT"
    systemctl --user stop "$UNIT"
    wait_for_unit_removal || die "timed out waiting for owned Monado unit cgroup to stop: $UNIT"
  fi
  remove_owned_state_file "$RUNTIME_STATE"
  remove_owned_state_file "$UNIT_MARKER"
  cleanup_owned_orphan_socket
}

start_instance() {
  local load_state invocation
  require_command ss
  require_command systemctl
  require_command systemd-run
  require_command timeout
  require_command python3
  resolve_monado_service
  resolve_monado_manifest
  prepare_instance_dir

  load_state="$(unit_load_state)" || die "failed to query transient unit: $UNIT"
  if [ "$load_state" != not-found ]; then
    if runtime_matches_owned_unit && runtime_state_matches_resolved_monado && unit_active && runtime_listener_ready; then
      printf 'Monado instance %s is already ready; leaving it unchanged.\n' "$INSTANCE"
      print_status
      return 0
    fi
    stop_managed_instance
  elif runtime_listener_ready; then
    die "a live exact listener already exists in $INSTANCE_DIR without an owned transient unit; refusing to replace it"
  else
    remove_owned_state_file "$RUNTIME_STATE"
    remove_owned_state_file "$UNIT_MARKER"
    cleanup_owned_orphan_socket
  fi

  ATTEMPT_ID="$(generate_attempt_id)"
  write_unit_marker "$ATTEMPT_ID" pending
  forward_graphics_environment
  if ! systemd-run --user --unit="${UNIT%.service}" --collect --service-type=exec \
    --property=KillMode=control-group --property=SendSIGKILL=no \
    --property="TimeoutStopSec=${STOP_TIMEOUT_SECONDS}s" \
    --description="STVR Monado instance: $INSTANCE" "${SYSTEMD_ENV[@]}" \
    "$MONADO_SERVICE" >/dev/null; then
    cleanup_failed_start
    die "could not start direct Monado transient unit: $UNIT"
  fi
  invocation="$(unit_property InvocationID)" || {
    cleanup_failed_start
    die "could not read InvocationID for owned Monado unit: $UNIT"
  }
  if ! [[ "$invocation" =~ ^[A-Fa-f0-9]{32}$ ]]; then
    cleanup_failed_start
    die "systemd returned an invalid InvocationID for $UNIT"
  fi
  write_unit_marker "$ATTEMPT_ID" "$invocation"
  write_runtime_state

  if ! wait_for_listener; then
    journalctl --user -u "$UNIT" -n 80 --no-pager >&2 || true
    cleanup_failed_start
    die "direct Monado unit did not produce a live exact IPC listener at $SOCKET"
  fi
  if ! wait_for_stable_listener; then
    journalctl --user -u "$UNIT" -n 80 --no-pager >&2 || true
    cleanup_failed_start
    die "Monado IPC listener did not remain ready during the bounded stability check"
  fi
  runtime_matches_owned_unit || {
    cleanup_failed_start
    die "owned Monado unit no longer matches its runtime marker"
  }
  print_status
}

restart_instance() {
  stop_managed_instance
  start_instance
}

print_status() {
  local load_state listener state
  load_state="$(unit_load_state)" || die "failed to query transient unit: $UNIT"
  listener="$(socket_listener 2>/dev/null || true)"
  state="not loaded"
  if [ "$load_state" != not-found ]; then
    if runtime_matches_owned_unit; then
      state="$(systemctl --user is-active "$UNIT" 2>/dev/null || true)"
    else
      state='conflict: marker, environment, or InvocationID mismatch'
    fi
  fi

  printf 'Instance: %s\nUnit: %s (%s)\nRuntime directory: %s\nIPC socket: %s\n' \
    "$INSTANCE" "$UNIT" "$state" "$INSTANCE_DIR" "$SOCKET"
  if [ -n "$listener" ] && runtime_matches_owned_unit; then
    printf 'Runtime: ready\nListener: %s\nXR runtime: %s\n' "$listener" "$(runtime_state_value xr_runtime_json)"
    return 0
  fi
  if [ -L "$SOCKET" ]; then
    printf 'Runtime: invalid symlink socket\n'
  elif socket_is_exact; then
    printf 'Runtime: invalid orphan socket (no exact live listener)\n'
  else
    printf 'Runtime: stopped\n'
  fi
  return 1
}

with_instance_lock() {
  prepare_instance_root
  local lock_file="$INSTANCE_ROOT/.stvr-monado-$INSTANCE.lock" lock_fd
  [ ! -L "$lock_file" ] || die "refusing to lock through symlink: $lock_file"
  exec {lock_fd}>"$lock_file"
  flock -w "$LOCK_TIMEOUT_SECONDS" "$lock_fd" || die "timed out waiting for Monado instance lock: $INSTANCE"
  "$@"
  flock -u "$lock_fd"
  exec {lock_fd}>&-
}

run_self_test() {
  valid_instance_name 'simulated-qwerty-fixed' || die 'self-test: valid instance rejected'
  ! valid_instance_name '../escape' || die 'self-test: traversal accepted'
  ! valid_instance_name 'space name' || die 'self-test: whitespace accepted'
  [ "$(parse_seconds STVR_MONADO_SELF_TEST_TIMEOUT 1 1 2)" = 1 ] || die 'self-test: timeout parser failed'
  local root old_parent old_instance
  root="$(mktemp -d)"
  old_parent="${STVR_MONADO_PARENT_RUNTIME:-}"
  old_instance="$INSTANCE"
  STVR_MONADO_PARENT_RUNTIME="$root/runtime"
  INSTANCE='self-test'
  mkdir -p -- "$STVR_MONADO_PARENT_RUNTIME"
  configure_paths
  prepare_instance_dir
  marker_is_owned || die 'self-test: instance marker is not owned'
  [ "$(LC_ALL=C printf %s "$SOCKET" | wc -c | tr -d '[:space:]')" -le 107 ] || die 'self-test: socket limit failed'
  rm -rf -- "$root"
  INSTANCE="$old_instance"
  if [ -n "$old_parent" ]; then
    STVR_MONADO_PARENT_RUNTIME="$old_parent"
  else
    unset STVR_MONADO_PARENT_RUNTIME
  fi
  printf '%s: self-test passed\n' "$SCRIPT_NAME"
}

if [ "$COMMAND" = '--self-test' ]; then
  [ "$#" -eq 1 ] || die 'usage: --self-test takes no instance name'
  run_self_test
  exit 0
fi

[ "$#" -le 2 ] || { usage >&2; exit 1; }
valid_instance_name "$INSTANCE" || die "invalid instance name '$INSTANCE' (use 1-64 letters, digits, _ or -; start with alphanumeric)"
configure_timeouts
configure_paths

case "$COMMAND" in
  start)
    with_instance_lock start_instance
    ;;
  restart)
    with_instance_lock restart_instance
    ;;
  status)
    require_command ss
    require_command systemctl
    require_command timeout
    resolve_monado_service
    resolve_monado_manifest
    print_status
    ;;
  stop)
    require_command ss
    require_command systemctl
    require_command timeout
    with_instance_lock stop_managed_instance
    printf 'Managed Monado instance %s stopped.\n' "$INSTANCE"
    ;;
  *)
    usage >&2
    exit 1
    ;;
esac
