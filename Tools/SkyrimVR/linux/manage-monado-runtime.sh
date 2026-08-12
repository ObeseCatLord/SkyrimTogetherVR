#!/usr/bin/env bash
set -euo pipefail

COMMAND="${1:-start}"
PROFILE="${2:-${STVR_MONADO_PROFILE:-simulated-qwerty-fixed}}"
UNIT="${STVR_MONADO_UNIT:-stvr-monado-runtime.service}"
SOCKET="${STVR_MONADO_IPC_SOCKET:-${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/monado_comp_ipc}"
START_TIMEOUT="${STVR_MONADO_START_TIMEOUT:-45}"
SYSTEMCTL_QUERY_TIMEOUT="${STVR_MONADO_SYSTEMCTL_QUERY_TIMEOUT:-5s}"

die() {
  printf 'manage-monado-runtime: %s\n' "$*" >&2
  exit 1
}

socket_listener() {
  [ -S "$SOCKET" ] || return 1
  ss -xlpH 2>/dev/null | awk -v socket="$SOCKET" '
    index($0, socket) && index($0, "LISTEN") && index($0, "monado-service") { print; exit }
  '
}

runtime_ready() {
  local listener
  listener="$(socket_listener)" || return 1
  [ -n "$listener" ]
}

remove_orphan_socket() {
  if [ -S "$SOCKET" ] && ! runtime_ready; then
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
  if ! runtime_ready; then
    pkill -x monado-tools 2>/dev/null || true
  fi
}

print_status() {
  local listener=""
  listener="$(socket_listener 2>/dev/null || true)"
  printf 'Unit: %s (%s)\n' "$UNIT" "$(systemctl --user is-active "$UNIT" 2>/dev/null || true)"
  printf 'Profile for managed starts: %s\n' "$PROFILE"
  printf 'IPC socket: %s\n' "$SOCKET"
  if [ -n "$listener" ]; then
    printf 'Runtime: ready\nListener: %s\n' "$listener"
    return 0
  fi
  if [ -S "$SOCKET" ]; then
    printf 'Runtime: invalid orphan socket (no monado-service listener)\n'
  else
    printf 'Runtime: stopped\n'
  fi
  return 1
}

start_runtime() {
  local deadline
  command -v envision >/dev/null 2>&1 || die "envision is not installed"
  command -v systemd-run >/dev/null 2>&1 || die "systemd-run is not installed"
  command -v ss >/dev/null 2>&1 || die "ss is not installed"

  if runtime_ready; then
    printf 'Monado runtime is already ready; leaving the live instance unchanged.\n'
    print_status
    return 0
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
    if ! systemctl --user is-active --quiet "$UNIT"; then
      journalctl --user -u "$UNIT" -n 80 --no-pager >&2 || true
      die "managed Envision unit exited before Monado became ready"
    fi
    (( SECONDS < deadline )) || {
      journalctl --user -u "$UNIT" -n 80 --no-pager >&2 || true
      die "timed out waiting for a live Monado IPC listener at $SOCKET"
    }
    sleep 0.25
  done

  sleep 2
  runtime_ready || die "Monado IPC listener disappeared during the stability check"
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
  stop)
    stop_managed_runtime
    printf 'Managed Monado runtime stopped.\n'
    ;;
  *)
    die "usage: $0 {start|restart|status|stop} [envision-profile-uuid]"
    ;;
esac
