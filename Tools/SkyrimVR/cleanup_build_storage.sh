#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
max_age_days=7
trim=0
scheduled=0
local_artifacts=1
rebuildable_caches=0

while (($#)); do
    case $1 in
        --max-age-days)
            max_age_days=${2:?--max-age-days requires a nonnegative integer}
            shift 2
            ;;
        --trim)
            trim=1
            shift
            ;;
        --scheduled)
            scheduled=1
            shift
            ;;
        --skip-local-artifacts)
            local_artifacts=0
            shift
            ;;
        --rebuildable-caches)
            rebuildable_caches=1
            shift
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

if [[ ! $max_age_days =~ ^[0-9]+$ ]]; then
    echo "--max-age-days must be a nonnegative integer." >&2
    exit 2
fi

if ((local_artifacts)); then
    artifact_root="$repo_root/artifacts/SkyrimTogetherVR"
    for artifact_dir in releasedbg release debug packages; do
        artifact_path="$artifact_root/$artifact_dir"
        if [[ -d $artifact_path ]] && { ((max_age_days == 0)) || find "$artifact_path" -maxdepth 0 -mtime "+$max_age_days" -print -quit | grep -q .; }; then
            echo "Removing expired local package output: $artifact_path"
            rm -rf -- "$artifact_path"
        fi
    done
fi

if ((rebuildable_caches)); then
    rm -rf -- "$HOME/.cache/thumbnails" "$HOME/.cache/yay"
    if command -v python3 >/dev/null 2>&1; then
        python3 -m pip cache purge || true
    fi
fi

winboat_powershell=${WINBOAT_POWERSHELL:-$HOME/.codex/skills/winboat-ssh/scripts/winboat-powershell}
winboat_repo=${STVR_WINBOAT_REPO:-'C:\Users\obesecatlord\Documents\Codex\SkyrimTogetherVR'}
if [[ ! -x $winboat_powershell ]]; then
    if ((scheduled)); then
        echo "WinBoat helper unavailable; skipping guest cleanup."
        exit 0
    fi
    echo "WinBoat PowerShell helper is not executable: $winboat_powershell" >&2
    exit 2
fi
if [[ $winboat_repo == *"'"* ]]; then
    echo "WinBoat paths containing a single quote are not supported." >&2
    exit 2
fi

read -r -d '' powershell_payload <<'POWERSHELL' || true
$ErrorActionPreference = "Stop"
$repo = '__WINBOAT_REPO__'
$cutoff = (Get-Date).ToUniversalTime().AddDays(-__MAX_AGE_DAYS__)
$trim = __TRIM__ -eq 1
$buildRoot = Split-Path -Parent $repo

$active = @(Get-CimInstance Win32_Process | Where-Object {
    $_.ProcessId -ne $PID -and
    $_.CommandLine -like '*SkyrimTogetherVR-build-*' -and
    $_.Name -match '^(xmake|cl|link|caprica|python|py|cmd|powershell)(\.exe)?$'
})
if ($active.Count -ne 0) {
    throw "A SkyrimTogetherVR build process is active; refusing build-worktree cleanup."
}

$registered = @{}
$currentPath = $null
foreach ($line in @(git -C $repo worktree list --porcelain)) {
    if ($line -like 'worktree *') {
        $currentPath = $line.Substring(9)
        if ($currentPath -like '*SkyrimTogetherVR-build-*') {
            $registered[$currentPath.Replace('/', '\').ToLowerInvariant()] = $currentPath
        }
    }
}
if ($LASTEXITCODE -ne 0) { throw "Could not list WinBoat worktrees." }

$removed = 0
$buildDirs = @(Get-ChildItem -LiteralPath $buildRoot -Directory -Filter 'SkyrimTogetherVR-build-*' -ErrorAction SilentlyContinue)
foreach ($dir in $buildDirs) {
    if ($dir.LastWriteTimeUtc -gt $cutoff) { continue }
    $key = $dir.FullName.ToLowerInvariant()
    if ($registered.ContainsKey($key)) {
        git -C $repo worktree remove --force $registered[$key]
        if ($LASTEXITCODE -ne 0) { throw "Failed to remove registered worktree $($dir.FullName)" }
    } else {
        Remove-Item -LiteralPath $dir.FullName -Recurse -Force
    }
    $removed++
}

git -C $repo worktree prune
if ($LASTEXITCODE -ne 0) { throw "Failed to prune WinBoat worktree metadata." }
"Removed WinBoat build directories: $removed"
if ($trim -and $removed -gt 0) {
    Optimize-Volume -DriveLetter C -ReTrim -Verbose
}
POWERSHELL

powershell_payload=${powershell_payload//__WINBOAT_REPO__/$winboat_repo}
powershell_payload=${powershell_payload//__MAX_AGE_DAYS__/$max_age_days}
powershell_payload=${powershell_payload//__TRIM__/$trim}

if ! "$winboat_powershell" "$powershell_payload"; then
    if ((scheduled)); then
        echo "WinBoat is unavailable or busy; scheduled guest cleanup skipped."
        exit 0
    fi
    exit 1
fi

df -h /
