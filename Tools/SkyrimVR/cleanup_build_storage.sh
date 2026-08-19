#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
max_age_days=7
min_free_gib=160
max_used_percent=93
trim=0
scheduled=0
apply=0
local_artifacts=1
rebuildable_caches=0
temp_artifacts=0
local_build_output=0
keep_count=2

usage() {
    cat >&2 <<EOF
Usage: ${0##*/} [options]

Plans cleanup by default. Pass --apply to remove only the listed generated paths.

Options:
  --apply                       Perform the planned removals
  --dry-run                     Print the plan without removing anything (default)
  --max-age-days DAYS           Expire non-retained generated output after DAYS
  --trim                        Request a guest retrim after guest worktree removal (requires --apply)
  --min-free-gib GIB            Scheduled pressure threshold
  --max-used-percent PERCENT    Scheduled pressure threshold
  --scheduled                   Use pressure-aware scheduled retention
  --skip-local-artifacts        Leave repository-local package/evidence/handoff artifacts untouched
  --rebuildable-caches          Include explicitly rebuildable user caches
  --temp-artifacts              Include bounded /tmp/stvr-* paths
  --local-build-output          Include ignored local xmake/Python output
EOF
}

while (($#)); do
    case $1 in
        --apply)
            apply=1
            shift
            ;;
        --dry-run)
            apply=0
            shift
            ;;
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
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage
            exit 2
            ;;
    esac
done

if [[ ! $max_age_days =~ ^[0-9]+$ || ! $min_free_gib =~ ^[0-9]+$ ||
      ! $max_used_percent =~ ^[0-9]+$ || $max_used_percent -lt 1 || $max_used_percent -gt 100 ]]; then
    echo "Cleanup retention and disk-pressure arguments are invalid." >&2
    exit 2
fi

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

if ((apply)); then
    echo "Applying bounded Skyrim Together VR cleanup (retaining current accepted artifacts and one rollback)."
else
    echo "Dry run: no paths will be removed. Re-run with --apply to perform this plan."
fi

remove_path() {
    local label=$1 path=$2
    [[ -e $path || -L $path ]] || return 0
    if ((apply)); then
        echo "Removing ${label}: $path"
        rm -rf -- "$path"
    else
        echo "Would remove ${label}: $path"
    fi
}

is_expired() {
    local path=$1 age_minutes
    ((max_age_days == 0)) && return 0
    age_minutes=$((max_age_days * 24 * 60))
    find "$path" -maxdepth 0 -mmin "+$((age_minutes - 1))" -print -quit | grep -q .
}

sorted_paths() {
    local root=$1 type=$2 pattern=$3
    [[ -d $root ]] || return 0
    find "$root" -xdev -mindepth 1 -maxdepth 1 -type "$type" -name "$pattern" \
        -printf '%T@ %p\n' | LC_ALL=C sort -nr | while IFS= read -r line; do
            printf '%s\n' "${line#* }"
        done
}

declare -A retained_handoffs=()
declare -A retained_references=()

retain_handoff_references() {
    local handoff_root="$repo_root/artifacts/SkyrimTogetherVR/review-handoff"
    local handoff index=0 member name
    while IFS= read -r handoff; do
        ((index < keep_count)) || break
        retained_handoffs["$handoff"]=1
        retained_handoffs["$handoff.sha256.txt"]=1
        if command -v unzip >/dev/null 2>&1; then
            while IFS= read -r member; do
                case $member in
                    */build/*.zip|*/evidence/*.zip)
                        name=${member##*/}
                        retained_references["$name"]=1
                        ;;
                esac
            done < <(unzip -Z1 -- "$handoff" 2>/dev/null || true)
        fi
        index=$((index + 1))
    done < <(sorted_paths "$handoff_root" f 'SkyrimTogetherVR-local-agent-complete-handoff-*.zip')
}

cleanup_handoffs() {
    local handoff_root="$repo_root/artifacts/SkyrimTogetherVR/review-handoff"
    local handoff
    while IFS= read -r handoff; do
        [[ -n ${retained_handoffs["$handoff"]+x} ]] && continue
        if is_expired "$handoff"; then
            remove_path "expired local handoff" "$handoff"
            remove_path "expired local handoff checksum" "$handoff.sha256.txt"
        fi
    done < <(sorted_paths "$handoff_root" f 'SkyrimTogetherVR-local-agent-complete-handoff-*.zip')
}

cleanup_referenced_archives() {
    local label=$1 root=$2 pattern=$3 archive index name
    local -A newest=()
    index=0
    while IFS= read -r archive; do
        name=${archive##*/}
        if ((index < keep_count)); then
            newest["$name"]=1
        fi
        index=$((index + 1))
    done < <(sorted_paths "$root" f "$pattern")

    if ! command -v unzip >/dev/null 2>&1; then
        echo "unzip is unavailable; preserving $label archives because retained-handoff references cannot be read."
        return
    fi

    while IFS= read -r archive; do
        name=${archive##*/}
        if [[ -n ${newest["$name"]+x} || -n ${retained_references["$name"]+x} ]]; then
            continue
        fi
        if is_expired "$archive"; then
            remove_path "expired $label archive" "$archive"
        fi
    done < <(sorted_paths "$root" f "$pattern")
}

cleanup_expanded_build_evidence() {
    local root="$repo_root/artifacts/SkyrimTogetherVR/build-evidence"
    local directory archive_name
    [[ -d $root ]] || return 0
    while IFS= read -r directory; do
        archive_name="${directory##*/}.zip"
        [[ -f $root/$archive_name ]] || continue
        if [[ -n ${retained_references["$archive_name"]+x} ]]; then
            continue
        fi
        if is_expired "$directory"; then
            remove_path "expired expanded build evidence" "$directory"
        fi
    done < <(sorted_paths "$root" d 'SkyrimTogetherVR-build-evidence-*')
}

cleanup_retained_candidates() {
    local label=$1 root=$2 pattern=$3 candidate index=0
    [[ -d $root ]] || return 0
    while IFS= read -r candidate; do
        if ((index >= keep_count)) && is_expired "$candidate"; then
            remove_path "expired $label" "$candidate"
        fi
        index=$((index + 1))
    done < <(sorted_paths "$root" d "$pattern")
}

if ((local_build_output)); then
    for build_path in "$repo_root/build/.objs" "$repo_root/build/linux"; do
        remove_path "reproducible local build output" "$build_path"
    done
    while IFS= read -r -d '' python_cache; do
        remove_path "reproducible project Python cache" "$python_cache"
    done < <(find "$repo_root" -xdev -type d -name __pycache__ -print0)
fi

if ((local_artifacts)); then
    retain_handoff_references
    cleanup_handoffs
    cleanup_referenced_archives \
        "local gameplay package" "$repo_root/artifacts/SkyrimTogetherVR/packages" 'SkyrimTogetherVR-*.zip'
    cleanup_referenced_archives \
        "local build evidence" "$repo_root/artifacts/SkyrimTogetherVR/build-evidence" 'SkyrimTogetherVR-build-evidence-*.zip'
    cleanup_expanded_build_evidence
    cleanup_retained_candidates \
        "repository portable-runtime candidate" "$repo_root/artifacts/SkyrimTogetherVR" 'openvr-runtimes-candidate-*'
    cleanup_retained_candidates \
        "candidate result" "${XDG_STATE_HOME:-$HOME/.local/state}/skyrim-together-vr/candidates" 'stvr-winboat-candidate-*'
fi

if ((rebuildable_caches)); then
    if ((apply)); then
        rm -rf -- "$HOME/.cache/thumbnails" "$HOME/.cache/yay"
        if command -v python3 >/dev/null 2>&1; then
            python3 -m pip cache purge || true
        fi
    else
        echo "Would remove explicitly rebuildable user caches: $HOME/.cache/thumbnails, $HOME/.cache/yay, and pip cache."
    fi
fi

if ((temp_artifacts)); then
    temp_root=${TMPDIR:-/tmp}
    while IFS= read -r -d '' temp_path; do
        if is_expired "$temp_path"; then
            remove_path "expired Skyrim Together temporary output" "$temp_path"
        fi
    done < <(
        find "$temp_root" -xdev -mindepth 1 -maxdepth 1 -name 'stvr-*' -print0
    )
fi

if ((apply == 0)); then
    echo "Dry run: guest WinBoat cleanup is not contacted."
    df -h /
    exit 0
fi

winboat_powershell=${WINBOAT_POWERSHELL:-$HOME/.codex/skills/winboat-ssh/scripts/winboat-powershell}
winboat_scp=${WINBOAT_SCP:-$HOME/.codex/skills/winboat-ssh/scripts/winboat-scp}
winboat_repo=${STVR_WINBOAT_REPO:-'C:\Users\obesecatlord\Documents\Codex\SkyrimTogetherVR'}
if [[ ! -x $winboat_powershell ]]; then
    if ((scheduled)); then
        echo "WinBoat helper unavailable; skipping guest cleanup."
        exit 0
    fi
    echo "WinBoat PowerShell helper is not executable: $winboat_powershell" >&2
    exit 2
fi
if [[ ! -x $winboat_scp ]]; then
    if ((scheduled)); then
        echo "WinBoat helper unavailable; skipping guest cleanup."
        exit 0
    fi
    echo "WinBoat SCP helper is not executable: $winboat_scp" >&2
    exit 2
fi
if [[ $winboat_repo == *"'"* ]]; then
    echo "WinBoat paths containing a single quote are not supported." >&2
    exit 2
fi
winboat_windows_user=${WINBOAT_WINDOWS_USER:-obesecatlord}
if [[ ! $winboat_windows_user =~ ^[a-zA-Z0-9_.-]+$ ]]; then
    echo "WinBoat Windows user contains unsupported path characters." >&2
    exit 2
fi

read -r -d '' powershell_payload <<'POWERSHELL' || true
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
$repo = '__WINBOAT_REPO__'
$cutoff = (Get-Date).ToUniversalTime().AddDays(-__MAX_AGE_DAYS__)
$trim = __TRIM__ -eq 1
$keepCount = __KEEP_COUNT__
$buildRoot = Split-Path -Parent $repo
$repoLeaf = Split-Path -Leaf $repo
$worktreePatterns = @("$repoLeaf-build-*", "$repoLeaf-candidate-*", "$repoLeaf-bridge-preflight-*")
$resultRoots = @("$repo-build-results", "$repo-candidate-results")
$workflowProcessPattern = [regex]::Escape($repoLeaf) + '-(build|candidate|bridge-preflight)-'
$candidateCutoff = (Get-Date).ToUniversalTime().AddDays(-2)

function Test-WorkflowWorktree {
    param([string]$Path)
    foreach ($pattern in $worktreePatterns) {
        if ($Path -like "*$pattern") { return $true }
    }
    return $false
}

$active = @(Get-CimInstance Win32_Process | Where-Object {
    $_.ProcessId -ne $PID -and
    $_.CommandLine -match $workflowProcessPattern -and
    $_.Name -match '^(xmake|cl|link|caprica|python|py|cmd|powershell)(\.exe)?$'
})
if ($active.Count -ne 0) {
    throw "A SkyrimTogetherVR build or candidate process is active; refusing workflow-worktree cleanup."
}

$registered = @{}
$currentPath = $null
foreach ($line in @(git -C $repo worktree list --porcelain)) {
    if ($line -like 'worktree *') {
        $currentPath = $line.Substring(9)
        if (Test-WorkflowWorktree $currentPath) {
            $registered[$currentPath.Replace('/', '\').ToLowerInvariant()] = $currentPath
        }
    }
}
if ($LASTEXITCODE -ne 0) { throw "Could not list WinBoat worktrees." }

$removed = 0
$buildDirs = @(Get-ChildItem -LiteralPath $buildRoot -Directory -ErrorAction SilentlyContinue |
    Where-Object { Test-WorkflowWorktree $_.FullName })
foreach ($dir in $buildDirs) {
    $effectiveCutoff = $cutoff
    if ($dir.Name -like "$repoLeaf-candidate-*") { $effectiveCutoff = $candidateCutoff }
    if ($dir.LastWriteTimeUtc -gt $effectiveCutoff) { continue }
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
foreach ($resultRoot in $resultRoots) {
    if (Test-Path -LiteralPath $resultRoot) {
        $resultDirs = @(Get-ChildItem -LiteralPath $resultRoot -Directory -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTimeUtc -Descending)
        for ($index = $keepCount; $index -lt $resultDirs.Count; $index++) {
            $dir = $resultDirs[$index]
            if ($dir.LastWriteTimeUtc -gt $cutoff) { continue }
            Remove-Item -LiteralPath $dir.FullName -Recurse -Force
            $removedResults++
        }
    }
}

git -C $repo worktree prune
if ($LASTEXITCODE -ne 0) { throw "Failed to prune WinBoat worktree metadata." }
"Removed WinBoat workflow worktrees: $removed"
"Removed expired WinBoat workflow result directories: $removedResults"
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
powershell_payload=${powershell_payload//__KEEP_COUNT__/$keep_count}

payload_file=""
guest_payload=""
guest_cleanup_expression=""
guest_payload_cleanup_needed=0
cleanup_winboat_payload() {
    local status=$?
    trap - EXIT
    if ((guest_payload_cleanup_needed)) && [[ -n $guest_cleanup_expression && -x $winboat_powershell ]]; then
        "$winboat_powershell" "$guest_cleanup_expression" >/dev/null 2>&1 || true
    fi
    if [[ -n $payload_file ]]; then
        rm -f -- "$payload_file" || true
    fi
    exit "$status"
}
trap cleanup_winboat_payload EXIT

payload_file=$(mktemp "${TMPDIR:-/tmp}/stvr-winboat-cleanup-XXXXXX.ps1")
payload_basename=${payload_file##*/}
if [[ ! $payload_basename =~ ^stvr-winboat-cleanup-[a-zA-Z0-9]+\.ps1$ ]]; then
    echo "Generated WinBoat cleanup payload name is invalid." >&2
    exit 2
fi
guest_payload="C:/Users/${winboat_windows_user}/AppData/Local/Temp/${payload_basename}"
guest_cleanup_expression="\$p='$guest_payload'; if (Test-Path -LiteralPath \$p) { Remove-Item -LiteralPath \$p -Force -ErrorAction SilentlyContinue }"
guest_execution_expression="\$ErrorActionPreference='Stop'; \$p='$guest_payload'; try { & powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \$p; \$s=\$LASTEXITCODE; if (\$s -ne 0) { throw \"WinBoat cleanup script failed with exit code \$s.\" } } finally { if (Test-Path -LiteralPath \$p) { Remove-Item -LiteralPath \$p -Force -ErrorAction Stop } }"
printf '%s\n' "$powershell_payload" >"$payload_file"
guest_payload_cleanup_needed=1

if ! "$winboat_scp" to-guest "$payload_file" "$guest_payload"; then
    if ((scheduled)); then
        echo "WinBoat is unavailable or busy; scheduled guest cleanup skipped."
        exit 0
    fi
    echo "WinBoat guest cleanup payload transfer failed." >&2
    exit 1
fi

if ! "$winboat_powershell" "$guest_execution_expression"; then
    if ((scheduled)); then
        echo "WinBoat is unavailable or busy; scheduled guest cleanup skipped."
        exit 0
    fi
    echo "WinBoat guest cleanup failed." >&2
    exit 1
fi

guest_payload_cleanup_needed=0

df -h /
