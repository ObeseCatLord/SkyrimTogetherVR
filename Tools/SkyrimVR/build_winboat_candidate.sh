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
commonlib_bundle_file=""
guest_patch=""
guest_payload=""
guest_commonlib_bundle=""
guest_result=""
winboat_powershell=""
winboat_build=""
winboat_repo=""
base_commit="NOT_RESOLVED"
candidate_ephemeral_revision="NOT_CREATED"
candidate_build_success=false
candidate_result_transferred=false
linux_result_path=""
commonlib_path="Libraries/CommonLibSSE-NG"
commonlib_gitlink_changed=false
commonlib_base_commit="NOT_REQUESTED"
commonlib_target_commit="NOT_REQUESTED"
commonlib_bundle_sha256="NOT_REQUESTED"

cleanup_after_build() {
    "$repo_root/Tools/SkyrimVR/cleanup_build_storage.sh" \
        --scheduled --max-age-days 2 --skip-local-artifacts --temp-artifacts || true
}

cleanup_guest_candidate() {
    if [[ -z $winboat_powershell || ! -x $winboat_powershell ]]; then
        return
    fi
    if [[ -z $guest_patch && -z $guest_payload && -z $guest_commonlib_bundle && -z $guest_result && -z $winboat_build ]]; then
        return
    fi

    local guest_cleanup
    read -r -d '' guest_cleanup <<'POWERSHELL' || true
$ErrorActionPreference = "Continue"
$repo = '__WINBOAT_REPO__'
$build = '__WINBOAT_BUILD__'
$patch = '__GUEST_PATCH__'
$payload = '__GUEST_PAYLOAD__'
$commonLibBundle = '__GUEST_COMMONLIB_BUNDLE__'
$result = '__GUEST_RESULT__'

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
if (-not [string]::IsNullOrWhiteSpace($commonLibBundle)) {
    Remove-Item -LiteralPath $commonLibBundle -Force -ErrorAction SilentlyContinue
}
if (-not [string]::IsNullOrWhiteSpace($result)) {
    Remove-Item -LiteralPath $result -Recurse -Force -ErrorAction SilentlyContinue
}

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
    guest_cleanup=${guest_cleanup//__GUEST_COMMONLIB_BUNDLE__/$guest_commonlib_bundle}
    guest_cleanup=${guest_cleanup//__GUEST_RESULT__/$guest_result}
    "$winboat_powershell" "$guest_cleanup" >/dev/null 2>&1 || true
}

cleanup_runtime() {
    local status=$?
    trap - EXIT
    cleanup_guest_candidate
    [[ -z $payload_file ]] || rm -f -- "$payload_file"
    [[ -z $commonlib_bundle_file ]] || rm -f -- "$commonlib_bundle_file"
    [[ -z $patch_file ]] || rm -f -- "$patch_file"
    [[ -z $patch_verify_file ]] || rm -f -- "$patch_verify_file"
    [[ -z $guest_report_file ]] || rm -f -- "$guest_report_file"
    if [[ $candidate_result_transferred != true && -n $linux_result_path ]]; then
        rm -rf -- "$linux_result_path"
    fi
    cleanup_after_build
    printf 'STVR_CANDIDATE_BASE=%s\n' "$base_commit"
    printf 'STVR_CANDIDATE_EPHEMERAL_REVISION=%s\n' "$candidate_ephemeral_revision"
    printf 'STVR_CANDIDATE_BUILD_SUCCESS=%s\n' "$candidate_build_success"
    printf 'STVR_CANDIDATE_COMMONLIB_BASE=%s\n' "$commonlib_base_commit"
    printf 'STVR_CANDIDATE_COMMONLIB_TARGET=%s\n' "$commonlib_target_commit"
    printf 'STVR_CANDIDATE_COMMONLIB_BUNDLE_SHA256=%s\n' "$commonlib_bundle_sha256"
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

base_commit=$(git rev-parse --verify HEAD^{commit})
commonlib_configured=false
while IFS=' ' read -r _ submodule_path; do
    if [[ $submodule_path == "$commonlib_path" ]]; then
        commonlib_configured=true
    fi
    if ! git diff --quiet HEAD -- "$submodule_path"; then
        if [[ $submodule_path != "$commonlib_path" ]]; then
            echo "Refusing candidate build with a changed submodule pointer: $submodule_path" >&2
            exit 2
        fi
        commonlib_gitlink_changed=true
    fi
done < <(git config -f .gitmodules --get-regexp '^submodule\..*\.path$')

if [[ $commonlib_configured != true ]]; then
    echo "Expected CommonLib submodule is not configured: $commonlib_path" >&2
    exit 2
fi

if [[ $commonlib_gitlink_changed == true ]]; then
    commonlib_base_commit=$(git rev-parse --verify "$base_commit:$commonlib_path")
    commonlib_target_commit=$(git -C "$commonlib_path" rev-parse --verify HEAD^{commit})
    commonlib_raw_delta=$(git diff --raw --no-abbrev --no-renames "$base_commit" -- "$commonlib_path")
    expected_commonlib_index_raw_delta=":160000 160000 $commonlib_base_commit $commonlib_target_commit M"$'\t'"$commonlib_path"
    expected_commonlib_worktree_raw_delta=":160000 160000 $commonlib_base_commit 0000000000000000000000000000000000000000 M"$'\t'"$commonlib_path"
    if [[ $commonlib_raw_delta != "$expected_commonlib_index_raw_delta" && \
          $commonlib_raw_delta != "$expected_commonlib_worktree_raw_delta" ]]; then
        echo "Refusing unexpected CommonLib gitlink delta; only a direct mode-160000 pointer advancement is supported." >&2
        exit 2
    fi
    commonlib_patch_delta=$(git diff --full-index --no-ext-diff --submodule=short "$base_commit" -- "$commonlib_path")
    expected_commonlib_patch_delta=$(printf 'diff --git a/%s b/%s\nindex %s..%s 160000\n--- a/%s\n+++ b/%s\n@@ -1 +1 @@\n-Subproject commit %s\n+Subproject commit %s' \
        "$commonlib_path" "$commonlib_path" "$commonlib_base_commit" "$commonlib_target_commit" \
        "$commonlib_path" "$commonlib_path" "$commonlib_base_commit" "$commonlib_target_commit")
    if [[ $commonlib_patch_delta != "$expected_commonlib_patch_delta" ]]; then
        echo "Refusing CommonLib gitlink delta whose patch does not name the requested target commit." >&2
        exit 2
    fi
    if ! git -C "$commonlib_path" cat-file -e "$commonlib_base_commit^{commit}" || \
       ! git -C "$commonlib_path" cat-file -e "$commonlib_target_commit^{commit}"; then
        echo "Refusing CommonLib gitlink delta with a missing commit object." >&2
        exit 2
    fi
    if ! git -C "$commonlib_path" merge-base --is-ancestor "$commonlib_base_commit" "$commonlib_target_commit"; then
        echo "Refusing CommonLib gitlink delta whose target is not a descendant of the parent gitlink base." >&2
        exit 2
    fi
fi

submodule_state=$(git submodule status --recursive)
allowed_commonlib_status_seen=false
while IFS= read -r submodule_line; do
    submodule_prefix=${submodule_line:0:1}
    case $submodule_prefix in
        -|U)
            echo "Refusing candidate build with an uninitialized or unresolved submodule." >&2
            exit 2
            ;;
        +)
            submodule_rest=${submodule_line:1}
            submodule_commit=${submodule_rest%% *}
            submodule_path_and_suffix=${submodule_rest#* }
            submodule_status_path=${submodule_path_and_suffix%% *}
            if [[ $commonlib_gitlink_changed != true || \
                  $submodule_status_path != "$commonlib_path" || \
                  $submodule_commit != "$commonlib_target_commit" || \
                  $allowed_commonlib_status_seen == true ]]; then
                echo "Refusing candidate build with an unexpected changed submodule state: $submodule_line" >&2
                exit 2
            fi
            allowed_commonlib_status_seen=true
            ;;
    esac
done <<<"$submodule_state"
if [[ $commonlib_gitlink_changed == true ]]; then
    commonlib_direct_status=$(git submodule status -- "$commonlib_path")
    commonlib_status_prefix=${commonlib_direct_status:0:1}
    commonlib_status_rest=${commonlib_direct_status:1}
    commonlib_status_commit=${commonlib_status_rest%% *}
    commonlib_status_path_and_suffix=${commonlib_status_rest#* }
    commonlib_status_path=${commonlib_status_path_and_suffix%% *}
    if [[ ( $commonlib_status_prefix != '+' && $commonlib_status_prefix != ' ' ) || \
          $commonlib_status_commit != "$commonlib_target_commit" || \
          $commonlib_status_path != "$commonlib_path" ]]; then
        echo "CommonLib gitlink changed but its worktree does not match the requested committed target." >&2
        exit 2
    fi
fi

short_commit=${base_commit:0:8}
status_before=$(git status --porcelain=v1 --untracked-files=all)
commonlib_head_before="NOT_REQUESTED"
if [[ $commonlib_gitlink_changed == true ]]; then
    commonlib_head_before=$(git -C "$commonlib_path" rev-parse --verify HEAD^{commit})
    commonlib_bundle_file=$(mktemp "${TMPDIR:-/tmp}/stvr-winboat-commonlib-${short_commit}-XXXXXX.bundle")
    git -C "$commonlib_path" bundle create "$commonlib_bundle_file" \
        HEAD "^$commonlib_base_commit"
    git -C "$commonlib_path" bundle verify "$commonlib_bundle_file" >/dev/null
    bundle_head_count=$(git -C "$commonlib_path" bundle list-heads "$commonlib_bundle_file" | awk 'END { print NR }')
    bundle_head_commit=$(git -C "$commonlib_path" bundle list-heads "$commonlib_bundle_file" | awk 'NR == 1 { print $1 }')
    bundle_head_ref=$(git -C "$commonlib_path" bundle list-heads "$commonlib_bundle_file" | awk 'NR == 1 { print $2 }')
    if [[ $bundle_head_count != 1 || $bundle_head_commit != "$commonlib_target_commit" || $bundle_head_ref != HEAD ]]; then
        echo "CommonLib bundle does not contain exactly the requested target commit." >&2
        exit 2
    fi
    commonlib_bundle_sha256=$(sha256sum "$commonlib_bundle_file" | awk '{print $1}')
fi
patch_file=$(mktemp "${TMPDIR:-/tmp}/stvr-winboat-candidate-${short_commit}-XXXXXX.patch")
git diff --binary --full-index --no-ext-diff --ignore-submodules=none "$base_commit" -- >"$patch_file"
status_after_first_snapshot=$(git status --porcelain=v1 --untracked-files=all)
base_after_first_snapshot=$(git rev-parse --verify HEAD^{commit})
patch_verify_file=$(mktemp "${TMPDIR:-/tmp}/stvr-winboat-candidate-${short_commit}-XXXXXX.verify.patch")
git diff --binary --full-index --no-ext-diff --ignore-submodules=none "$base_commit" -- >"$patch_verify_file"
status_after_second_snapshot=$(git status --porcelain=v1 --untracked-files=all)
base_after_second_snapshot=$(git rev-parse --verify HEAD^{commit})
if [[ $status_before != "$status_after_first_snapshot" || \
      $status_before != "$status_after_second_snapshot" || \
      $base_commit != "$base_after_first_snapshot" || \
      $base_commit != "$base_after_second_snapshot" || \
      $commonlib_head_before != "$commonlib_target_commit" ]]; then
    echo "Linux working tree or HEAD changed while creating the candidate patch; retry from a stable tree." >&2
    exit 2
fi
if [[ $commonlib_gitlink_changed == true && \
      $(git -C "$commonlib_path" rev-parse --verify HEAD^{commit}) != "$commonlib_target_commit" ]]; then
    echo "CommonLib HEAD changed while creating the candidate patch; retry from a stable tree." >&2
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
guest_result="${winboat_repo}-candidate-results\\${short_commit}-${timestamp}"
guest_patch="C:/Users/obesecatlord/AppData/Local/Temp/stvr-winboat-candidate-${short_commit}-${timestamp}.patch"
guest_payload="C:/Users/obesecatlord/AppData/Local/Temp/stvr-winboat-candidate-${short_commit}-${timestamp}.ps1"
if [[ $commonlib_gitlink_changed == true ]]; then
    guest_commonlib_bundle="C:/Users/obesecatlord/AppData/Local/Temp/stvr-winboat-commonlib-${short_commit}-${timestamp}.bundle"
fi

for value in "$winboat_repo" "$winboat_build" "$guest_result" "$guest_patch" "$guest_payload" "$guest_commonlib_bundle"; do
    if [[ $value == *"'"* ]]; then
        echo "WinBoat paths containing a single quote are not supported." >&2
        exit 2
    fi
done

printf 'STVR_CANDIDATE_BASE=%s\n' "$base_commit"
printf 'STVR_CANDIDATE_PATCH_SHA256=%s\n' "$patch_sha256"
printf 'STVR_CANDIDATE_COMMONLIB_BASE=%s\n' "$commonlib_base_commit"
printf 'STVR_CANDIDATE_COMMONLIB_TARGET=%s\n' "$commonlib_target_commit"
printf 'STVR_CANDIDATE_COMMONLIB_BUNDLE_SHA256=%s\n' "$commonlib_bundle_sha256"

read -r -d '' powershell_payload <<'POWERSHELL' || true
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
$repo = '__WINBOAT_REPO__'
$build = '__WINBOAT_BUILD__'
$patch = '__GUEST_PATCH__'
$baseCommit = '__BASE_COMMIT__'
$patchSha256 = '__PATCH_SHA256__'
$hasCommonLibGitlink = [System.Convert]::ToBoolean('__HAS_COMMONLIB_GITLINK__')
$commonLibPath = 'Libraries/CommonLibSSE-NG'
$commonLibBase = '__COMMONLIB_BASE__'
$commonLibTarget = '__COMMONLIB_TARGET__'
$commonLibBundle = '__GUEST_COMMONLIB_BUNDLE__'
$commonLibBundleSha256 = '__COMMONLIB_BUNDLE_SHA256__'
$result = '__GUEST_RESULT__'
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

    $commonLibWorktree = Join-Path $build $commonLibPath
    if ($hasCommonLibGitlink) {
        if (-not (Test-Path -LiteralPath $commonLibBundle)) {
            throw "Transferred CommonLib bundle is missing."
        }
        $initialCommonLibHead = (git -C $commonLibWorktree rev-parse HEAD).Trim()
        if ($LASTEXITCODE -ne 0 -or $initialCommonLibHead -ne $commonLibBase) {
            throw "Candidate CommonLib checkout does not match the parent gitlink base."
        }
        $actualCommonLibBundleSha256 = (Get-FileHash -LiteralPath $commonLibBundle -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualCommonLibBundleSha256 -ne $commonLibBundleSha256) {
            throw "Transferred CommonLib bundle SHA-256 does not match the Linux snapshot."
        }
        git -C $commonLibWorktree bundle verify $commonLibBundle
        if ($LASTEXITCODE -ne 0) { throw "CommonLib bundle prerequisites are not satisfied by the candidate checkout." }
        $bundleHeads = @(git -C $commonLibWorktree bundle list-heads $commonLibBundle)
        if ($LASTEXITCODE -ne 0 -or $bundleHeads.Count -ne 1) {
            throw "CommonLib bundle does not have exactly one advertised target." }
        $bundleHeadParts = @($bundleHeads[0] -split '\s+')
        $bundleTarget = $bundleHeadParts[0].Trim()
        $bundleRef = $bundleHeadParts[1].Trim()
        if ($bundleHeadParts.Count -ne 2 -or $bundleTarget -ne $commonLibTarget -or $bundleRef -ne 'HEAD') {
            throw "CommonLib bundle target does not match the requested gitlink." }
        git -C $commonLibWorktree bundle unbundle $commonLibBundle | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Could not import the CommonLib bundle into the candidate object database." }
        git -C $commonLibWorktree cat-file -e "$commonLibTarget^{commit}"
        if ($LASTEXITCODE -ne 0) { throw "Imported CommonLib target commit is not available in the candidate object database." }
    }

    $actualPatchSha256 = (Get-FileHash -LiteralPath $patch -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualPatchSha256 -ne $patchSha256) { throw "Transferred candidate patch SHA-256 does not match the Linux snapshot." }
    git apply --index --binary --whitespace=nowarn -- $patch
    if ($LASTEXITCODE -ne 0) { throw "Could not apply the Linux candidate patch to the detached WinBoat worktree." }
    $staged = @(git diff --cached --name-only)
    if ($LASTEXITCODE -ne 0 -or $staged.Count -eq 0) { throw "Candidate patch did not stage any tracked changes." }
    if ($hasCommonLibGitlink) {
        $stagedCommonLibGitlink = (git rev-parse ":$commonLibPath").Trim()
        if ($LASTEXITCODE -ne 0 -or $stagedCommonLibGitlink -ne $commonLibTarget) {
            throw "Candidate patch did not stage the requested CommonLib gitlink."
        }
        git -C $commonLibWorktree checkout --detach $commonLibTarget
        if ($LASTEXITCODE -ne 0) { throw "Could not checkout the staged CommonLib gitlink in the candidate worktree." }
        git -C $commonLibWorktree submodule sync --recursive
        if ($LASTEXITCODE -ne 0) { throw "Could not synchronize nested CommonLib submodules." }
        git -C $commonLibWorktree submodule update --init --recursive --checkout
        if ($LASTEXITCODE -ne 0) { throw "Could not initialize nested CommonLib submodules." }
        $updatedCommonLibHead = (git -C $commonLibWorktree rev-parse HEAD).Trim()
        if ($LASTEXITCODE -ne 0 -or $updatedCommonLibHead -ne $commonLibTarget) {
            throw "Candidate CommonLib checkout does not match the staged gitlink target."
        }
        $commonLibStatus = @(git -C $commonLibWorktree status --porcelain=v1 --untracked-files=all)
        if ($LASTEXITCODE -ne 0 -or $commonLibStatus.Count -ne 0) {
            throw "Candidate CommonLib checkout is dirty after staging the gitlink."
        }
    }
    $submoduleState = @(git submodule status --recursive)
    if ($LASTEXITCODE -ne 0 -or @($submoduleState | Where-Object { $_ -match '^[+\-U]' }).Count -ne 0) {
        throw "Candidate patch left an unresolved or mismatched submodule state."
    }

    git -c user.name="STVR Candidate Builder" -c user.email="stvr-candidate@local.invalid" commit --no-gpg-sign --no-verify -m "STVR candidate build $baseCommit"
    if ($LASTEXITCODE -ne 0) { throw "Could not create the ephemeral WinBoat candidate commit." }
    $candidateRevision = (git rev-parse HEAD).Trim()
    if ($candidateRevision -notmatch '^[0-9a-fA-F]{40}$') { throw "Candidate commit did not resolve to a full revision." }
    $clean = @(git status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0 -or $clean.Count -ne 0) { throw "Ephemeral WinBoat candidate commit is unexpectedly dirty before the audited build." }

    $env:Path = "C:\Users\obesecatlord\AppData\Local\Microsoft\WinGet\Links;$env:Path"
    & .\BuildAuditCollectSkyrimTogetherVR-Windows.bat --gameplay
    if ($LASTEXITCODE -ne 0) { throw "Audited gameplay candidate build failed with exit code $LASTEXITCODE." }

    $package = Join-Path $build 'artifacts\SkyrimTogetherVR\packages\gameplay'
    $evidence = Get-ChildItem -LiteralPath (Join-Path $build 'artifacts\SkyrimTogetherVR\build-evidence') -Filter 'SkyrimTogetherVR-build-evidence-gameplay-*.zip' |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if (-not (Test-Path -LiteralPath $package)) { throw "Gameplay package was not created: $package" }
    if ($null -eq $evidence) { throw "Gameplay build evidence archive was not created." }
    if (Test-Path -LiteralPath $result) { throw "Fresh candidate result directory already exists: $result" }

    $resultPackage = Join-Path $result 'gameplay'
    New-Item -ItemType Directory -Path $resultPackage -Force | Out-Null
    Copy-Item -Path (Join-Path $package '*') -Destination $resultPackage -Recurse -Force
    $resultEvidence = Join-Path $result $evidence.Name
    Copy-Item -LiteralPath $evidence.FullName -Destination $resultEvidence -Force
    @(
        'schema=stvr-winboat-candidate-result-v1'
        "baseCommit=$baseCommit"
        "candidateRevision=$candidateRevision"
        "patchSha256=$patchSha256"
        "commonLibBase=$commonLibBase"
        "commonLibTarget=$commonLibTarget"
        "commonLibBundleSha256=$commonLibBundleSha256"
        "buildEvidence=$($evidence.Name)"
    ) | Set-Content -LiteralPath (Join-Path $result 'STVR_CandidateProvenance.txt') -Encoding ascii
    $buildSucceeded = $true
} finally {
    Remove-Item -LiteralPath $patch -Force -ErrorAction SilentlyContinue
    if (-not [string]::IsNullOrWhiteSpace($commonLibBundle)) {
        Remove-Item -LiteralPath $commonLibBundle -Force -ErrorAction SilentlyContinue
    }
    if (-not $buildSucceeded -and (Test-Path -LiteralPath $result)) {
        Remove-Item -LiteralPath $result -Recurse -Force -ErrorAction SilentlyContinue
    }
    Set-Location $repo
    if ($worktreeCreated) {
        Remove-CandidateWorktree -Path $build -FailIfActive $false
    }
    git -C $repo worktree prune 2>$null
    $normalizedBuildSucceeded = if ($buildSucceeded) { "true" } else { "false" }
    "STVR_CANDIDATE_BASE=$baseCommit"
    "STVR_CANDIDATE_EPHEMERAL_REVISION=$candidateRevision"
    "STVR_CANDIDATE_BUILD_SUCCESS=$normalizedBuildSucceeded"
    "STVR_CANDIDATE_GUEST_RESULT=$result"
    "STVR_CANDIDATE_COMMONLIB_BASE=$commonLibBase"
    "STVR_CANDIDATE_COMMONLIB_TARGET=$commonLibTarget"
    "STVR_CANDIDATE_COMMONLIB_BUNDLE_SHA256=$commonLibBundleSha256"
}
POWERSHELL

powershell_payload=${powershell_payload//__WINBOAT_REPO__/$winboat_repo}
powershell_payload=${powershell_payload//__WINBOAT_BUILD__/$winboat_build}
powershell_payload=${powershell_payload//__GUEST_PATCH__/$guest_patch}
powershell_payload=${powershell_payload//__BASE_COMMIT__/$base_commit}
powershell_payload=${powershell_payload//__PATCH_SHA256__/$patch_sha256}
powershell_payload=${powershell_payload//__HAS_COMMONLIB_GITLINK__/$commonlib_gitlink_changed}
powershell_payload=${powershell_payload//__COMMONLIB_BASE__/$commonlib_base_commit}
powershell_payload=${powershell_payload//__COMMONLIB_TARGET__/$commonlib_target_commit}
powershell_payload=${powershell_payload//__GUEST_COMMONLIB_BUNDLE__/$guest_commonlib_bundle}
powershell_payload=${powershell_payload//__COMMONLIB_BUNDLE_SHA256__/$commonlib_bundle_sha256}
powershell_payload=${powershell_payload//__GUEST_RESULT__/$guest_result}

payload_file=$(mktemp "${TMPDIR:-/tmp}/stvr-winboat-candidate-${short_commit}-XXXXXX.ps1")
guest_report_file=$(mktemp "${TMPDIR:-/tmp}/stvr-winboat-candidate-${short_commit}-XXXXXX.log")
printf '%s\n' "$powershell_payload" >"$payload_file"
"$winboat_scp" to-guest "$patch_file" "$guest_patch"
if [[ $commonlib_gitlink_changed == true ]]; then
    "$winboat_scp" to-guest "$commonlib_bundle_file" "$guest_commonlib_bundle"
fi
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
guest_commonlib_base=$(awk -F= '/^STVR_CANDIDATE_COMMONLIB_BASE=/ { value = $2 } END { print value }' "$guest_report_file")
guest_commonlib_target=$(awk -F= '/^STVR_CANDIDATE_COMMONLIB_TARGET=/ { value = $2 } END { print value }' "$guest_report_file")
guest_commonlib_bundle_sha256=$(awk -F= '/^STVR_CANDIDATE_COMMONLIB_BUNDLE_SHA256=/ { value = $2 } END { print value }' "$guest_report_file")
guest_commonlib_base=${guest_commonlib_base//$'\r'/}
guest_commonlib_target=${guest_commonlib_target//$'\r'/}
guest_commonlib_bundle_sha256=${guest_commonlib_bundle_sha256//$'\r'/}
if ((guest_status != 0)); then
    exit "$guest_status"
fi
if [[ $commonlib_gitlink_changed == true && \
      ( $guest_commonlib_base != "$commonlib_base_commit" || \
        $guest_commonlib_target != "$commonlib_target_commit" || \
        $guest_commonlib_bundle_sha256 != "$commonlib_bundle_sha256" ) ]]; then
    echo "WinBoat candidate build did not report the verified CommonLib bundle identity." >&2
    exit 2
fi
if [[ ! $candidate_ephemeral_revision =~ ^[0-9a-fA-F]{40}$ || $candidate_build_success != true ]]; then
    echo "WinBoat candidate build did not report a successful ephemeral revision." >&2
    exit 2
fi

guest_candidate_result=$(awk -F= '/^STVR_CANDIDATE_GUEST_RESULT=/ { value = $2 } END { print value }' "$guest_report_file")
guest_candidate_result=${guest_candidate_result//$'\r'/}
if [[ $guest_candidate_result != "$guest_result" ]]; then
    echo "WinBoat candidate build did not report the expected guest result directory." >&2
    exit 2
fi

linux_result_root=${STVR_CANDIDATE_RESULT_ROOT:-"${XDG_STATE_HOME:-$HOME/.local/state}/skyrim-together-vr/candidates"}
mkdir -p -- "$linux_result_root"
linux_result_root=$(cd "$linux_result_root" && pwd -P)
repo_root_physical=$(cd "$repo_root" && pwd -P)
case "$linux_result_root/" in
    "$repo_root_physical/"*)
        echo "STVR_CANDIDATE_RESULT_ROOT must be outside the repository: $linux_result_root" >&2
        exit 2
        ;;
esac
linux_result_min_free_kib=${STVR_CANDIDATE_MIN_FREE_KIB:-1048576}
if [[ ! $linux_result_min_free_kib =~ ^[0-9]+$ ]]; then
    echo "STVR_CANDIDATE_MIN_FREE_KIB must be a non-negative integer." >&2
    exit 2
fi
linux_result_available_kib=$(df -Pk "$linux_result_root" | awk 'NR == 2 { print $4 }')
if [[ ! $linux_result_available_kib =~ ^[0-9]+$ || $linux_result_available_kib -lt $linux_result_min_free_kib ]]; then
    echo "Insufficient free space under STVR_CANDIDATE_RESULT_ROOT ($linux_result_root): need ${linux_result_min_free_kib} KiB free." >&2
    exit 2
fi

linux_result_path=$(mktemp -d "$linux_result_root/stvr-winboat-candidate-${short_commit}-${timestamp}-XXXXXX")
guest_result_scp=${guest_result//\\//}
"$winboat_scp" from-guest "$guest_result_scp" "$linux_result_path/result" --recursive

if [[ ! -f $linux_result_path/result/gameplay/SkyrimTogetherVR_BuildManifest.json ]]; then
    echo "Transferred WinBoat candidate result is missing the gameplay package manifest." >&2
    exit 2
fi
if ! find "$linux_result_path/result" -maxdepth 1 -type f -name 'SkyrimTogetherVR-build-evidence-gameplay-*.zip' -print -quit | grep -q .; then
    echo "Transferred WinBoat candidate result is missing the gameplay build-evidence zip." >&2
    exit 2
fi
if [[ ! -f $linux_result_path/result/STVR_CandidateProvenance.txt ]]; then
    echo "Transferred WinBoat candidate result is missing its provenance record." >&2
    exit 2
fi

candidate_result_transferred=true
printf 'STVR_CANDIDATE_LINUX_RESULT=%s\n' "$linux_result_path/result"
exit 0
