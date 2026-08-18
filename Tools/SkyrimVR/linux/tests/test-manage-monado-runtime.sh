#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
MANAGER="$SCRIPT_DIR/../manage-monado-runtime.sh"
TMPDIR_LOCAL="$(mktemp -d)"
trap 'rm -rf -- "$TMPDIR_LOCAL"' EXIT

fake_bin="$TMPDIR_LOCAL/fake-bin"
state="$TMPDIR_LOCAL/state"
runtime="$TMPDIR_LOCAL/runtime"
socket="$runtime/monado_comp_ipc"
mkdir -p "$fake_bin" "$state" "$runtime"

cat > "$fake_bin/envision" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF

cat > "$fake_bin/systemd-run" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
state="${STVR_FAKE_STATE:?}"
socket="${STVR_MONADO_IPC_SOCKET:?}"
printf '%q ' "$@" >> "$state/systemd-run.log"
printf '\n' >> "$state/systemd-run.log"
# Envision forwards to its singleton and returns, so the transient unit is
# deliberately inactive while the singleton-owned monado-service is live.
/usr/bin/python3 - "$socket" <<'PY'
import os
import socket
import sys
try:
    os.unlink(sys.argv[1])
except FileNotFoundError:
    pass
sock = socket.socket(socket.AF_UNIX)
sock.bind(sys.argv[1])
sock.close()
PY
EOF

cat > "$fake_bin/systemctl" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
[ "${STVR_FAIL_SYSTEMCTL:-0}" != 1 ] || exit 99
case " $* " in
  *' show '*) printf 'not-found\n' ;;
  *' is-active '*) exit 3 ;;
  *' stop '*) exit 0 ;;
  *) exit 2 ;;
esac
EOF

cat > "$fake_bin/ss" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
socket="${STVR_MONADO_IPC_SOCKET:?}"
[ -S "$socket" ] || exit 0
printf 'u_str LISTEN 0 32 %s 0 * 0 users:(("monado-service",pid=1,fd=3))\n' "$socket"
# Keep producing data after the match. A listener parser that exits early will
# SIGPIPE this producer under pipefail and incorrectly report no listener.
for i in $(seq 1 4096); do
  printf 'u_str LISTEN 0 32 /tmp/unrelated-%s 0 * 0 users:(("other",pid=2,fd=4))\n' "$i"
done
EOF

cat > "$fake_bin/timeout" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
[ "${1:-}" = --foreground ] && shift
shift
exec "$@"
EOF

cat > "$fake_bin/python3" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
if [ "${1:-}" = - ]; then
  cat >/dev/null
  case "${STVR_FAKE_CANARY:-ready}" in
    compositor-error) printf 'compositor startup failed\n' ;;
    fail) printf 'xrGetSystem failed: -7\n' >&2; exit 1 ;;
    *) printf 'openxr-system=1 view-configurations=2\n' ;;
  esac
  exit 0
fi
exec /usr/bin/python3 "$@"
EOF
chmod 755 "$fake_bin"/*

common_env=(
  PATH="$fake_bin:$PATH"
  STVR_FAKE_STATE="$state"
  STVR_MONADO_IPC_SOCKET="$socket"
  STVR_MONADO_START_TIMEOUT=1
  STVR_MONADO_OPENXR_CANARY_TIMEOUT=1
)

# A forwarded Envision invocation exits its transient unit immediately, but a
# real singleton monado-service and OpenXR canary still make the start succeed.
env "${common_env[@]}" "$MANAGER" start simulated-qwerty-fixed >/dev/null
grep -Fq -- '--unit=stvr-monado-runtime ' "$state/systemd-run.log"
env "${common_env[@]}" "$MANAGER" status | grep -Fq 'Runtime: ready'

# check is an admission-only listener/OpenXR canary: it must not query or
# mutate systemd state.
before_check="$(wc -l < "$state/systemd-run.log")"
env "${common_env[@]}" STVR_FAIL_SYSTEMCTL=1 "$MANAGER" check | grep -Fq 'Runtime check: ready'
[ "$(wc -l < "$state/systemd-run.log")" = "$before_check" ] || {
  printf 'manager check mutated systemd runtime state\n' >&2
  exit 1
}

# A socket listener alone is insufficient: compositor startup errors reported
# by the canary must fail closed without unlinking a live, potentially
# singleton-owned listener or launching a replacement.
before="$(wc -l < "$state/systemd-run.log")"
if env "${common_env[@]}" STVR_FAKE_CANARY=compositor-error "$MANAGER" start >/dev/null 2>&1; then
  printf 'manager accepted a compositor-error OpenXR canary\n' >&2
  exit 1
fi
[ -S "$socket" ] || { printf 'manager removed a live listener after canary failure\n' >&2; exit 1; }
[ "$(wc -l < "$state/systemd-run.log")" = "$before" ] || {
  printf 'manager launched a replacement despite an existing live listener\n' >&2
  exit 1
}

if env "${common_env[@]}" STVR_FAKE_CANARY=fail "$MANAGER" status >/dev/null 2>&1; then
  printf 'manager accepted a failed OpenXR system enumeration\n' >&2
  exit 1
fi

printf 'Managed Monado runtime tests passed\n'
