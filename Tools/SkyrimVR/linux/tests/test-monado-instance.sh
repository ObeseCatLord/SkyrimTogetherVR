#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
MANAGER="$SCRIPT_DIR/../manage-monado-instance.sh"
TMPDIR_LOCAL="$(mktemp -d)"
trap 'rm -rf -- "$TMPDIR_LOCAL"' EXIT

fake_bin="$TMPDIR_LOCAL/fake-bin"
fake_state="$TMPDIR_LOCAL/fake-state"
runtime="$TMPDIR_LOCAL/runtime"
prefix="$TMPDIR_LOCAL/monado-prefix"
mkdir -p "$fake_bin" "$fake_state/units" "$runtime" "$prefix/bin" "$prefix/lib" "$prefix/share/openxr/1"
touch "$prefix/lib/libopenxr_monado.so" "$prefix/lib/libmonado.so"
printf '%s\n' '#!/usr/bin/env bash' 'exit 0' > "$prefix/bin/monado-service"
chmod 755 "$prefix/bin/monado-service"
cat > "$prefix/share/openxr/1/openxr_monado.json" <<'EOF'
{"file_format_version":"1.0.0","runtime":{"library_path":"../../../lib/libopenxr_monado.so","MND_libmonado_path":"../../../lib/libmonado.so"}}
EOF

cat > "$fake_bin/systemd-run" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
state="${STVR_FAKE_SYSTEMD_STATE:?}"
unit=''
runtime=''
environment=()
printf '%q ' "$@" >> "$state/systemd-run.log"
printf '\n' >> "$state/systemd-run.log"
for argument in "$@"; do
  case "$argument" in
    --unit=*) unit="${argument#--unit=}" ;;
    --setenv=*)
      value="${argument#--setenv=}"
      environment+=("$value")
      [ "${value%%=*}" = XDG_RUNTIME_DIR ] && runtime="${value#XDG_RUNTIME_DIR=}"
      ;;
  esac
done
[ -n "$unit" ] || exit 2
unit="${unit%.service}.service"
directory="$state/units/$unit"
mkdir -p "$directory"
printf '%s\n' loaded > "$directory/load"
printf '%s\n' active > "$directory/active"
printf '%032x\n' "$(printf %s "$unit" | cksum | awk '{print $1}')" > "$directory/invocation"
printf '%s\n' 0 > "$directory/mainpid"
printf '%s\n' "$runtime" > "$directory/runtime"
printf '%s\n' "${environment[*]}" > "$directory/environment"
if [ "${STVR_FAKE_SYSTEMD_RUN_DELAY:-0}" != 0 ]; then sleep "$STVR_FAKE_SYSTEMD_RUN_DELAY"; fi
if [ "${STVR_FAKE_NO_LISTENER:-0}" != 1 ]; then
  python3 - "$runtime/monado_comp_ipc" <<'PY'
import os
import socket
import sys
path = sys.argv[1]
os.makedirs(os.path.dirname(path), exist_ok=True)
try:
    os.unlink(path)
except FileNotFoundError:
    pass
sock = socket.socket(socket.AF_UNIX)
sock.bind(path)
sock.close()
PY
  printf '%s\n' 1 > "$directory/listener"
else
  printf '%s\n' 0 > "$directory/listener"
fi
EOF
chmod 755 "$fake_bin/systemd-run"

cat > "$fake_bin/systemctl" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
state="${STVR_FAKE_SYSTEMD_STATE:?}"
command=''
property=''
quiet=0
unit=''
for argument in "$@"; do
  case "$argument" in
    show|stop|is-active) command="$argument" ;;
    --property=*) property="${argument#--property=}" ;;
    --quiet) quiet=1 ;;
    *.service) unit="$argument" ;;
  esac
done
[ -n "$unit" ] || exit 2
directory="$state/units/$unit"
field() { [ -f "$directory/$1" ] && cat "$directory/$1" || printf '%s\n' "$2"; }
case "$command" in
  show)
    case "$property" in
      LoadState) field load not-found ;;
      ActiveState) field active inactive ;;
      Environment) field environment '' ;;
      InvocationID) field invocation '' ;;
      MainPID) field mainpid 0 ;;
      ControlGroup) printf '/user.slice/test.scope/%s\n' "$unit" ;;
      *) printf '\n' ;;
    esac
    ;;
  is-active)
    [ "$(field active inactive)" = active ] && [ "$(field load not-found)" != not-found ] || exit 3
    [ "$quiet" = 1 ] || printf 'active\n'
    ;;
  stop)
    printf '%s\n' "$unit" >> "$state/stops.log"
    runtime="$(field runtime '')"
    [ -n "$runtime" ] && unlink -- "$runtime/monado_comp_ipc" 2>/dev/null || true
    printf '%s\n' not-found > "$directory/load"
    printf '%s\n' inactive > "$directory/active"
    ;;
  *) exit 2 ;;
esac
EOF
chmod 755 "$fake_bin/systemctl"

cat > "$fake_bin/ss" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
state="${STVR_FAKE_SYSTEMD_STATE:?}"
for directory in "$state"/units/*.service; do
  [ -d "$directory" ] || continue
  [ "$(cat "$directory/load" 2>/dev/null || true)" = loaded ] || continue
  [ "$(cat "$directory/active" 2>/dev/null || true)" = active ] || continue
  [ "$(cat "$directory/listener" 2>/dev/null || true)" = 1 ] || continue
  runtime="$(cat "$directory/runtime")"
  socket="$runtime/monado_comp_ipc"
  [ -S "$socket" ] || continue
  # Deliberately not named monado-service: readiness is an exact endpoint test.
  printf 'u_str LISTEN 0 32 %s 0 * 0 users:(("runtime-wrapper",pid=1,fd=3))\n' "$socket"
done
EOF
chmod 755 "$fake_bin/ss"

common_env=(
  PATH="$fake_bin:$PATH"
  STVR_FAKE_SYSTEMD_STATE="$fake_state"
  STVR_MONADO_PARENT_RUNTIME="$runtime"
  STVR_MONADO_SERVICE="$prefix/bin/monado-service"
  STVR_MONADO_START_TIMEOUT=2
  STVR_MONADO_STABILITY_SECONDS=1
  STVR_MONADO_STOP_TIMEOUT=2
  STVR_MONADO_SYSTEMCTL_QUERY_TIMEOUT=2
  WAYLAND_DISPLAY=wayland-test
  DBUS_SESSION_BUS_ADDRESS=unix:path=bus
  PULSE_SERVER=unix:pulse/native
  PIPEWIRE_REMOTE=pipewire-test
)

if env "${common_env[@]}" STVR_MONADO_START_TIMEOUT=301 "$MANAGER" status bounds >/dev/null 2>&1; then
  printf 'manager accepted an unbounded start timeout\n' >&2
  exit 1
fi
long_parent="$TMPDIR_LOCAL/$(printf 'socket-path-%.0s' {1..12})"
mkdir -p "$long_parent"
if env "${common_env[@]}" STVR_MONADO_PARENT_RUNTIME="$long_parent" "$MANAGER" status short >/dev/null 2>&1; then
  printf 'manager accepted an overlong AF_UNIX socket path\n' >&2
  exit 1
fi

env "${common_env[@]}" "$MANAGER" start alpha >/dev/null
alpha_unit='stvr-monado-instance-alpha.service'
alpha_dir="$fake_state/units/$alpha_unit"
grep -Fq -- '--property=KillMode=control-group' "$fake_state/systemd-run.log"
grep -Fq -- '--property=SendSIGKILL=no' "$fake_state/systemd-run.log"
grep -Fq 'XRT_NO_STDIN=1' "$alpha_dir/environment"
grep -Fq "WAYLAND_DISPLAY=$runtime/wayland-test" "$alpha_dir/environment" || {
  printf 'relative Wayland endpoint was not preserved against the parent runtime\n' >&2
  exit 1
}
grep -Fq "DBUS_SESSION_BUS_ADDRESS=unix:path=$runtime/bus" "$alpha_dir/environment"
grep -Fq "PULSE_SERVER=unix:$runtime/pulse/native" "$alpha_dir/environment"
grep -Fq "PIPEWIRE_REMOTE=$runtime/pipewire-test" "$alpha_dir/environment"
# The fake reports MainPID=0 while its cgroup remains active; status must still
# be ready because ownership is marker/environment/InvocationID based.
env "${common_env[@]}" "$MANAGER" status alpha | grep -Fq 'Runtime: ready'

STVR_FAKE_SYSTEMD_RUN_DELAY=1 env "${common_env[@]}" "$MANAGER" start concurrent >/dev/null &
first=$!
sleep 0.1
STVR_FAKE_SYSTEMD_RUN_DELAY=1 env "${common_env[@]}" "$MANAGER" start concurrent >/dev/null &
second=$!
wait "$first"
wait "$second"
[ "$(grep -c -- '--unit=stvr-monado-instance-concurrent ' "$fake_state/systemd-run.log")" -eq 1 ] || {
  printf 'per-name Monado lock allowed duplicate starts\n' >&2
  exit 1
}

alpha_invocation="$runtime/stvr-monado-instances/alpha/.stvr-monado-unit"
sed -i 's/^invocation_id=.*/invocation_id=00000000000000000000000000000000/' "$alpha_invocation"
if env "${common_env[@]}" "$MANAGER" stop alpha >/dev/null 2>&1; then
  printf 'manager stopped a unit after InvocationID ownership changed\n' >&2
  exit 1
fi
[ ! -f "$fake_state/stops.log" ] || ! grep -Fqx "$alpha_unit" "$fake_state/stops.log" || {
  printf 'manager sent stop to a mismatched unit\n' >&2
  exit 1
}

env "${common_env[@]}" "$MANAGER" start orphan >/dev/null
orphan_unit='stvr-monado-instance-orphan.service'
orphan_socket="$runtime/stvr-monado-instances/orphan/monado_comp_ipc"
printf '%s\n' not-found > "$fake_state/units/$orphan_unit/load"
printf '%s\n' inactive > "$fake_state/units/$orphan_unit/active"
env "${common_env[@]}" "$MANAGER" stop orphan >/dev/null
[ ! -e "$orphan_socket" ] || { printf 'owned orphan socket was not cleaned\n' >&2; exit 1; }

symlink_dir="$runtime/stvr-monado-instances/symlink"
mkdir -p "$symlink_dir" "$TMPDIR_LOCAL/outside"
printf '%s\n' 'format=1' 'kind=stvr-monado-instance' 'instance=symlink' \
  'unit=stvr-monado-instance-symlink.service' "runtime_dir=$symlink_dir" > "$symlink_dir/.stvr-monado-instance"
python3 - "$TMPDIR_LOCAL/outside/monado_comp_ipc" <<'PY'
import socket
import sys
sock = socket.socket(socket.AF_UNIX)
sock.bind(sys.argv[1])
sock.close()
PY
ln -s "$TMPDIR_LOCAL/outside/monado_comp_ipc" "$symlink_dir/monado_comp_ipc"
if env "${common_env[@]}" "$MANAGER" status symlink >/dev/null 2>&1; then
  printf 'manager accepted a symlink IPC socket\n' >&2
  exit 1
fi
[ -L "$symlink_dir/monado_comp_ipc" ] && [ -S "$TMPDIR_LOCAL/outside/monado_comp_ipc" ] || {
  printf 'manager altered an unowned symlink socket target\n' >&2
  exit 1
}

if STVR_FAKE_NO_LISTENER=1 env "${common_env[@]}" "$MANAGER" start timeout >/dev/null 2>&1; then
  printf 'manager accepted a unit with no live exact listener\n' >&2
  exit 1
fi
timeout_unit='stvr-monado-instance-timeout.service'
[ "$(cat "$fake_state/units/$timeout_unit/load")" = not-found ] || {
  printf 'failed start did not clean its owned transient unit\n' >&2
  exit 1
}
[ ! -e "$runtime/stvr-monado-instances/timeout/monado_comp_ipc" ] || {
  printf 'failed start did not clean its owned orphan socket\n' >&2
  exit 1
}

printf 'Monado instance manager tests passed\n'
