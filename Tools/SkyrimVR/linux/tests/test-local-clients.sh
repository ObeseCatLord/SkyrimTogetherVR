#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
MANAGER="$SCRIPT_DIR/../manage-local-clients.sh"
TMPDIR_LOCAL="$(mktemp -d)"
trap 'rm -rf -- "$TMPDIR_LOCAL"' EXIT

base_game="$TMPDIR_LOCAL/base-game"
base_compat="$TMPDIR_LOCAL/base-compatdata"
client_root="$TMPDIR_LOCAL/clients"
mkdir -p "$base_game/Data" "$base_compat/pfx/drive_c/users/steamuser"
mkdir -p "$base_game/Data/SkyrimTogetherReborn"
printf 'game fixture\n' > "$base_game/SkyrimVR.exe"
printf 'launcher fixture\n' > "$base_game/SkyrimTogetherVR.exe"
printf 'stale control fixture\n' > "$base_game/Data/SkyrimTogetherReborn/SkyrimTogetherVR.status"
printf 'prefix fixture\n' > "$base_compat/pfx/system.reg"
printf 'hard link fixture\n' > "$base_game/hard-link-a"
ln "$base_game/hard-link-a" "$base_game/hard-link-b"

"$MANAGER" self-test
"$MANAGER" --root "$client_root" --base-game "$base_game" --base-compatdata "$base_compat" prepare alice
"$MANAGER" --root "$client_root" status alice | grep -Fq 'State: prepared'
"$MANAGER" --dry-run --root "$client_root" launch alice 127.0.0.1:26099 | grep -Fq 'Would launch client: alice'

[ "$(stat -c '%d:%i' "$base_game/SkyrimVR.exe")" != "$(stat -c '%d:%i' "$client_root/alice/game/SkyrimVR.exe")" ]
[ "$(stat -c '%d:%i' "$client_root/alice/game/hard-link-a")" = "$(stat -c '%d:%i' "$client_root/alice/game/hard-link-b")" ]
[ -d "$client_root/alice/compatdata/pfx" ]
[ -f "$client_root/alice/metadata" ]
[ -f "$client_root/alice/.stvr-local-client" ]
[ ! -e "$client_root/alice/game/Data/SkyrimTogetherReborn/SkyrimTogetherVR.status" ]
[ -f "$client_root/alice/stale-runtime-seed/SkyrimTogetherVR.status" ]
grep -Fq "runtime_dir=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/stvr-monado-instances/alice" "$client_root/alice/metadata"
if find "$client_root" -maxdepth 1 -name '.stvr-local-client-stage-*' -print -quit | grep -q .; then
  printf 'successful prepare left a staging directory behind\n' >&2
  exit 1
fi

if "$MANAGER" --root "$base_game" --base-game "$base_game" --base-compatdata "$base_compat" prepare recurse >/dev/null 2>&1; then
  printf 'prepare accepted a source that contains the staging root\n' >&2
  exit 1
fi

if "$MANAGER" --dry-run --root "$client_root" prepare '../escape' >/dev/null 2>&1; then
  printf 'unsafe instance name was accepted\n' >&2
  exit 1
fi

"$MANAGER" --root "$client_root" --base-game "$base_game" --base-compatdata "$base_compat" prepare concurrent >/dev/null &
first=$!
"$MANAGER" --root "$client_root" --base-game "$base_game" --base-compatdata "$base_compat" prepare concurrent >/dev/null 2>&1 &
second=$!
first_status=0
wait "$first" || first_status=$?
second_status=0
wait "$second" || second_status=$?
if { [ "$first_status" -eq 0 ] && [ "$second_status" -eq 0 ]; } ||
   { [ "$first_status" -ne 0 ] && [ "$second_status" -ne 0 ]; }; then
  printf 'per-name client prepare lock did not produce exactly one winner\n' >&2
  exit 1
fi
[ -d "$client_root/concurrent" ]
if find "$client_root" -maxdepth 1 -name '.stvr-local-client-stage-concurrent.*' -print -quit | grep -q .; then
  printf 'failed concurrent prepare left staging data behind\n' >&2
  exit 1
fi

printf 'local client manager tests passed\n'
