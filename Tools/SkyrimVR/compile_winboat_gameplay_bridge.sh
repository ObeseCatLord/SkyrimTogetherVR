#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$repo_root"

build_lock_file="${XDG_RUNTIME_DIR:-/tmp}/skyrim-together-vr-build-active.lock"
exec 8>"$build_lock_file"
if ! flock -n 8; then
    echo "Another Skyrim Together VR build or cleanup is active; refusing to overlap it." >&2
    exit 2
fi
export STVR_BUILD_LOCK_HELD=1

payload_file=""
guest_payload=""
guest_payload_uploaded=0
winboat_powershell=""
cleanup_runtime() {
    local status=$?
    trap - EXIT
    if [[ $guest_payload_uploaded -eq 1 && -n $guest_payload && -x $winboat_powershell ]]; then
        "$winboat_powershell" \
            "Remove-Item -LiteralPath '$guest_payload' -Force -ErrorAction SilentlyContinue" \
            >/dev/null 2>&1 || true
    fi
    [[ -z $payload_file ]] || rm -f -- "$payload_file"
    "$repo_root/Tools/SkyrimVR/cleanup_build_storage.sh" \
        --scheduled --max-age-days 2 --skip-local-artifacts --temp-artifacts || true
    exit "$status"
}
trap cleanup_runtime EXIT

if [[ -n $(git status --porcelain=v1 --untracked-files=all) ]]; then
    echo "Refusing WinBoat compile preflight from a dirty Linux worktree." >&2
    exit 2
fi

revision=${1:-HEAD}
commit=$(git rev-parse --verify "${revision}^{commit}")
short_commit=${commit:0:8}
git fetch --force --tags github main
if ! git merge-base --is-ancestor "$commit" FETCH_HEAD; then
    echo "Commit $commit is not reachable from github/main. Push it before compiling." >&2
    exit 2
fi

winboat_powershell=${WINBOAT_POWERSHELL:-$HOME/.codex/skills/winboat-ssh/scripts/winboat-powershell}
winboat_ssh=${WINBOAT_SSH:-$HOME/.codex/skills/winboat-ssh/scripts/winboat-ssh}
winboat_scp=${WINBOAT_SCP:-$HOME/.codex/skills/winboat-ssh/scripts/winboat-scp}
for helper in "$winboat_powershell" "$winboat_ssh" "$winboat_scp"; do
    if [[ ! -x $helper ]]; then
        echo "WinBoat helper is not executable: $helper" >&2
        exit 2
    fi
done

"$repo_root/Tools/SkyrimVR/cleanup_build_storage.sh" \
    --max-age-days 0 --skip-local-artifacts --local-build-output --temp-artifacts

winboat_repo=${STVR_WINBOAT_REPO:-'C:\Users\obesecatlord\Documents\Codex\SkyrimTogetherVR'}
timestamp=$(date -u +%Y%m%d%H%M%SZ)
winboat_build="${winboat_repo}-bridge-preflight-${short_commit}-${timestamp}"

for value in "$winboat_repo" "$winboat_build"; do
    if [[ $value == *"'"* ]]; then
        echo "WinBoat paths containing a single quote are not supported." >&2
        exit 2
    fi
done

read -r -d '' powershell_payload <<'POWERSHELL' || true
$ErrorActionPreference = "Stop"
$repo = '__WINBOAT_REPO__'
$build = '__WINBOAT_BUILD__'
$commit = '__COMMIT__'
$worktreeCreated = $false

try {
    git -C $repo fetch --force --tags origin main
    if ($LASTEXITCODE -ne 0) { throw "Could not fetch origin/main in the WinBoat checkout." }
    git -C $repo cat-file -e "$commit`^{commit}"
    if ($LASTEXITCODE -ne 0) { throw "Commit $commit is unavailable in the WinBoat checkout." }
    if (Test-Path -LiteralPath $build) { throw "Fresh bridge preflight worktree already exists: $build" }

    git -C $repo worktree add --detach $build $commit
    if ($LASTEXITCODE -ne 0) { throw "Could not create detached WinBoat bridge preflight worktree." }
    $worktreeCreated = $true

    Set-Location $build
    git submodule sync --recursive
    if ($LASTEXITCODE -ne 0) { throw "Could not synchronize submodule URLs." }
    git submodule update --init --recursive --checkout
    if ($LASTEXITCODE -ne 0) { throw "Could not initialize pinned submodules." }

    $dirty = @(git status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0 -or $dirty.Count -ne 0) { throw "Fresh WinBoat bridge preflight worktree is unexpectedly dirty." }

    $env:Path = "C:\Users\obesecatlord\AppData\Local\Microsoft\WinGet\Links;$env:Path"
    & .\BuildSkyrimTogetherVR-Windows.ps1 `
        -Targets SkyrimTogetherVRGameplayBridge `
        -NoPackage `
        -SkipGameFiles `
        -SkipCompanionPanel `
        -SkipPapyrusCompile
    if ($LASTEXITCODE -ne 0) { throw "Gameplay bridge compile preflight failed with exit code $LASTEXITCODE." }

    "STVR_BRIDGE_PREFLIGHT_COMMIT=$commit"
    "STVR_BRIDGE_PREFLIGHT_RESULT=success"
} finally {
    Set-Location $repo
    if ($worktreeCreated) {
        git -C $repo worktree remove --force $build
        if ($LASTEXITCODE -ne 0) {
            Remove-Item -LiteralPath $build -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    git -C $repo worktree prune
}
POWERSHELL

powershell_payload=${powershell_payload//__WINBOAT_REPO__/$winboat_repo}
powershell_payload=${powershell_payload//__WINBOAT_BUILD__/$winboat_build}
powershell_payload=${powershell_payload//__COMMIT__/$commit}

payload_file=$(mktemp "${TMPDIR:-/tmp}/stvr-winboat-bridge-${short_commit}-${timestamp}-XXXXXX.ps1")
guest_payload="C:/Users/obesecatlord/AppData/Local/Temp/stvr-winboat-bridge-${short_commit}-${timestamp}.ps1"
printf '%s\n' "$powershell_payload" >"$payload_file"
"$winboat_scp" to-guest "$payload_file" "$guest_payload"
guest_payload_uploaded=1
"$winboat_ssh" powershell.exe -NoLogo -NoProfile -NonInteractive \
    -ExecutionPolicy Bypass -File "$guest_payload"
