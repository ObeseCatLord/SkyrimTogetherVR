#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
max_age_days=7
min_free_gib=160
max_used_percent=93
trim=0
scheduled=0
local_artifacts=1
rebuildable_caches=0
temp_artifacts=0
local_build_output=0

if [[ ${STVR_BUILD_LOCK_HELD:-0} != 1 ]]; then
    build_lock_file="${XDG_RUNTIME_DIR:-/tmp}/skyrim-together-vr-build-active.lock"
    exec 8>"$build_lock_file"
    if ! flock -n 8; then
        echo "A Skyrim Together VR build is active; skipping cleanup."
        exit 0
    fi
fi

lock_file="${XDG_RUNTIME_DIR:-/tmp}/skyrim-together-vr-build-cleanup.lock"
exec 9>"$lock_file"
if ! flock -n 9; then
    echo "Another Skyrim Together VR build cleanup is already running; skipping."
    exit 0
fi

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
        --min-free-gib)
            min_free_gib=${2:?--min-free-gib requires a nonnegative integer}
            shift 2
            ;;
        --max-used-percent)
            max_used_percent=${2:?--max-used-percent requires an integer from 1 to 100}
            shift 2
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
        --temp-artifacts)
            temp_artifacts=1
            shift
            ;;
        --local-build-output)
            local_build_output=1
            shift
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

if [[ ! $max_age_days =~ ^[0-9]+$ || ! $min_free_gib =~ ^[0-9]+$ ||
      ! $max_used_percent =~ ^[0-9]+$ || $max_used_percent -lt 1 || $max_used_percent -gt 100 ]]; then
    echo "Cleanup retention and disk-pressure arguments are invalid." >&2
    exit 2
fi

read -r root_used_percent root_available_bytes < <(
    df -B1 --output=pcent,avail / | awk 'NR == 2 { gsub(/%/, "", $1); print $1, $2 }'
)
min_free_bytes=$((min_free_gib * 1024 * 1024 * 1024))
if ((scheduled && (root_used_percent >= max_used_percent || root_available_bytes < min_free_bytes))); then
    echo "Root filesystem is under pressure (${root_used_percent}% used); tightening generated-output retention."
    max_age_days=0
    temp_artifacts=1
    rebuildable_caches=1
    local_build_output=1
fi

if ((local_build_output)); then
    for build_path in "$repo_root/build/.objs" "$repo_root/build/linux"; do
        if [[ -e $build_path ]]; then
            echo "Removing reproducible local build output: $build_path"
            rm -rf -- "$build_path"
        fi
    done
    while IFS= read -r -d '' python_cache; do
        echo "Removing reproducible project Python cache: $python_cache"
        rm -rf -- "$python_cache"
    done < <(find "$repo_root" -xdev -type d -name __pycache__ -print0)
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

if ((temp_artifacts)); then
    temp_root=${TMPDIR:-/tmp}
    while IFS= read -r -d '' temp_path; do
        echo "Removing expired Skyrim Together temporary output: $temp_path"
        rm -rf -- "$temp_path"
    done < <(
        find "$temp_root" -xdev -mindepth 1 -maxdepth 1 -name 'stvr-*' \
            -mtime "+$max_age_days" -print0
    )
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
$ProgressPreference = "SilentlyContinue"
$repo = '__WINBOAT_REPO__'
$cutoff = (Get-Date).ToUniversalTime().AddDays(-__MAX_AGE_DAYS__)
$trim = __TRIM__ -eq 1
$buildRoot = Split-Path -Parent $repo
$resultRoot = "$repo-build-results"

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
$buildDirs = @(Get-ChildItem -LiteralPath $buildRoot -Directory -Filter 'SkyrimTogetherVR-build-*' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -ne $resultRoot })
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

$removedResults = 0
if (Test-Path -LiteralPath $resultRoot) {
    $resultDirs = @(Get-ChildItem -LiteralPath $resultRoot -Directory -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTimeUtc -Descending)
    for ($index = 1; $index -lt $resultDirs.Count; $index++) {
        $dir = $resultDirs[$index]
        if ($dir.LastWriteTimeUtc -gt $cutoff) { continue }
        Remove-Item -LiteralPath $dir.FullName -Recurse -Force
        $removedResults++
    }
}

git -C $repo worktree prune
if ($LASTEXITCODE -ne 0) { throw "Failed to prune WinBoat worktree metadata." }
"Removed WinBoat build directories: $removed"
"Removed expired WinBoat build result directories: $removedResults"
if ($trim -and $removed -gt 0) {
    try {
        Optimize-Volume -DriveLetter C -ReTrim -ErrorAction Stop | Out-Null
        "Requested WinBoat C: retrim."
    } catch {
        if ($_.Exception.Message -match 'optimization operation is currently in progress') {
            "WinBoat C: optimization is already in progress; retrim skipped."
        } else {
            throw
        }
    }
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
