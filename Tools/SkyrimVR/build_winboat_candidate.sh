#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$repo_root"

usage() {
    printf 'Usage: %s\n' "${0##*/}" >&2
}

if (($# > 0)); then
    case $1 in
        --help|-h)
            usage
            exit 0
            ;;
        *)
            usage
            exit 2
            ;;
    esac
fi

build_lock_file="${XDG_RUNTIME_DIR:-/tmp}/skyrim-together-vr-build-active.lock"
exec 8>"$build_lock_file"
if ! flock -n 8; then
    echo "Another Skyrim Together VR build or cleanup is active; refusing to overlap it." >&2
    exit 2
fi
export STVR_BUILD_LOCK_HELD=1

patch_file=""
patch_verify_file=""
payload_file=""
guest_report_file=""
guest_patch=""
guest_payload=""
winboat_powershell=""
winboat_build=""
winboat_repo=""
base_commit="NOT_RESOLVED"
candidate_ephemeral_revision="NOT_CREATED"
candidate_build_success=false

cleanup_after_build() {
    "$repo_root/Tools/SkyrimVR/cleanup_build_storage.sh" \
        --scheduled --max-age-days 2 --skip-local-artifacts --temp-artifacts || true
}

cleanup_guest_candidate() {
    if [[ -z $winboat_powershell || ! -x $winboat_powershell ]]; then
        return
    fi
    if [[ -z $guest_patch && -z $guest_payload && -z $winboat_build ]]; then
        return
    fi

    local guest_cleanup
    read -r -d '' guest_cleanup <<'POWERSHELL' || true
$ErrorActionPreference = "Continue"
$repo = '__WINBOAT_REPO__'
$build = '__WINBOAT_BUILD__'
$patch = '__GUEST_PATCH__'
$payload = '__GUEST_PAYLOAD__'

function Test-CandidateProcessActive {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) { return $false }
    $leaf = Split-Path -Leaf $Path
    $escapedLeaf = [regex]::Escape($leaf)
    return @(
        Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
            $_.ProcessId -ne $PID -and
            -not [string]::IsNullOrWhiteSpace($_.CommandLine) -and
            $_.CommandLine -match $escapedLeaf
        }
    ).Count -ne 0
}

Remove-Item -LiteralPath $patch -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $payload -Force -ErrorAction SilentlyContinue

if (-not [string]::IsNullOrWhiteSpace($build) -and (Test-Path -LiteralPath $build)) {
    if (Test-CandidateProcessActive $build) {
        Write-Warning "Candidate worktree still has an active process; leaving it for bounded stale cleanup: $build"
    } else {
        git -C $repo worktree remove --force $build 2>$null
        if ($LASTEXITCODE -ne 0 -and (Test-Path -LiteralPath $build)) {
            Remove-Item -LiteralPath $build -Recurse -Force -ErrorAction SilentlyContinue
        }
        git -C $repo worktree prune 2>$null
    }
}
POWERSHELL

    guest_cleanup=${guest_cleanup//__WINBOAT_REPO__/$winboat_repo}
    guest_cleanup=${guest_cleanup//__WINBOAT_BUILD__/$winboat_build}
    guest_cleanup=${guest_cleanup//__GUEST_PATCH__/$guest_patch}
    guest_cleanup=${guest_cleanup//__GUEST_PAYLOAD__/$guest_payload}
    "$winboat_powershell" "$guest_cleanup" >/dev/null 2>&1 || true
}

cleanup_runtime() {
    local status=$?
    trap - EXIT
    cleanup_guest_candidate
    [[ -z $payload_file ]] || rm -f -- "$payload_file"
    [[ -z $patch_file ]] || rm -f -- "$patch_file"
    [[ -z $patch_verify_file ]] || rm -f -- "$patch_verify_file"
    [[ -z $guest_report_file ]] || rm -f -- "$guest_report_file"
    cleanup_after_build
    printf 'STVR_CANDIDATE_BASE=%s\n' "$base_commit"
    printf 'STVR_CANDIDATE_EPHEMERAL_REVISION=%s\n' "$candidate_ephemeral_revision"
    printf 'STVR_CANDIDATE_BUILD_SUCCESS=%s\n' "$candidate_build_success"
    exit "$status"
}
trap cleanup_runtime EXIT

if [[ -n $(git ls-files --others --exclude-standard) ]]; then
    echo "Refusing candidate build with untracked files. Stage or remove them before creating the tracked delta snapshot." >&2
    exit 2
fi

if ! git submodule foreach --recursive '
    if test -n "$(git status --porcelain=v1 --untracked-files=all)"; then
        echo "Dirty submodule: $displaypath" >&2
        exit 1
    fi
'; then
    echo "Refusing candidate build with a dirty submodule." >&2
    exit 2
fi

submodule_state=$(git submodule status --recursive)
while IFS= read -r submodule_line; do
    if [[ $submodule_line =~ ^[-+U] ]]; then
        echo "Refusing candidate build with an uninitialized or unresolved submodule." >&2
        exit 2
    fi
done <<<"$submodule_state"

while IFS=' ' read -r _ submodule_path; do
    if ! git diff --quiet HEAD -- "$submodule_path"; then
        echo "Refusing candidate build with a changed submodule pointer: $submodule_path" >&2
        exit 2
    fi
done < <(git config -f .gitmodules --get-regexp '^submodule\..*\.path$')

base_commit=$(git rev-parse --verify HEAD^{commit})
short_commit=${base_commit:0:8}
status_before=$(git status --porcelain=v1 --untracked-files=all)
patch_file=$(mktemp "${TMPDIR:-/tmp}/stvr-winboat-candidate-${short_commit}-XXXXXX.patch")
git diff --binary --full-index --no-ext-diff --ignore-submodules=all "$base_commit" -- >"$patch_file"
status_after_first_snapshot=$(git status --porcelain=v1 --untracked-files=all)
base_after_first_snapshot=$(git rev-parse --verify HEAD^{commit})
patch_verify_file=$(mktemp "${TMPDIR:-/tmp}/stvr-winboat-candidate-${short_commit}-XXXXXX.verify.patch")
git diff --binary --full-index --no-ext-diff --ignore-submodules=all "$base_commit" -- >"$patch_verify_file"
status_after_second_snapshot=$(git status --porcelain=v1 --untracked-files=all)
base_after_second_snapshot=$(git rev-parse --verify HEAD^{commit})
if [[ $status_before != "$status_after_first_snapshot" || \
      $status_before != "$status_after_second_snapshot" || \
      $base_commit != "$base_after_first_snapshot" || \
      $base_commit != "$base_after_second_snapshot" ]]; then
    echo "Linux working tree or HEAD changed while creating the candidate patch; retry from a stable tree." >&2
    exit 2
fi
if [[ ! -s $patch_file ]]; then
    echo "No tracked working-tree delta exists. Use the normal clean WinBoat build instead." >&2
    exit 2
fi
patch_sha256=$(sha256sum "$patch_file" | awk '{print $1}')
patch_verify_sha256=$(sha256sum "$patch_verify_file" | awk '{print $1}')
if [[ $patch_sha256 != "$patch_verify_sha256" ]]; then
    echo "Linux candidate patch changed between snapshots; retry from a stable tree." >&2
    exit 2
fi
rm -f -- "$patch_verify_file"
patch_verify_file=""

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
winboat_build="${winboat_repo}-candidate-${short_commit}-${timestamp}"
guest_patch="C:/Users/obesecatlord/AppData/Local/Temp/stvr-winboat-candidate-${short_commit}-${timestamp}.patch"
guest_payload="C:/Users/obesecatlord/AppData/Local/Temp/stvr-winboat-candidate-${short_commit}-${timestamp}.ps1"

for value in "$winboat_repo" "$winboat_build" "$guest_patch" "$guest_payload"; do
    if [[ $value == *"'"* ]]; then
        echo "WinBoat paths containing a single quote are not supported." >&2
        exit 2
    fi
done

printf 'STVR_CANDIDATE_BASE=%s\n' "$base_commit"
printf 'STVR_CANDIDATE_PATCH_SHA256=%s\n' "$patch_sha256"

read -r -d '' powershell_payload <<'POWERSHELL' || true
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
$repo = '__WINBOAT_REPO__'
$build = '__WINBOAT_BUILD__'
$patch = '__GUEST_PATCH__'
$baseCommit = '__BASE_COMMIT__'
$patchSha256 = '__PATCH_SHA256__'
$worktreeCreated = $false
$candidateRevision = "NOT_CREATED"
$buildSucceeded = $false

function Get-CandidateProcesses {
    param([string]$Path)
    $leaf = Split-Path -Leaf $Path
    $escapedLeaf = [regex]::Escape($leaf)
    return @(
        Get-CimInstance Win32_Process | Where-Object {
            $_.ProcessId -ne $PID -and
            -not [string]::IsNullOrWhiteSpace($_.CommandLine) -and
            $_.CommandLine -match $escapedLeaf
        }
    )
}

function Remove-CandidateWorktree {
    param(
        [string]$Path,
        [bool]$FailIfActive
    )

    if (-not (Test-Path -LiteralPath $Path)) { return }
    $active = @(Get-CandidateProcesses $Path)
    if ($active.Count -ne 0) {
        $message = "Candidate worktree still has active processes: $Path"
        if ($FailIfActive) { throw $message }
        Write-Warning "$message; leaving it for bounded stale cleanup."
        return
    }

    git -C $repo worktree remove --force $Path
    if ($LASTEXITCODE -ne 0 -and (Test-Path -LiteralPath $Path)) {
        Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $Path) {
        $message = "Could not remove candidate worktree: $Path"
        if ($FailIfActive) { throw $message }
        Write-Warning $message
    }
}

function Remove-StaleCandidateWorktrees {
    $repoParent = Split-Path -Parent $repo
    $repoLeaf = Split-Path -Leaf $repo
    $candidatePattern = "$repoLeaf-candidate-*"
    $active = @(
        Get-CimInstance Win32_Process | Where-Object {
            $_.ProcessId -ne $PID -and
            -not [string]::IsNullOrWhiteSpace($_.CommandLine) -and
            $_.CommandLine -match [regex]::Escape($candidatePattern.Replace('*', ''))
        }
    )
    if ($active.Count -ne 0) {
        throw "A WinBoat candidate build process is active; refusing stale candidate cleanup."
    }

    foreach ($directory in @(Get-ChildItem -LiteralPath $repoParent -Directory -Filter $candidatePattern -ErrorAction SilentlyContinue)) {
        Remove-CandidateWorktree -Path $directory.FullName -FailIfActive $true
    }
    git -C $repo worktree prune
    if ($LASTEXITCODE -ne 0) { throw "Could not prune stale WinBoat candidate worktree metadata." }
}

function Test-CandidateBaseAncestorOfOriginMain {
    git -C $repo show-ref --verify --quiet refs/remotes/origin/main
    if ($LASTEXITCODE -ne 0) { return $false }

    git -C $repo merge-base --is-ancestor $baseCommit refs/remotes/origin/main
    return $LASTEXITCODE -eq 0
}

try {
    if (-not (Test-CandidateBaseAncestorOfOriginMain)) {
        git -C $repo fetch --no-tags origin +refs/heads/main:refs/remotes/origin/main
        if ($LASTEXITCODE -ne 0) { throw "Could not fetch origin/main while verifying the candidate base." }
        if (-not (Test-CandidateBaseAncestorOfOriginMain)) {
            throw "Candidate base $baseCommit is not an ancestor of refs/remotes/origin/main in WinBoat."
        }
    }

    Remove-StaleCandidateWorktrees
    if (Test-Path -LiteralPath $build) { throw "Fresh candidate worktree already exists: $build" }
    git -C $repo worktree add --detach $build $baseCommit
    if ($LASTEXITCODE -ne 0) { throw "Could not create detached WinBoat candidate worktree." }
    $worktreeCreated = $true

    Set-Location $build
    git submodule sync --recursive
    if ($LASTEXITCODE -ne 0) { throw "Could not synchronize submodule URLs." }
    git submodule update --init --recursive --checkout
    if ($LASTEXITCODE -ne 0) { throw "Could not initialize pinned submodules." }

    $clean = @(git status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0 -or $clean.Count -ne 0) { throw "Fresh WinBoat candidate worktree is unexpectedly dirty." }
    $submoduleState = @(git submodule status --recursive)
    if ($LASTEXITCODE -ne 0 -or @($submoduleState | Where-Object { $_ -match '^[+\-U]' }).Count -ne 0) {
        throw "Fresh WinBoat candidate worktree has an unresolved submodule."
    }

    $actualPatchSha256 = (Get-FileHash -LiteralPath $patch -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualPatchSha256 -ne $patchSha256) { throw "Transferred candidate patch SHA-256 does not match the Linux snapshot." }
    git apply --index --binary --whitespace=nowarn -- $patch
    if ($LASTEXITCODE -ne 0) { throw "Could not apply the Linux candidate patch to the detached WinBoat worktree." }
    $staged = @(git diff --cached --name-only)
    if ($LASTEXITCODE -ne 0 -or $staged.Count -eq 0) { throw "Candidate patch did not stage any tracked changes." }

    git -c user.name="STVR Candidate Builder" -c user.email="stvr-candidate@local.invalid" commit --no-gpg-sign --no-verify -m "STVR candidate build $baseCommit"
    if ($LASTEXITCODE -ne 0) { throw "Could not create the ephemeral WinBoat candidate commit." }
    $candidateRevision = (git rev-parse HEAD).Trim()
    if ($candidateRevision -notmatch '^[0-9a-fA-F]{40}$') { throw "Candidate commit did not resolve to a full revision." }
    $clean = @(git status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0 -or $clean.Count -ne 0) { throw "Ephemeral WinBoat candidate commit is unexpectedly dirty before the audited build." }

    $env:Path = "C:\Users\obesecatlord\AppData\Local\Microsoft\WinGet\Links;$env:Path"
    & .\BuildAuditCollectSkyrimTogetherVR-Windows.bat --gameplay
    if ($LASTEXITCODE -ne 0) { throw "Audited gameplay candidate build failed with exit code $LASTEXITCODE." }
    $buildSucceeded = $true
} finally {
    Remove-Item -LiteralPath $patch -Force -ErrorAction SilentlyContinue
    Set-Location $repo
    if ($worktreeCreated) {
        Remove-CandidateWorktree -Path $build -FailIfActive $false
    }
    git -C $repo worktree prune 2>$null
    $normalizedBuildSucceeded = if ($buildSucceeded) { "true" } else { "false" }
    "STVR_CANDIDATE_BASE=$baseCommit"
    "STVR_CANDIDATE_EPHEMERAL_REVISION=$candidateRevision"
    "STVR_CANDIDATE_BUILD_SUCCESS=$normalizedBuildSucceeded"
}
POWERSHELL

powershell_payload=${powershell_payload//__WINBOAT_REPO__/$winboat_repo}
powershell_payload=${powershell_payload//__WINBOAT_BUILD__/$winboat_build}
powershell_payload=${powershell_payload//__GUEST_PATCH__/$guest_patch}
powershell_payload=${powershell_payload//__BASE_COMMIT__/$base_commit}
powershell_payload=${powershell_payload//__PATCH_SHA256__/$patch_sha256}

payload_file=$(mktemp "${TMPDIR:-/tmp}/stvr-winboat-candidate-${short_commit}-XXXXXX.ps1")
guest_report_file=$(mktemp "${TMPDIR:-/tmp}/stvr-winboat-candidate-${short_commit}-XXXXXX.log")
printf '%s\n' "$powershell_payload" >"$payload_file"
"$winboat_scp" to-guest "$patch_file" "$guest_patch"
"$winboat_scp" to-guest "$payload_file" "$guest_payload"
set +e
"$winboat_ssh" powershell.exe -NoLogo -NoProfile -NonInteractive \
    -ExecutionPolicy Bypass -File "$guest_payload" | tee "$guest_report_file"
guest_status=${PIPESTATUS[0]}
set -e

candidate_ephemeral_revision=$(awk -F= '/^STVR_CANDIDATE_EPHEMERAL_REVISION=/ { value = $2 } END { print value }' "$guest_report_file")
candidate_build_success=$(awk -F= '/^STVR_CANDIDATE_BUILD_SUCCESS=/ { value = $2 } END { print value }' "$guest_report_file")
candidate_ephemeral_revision=${candidate_ephemeral_revision//$'\r'/}
candidate_build_success=${candidate_build_success//$'\r'/}
candidate_build_success=${candidate_build_success,,}
candidate_ephemeral_revision=${candidate_ephemeral_revision:-NOT_CREATED}
candidate_build_success=${candidate_build_success:-false}
if ((guest_status != 0)); then
    exit "$guest_status"
fi
if [[ ! $candidate_ephemeral_revision =~ ^[0-9a-fA-F]{40}$ || $candidate_build_success != true ]]; then
    echo "WinBoat candidate build did not report a successful ephemeral revision." >&2
    exit 2
fi
exit 0
