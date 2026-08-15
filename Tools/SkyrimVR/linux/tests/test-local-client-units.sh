#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
MANAGER="$SCRIPT_DIR/../manage-local-clients.sh"
TMPDIR_LOCAL="$(mktemp -d)"
trap 'rm -rf -- "$TMPDIR_LOCAL"' EXIT

game="$TMPDIR_LOCAL/game"
compat="$TMPDIR_LOCAL/compat"
root="$TMPDIR_LOCAL/clients"
prefix="$TMPDIR_LOCAL/monado-prefix"
runtime="$TMPDIR_LOCAL/runtime"
fake_bin="$TMPDIR_LOCAL/fake-bin"
state="$TMPDIR_LOCAL/state"
mkdir -p "$game" "$compat/pfx" "$prefix/lib" "$prefix/share/openxr/1" "$fake_bin" "$state/units" "$runtime"
touch "$game/SkyrimVR.exe" "$game/SkyrimTogetherVR.exe" "$prefix/lib/libopenxr_monado.so" "$prefix/lib/libmonado.so"
cat > "$prefix/share/openxr/1/openxr_monado.json" <<'EOF'
{"runtime":{"library_path":"../../../lib/libopenxr_monado.so","MND_libmonado_path":"../../../lib/libmonado.so"}}
EOF

fake_launcher="$TMPDIR_LOCAL/fake-launcher"
printf '%s\n' '#!/usr/bin/env bash' 'exit 0' > "$fake_launcher"
chmod 755 "$fake_launcher"

fake_monado="$TMPDIR_LOCAL/fake-monado"
cat > "$fake_monado" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
command="$1"
name="$2"
runtime="${STVR_MONADO_PARENT_RUNTIME:?}/stvr-monado-instances/$name"
case "$command" in
  start)
    mkdir -p "$runtime"
    printf '%s\n' \
      'format=1' 'kind=stvr-monado-runtime' "instance=$name" "runtime_dir=$runtime" \
      "monado_prefix=${STVR_TEST_MONADO_PREFIX:?}" \
      "xr_runtime_json=${STVR_TEST_MONADO_PREFIX:?}/share/openxr/1/openxr_monado.json" \
      "monado_library_path=${STVR_TEST_MONADO_PREFIX:?}/lib" \
      'host_mounts=' 'wayland_display=' 'dbus_session_bus_address=unix:path=/tmp/bus' \
      'pulse_server=unix:/tmp/pulse' 'pipewire_remote=/tmp/pipewire' > "$runtime/.stvr-monado-runtime"
    ;;
  status|stop) ;;
  *) exit 2 ;;
esac
EOF
chmod 755 "$fake_monado"

cat > "$fake_bin/systemd-run" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
state="${STVR_TEST_CLIENT_STATE:?}"
printf '%q ' "$@" >> "$state/run.log"
printf '\n' >> "$state/run.log"
[ "${STVR_TEST_CLIENT_RUN_FAIL:-0}" != 1 ] || exit 1
unit=''
environment=()
for argument in "$@"; do
  case "$argument" in
    --unit=*) unit="${argument#--unit=}" ;;
    --setenv=*) environment+=("${argument#--setenv=}") ;;
  esac
done
unit="${unit%.service}.service"
dir="$state/units/$unit"
mkdir -p "$dir"
printf 'loaded\n' > "$dir/load"
printf 'active\n' > "$dir/active"
printf '%032x\n' "$(printf %s "$unit" | cksum | awk '{print $1}')" > "$dir/invocation"
printf '%s\n' "${environment[*]}" > "$dir/environment"
printf '0\n' > "$dir/mainpid"
EOF
chmod 755 "$fake_bin/systemd-run"

cat > "$fake_bin/systemctl" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
state="${STVR_TEST_CLIENT_STATE:?}"
command='' property='' quiet=0 unit=''
for argument in "$@"; do
  case "$argument" in
    show|stop|is-active) command="$argument" ;;
    --property=*) property="${argument#--property=}" ;;
    --quiet) quiet=1 ;;
    *.service) unit="$argument" ;;
  esac
done
dir="$state/units/$unit"
field() { [ -f "$dir/$1" ] && cat "$dir/$1" || printf '%s\n' "$2"; }
case "$command" in
  show)
    case "$property" in
      LoadState) field load not-found ;;
      Environment) field environment '' ;;
      InvocationID) field invocation '' ;;
      MainPID) field mainpid 0 ;;
      *) printf '\n' ;;
    esac
    ;;
  is-active)
    [ "$(field load not-found)" != not-found ] && [ "$(field active inactive)" = active ] || exit 3
    [ "$quiet" = 1 ] || printf 'active\n'
    ;;
  stop)
    printf '%s\n' "$unit" >> "$state/stops.log"
    printf 'not-found\n' > "$dir/load"
    printf 'inactive\n' > "$dir/active"
    ;;
  *) exit 2 ;;
esac
EOF
chmod 755 "$fake_bin/systemctl"

"$MANAGER" --root "$root" --base-game "$game" --base-compatdata "$compat" prepare unitclient >/dev/null
"$MANAGER" --root "$root" --base-game "$game" --base-compatdata "$compat" prepare failure >/dev/null
common_env=(
  PATH="$fake_bin:$PATH"
  STVR_TEST_CLIENT_STATE="$state"
  STVR_TEST_MONADO_PREFIX="$prefix"
  STVR_MONADO_PARENT_RUNTIME="$runtime"
  STVR_MONADO_MANAGER="$fake_monado"
  STVR_CLIENT_LAUNCHER="$fake_launcher"
  STVR_CLIENT_SYSTEMCTL_QUERY_TIMEOUT=2
  STVR_CLIENT_STOP_TIMEOUT=2
)

env "${common_env[@]}" "$MANAGER" --root "$root" launch unitclient 127.0.0.1:26099 >/dev/null
unit='stvr-local-client-unitclient.service'
grep -Fq -- '--property=KillMode=control-group' "$state/run.log"
grep -Fq -- '--property=SendSIGKILL=no' "$state/run.log"
grep -Fq 'STVR_LOCAL_CLIENT_MANAGER=1' "$state/units/$unit/environment"
grep -Fq 'STVR_MONADO_XR_RUNTIME_JSON=' "$state/units/$unit/environment"
grep -Fq 'GAMEID=umu-611670-unitclient' "$state/run.log" && {
  printf 'client manager set an invalid per-name UMU GAMEID\n' >&2
  exit 1
}
# MainPID is deliberately 0. Status proves ownership is cgroup based.
env "${common_env[@]}" "$MANAGER" --root "$root" status unitclient | grep -Fq 'owned cgroup; leader PID is not used'

sed -i 's/^invocation_id=.*/invocation_id=00000000000000000000000000000000/' "$root/unitclient/.stvr-local-client-unit"
if env "${common_env[@]}" "$MANAGER" --root "$root" stop unitclient >/dev/null 2>&1; then
  printf 'client stop accepted a mismatched InvocationID\n' >&2
  exit 1
fi
[ ! -f "$state/stops.log" ] || ! grep -Fqx "$unit" "$state/stops.log" || {
  printf 'client stop targeted an unowned transient unit\n' >&2
  exit 1
}

if env "${common_env[@]}" STVR_TEST_CLIENT_RUN_FAIL=1 "$MANAGER" --root "$root" launch failure >/dev/null 2>&1; then
  printf 'client launch accepted a failed transient unit start\n' >&2
  exit 1
fi
[ ! -e "$root/failure/.stvr-local-client-unit" ] || {
  printf 'failed client start left its owned unit marker behind\n' >&2
  exit 1
}

printf 'local client transient-unit tests passed\n'
