#!/usr/bin/env bash
set -euo pipefail

COMMAND="${1:-start}"
PROFILE="${2:-${STVR_MONADO_PROFILE:-simulated-qwerty-fixed}}"
UNIT="${STVR_MONADO_UNIT:-stvr-monado-runtime.service}"
SOCKET="${STVR_MONADO_IPC_SOCKET:-${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/monado_comp_ipc}"
START_TIMEOUT="${STVR_MONADO_START_TIMEOUT:-45}"
SYSTEMCTL_QUERY_TIMEOUT="${STVR_MONADO_SYSTEMCTL_QUERY_TIMEOUT:-5s}"
OPENXR_CANARY_TIMEOUT="${STVR_MONADO_OPENXR_CANARY_TIMEOUT:-8}"
OPENXR_CANARY_FAILURE=""

die() {
  printf 'manage-monado-runtime: %s\n' "$*" >&2
  exit 1
}

socket_listener() {
  [ -S "$SOCKET" ] || return 1
  ss -xlpH 2>/dev/null | awk -v socket="$SOCKET" '
    !found && index($0, socket) && index($0, "LISTEN") && index($0, "monado-service") {
      print
      found = 1
    }
  '
}

openxr_canary() {
  # Do not create a session: xrGetSystem and view-config enumeration prove that
  # the loader can reach the compositor and enumerate its HMD without launching
  # a client such as Skyrim.
  local output status
  status=0
  output="$(XDG_RUNTIME_DIR="$(dirname -- "$SOCKET")" \
    timeout --foreground "$OPENXR_CANARY_TIMEOUT" python3 - <<'PY' 2>&1
import ctypes
import sys

XR_SUCCESS = 0
XR_TYPE_INSTANCE_CREATE_INFO = 3
XR_TYPE_SYSTEM_GET_INFO = 4
XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY = 1
XR_CURRENT_API_VERSION = 1 << 48  # XR_MAKE_VERSION(1, 0, 0)

class XrApplicationInfo(ctypes.Structure):
    _fields_ = [
        ("applicationName", ctypes.c_char * 128),
        ("applicationVersion", ctypes.c_uint32),
        ("engineName", ctypes.c_char * 128),
        ("engineVersion", ctypes.c_uint32),
        ("apiVersion", ctypes.c_uint64),
    ]

class XrInstanceCreateInfo(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_int32),
        ("next", ctypes.c_void_p),
        ("createFlags", ctypes.c_uint64),
        ("applicationInfo", XrApplicationInfo),
        ("enabledApiLayerCount", ctypes.c_uint32),
        ("enabledApiLayerNames", ctypes.POINTER(ctypes.c_char_p)),
        ("enabledExtensionCount", ctypes.c_uint32),
        ("enabledExtensionNames", ctypes.POINTER(ctypes.c_char_p)),
    ]

class XrSystemGetInfo(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_int32),
        ("next", ctypes.c_void_p),
        ("formFactor", ctypes.c_int32),
    ]

try:
    xr = ctypes.CDLL("libopenxr_loader.so.1")
except OSError as error:
    print("OpenXR loader unavailable: %s" % error, file=sys.stderr)
    sys.exit(1)

xr.xrCreateInstance.argtypes = [ctypes.POINTER(XrInstanceCreateInfo), ctypes.POINTER(ctypes.c_uint64)]
xr.xrCreateInstance.restype = ctypes.c_int32
xr.xrDestroyInstance.argtypes = [ctypes.c_uint64]
xr.xrDestroyInstance.restype = ctypes.c_int32
xr.xrGetSystem.argtypes = [ctypes.c_uint64, ctypes.POINTER(XrSystemGetInfo), ctypes.POINTER(ctypes.c_uint64)]
xr.xrGetSystem.restype = ctypes.c_int32
xr.xrEnumerateViewConfigurations.argtypes = [
    ctypes.c_uint64, ctypes.c_uint64, ctypes.c_uint32,
    ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_int32),
]
xr.xrEnumerateViewConfigurations.restype = ctypes.c_int32

create_info = XrInstanceCreateInfo()
create_info.type = XR_TYPE_INSTANCE_CREATE_INFO
create_info.applicationInfo.applicationName = b"stvr-monado-canary"
create_info.applicationInfo.applicationVersion = 1
create_info.applicationInfo.engineName = b"SkyrimTogetherVR"
create_info.applicationInfo.engineVersion = 1
create_info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION
instance = ctypes.c_uint64()
result = xr.xrCreateInstance(ctypes.byref(create_info), ctypes.byref(instance))
if result != XR_SUCCESS:
    print("xrCreateInstance failed: %d" % result, file=sys.stderr)
    sys.exit(1)

try:
    system_info = XrSystemGetInfo()
    system_info.type = XR_TYPE_SYSTEM_GET_INFO
    system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY
    system = ctypes.c_uint64()
    result = xr.xrGetSystem(instance, ctypes.byref(system_info), ctypes.byref(system))
    if result != XR_SUCCESS or system.value == 0:
        print("xrGetSystem failed: %d" % result, file=sys.stderr)
        sys.exit(1)

    count = ctypes.c_uint32()
    result = xr.xrEnumerateViewConfigurations(instance, system, 0, ctypes.byref(count), None)
    if result != XR_SUCCESS or count.value == 0:
        print("xrEnumerateViewConfigurations failed: %d (count=%d)" % (result, count.value), file=sys.stderr)
        sys.exit(1)
    configurations = (ctypes.c_int32 * count.value)()
    result = xr.xrEnumerateViewConfigurations(instance, system, count, ctypes.byref(count), configurations)
    if result != XR_SUCCESS or count.value == 0:
        print("OpenXR system has no view configurations: %d" % result, file=sys.stderr)
        sys.exit(1)
    print("openxr-system=%d view-configurations=%d" % (system.value, count.value))
finally:
    xr.xrDestroyInstance(instance)
PY
  )" || status=$?
  if [ "$status" -ne 0 ]; then
    OPENXR_CANARY_FAILURE="$output"
    return 1
  fi
  if grep -Eqi '(compositor|ipc).*(error|fail|unavailable|not created|not running)|(error|fail|unavailable|not created|not running).*(compositor|ipc)' <<<"$output"; then
    OPENXR_CANARY_FAILURE="$output"
    return 1
  fi
  OPENXR_CANARY_FAILURE=""
  printf '%s\n' "$output"
}

runtime_ready() {
  local listener canary
  listener="$(socket_listener)" || return 1
  [ -n "$listener" ] || return 1
  canary="$(openxr_canary)" || return 1
  [ -n "$canary" ]
}

remove_orphan_socket() {
  # A live listener may belong to Envision's singleton, which intentionally
  # outlives the transient launcher unit. Never unlink it just because its
  # OpenXR canary is presently failing.
  if [ -S "$SOCKET" ] && ! socket_listener >/dev/null; then
    unlink "$SOCKET"
    printf 'Removed orphan Monado IPC socket: %s\n' "$SOCKET"
  fi
}

wait_for_unit_removal() {
  local deadline=$((SECONDS + 10))
  local load_state
  while :; do
    load_state="$(unit_load_state)" || die "failed to query managed unit load state: $UNIT"
    [ "$load_state" = not-found ] && return
    (( SECONDS < deadline )) || die "timed out waiting for transient unit removal: $UNIT"
    sleep 0.2
  done
}

unit_load_state() {
  local load_state
  load_state="$(timeout --foreground "$SYSTEMCTL_QUERY_TIMEOUT" \
    systemctl --user show --property=LoadState --value "$UNIT" 2>/dev/null)" || return 1
  [ -n "$load_state" ] || return 1
  printf '%s\n' "$load_state"
}

stop_managed_runtime() {
  local load_state
  load_state="$(unit_load_state)" || die "failed to query managed unit load state: $UNIT"
  if [ "$load_state" != not-found ]; then
    systemctl --user stop "$UNIT"
    wait_for_unit_removal
  fi
  remove_orphan_socket
  # Do not guess at a PID or kill a process by name: Envision may own an
  # unrelated singleton outside this transient unit.
}

print_status() {
  local listener=""
  listener="$(socket_listener 2>/dev/null || true)"
  printf 'Unit: %s (%s)\n' "$UNIT" "$(systemctl --user is-active "$UNIT" 2>/dev/null || true)"
  printf 'Profile for managed starts: %s\n' "$PROFILE"
  printf 'IPC socket: %s\n' "$SOCKET"
  if [ -n "$listener" ]; then
    if runtime_ready; then
      printf 'Runtime: ready\nListener: %s\n' "$listener"
      return 0
    fi
    printf 'Runtime: listener present but OpenXR canary failed\n'
    [ -z "$OPENXR_CANARY_FAILURE" ] || printf 'Canary: %s\n' "$OPENXR_CANARY_FAILURE"
    return 1
  fi
  if [ -S "$SOCKET" ]; then
    printf 'Runtime: invalid orphan socket (no monado-service listener)\n'
  else
    printf 'Runtime: stopped\n'
  fi
  return 1
}

check_runtime() {
  command -v ss >/dev/null 2>&1 || die "ss is not installed"
  command -v timeout >/dev/null 2>&1 || die "timeout is not installed"
  command -v python3 >/dev/null 2>&1 || die "python3 is not installed for the OpenXR readiness canary"
  if runtime_ready; then
    printf 'Runtime check: ready\n'
    return 0
  fi
  if [ -S "$SOCKET" ]; then
    die "Monado runtime listener or OpenXR canary is not ready at $SOCKET${OPENXR_CANARY_FAILURE:+: $OPENXR_CANARY_FAILURE}"
  fi
  die "Monado runtime listener is not ready at $SOCKET"
}

start_runtime() {
  local deadline
  command -v envision >/dev/null 2>&1 || die "envision is not installed"
  command -v systemd-run >/dev/null 2>&1 || die "systemd-run is not installed"
  command -v ss >/dev/null 2>&1 || die "ss is not installed"
  command -v timeout >/dev/null 2>&1 || die "timeout is not installed"
  command -v python3 >/dev/null 2>&1 || die "python3 is not installed for the OpenXR readiness canary"

  if runtime_ready; then
    printf 'Monado runtime is already ready; leaving the live instance unchanged.\n'
    print_status
    return 0
  fi

  if socket_listener >/dev/null; then
    die "a live Monado listener exists at $SOCKET but the OpenXR canary failed; refusing to replace it"
  fi

  stop_managed_runtime
  remove_orphan_socket

  systemd-run --user \
    --unit="${UNIT%.service}" \
    --property=Type=simple \
    --collect \
    /usr/bin/envision --profile "$PROFILE" --start >/dev/null

  deadline=$((SECONDS + START_TIMEOUT))
  while ! runtime_ready; do
    (( SECONDS < deadline )) || {
      journalctl --user -u "$UNIT" -n 80 --no-pager >&2 || true
      [ -z "$OPENXR_CANARY_FAILURE" ] || printf 'OpenXR canary failure: %s\n' "$OPENXR_CANARY_FAILURE" >&2
      die "timed out waiting for a live Monado OpenXR runtime at $SOCKET"
    }
    sleep 0.25
  done

  sleep 2
  runtime_ready || die "Monado OpenXR runtime failed during the stability check"
  print_status
}

case "$COMMAND" in
  start)
    start_runtime
    ;;
  restart)
    stop_managed_runtime
    start_runtime
    ;;
  status)
    print_status
    ;;
  check)
    check_runtime
    ;;
  stop)
    stop_managed_runtime
    printf 'Managed Monado runtime stopped.\n'
    ;;
  *)
    die "usage: $0 {start|restart|status|check|stop} [envision-profile-uuid]"
    ;;
esac
