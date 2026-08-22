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
commonlib_probe_repo=""
candidate_bundle_file=""
planck_bundle_file=""
planck_bundle_probe_repo=""
planck_snapshot_ref=""
root_snapshot_bundle_file=""
root_snapshot_ref=""
planck_temp_index=""
root_temp_index=""
guest_patch=""
guest_payload=""
guest_commonlib_bundle=""
guest_commonlib_transfer_ref=""
guest_commonlib_trusted_ref=""
guest_candidate_bundle=""
guest_planck_bundle=""
guest_planck_transfer_ref=""
guest_root_snapshot_bundle=""
guest_root_snapshot_transfer_ref=""
guest_result=""
winboat_powershell=""
winboat_build=""
winboat_repo=""
base_commit="NOT_RESOLVED"
candidate_common_ancestor="NOT_RESOLVED"
candidate_bundle_sha256="NOT_REQUIRED"
candidate_bundle_required=false
guest_candidate_transfer_ref=""
guest_trusted_remote_ref=""
candidate_ephemeral_revision="NOT_CREATED"
candidate_build_success=false
candidate_result_transferred=false
linux_result_path=""
commonlib_path="Libraries/CommonLibSSE-NG"
planck_path="Libraries/activeragdoll"
planck_dirty=false
planck_base_commit="NOT_RESOLVED"
planck_synthetic_commit="NOT_CREATED"
planck_bundle_sha256="NOT_REQUIRED"
root_synthetic_commit="NOT_CREATED"
root_synthetic_tree="NOT_CREATED"
root_snapshot_bundle_sha256="NOT_CREATED"
commonlib_gitlink_changed=false
commonlib_base_commit="NOT_RESOLVED"
commonlib_target_commit="NOT_RESOLVED"
commonlib_bundle_sha256="NOT_REQUIRED"
commonlib_bundle_required=false
commonlib_common_ancestor="NOT_RESOLVED"
commonlib_trusted_ref=""
commonlib_trusted_upstream_commit="NOT_RESOLVED"
commonlib_trusted_upstream_url="https://github.com/alandtse/CommonLibVR.git"
commonlib_verification_source="NETWORK"
commonlib_trusted_upstream_match_required=true
commonlib_base_upstream_available=false
commonlib_target_upstream_available=false
commonlib_cache_file=""
commonlib_source_git_dir=""

cleanup_after_build() {
    "$repo_root/Tools/SkyrimVR/cleanup_build_storage.sh" \
        --apply --scheduled --max-age-days 2 --skip-local-artifacts --temp-artifacts || true
}

cleanup_guest_candidate() {
    if [[ -z $winboat_powershell || ! -x $winboat_powershell ]]; then
        return
    fi
    if [[ -z $guest_patch && -z $guest_payload && -z $guest_commonlib_bundle && -z $guest_candidate_bundle && -z $guest_planck_bundle && -z $guest_root_snapshot_bundle && -z $guest_result && -z $winboat_build ]]; then
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
$commonLibPath = 'Libraries/CommonLibSSE-NG'
$commonLibTransferRef = '__GUEST_COMMONLIB_TRANSFER_REF__'
$commonLibTrustedRef = '__GUEST_COMMONLIB_TRUSTED_REF__'
$candidateBundle = '__GUEST_CANDIDATE_BUNDLE__'
$planckBundle = '__GUEST_PLANCK_BUNDLE__'
$planckPath = 'Libraries/activeragdoll'
$planckTransferRef = '__GUEST_PLANCK_TRANSFER_REF__'
$rootSnapshotBundle = '__GUEST_ROOT_SNAPSHOT_BUNDLE__'
$rootSnapshotTransferRef = '__GUEST_ROOT_SNAPSHOT_TRANSFER_REF__'
$candidateTransferRef = '__GUEST_CANDIDATE_TRANSFER_REF__'
$trustedRemoteRef = '__GUEST_TRUSTED_REMOTE_REF__'
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
if (-not [string]::IsNullOrWhiteSpace($candidateBundle)) {
    Remove-Item -LiteralPath $candidateBundle -Force -ErrorAction SilentlyContinue
}
if (-not [string]::IsNullOrWhiteSpace($planckBundle)) { Remove-Item -LiteralPath $planckBundle -Force -ErrorAction SilentlyContinue }
if (-not [string]::IsNullOrWhiteSpace($rootSnapshotBundle)) { Remove-Item -LiteralPath $rootSnapshotBundle -Force -ErrorAction SilentlyContinue }
if (-not [string]::IsNullOrWhiteSpace($candidateTransferRef)) {
    git -C $repo update-ref -d $candidateTransferRef 2>$null
}
if (-not [string]::IsNullOrWhiteSpace($trustedRemoteRef)) {
    git -C $repo update-ref -d $trustedRemoteRef 2>$null
}
if (-not [string]::IsNullOrWhiteSpace($rootSnapshotTransferRef)) {
    git -C $repo update-ref -d $rootSnapshotTransferRef 2>$null
}
if (-not [string]::IsNullOrWhiteSpace($build)) {
    $commonLibWorktree = Join-Path $build $commonLibPath
    if (Test-Path -LiteralPath $commonLibWorktree) {
        if (-not [string]::IsNullOrWhiteSpace($commonLibTransferRef)) {
            git -C $commonLibWorktree update-ref -d $commonLibTransferRef 2>$null
        }
        if (-not [string]::IsNullOrWhiteSpace($commonLibTrustedRef)) {
            git -C $commonLibWorktree update-ref -d $commonLibTrustedRef 2>$null
        }
    }
    $planckWorktree = Join-Path $build $planckPath
    if ((Test-Path -LiteralPath $planckWorktree) -and -not [string]::IsNullOrWhiteSpace($planckTransferRef)) {
        git -C $planckWorktree update-ref -d $planckTransferRef 2>$null
    }
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
    guest_cleanup=${guest_cleanup//__GUEST_COMMONLIB_TRANSFER_REF__/$guest_commonlib_transfer_ref}
    guest_cleanup=${guest_cleanup//__GUEST_COMMONLIB_TRUSTED_REF__/$guest_commonlib_trusted_ref}
    guest_cleanup=${guest_cleanup//__GUEST_CANDIDATE_BUNDLE__/$guest_candidate_bundle}
    guest_cleanup=${guest_cleanup//__GUEST_PLANCK_BUNDLE__/$guest_planck_bundle}
    guest_cleanup=${guest_cleanup//__GUEST_PLANCK_TRANSFER_REF__/$guest_planck_transfer_ref}
    guest_cleanup=${guest_cleanup//__GUEST_ROOT_SNAPSHOT_BUNDLE__/$guest_root_snapshot_bundle}
    guest_cleanup=${guest_cleanup//__GUEST_ROOT_SNAPSHOT_TRANSFER_REF__/$guest_root_snapshot_transfer_ref}
    guest_cleanup=${guest_cleanup//__GUEST_CANDIDATE_TRANSFER_REF__/$guest_candidate_transfer_ref}
    guest_cleanup=${guest_cleanup//__GUEST_TRUSTED_REMOTE_REF__/$guest_trusted_remote_ref}
    guest_cleanup=${guest_cleanup//__GUEST_RESULT__/$guest_result}
    "$winboat_powershell" "$guest_cleanup" >/dev/null 2>&1 || true
}

retain_failed_guest_report() {
    if [[ -z $guest_report_file || ! -f $guest_report_file || $candidate_build_success == true ]]; then
        return
    fi

    local state_dir repo_root_physical retained_report stale_report
    state_dir="${XDG_STATE_HOME:-$HOME/.local/state}/skyrim-together-vr/winboat-candidate-failures"
    if ! (umask 077; mkdir -p -- "$state_dir" && chmod 700 -- "$state_dir"); then
        echo "Unable to retain failed WinBoat candidate log under: $state_dir" >&2
        return
    fi
    state_dir=$(cd "$state_dir" && pwd -P) || return 0
    repo_root_physical=$(cd "$repo_root" && pwd -P) || return 0
    case "$state_dir/" in
        "$repo_root_physical/"*)
            echo "Refusing to retain failed WinBoat candidate log inside the repository: $state_dir" >&2
            return
            ;;
    esac

    retained_report="$state_dir/stvr-winboat-candidate-failure-${short_commit}-${timestamp}.log"
    local collision_suffix=1
    while [[ -e $retained_report ]]; do
        retained_report="$state_dir/stvr-winboat-candidate-failure-${short_commit}-${timestamp}-${collision_suffix}.log"
        collision_suffix=$((collision_suffix + 1))
    done
    if ! mv -T -- "$guest_report_file" "$retained_report"; then
        echo "Unable to retain failed WinBoat candidate log: $retained_report" >&2
        return
    fi
    guest_report_file=""
    chmod 600 -- "$retained_report" || true
    printf 'STVR_CANDIDATE_FAILURE_LOG=%s\n' "$retained_report" >&2

    while IFS= read -r stale_report; do
        rm -f -- "$state_dir/$stale_report" || true
    done < <(find "$state_dir" -maxdepth 1 -type f -name 'stvr-winboat-candidate-failure-*.log' -printf '%T@ %f\n' |
        LC_ALL=C sort -nr | awk 'NR > 5 { sub(/^[^ ]+ /, ""); print }')
}

cleanup_runtime() {
    local status=$?
    trap - EXIT
    cleanup_guest_candidate
    [[ -z $payload_file ]] || rm -f -- "$payload_file"
    [[ -z $commonlib_bundle_file ]] || rm -f -- "$commonlib_bundle_file"
    [[ -z $commonlib_probe_repo ]] || rm -rf -- "$commonlib_probe_repo"
    if [[ -n $commonlib_trusted_ref ]]; then
        git -C "$commonlib_path" update-ref -d "$commonlib_trusted_ref" 2>/dev/null || true
    fi
    [[ -z $candidate_bundle_file ]] || rm -f -- "$candidate_bundle_file"
    [[ -z $planck_bundle_file ]] || rm -f -- "$planck_bundle_file"
    [[ -z $planck_bundle_probe_repo ]] || rm -rf -- "$planck_bundle_probe_repo"
    if [[ -n $planck_snapshot_ref ]]; then
        git -C "$planck_path" update-ref -d "$planck_snapshot_ref" 2>/dev/null || true
    fi
    [[ -z $root_snapshot_bundle_file ]] || rm -f -- "$root_snapshot_bundle_file"
    if [[ -n $root_snapshot_ref ]]; then
        git update-ref -d "$root_snapshot_ref" 2>/dev/null || true
    fi
    [[ -z $planck_temp_index ]] || rm -f -- "$planck_temp_index"
    [[ -z $root_temp_index ]] || rm -f -- "$root_temp_index"
    [[ -z $patch_file ]] || rm -f -- "$patch_file"
    [[ -z $patch_verify_file ]] || rm -f -- "$patch_verify_file"
    retain_failed_guest_report
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
    printf 'STVR_CANDIDATE_COMMONLIB_COMMON_ANCESTOR=%s\n' "$commonlib_common_ancestor"
    printf 'STVR_CANDIDATE_COMMONLIB_TRUSTED_UPSTREAM=%s\n' "$commonlib_trusted_upstream_commit"
    printf 'STVR_CANDIDATE_COMMONLIB_BUNDLE_SHA256=%s\n' "$commonlib_bundle_sha256"
    printf 'STVR_CANDIDATE_COMMONLIB_VERIFICATION=%s\n' "$commonlib_verification_source"
    printf 'STVR_CANDIDATE_PLANCK_BASE=%s\n' "$planck_base_commit"
    printf 'STVR_CANDIDATE_PLANCK_SYNTHETIC_COMMIT=%s\n' "$planck_synthetic_commit"
    printf 'STVR_CANDIDATE_PLANCK_BUNDLE_SHA256=%s\n' "$planck_bundle_sha256"
    printf 'STVR_CANDIDATE_ROOT_SYNTHETIC_COMMIT=%s\n' "$root_synthetic_commit"
    printf 'STVR_CANDIDATE_ROOT_SYNTHETIC_TREE=%s\n' "$root_synthetic_tree"
    printf 'STVR_CANDIDATE_ROOT_SNAPSHOT_BUNDLE_SHA256=%s\n' "$root_snapshot_bundle_sha256"
    exit "$status"
}
trap cleanup_runtime EXIT

if [[ -n $(git ls-files --others --exclude-standard) ]]; then
    echo "Refusing candidate build with untracked files. Stage or remove them before creating the tracked delta snapshot." >&2
    exit 2
fi

while IFS=' ' read -r _ candidate_submodule_path; do
    candidate_submodule_status=$(git -C "$candidate_submodule_path" status --porcelain=v1 --untracked-files=all)
    if [[ -n $candidate_submodule_status ]]; then
        if [[ $candidate_submodule_path != "$planck_path" ]]; then
            echo "Refusing candidate build with a dirty submodule: $candidate_submodule_path" >&2
            exit 2
        fi
        planck_dirty=true
    fi
done < <(git config -f .gitmodules --get-regexp '^submodule\..*\.path$')

# PLANCK is the sole dirty submodule accepted by this candidate path.  Its
# complete tracked/untracked source state is converted to a local-only commit
# and bundle below; no real branch, index, or remote is modified.
if git submodule foreach --recursive '
    if test "$displaypath" != "Libraries/activeragdoll" && test -n "$(git status --porcelain=v1 --untracked-files=all)"; then
        echo "Dirty nested submodule: $displaypath" >&2
        exit 1
    fi
'; then :; else
    echo "Refusing candidate build with an unsupported dirty nested submodule." >&2
    exit 2
fi

base_commit=$(git rev-parse --verify HEAD^{commit})
commonlib_configured=false
planck_configured=false
while IFS=' ' read -r _ submodule_path; do
    if [[ $submodule_path == "$commonlib_path" ]]; then
        commonlib_configured=true
    fi
    if [[ $submodule_path == "$planck_path" ]]; then
        planck_configured=true
    fi
    if ! git diff --quiet HEAD -- "$submodule_path"; then
        if [[ $submodule_path != "$commonlib_path" && $submodule_path != "$planck_path" ]]; then
            echo "Refusing candidate build with a changed submodule pointer: $submodule_path" >&2
            exit 2
        fi
        if [[ $submodule_path == "$commonlib_path" ]]; then
            commonlib_gitlink_changed=true
        fi
    fi
done < <(git config -f .gitmodules --get-regexp '^submodule\..*\.path$')

if [[ $commonlib_configured != true ]]; then
    echo "Expected CommonLib submodule is not configured: $commonlib_path" >&2
    exit 2
fi
if [[ $planck_configured != true ]]; then
    echo "Expected PLANCK submodule is not configured: $planck_path" >&2
    exit 2
fi

planck_index_commit=$(git rev-parse --verify ":$planck_path")
planck_worktree_head=$(git -C "$planck_path" rev-parse --verify HEAD^{commit})
if planck_base_commit=$(git rev-parse --verify "$base_commit:$planck_path" 2>/dev/null); then
    if [[ $planck_index_commit != "$planck_base_commit" || $planck_worktree_head != "$planck_base_commit" ]]; then
        echo "PLANCK source state does not match the project-base gitlink; only a dirty snapshot rooted at the pinned gitlink is supported." >&2
        exit 2
    fi
else
    expected_planck_addition=":000000 160000 0000000000000000000000000000000000000000 $planck_index_commit A"$'\t'"$planck_path"
    actual_planck_addition=$(git diff --cached --raw --no-abbrev --no-renames "$base_commit" -- "$planck_path")
    if [[ $actual_planck_addition != "$expected_planck_addition" || $planck_worktree_head != "$planck_index_commit" ]]; then
        echo "New PLANCK submodule must be staged as a direct mode-160000 addition whose gitlink matches its checked-out HEAD." >&2
        exit 2
    fi
    planck_base_commit=$planck_index_commit
fi
if ! git -C "$planck_path" cat-file -e "$planck_base_commit^{commit}"; then
    echo "PLANCK pinned source commit is unavailable locally: $planck_base_commit" >&2
    exit 2
fi
planck_synthetic_commit=$planck_base_commit
if [[ $planck_dirty == true ]]; then
    planck_temp_index=$(mktemp "${TMPDIR:-/tmp}/stvr-planck-candidate-index-${base_commit:0:8}-XXXXXX")
    rm -f -- "$planck_temp_index"
    GIT_INDEX_FILE=$planck_temp_index git -C "$planck_path" read-tree "$planck_base_commit"
    GIT_INDEX_FILE=$planck_temp_index git -C "$planck_path" add -A
    planck_synthetic_tree=$(GIT_INDEX_FILE=$planck_temp_index git -C "$planck_path" write-tree)
    planck_synthetic_commit=$(GIT_AUTHOR_NAME='STVR Candidate Builder' GIT_AUTHOR_EMAIL='stvr-candidate@local.invalid' \
        GIT_COMMITTER_NAME='STVR Candidate Builder' GIT_COMMITTER_EMAIL='stvr-candidate@local.invalid' \
        git -C "$planck_path" commit-tree "$planck_synthetic_tree" -p "$planck_base_commit" -m 'STVR candidate PLANCK snapshot')
    if [[ ! $planck_synthetic_commit =~ ^[0-9a-fA-F]{40}$ ]]; then
        echo "Could not construct a local-only PLANCK synthetic commit." >&2
        exit 2
    fi
    # The synthetic snapshot must bootstrap an empty guest repository.  Keep
    # a temporary local ref solely to name the advertised bundle head, then
    # remove it in the exit trap; no branch, remote, or persistent ref changes.
    planck_snapshot_ref="refs/stvr/linux-candidate/planck-snapshot-${base_commit}"
    if git -C "$planck_path" show-ref --verify --quiet "$planck_snapshot_ref"; then
        echo "Refusing to reuse an existing temporary PLANCK snapshot ref: $planck_snapshot_ref" >&2
        exit 2
    fi
    git -C "$planck_path" update-ref "$planck_snapshot_ref" "$planck_synthetic_commit"
    planck_bundle_file=$(mktemp "${TMPDIR:-/tmp}/stvr-planck-candidate-${base_commit:0:8}-XXXXXX.bundle")
    git -C "$planck_path" bundle create "$planck_bundle_file" "$planck_snapshot_ref"
    planck_bundle_probe_repo=$(mktemp -d "${TMPDIR:-/tmp}/stvr-planck-candidate-probe-${base_commit:0:8}-XXXXXX")
    git init --bare --quiet "$planck_bundle_probe_repo"
    git -C "$planck_bundle_probe_repo" bundle verify "$planck_bundle_file" >/dev/null
    bundle_head_count=$(git -C "$planck_bundle_probe_repo" bundle list-heads "$planck_bundle_file" | awk 'END { print NR }')
    bundle_head_commit=$(git -C "$planck_bundle_probe_repo" bundle list-heads "$planck_bundle_file" | awk 'NR == 1 { print $1 }')
    bundle_head_ref=$(git -C "$planck_bundle_probe_repo" bundle list-heads "$planck_bundle_file" | awk 'NR == 1 { print $2 }')
    if [[ $bundle_head_count != 1 || $bundle_head_commit != "$planck_synthetic_commit" || $bundle_head_ref != "$planck_snapshot_ref" ]]; then
        echo "PLANCK synthetic bundle must advertise exactly the local-only synthetic commit." >&2
        exit 2
    fi
    git -C "$planck_bundle_probe_repo" fetch --no-tags --no-write-fetch-head "$planck_bundle_file" "$planck_snapshot_ref:refs/stvr/planck-bundle-probe"
    if [[ $(git -C "$planck_bundle_probe_repo" rev-parse --verify refs/stvr/planck-bundle-probe^{commit}) != "$planck_synthetic_commit" || \
          $(git -C "$planck_bundle_probe_repo" rev-parse --verify refs/stvr/planck-bundle-probe^) != "$planck_base_commit" ]]; then
        echo "Self-contained PLANCK synthetic bundle does not retain the pinned base history." >&2
        exit 2
    fi
    planck_bundle_sha256=$(sha256sum "$planck_bundle_file" | awk '{print $1}')
fi

commonlib_base_commit=$(git rev-parse --verify "$base_commit:$commonlib_path")
commonlib_target_commit=$commonlib_base_commit
if [[ $commonlib_gitlink_changed == true ]]; then
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
fi
if ! git -C "$commonlib_path" cat-file -e "$commonlib_base_commit^{commit}" || \
   ! git -C "$commonlib_path" cat-file -e "$commonlib_target_commit^{commit}"; then
    echo "Refusing CommonLib source state with a missing base or target commit object." >&2
    exit 2
fi
if ! git -C "$commonlib_path" merge-base --is-ancestor "$commonlib_base_commit" "$commonlib_target_commit"; then
    echo "Refusing CommonLib source state whose target is not descended from the exact project-base gitlink." >&2
    exit 2
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
commonlib_direct_status=$(git submodule status -- "$commonlib_path")
commonlib_status_prefix=${commonlib_direct_status:0:1}
commonlib_status_rest=${commonlib_direct_status:1}
commonlib_status_commit=${commonlib_status_rest%% *}
commonlib_status_path_and_suffix=${commonlib_status_rest#* }
commonlib_status_path=${commonlib_status_path_and_suffix%% *}
if [[ ( $commonlib_status_prefix != '+' && $commonlib_status_prefix != ' ' ) || \
      $commonlib_status_commit != "$commonlib_target_commit" || \
      $commonlib_status_path != "$commonlib_path" ]]; then
    echo "CommonLib worktree does not match the exact requested base/target source state." >&2
    exit 2
fi

prepare_commonlib_cache_path() {
    local cache_root cache_key git_dir
    git_dir=$(git -C "$commonlib_path" rev-parse --git-dir)
    if [[ $git_dir != /* ]]; then
        git_dir="$commonlib_path/$git_dir"
    fi
    commonlib_source_git_dir=$(cd "$git_dir" && pwd -P)
    cache_root="${XDG_STATE_HOME:-$HOME/.local/state}/skyrim-together-vr/commonlib-verification"
    cache_key=$(printf '%s\0' \
        "$commonlib_trusted_upstream_url" "$commonlib_source_git_dir" \
        "$commonlib_base_commit" "$commonlib_target_commit" | sha256sum | awk '{print $1}')
    commonlib_cache_file="$cache_root/${cache_key}.verified"
}

load_commonlib_verification_cache() {
    local key value mode cache_root
    local cache_schema='' cache_url='' cache_git_dir='' cache_base='' cache_target=''
    local cache_ancestor='' cache_upstream='' cache_base_available='' cache_target_available=''

    prepare_commonlib_cache_path
    cache_root=${commonlib_cache_file%/*}
    [[ -d $cache_root && ! -L $cache_root && -O $cache_root ]] || return 1
    mode=$(stat -c '%a' -- "$cache_root" 2>/dev/null) || return 1
    (( (8#$mode & 077) == 0 )) || return 1
    [[ -f $commonlib_cache_file && ! -L $commonlib_cache_file && -O $commonlib_cache_file ]] || return 1
    mode=$(stat -c '%a' -- "$commonlib_cache_file" 2>/dev/null) || return 1
    (( (8#$mode & 022) == 0 )) || return 1

    while IFS='=' read -r key value || [[ -n $key ]]; do
        case $key in
            schema) cache_schema=$value ;;
            upstream_url) cache_url=$value ;;
            source_git_dir) cache_git_dir=$value ;;
            base_commit) cache_base=$value ;;
            target_commit) cache_target=$value ;;
            common_ancestor) cache_ancestor=$value ;;
            trusted_upstream_commit) cache_upstream=$value ;;
            base_upstream_available) cache_base_available=$value ;;
            target_upstream_available) cache_target_available=$value ;;
            *) return 1 ;;
        esac
    done < "$commonlib_cache_file"

    [[ $cache_schema == stvr-commonlib-verification-v1 && \
           $cache_url == "$commonlib_trusted_upstream_url" && \
           $cache_git_dir == "$commonlib_source_git_dir" && \
           $cache_base == "$commonlib_base_commit" && \
           $cache_target == "$commonlib_target_commit" && \
           $cache_ancestor =~ ^[0-9a-fA-F]{40}$ && \
           $cache_upstream =~ ^[0-9a-fA-F]{40}$ && \
           ( $cache_base_available == true || $cache_base_available == false ) && \
           ( $cache_target_available == true || $cache_target_available == false ) ]] || return 1
    git -C "$commonlib_path" cat-file -e "$cache_ancestor^{commit}" || return 1
    git -C "$commonlib_path" merge-base --is-ancestor "$cache_ancestor" "$commonlib_base_commit" || return 1
    git -C "$commonlib_path" merge-base --is-ancestor "$cache_ancestor" "$commonlib_target_commit" || return 1

    commonlib_common_ancestor=$cache_ancestor
    commonlib_trusted_upstream_commit=$cache_upstream
    commonlib_base_upstream_available=$cache_base_available
    commonlib_target_upstream_available=$cache_target_available
    commonlib_verification_source="LOCAL_CACHE"
    commonlib_trusted_upstream_match_required=false
}

write_commonlib_verification_cache() {
    local cache_root temporary
    prepare_commonlib_cache_path
    cache_root=${commonlib_cache_file%/*}
    if ! (umask 077; mkdir -p -- "$cache_root" && chmod 700 -- "$cache_root"); then
        echo "Unable to create CommonLib verification cache; continuing without reuse: $cache_root" >&2
        return 0
    fi
    temporary=$(mktemp "$cache_root/.commonlib-verification-XXXXXX") || {
        echo "Unable to create CommonLib verification cache entry; continuing without reuse." >&2
        return 0
    }
    {
        printf 'schema=stvr-commonlib-verification-v1\n'
        printf 'upstream_url=%s\n' "$commonlib_trusted_upstream_url"
        printf 'source_git_dir=%s\n' "$commonlib_source_git_dir"
        printf 'base_commit=%s\n' "$commonlib_base_commit"
        printf 'target_commit=%s\n' "$commonlib_target_commit"
        printf 'common_ancestor=%s\n' "$commonlib_common_ancestor"
        printf 'trusted_upstream_commit=%s\n' "$commonlib_trusted_upstream_commit"
        printf 'base_upstream_available=%s\n' "$commonlib_base_upstream_available"
        printf 'target_upstream_available=%s\n' "$commonlib_target_upstream_available"
    } > "$temporary"
    chmod 600 -- "$temporary"
    mv -f -- "$temporary" "$commonlib_cache_file"
}

short_commit=${base_commit:0:8}

# Cleanup must run before this invocation creates its transfer artifacts.
# With --max-age-days 0, the temporary-artifact sweep intentionally removes
# every matching /tmp/stvr-* file, including a patch created moments earlier.
"$repo_root/Tools/SkyrimVR/cleanup_build_storage.sh" \
    --apply --max-age-days 0 --skip-local-artifacts --local-build-output --temp-artifacts

status_before=$(git status --porcelain=v1 --untracked-files=all)
commonlib_head_before=$(git -C "$commonlib_path" rev-parse --verify HEAD^{commit})
if load_commonlib_verification_cache; then
    printf 'Reusing locally verified CommonLib source state for %s -> %s.\n' \
        "$commonlib_base_commit" "$commonlib_target_commit"
else
    commonlib_verification_source="NETWORK"
    commonlib_trusted_upstream_match_required=true
    commonlib_probe_repo=$(mktemp -d "${TMPDIR:-/tmp}/stvr-winboat-commonlib-probe-${short_commit}-XXXXXX")
    git -C "$commonlib_probe_repo" init --bare --quiet
    probe_trusted_ref="refs/stvr/winboat-candidate/trusted-upstream"
    git -C "$commonlib_probe_repo" fetch --quiet --no-tags --no-write-fetch-head \
        "$commonlib_trusted_upstream_url" "+HEAD:$probe_trusted_ref"
    commonlib_trusted_upstream_commit=$(git -C "$commonlib_probe_repo" rev-parse --verify "$probe_trusted_ref^{commit}")
    if [[ ! $commonlib_trusted_upstream_commit =~ ^[0-9a-fA-F]{40}$ ]]; then
        echo "Trusted CommonLib upstream HEAD did not resolve to a full commit." >&2
        exit 2
    fi

    commonlib_trusted_ref="refs/stvr/winboat-candidate/trusted-upstream-${short_commit}-$$"
    if git -C "$commonlib_path" show-ref --verify --quiet "$commonlib_trusted_ref"; then
        echo "Temporary local CommonLib trusted-upstream ref already exists: $commonlib_trusted_ref" >&2
        exit 2
    fi
    git -C "$commonlib_path" fetch --quiet --no-tags --no-write-fetch-head \
        "$commonlib_trusted_upstream_url" "+HEAD:$commonlib_trusted_ref"
    if [[ $(git -C "$commonlib_path" rev-parse --verify "$commonlib_trusted_ref^{commit}") != "$commonlib_trusted_upstream_commit" ]]; then
        echo "Local CommonLib trusted-upstream ref does not match the isolated upstream probe." >&2
        exit 2
    fi
    if ! commonlib_common_ancestor=$(git -C "$commonlib_path" merge-base "$commonlib_base_commit" "$commonlib_trusted_ref"); then
        echo "Cannot derive a trusted-upstream ancestor for the exact CommonLib base gitlink." >&2
        exit 2
    fi
    if ! git -C "$commonlib_path" merge-base --is-ancestor "$commonlib_common_ancestor" "$commonlib_base_commit" || \
       ! git -C "$commonlib_path" merge-base --is-ancestor "$commonlib_common_ancestor" "$commonlib_trusted_ref"; then
        echo "Derived CommonLib bundle prerequisite failed its ancestry checks." >&2
        exit 2
    fi

    if git -C "$commonlib_probe_repo" fetch --quiet --no-tags --no-write-fetch-head \
        "$commonlib_trusted_upstream_url" "$commonlib_base_commit:refs/stvr/winboat-candidate/probe-base" \
        >/dev/null 2>&1 && \
       [[ $(git -C "$commonlib_probe_repo" rev-parse --verify 'refs/stvr/winboat-candidate/probe-base^{commit}') == "$commonlib_base_commit" ]]; then
        commonlib_base_upstream_available=true
    fi
    if [[ $commonlib_target_commit == "$commonlib_base_commit" ]]; then
        commonlib_target_upstream_available=$commonlib_base_upstream_available
    elif git -C "$commonlib_probe_repo" fetch --quiet --no-tags --no-write-fetch-head \
        "$commonlib_trusted_upstream_url" "$commonlib_target_commit:refs/stvr/winboat-candidate/probe-target" \
        >/dev/null 2>&1 && \
         [[ $(git -C "$commonlib_probe_repo" rev-parse --verify 'refs/stvr/winboat-candidate/probe-target^{commit}') == "$commonlib_target_commit" ]]; then
        commonlib_target_upstream_available=true
    fi
    write_commonlib_verification_cache
fi

if [[ $commonlib_base_upstream_available != true || $commonlib_target_upstream_available != true ]]; then
    commonlib_bundle_required=true
    commonlib_bundle_file=$(mktemp "${TMPDIR:-/tmp}/stvr-winboat-commonlib-${short_commit}-XXXXXX.bundle")
    if [[ $(git -C "$commonlib_path" rev-parse --verify HEAD^{commit}) != "$commonlib_target_commit" ]]; then
        echo "CommonLib HEAD changed before creating its candidate bundle; retry from a stable tree." >&2
        exit 2
    fi
    git -C "$commonlib_path" bundle create "$commonlib_bundle_file" \
        HEAD "^$commonlib_common_ancestor"
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
rm -rf -- "$commonlib_probe_repo"
commonlib_probe_repo=""
if [[ -n $commonlib_trusted_ref ]]; then
    git -C "$commonlib_path" update-ref -d "$commonlib_trusted_ref"
fi
commonlib_trusted_ref=""
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
if [[ $(git -C "$commonlib_path" rev-parse --verify HEAD^{commit}) != "$commonlib_target_commit" ]]; then
    echo "CommonLib HEAD changed while creating the candidate patch; retry from a stable tree." >&2
    exit 2
fi
if [[ $planck_dirty == true ]]; then
    planck_verify_index=$(mktemp "${TMPDIR:-/tmp}/stvr-planck-candidate-verify-index-${short_commit}-XXXXXX")
    rm -f -- "$planck_verify_index"
    GIT_INDEX_FILE=$planck_verify_index git -C "$planck_path" read-tree "$planck_base_commit"
    GIT_INDEX_FILE=$planck_verify_index git -C "$planck_path" add -A
    planck_verify_tree=$(GIT_INDEX_FILE=$planck_verify_index git -C "$planck_path" write-tree)
    rm -f -- "$planck_verify_index"
    if [[ $planck_verify_tree != "$planck_synthetic_tree" ]]; then
        echo "PLANCK source changed while creating the candidate synthetic snapshot; retry from a stable tree." >&2
        exit 2
    fi
fi

# Root synthetic snapshot: apply the exact root delta to an isolated index,
# pin its PLANCK gitlink to the local-only PLANCK commit, and retain the
# resulting commit object only in the local object database/bundle.
root_temp_index=$(mktemp "${TMPDIR:-/tmp}/stvr-root-candidate-index-${short_commit}-XXXXXX")
rm -f -- "$root_temp_index"
GIT_INDEX_FILE=$root_temp_index git read-tree "$base_commit"
GIT_INDEX_FILE=$root_temp_index git apply --cached --binary --whitespace=nowarn -- "$patch_file"
GIT_INDEX_FILE=$root_temp_index git update-index --add --cacheinfo "160000,$planck_synthetic_commit,$planck_path"
root_synthetic_tree=$(GIT_INDEX_FILE=$root_temp_index git write-tree)
root_synthetic_commit=$(GIT_AUTHOR_NAME='STVR Candidate Builder' GIT_AUTHOR_EMAIL='stvr-candidate@local.invalid' \
    GIT_COMMITTER_NAME='STVR Candidate Builder' GIT_COMMITTER_EMAIL='stvr-candidate@local.invalid' \
    git commit-tree "$root_synthetic_tree" -p "$base_commit" -m 'STVR candidate root synthetic snapshot')
if [[ ! $root_synthetic_commit =~ ^[0-9a-fA-F]{40}$ ]]; then
    echo "Could not construct the local-only root synthetic snapshot." >&2
    exit 2
fi

if ! local_origin_main=$(git rev-parse --verify refs/remotes/origin/main^{commit}); then
    echo "Cannot derive a WinBoat-transfer ancestor: local refs/remotes/origin/main is missing." >&2
    exit 2
fi
if ! candidate_common_ancestor=$(git merge-base "$base_commit" "$local_origin_main"); then
    echo "Cannot derive a common ancestor between the candidate base and local origin/main." >&2
    exit 2
fi
if ! git merge-base --is-ancestor "$candidate_common_ancestor" "$base_commit"; then
    echo "Derived candidate-transfer ancestor is not an ancestor of the candidate base." >&2
    exit 2
fi
if ! git merge-base --is-ancestor "$base_commit" "$local_origin_main"; then
    candidate_bundle_required=true
    candidate_bundle_file=$(mktemp "${TMPDIR:-/tmp}/stvr-winboat-base-${short_commit}-XXXXXX.bundle")
    if [[ $(git rev-parse --verify HEAD^{commit}) != "$base_commit" ]]; then
        echo "Linux HEAD changed before creating the candidate base bundle; retry from a stable tree." >&2
        exit 2
    fi
    git bundle create "$candidate_bundle_file" HEAD "^$candidate_common_ancestor"
    git bundle verify "$candidate_bundle_file" >/dev/null
    bundle_head_count=$(git bundle list-heads "$candidate_bundle_file" | awk 'END { print NR }')
    bundle_head_commit=$(git bundle list-heads "$candidate_bundle_file" | awk 'NR == 1 { print $1 }')
    bundle_head_ref=$(git bundle list-heads "$candidate_bundle_file" | awk 'NR == 1 { print $2 }')
    if [[ $bundle_head_count != 1 || $bundle_head_commit != "$base_commit" || $bundle_head_ref != HEAD ]]; then
        echo "Candidate base bundle does not contain exactly the requested base commit." >&2
        exit 2
    fi
    candidate_bundle_sha256=$(sha256sum "$candidate_bundle_file" | awk '{print $1}')
fi

root_snapshot_ref="refs/stvr/linux-candidate/root-snapshot-${base_commit}"
if git show-ref --verify --quiet "$root_snapshot_ref"; then
    echo "Refusing to reuse an existing temporary root synthetic snapshot ref: $root_snapshot_ref" >&2
    exit 2
fi
git update-ref "$root_snapshot_ref" "$root_synthetic_commit"
root_snapshot_bundle_file=$(mktemp "${TMPDIR:-/tmp}/stvr-root-candidate-${short_commit}-XXXXXX.bundle")
git bundle create "$root_snapshot_bundle_file" "$root_snapshot_ref" "^$candidate_common_ancestor"
git bundle verify "$root_snapshot_bundle_file" >/dev/null
bundle_head_count=$(git bundle list-heads "$root_snapshot_bundle_file" | awk 'END { print NR }')
bundle_head_commit=$(git bundle list-heads "$root_snapshot_bundle_file" | awk 'NR == 1 { print $1 }')
bundle_head_ref=$(git bundle list-heads "$root_snapshot_bundle_file" | awk 'NR == 1 { print $2 }')
if [[ $bundle_head_count != 1 || $bundle_head_commit != "$root_synthetic_commit" || $bundle_head_ref != "$root_snapshot_ref" ]]; then
    echo "Root synthetic snapshot bundle must advertise exactly the local-only root commit." >&2
    exit 2
fi
root_snapshot_bundle_sha256=$(sha256sum "$root_snapshot_bundle_file" | awk '{print $1}')

if [[ ! -s $patch_file && $planck_dirty != true ]]; then
    echo "No tracked working-tree delta or dirty PLANCK snapshot exists. Use the normal clean WinBoat build instead." >&2
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

guest_havok_archive=""
guest_planck_dependency_root=""
reported_havok_sha256=""
reported_sksevr_sha256=""
provision_output=$("$repo_root/Tools/SkyrimVR/provision_winboat_planck_dependencies.sh" \
    --winboat-powershell "$winboat_powershell" --winboat-scp "$winboat_scp")
while IFS='=' read -r key value; do
    case $key in
        STVR_GUEST_HAVOK_ARCHIVE) guest_havok_archive=$value ;;
        STVR_GUEST_PLANCK_DEPENDENCY_ROOT) guest_planck_dependency_root=$value ;;
        STVR_HAVOK_ARCHIVE_SHA256) reported_havok_sha256=$value ;;
        STVR_SKSEVR_SOURCE_SHA256) reported_sksevr_sha256=$value ;;
    esac
done <<<"$provision_output"
if [[ -z $guest_havok_archive || -z $guest_planck_dependency_root || \
      $reported_havok_sha256 != 7349946401a820784fc86aa13bc667def6c409ed938865b01c8e6c3d86692555 || \
      $reported_sksevr_sha256 != edbb4945544718054279c9f949ac689e735b13c8efcd3272b6f74e2398dd5d53 ]]; then
    echo "PLANCK dependency provisioner did not return the pinned durable guest provenance." >&2
    exit 2
fi

winboat_repo=${STVR_WINBOAT_REPO:-'C:\Users\obesecatlord\Documents\Codex\SkyrimTogetherVR'}
timestamp=$(date -u +%Y%m%d%H%M%SZ)
winboat_build="${winboat_repo}-candidate-${short_commit}-${timestamp}"
guest_result="${winboat_repo}-candidate-results\\${short_commit}-${timestamp}"
guest_patch="C:/Users/obesecatlord/AppData/Local/Temp/stvr-winboat-candidate-${short_commit}-${timestamp}.patch"
guest_payload="C:/Users/obesecatlord/AppData/Local/Temp/stvr-winboat-candidate-${short_commit}-${timestamp}.ps1"
guest_trusted_remote_ref="refs/stvr/winboat-candidate/trusted-main-${short_commit}-${timestamp}"
guest_root_snapshot_transfer_ref="refs/stvr/winboat-candidate/root-snapshot-${short_commit}-${timestamp}"
guest_commonlib_transfer_ref="refs/stvr/winboat-candidate/commonlib-target-${short_commit}-${timestamp}"
guest_commonlib_trusted_ref="refs/stvr/winboat-candidate/commonlib-upstream-${short_commit}-${timestamp}"
guest_planck_transfer_ref="refs/stvr/winboat-candidate/planck-snapshot-${short_commit}-${timestamp}"
guest_planck_bundle="C:/Users/obesecatlord/AppData/Local/Temp/stvr-planck-candidate-${short_commit}-${timestamp}.bundle"
guest_root_snapshot_bundle="C:/Users/obesecatlord/AppData/Local/Temp/stvr-root-candidate-${short_commit}-${timestamp}.bundle"
if [[ $candidate_bundle_required == true ]]; then
    guest_candidate_bundle="C:/Users/obesecatlord/AppData/Local/Temp/stvr-winboat-base-${short_commit}-${timestamp}.bundle"
    guest_candidate_transfer_ref="refs/stvr/winboat-candidate/base-${short_commit}-${timestamp}"
fi
if [[ $commonlib_bundle_required == true ]]; then
    guest_commonlib_bundle="C:/Users/obesecatlord/AppData/Local/Temp/stvr-winboat-commonlib-${short_commit}-${timestamp}.bundle"
fi

for value in "$winboat_repo" "$winboat_build" "$guest_result" "$guest_patch" "$guest_payload" "$guest_commonlib_bundle" "$guest_commonlib_transfer_ref" "$guest_commonlib_trusted_ref" "$guest_candidate_bundle" "$guest_candidate_transfer_ref" "$guest_trusted_remote_ref" "$guest_planck_bundle" "$guest_planck_transfer_ref" "$guest_root_snapshot_bundle" "$guest_havok_archive" "$guest_planck_dependency_root"; do
    if [[ $value == *"'"* ]]; then
        echo "WinBoat paths containing a single quote are not supported." >&2
        exit 2
    fi
done

printf 'STVR_CANDIDATE_BASE=%s\n' "$base_commit"
printf 'STVR_CANDIDATE_COMMON_ANCESTOR=%s\n' "$candidate_common_ancestor"
printf 'STVR_CANDIDATE_BUNDLE_SHA256=%s\n' "$candidate_bundle_sha256"
printf 'STVR_CANDIDATE_PATCH_SHA256=%s\n' "$patch_sha256"
printf 'STVR_CANDIDATE_COMMONLIB_BASE=%s\n' "$commonlib_base_commit"
printf 'STVR_CANDIDATE_COMMONLIB_TARGET=%s\n' "$commonlib_target_commit"
printf 'STVR_CANDIDATE_COMMONLIB_COMMON_ANCESTOR=%s\n' "$commonlib_common_ancestor"
printf 'STVR_CANDIDATE_COMMONLIB_TRUSTED_UPSTREAM=%s\n' "$commonlib_trusted_upstream_commit"
printf 'STVR_CANDIDATE_COMMONLIB_BUNDLE_SHA256=%s\n' "$commonlib_bundle_sha256"
printf 'STVR_CANDIDATE_COMMONLIB_VERIFICATION=%s\n' "$commonlib_verification_source"
printf 'STVR_CANDIDATE_PLANCK_BASE=%s\n' "$planck_base_commit"
printf 'STVR_CANDIDATE_PLANCK_SYNTHETIC_COMMIT=%s\n' "$planck_synthetic_commit"
printf 'STVR_CANDIDATE_PLANCK_BUNDLE_SHA256=%s\n' "$planck_bundle_sha256"
printf 'STVR_CANDIDATE_ROOT_SYNTHETIC_COMMIT=%s\n' "$root_synthetic_commit"
printf 'STVR_CANDIDATE_ROOT_SYNTHETIC_TREE=%s\n' "$root_synthetic_tree"
printf 'STVR_CANDIDATE_ROOT_SNAPSHOT_BUNDLE_SHA256=%s\n' "$root_snapshot_bundle_sha256"

read -r -d '' powershell_payload <<'POWERSHELL' || true
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
$repo = '__WINBOAT_REPO__'
$build = '__WINBOAT_BUILD__'
$patch = '__GUEST_PATCH__'
$baseCommit = '__BASE_COMMIT__'
$commonAncestor = '__CANDIDATE_COMMON_ANCESTOR__'
$hasCandidateBundle = [System.Convert]::ToBoolean('__HAS_CANDIDATE_BUNDLE__')
$candidateBundle = '__GUEST_CANDIDATE_BUNDLE__'
$candidateBundleSha256 = '__CANDIDATE_BUNDLE_SHA256__'
$candidateTransferRef = '__GUEST_CANDIDATE_TRANSFER_REF__'
$trustedRemoteRef = '__GUEST_TRUSTED_REMOTE_REF__'
$patchSha256 = '__PATCH_SHA256__'
$planckPath = 'Libraries/activeragdoll'
$planckBase = '__PLANCK_BASE__'
$planckSyntheticCommit = '__PLANCK_SYNTHETIC_COMMIT__'
$planckBundle = '__GUEST_PLANCK_BUNDLE__'
$planckBundleSha256 = '__PLANCK_BUNDLE_SHA256__'
$planckBundleRef = '__PLANCK_BUNDLE_REF__'
$planckTransferRef = '__GUEST_PLANCK_TRANSFER_REF__'
$rootSyntheticCommit = '__ROOT_SYNTHETIC_COMMIT__'
$rootSyntheticTree = '__ROOT_SYNTHETIC_TREE__'
$rootSnapshotBundle = '__GUEST_ROOT_SNAPSHOT_BUNDLE__'
$rootSnapshotBundleSha256 = '__ROOT_SNAPSHOT_BUNDLE_SHA256__'
$rootSnapshotBundleRef = '__ROOT_SNAPSHOT_BUNDLE_REF__'
$rootSnapshotTransferRef = '__ROOT_SNAPSHOT_TRANSFER_REF__'
$hasCommonLibGitlink = [System.Convert]::ToBoolean('__HAS_COMMONLIB_GITLINK__')
$hasCommonLibBundle = [System.Convert]::ToBoolean('__HAS_COMMONLIB_BUNDLE__')
$commonLibPath = 'Libraries/CommonLibSSE-NG'
$commonLibBase = '__COMMONLIB_BASE__'
$commonLibTarget = '__COMMONLIB_TARGET__'
$commonLibCommonAncestor = '__COMMONLIB_COMMON_ANCESTOR__'
$commonLibTrustedUpstream = '__COMMONLIB_TRUSTED_UPSTREAM__'
$commonLibTrustedUpstreamRequired = [System.Convert]::ToBoolean('__COMMONLIB_TRUSTED_UPSTREAM_REQUIRED__')
$commonLibTrustedUpstreamUrl = '__COMMONLIB_TRUSTED_UPSTREAM_URL__'
$commonLibBundle = '__GUEST_COMMONLIB_BUNDLE__'
$commonLibBundleSha256 = '__COMMONLIB_BUNDLE_SHA256__'
$commonLibTransferRef = '__GUEST_COMMONLIB_TRANSFER_REF__'
$commonLibTrustedRef = '__GUEST_COMMONLIB_TRUSTED_REF__'
$result = '__GUEST_RESULT__'
$havokArchive = '__GUEST_HAVOK_ARCHIVE__'
$planckDependencyRoot = '__GUEST_PLANCK_DEPENDENCY_ROOT__'
$worktreeCreated = $false
$candidateRevision = "NOT_CREATED"
$buildSucceeded = $false
$trustedRemoteCommit = "NOT_VERIFIED"
$commonLibWorktree = ""
$planckWorktree = ""

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

function Invoke-GitStatusProbe {
    param([string[]]$Arguments)

    $savedErrorActionPreference = $ErrorActionPreference
    $hadNativeErrorPreference = Test-Path Variable:PSNativeCommandUseErrorActionPreference
    if ($hadNativeErrorPreference) {
        $savedNativeErrorPreference = $PSNativeCommandUseErrorActionPreference
    }
    try {
        $ErrorActionPreference = "Continue"
        $PSNativeCommandUseErrorActionPreference = $false
        & git @Arguments 2>$null | Out-Null
        $probeExitCode = $LASTEXITCODE
    } catch {
        if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
            $probeExitCode = $LASTEXITCODE
        } else {
            $probeExitCode = 1
        }
    } finally {
        $ErrorActionPreference = $savedErrorActionPreference
        if ($hadNativeErrorPreference) {
            $PSNativeCommandUseErrorActionPreference = $savedNativeErrorPreference
        } else {
            Remove-Variable -Name PSNativeCommandUseErrorActionPreference -Scope Local -ErrorAction SilentlyContinue
        }
    }
    return [int]$probeExitCode
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

    $removeStatus = Invoke-GitStatusProbe -Arguments @('-C', $repo, 'worktree', 'remove', '--force', $Path)
    if ($removeStatus -ne 0 -and (Test-Path -LiteralPath $Path)) {
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
    $candidateCutoff = (Get-Date).ToUniversalTime().AddDays(-2)
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
        if ($directory.LastWriteTimeUtc -gt $candidateCutoff) { continue }
        Remove-CandidateWorktree -Path $directory.FullName -FailIfActive $true
    }
    git -C $repo worktree prune
    if ($LASTEXITCODE -ne 0) { throw "Could not prune stale WinBoat candidate worktree metadata." }
}

try {
    git -C $repo fetch --no-tags --no-write-fetch-head origin "+refs/heads/main:$trustedRemoteRef"
    if ($LASTEXITCODE -ne 0) { throw "Could not fetch origin/main into the temporary trusted WinBoat ref." }
    $trustedRemoteCommit = (git -C $repo rev-parse "$trustedRemoteRef^{commit}").Trim()
    if ($LASTEXITCODE -ne 0 -or $trustedRemoteCommit -notmatch '^[0-9a-fA-F]{40}$') {
        throw "Temporary trusted WinBoat remote ref did not resolve to a full commit."
    }
    git -C $repo cat-file -e "$commonAncestor^{commit}"
    if ($LASTEXITCODE -ne 0) { throw "Candidate common ancestor is not available after fetching the trusted WinBoat remote ref." }
    git -C $repo merge-base --is-ancestor $commonAncestor $trustedRemoteRef
    if ($LASTEXITCODE -ne 0) {
        throw "Candidate common ancestor $commonAncestor is not an ancestor of the trusted WinBoat remote ref."
    }

    if ($hasCandidateBundle) {
        if ([string]::IsNullOrWhiteSpace($candidateBundle) -or [string]::IsNullOrWhiteSpace($candidateTransferRef)) {
            throw "Candidate base bundle transfer paths are missing."
        }
        if (-not (Test-Path -LiteralPath $candidateBundle)) { throw "Transferred candidate base bundle is missing." }
        $actualCandidateBundleSha256 = (Get-FileHash -LiteralPath $candidateBundle -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualCandidateBundleSha256 -ne $candidateBundleSha256) {
            throw "Transferred candidate base bundle SHA-256 does not match the Linux snapshot."
        }
        git -C $repo bundle verify $candidateBundle
        if ($LASTEXITCODE -ne 0) { throw "Candidate base bundle prerequisites are not satisfied by the trusted WinBoat remote ref." }
        $candidateBundleHeads = @(git -C $repo bundle list-heads $candidateBundle)
        if ($LASTEXITCODE -ne 0 -or $candidateBundleHeads.Count -ne 1) {
            throw "Candidate base bundle does not have exactly one advertised base commit."
        }
        $candidateBundleHeadParts = @($candidateBundleHeads[0] -split '\s+')
        if ($candidateBundleHeadParts.Count -ne 2 -or $candidateBundleHeadParts[0].Trim() -ne $baseCommit -or $candidateBundleHeadParts[1].Trim() -ne 'HEAD') {
            throw "Candidate base bundle advertised commit does not match the requested base."
        }
        git -C $repo fetch --no-tags --no-write-fetch-head $candidateBundle "HEAD:$candidateTransferRef"
        if ($LASTEXITCODE -ne 0) { throw "Could not fetch the verified candidate base bundle into its temporary WinBoat ref." }
        $transferredBase = (git -C $repo rev-parse "$candidateTransferRef^{commit}").Trim()
        if ($LASTEXITCODE -ne 0 -or $transferredBase -ne $baseCommit) {
            throw "Temporary WinBoat candidate base ref does not match the requested base commit."
        }
        git -C $repo cat-file -e "$baseCommit^{commit}"
        if ($LASTEXITCODE -ne 0) { throw "Transferred candidate base commit object is unavailable in WinBoat." }
        git -C $repo merge-base --is-ancestor $commonAncestor $baseCommit
        if ($LASTEXITCODE -ne 0) { throw "Transferred candidate base is not descended from the verified common ancestor." }
    } else {
        git -C $repo cat-file -e "$baseCommit^{commit}"
        if ($LASTEXITCODE -ne 0) { throw "Candidate base commit is unavailable after fetching the trusted WinBoat remote ref." }
        git -C $repo merge-base --is-ancestor $baseCommit $trustedRemoteRef
        if ($LASTEXITCODE -ne 0) { throw "Candidate base is not an ancestor of the trusted WinBoat remote ref." }
    }

    if (-not (Test-Path -LiteralPath $rootSnapshotBundle)) { throw "Transferred root synthetic snapshot bundle is missing." }
    if ((Get-FileHash -LiteralPath $rootSnapshotBundle -Algorithm SHA256).Hash.ToLowerInvariant() -ne $rootSnapshotBundleSha256) {
        throw "Transferred root synthetic snapshot bundle SHA-256 does not match Linux provenance."
    }
    git -C $repo bundle verify $rootSnapshotBundle
    if ($LASTEXITCODE -ne 0) { throw "Root synthetic snapshot bundle prerequisites are not satisfied by trusted WinBoat history." }
    $rootBundleHeads = @(git -C $repo bundle list-heads $rootSnapshotBundle)
    if ($LASTEXITCODE -ne 0 -or $rootBundleHeads.Count -ne 1) {
        throw "Root synthetic snapshot bundle does not advertise exactly one expected commit."
    }
    $rootBundleHeadParts = @($rootBundleHeads[0] -split '\s+')
    if ($rootBundleHeadParts.Count -ne 2 -or
        $rootBundleHeadParts[0].Trim() -ne $rootSyntheticCommit -or $rootBundleHeadParts[1].Trim() -ne $rootSnapshotBundleRef) {
        throw "Root synthetic snapshot bundle does not advertise the exact expected commit."
    }
    git -C $repo fetch --no-tags --no-write-fetch-head $rootSnapshotBundle "${rootSnapshotBundleRef}:$rootSnapshotTransferRef"
    if ($LASTEXITCODE -ne 0 -or (git -C $repo rev-parse "$rootSnapshotTransferRef^{commit}").Trim() -ne $rootSyntheticCommit) {
        throw "Could not import the exact root synthetic snapshot commit."
    }

    Remove-StaleCandidateWorktrees
    if (Test-Path -LiteralPath $build) { throw "Fresh candidate worktree already exists: $build" }
    git -C $repo worktree add --detach $build $baseCommit
    if ($LASTEXITCODE -ne 0) { throw "Could not create detached WinBoat candidate worktree." }
    $worktreeCreated = $true

    Set-Location $build
    git submodule sync -- $commonLibPath
    if ($LASTEXITCODE -ne 0) { throw "Could not synchronize the CommonLib submodule URL." }
    $earlyCommonLibUpdateStatus = Invoke-GitStatusProbe -Arguments @(
        'submodule', 'update', '--init', '--no-fetch', '--checkout', '--', $commonLibPath
    )
    $commonLibWorktree = Join-Path $build $commonLibPath
    git -C $commonLibWorktree rev-parse --git-dir | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Could not initialize the CommonLib repository before recursive submodule checkout." }
    if ($earlyCommonLibUpdateStatus -ne 0) {
        $baseObjectProbeStatus = Invoke-GitStatusProbe -Arguments @(
            '-C', $commonLibWorktree, 'cat-file', '-e', "$commonLibBase^{commit}"
        )
        if ($baseObjectProbeStatus -eq 0) {
            throw "Early CommonLib update failed even though the exact base object was already present."
        }
    }

    git -C $commonLibWorktree fetch --no-tags --no-write-fetch-head $commonLibTrustedUpstreamUrl "+HEAD:$commonLibTrustedRef"
    if ($LASTEXITCODE -ne 0) { throw "Could not fetch trusted CommonLib upstream HEAD into its temporary ref." }
    $guestCommonLibTrustedUpstream = (git -C $commonLibWorktree rev-parse "$commonLibTrustedRef^{commit}").Trim()
    if ($LASTEXITCODE -ne 0 -or $guestCommonLibTrustedUpstream -notmatch '^[0-9a-fA-F]{40}$') {
        throw "Guest CommonLib trusted-upstream ref did not resolve to a full commit."
    }
    if ($commonLibTrustedUpstreamRequired -and $guestCommonLibTrustedUpstream -ne $commonLibTrustedUpstream) {
        throw "Guest CommonLib trusted-upstream ref does not match the Linux probe."
    }
    git -C $commonLibWorktree cat-file -e "$commonLibCommonAncestor^{commit}"
    if ($LASTEXITCODE -ne 0) { throw "CommonLib bundle prerequisite is missing from the trusted guest upstream checkout." }
    git -C $commonLibWorktree merge-base --is-ancestor $commonLibCommonAncestor $commonLibTrustedRef
    if ($LASTEXITCODE -ne 0) { throw "CommonLib bundle prerequisite is not descended from trusted guest upstream HEAD." }

    if ($hasCommonLibBundle) {
        if ([string]::IsNullOrWhiteSpace($commonLibBundle) -or -not (Test-Path -LiteralPath $commonLibBundle)) {
            throw "Transferred CommonLib bundle is missing."
        }
        $actualCommonLibBundleSha256 = (Get-FileHash -LiteralPath $commonLibBundle -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualCommonLibBundleSha256 -ne $commonLibBundleSha256) {
            throw "Transferred CommonLib bundle SHA-256 does not match the Linux snapshot."
        }
        git -C $commonLibWorktree bundle verify $commonLibBundle
        if ($LASTEXITCODE -ne 0) { throw "CommonLib bundle prerequisites are not satisfied by trusted upstream HEAD." }
        $bundleHeads = @(git -C $commonLibWorktree bundle list-heads $commonLibBundle)
        if ($LASTEXITCODE -ne 0 -or $bundleHeads.Count -ne 1) {
            throw "CommonLib bundle does not have exactly one advertised target."
        }
        $bundleHeadParts = @($bundleHeads[0] -split '\s+')
        if ($bundleHeadParts.Count -ne 2 -or $bundleHeadParts[0].Trim() -ne $commonLibTarget -or $bundleHeadParts[1].Trim() -ne 'HEAD') {
            throw "CommonLib bundle target does not match the requested gitlink target."
        }
        git -C $commonLibWorktree fetch --no-tags --no-write-fetch-head $commonLibBundle "HEAD:$commonLibTransferRef"
        if ($LASTEXITCODE -ne 0) { throw "Could not fetch the verified CommonLib bundle into its temporary ref." }
    } else {
        if ($commonLibBundleSha256 -ne 'NOT_REQUIRED' -or -not [string]::IsNullOrWhiteSpace($commonLibBundle)) {
            throw "CommonLib no-bundle provenance is inconsistent."
        }
        git -C $commonLibWorktree fetch --no-tags --no-write-fetch-head $commonLibTrustedUpstreamUrl "${commonLibTarget}:$commonLibTransferRef"
        if ($LASTEXITCODE -ne 0) { throw "Trusted CommonLib upstream could not provide the exact requested target." }
    }

    $importedCommonLibTarget = (git -C $commonLibWorktree rev-parse "$commonLibTransferRef^{commit}").Trim()
    if ($LASTEXITCODE -ne 0 -or $importedCommonLibTarget -ne $commonLibTarget) {
        throw "Temporary CommonLib transfer ref does not match the exact requested target."
    }
    git -C $commonLibWorktree cat-file -e "$commonLibBase^{commit}"
    if ($LASTEXITCODE -ne 0) { throw "Exact CommonLib base object is unavailable after the verified transfer." }
    git -C $commonLibWorktree merge-base --is-ancestor $commonLibCommonAncestor $commonLibBase
    if ($LASTEXITCODE -ne 0) { throw "Exact CommonLib base is not descended from the verified upstream prerequisite." }
    git -C $commonLibWorktree merge-base --is-ancestor $commonLibBase $commonLibTarget
    if ($LASTEXITCODE -ne 0) { throw "Exact CommonLib target is not descended from the project-base gitlink." }
    git -C $commonLibWorktree checkout --detach $commonLibBase
    if ($LASTEXITCODE -ne 0) { throw "Could not establish the exact CommonLib base checkout before applying the patch." }
    $initialCommonLibHead = (git -C $commonLibWorktree rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $initialCommonLibHead -ne $commonLibBase) {
        throw "Candidate CommonLib source state does not match the exact project-base gitlink."
    }

    git submodule sync --recursive
    if ($LASTEXITCODE -ne 0) { throw "Could not synchronize submodule URLs." }
    git submodule update --init --recursive --checkout
    if ($LASTEXITCODE -ne 0) { throw "Could not initialize pinned submodules after preparing CommonLib." }

    $clean = @(git status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0 -or $clean.Count -ne 0) { throw "Fresh WinBoat candidate worktree is unexpectedly dirty." }
    $submoduleState = @(git submodule status --recursive)
    $unexpectedSubmoduleState = @($submoduleState | Where-Object {
        $_ -match '^[\-U]' -or ($_ -match '^\+' -and ($planckBundleSha256 -eq 'NOT_REQUIRED' -or $_ -notmatch [regex]::Escape($planckPath)))
    })
    if ($LASTEXITCODE -ne 0 -or $unexpectedSubmoduleState.Count -ne 0) {
        throw "Fresh WinBoat candidate worktree has an unresolved submodule."
    }

    # Establish the root delta before touching PLANCK: base HEAD may not yet
    # contain the new gitlink or a repository at its path.
    $actualPatchSha256 = (Get-FileHash -LiteralPath $patch -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualPatchSha256 -ne $patchSha256) { throw "Transferred candidate patch SHA-256 does not match the Linux snapshot." }
    if ((Get-Item -LiteralPath $patch).Length -gt 0) {
        git apply --index --binary --whitespace=nowarn -- $patch
        if ($LASTEXITCODE -ne 0) { throw "Could not apply the Linux candidate patch to the detached WinBoat worktree." }
        $staged = @(git diff --cached --name-only)
        if ($LASTEXITCODE -ne 0 -or $staged.Count -eq 0) { throw "Candidate patch did not stage any tracked changes." }
    }
    $planckWorktree = Join-Path $build $planckPath
    if ($planckBundleSha256 -ne 'NOT_REQUIRED') {
        $stagedPlanckGitlink = (git rev-parse ":$planckPath").Trim()
        if ($LASTEXITCODE -ne 0 -or $stagedPlanckGitlink -ne $planckBase) {
            throw "Candidate patch did not stage the pinned PLANCK source gitlink before synthetic materialization."
        }
    }
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
    if ($planckBundleSha256 -ne 'NOT_REQUIRED') {
        if ([string]::IsNullOrWhiteSpace($planckBundleRef) -or [string]::IsNullOrWhiteSpace($planckTransferRef) -or
            -not (Test-Path -LiteralPath $planckBundle) -or
            (Get-FileHash -LiteralPath $planckBundle -Algorithm SHA256).Hash.ToLowerInvariant() -ne $planckBundleSha256) {
            throw "Transferred self-contained PLANCK synthetic bundle provenance is incomplete or has an unexpected SHA-256."
        }
        if (Test-Path -LiteralPath $planckWorktree) {
            $existingPlanckRepository = Invoke-GitStatusProbe -Arguments @('-C', $planckWorktree, 'rev-parse', '--git-dir')
            if ($existingPlanckRepository -ne 0) {
                throw "PLANCK path exists after applying the root patch but is not a Git repository: $planckWorktree"
            }
        } else {
            New-Item -ItemType Directory -Path $planckWorktree -Force | Out-Null
            git -C $planckWorktree init --quiet
            if ($LASTEXITCODE -ne 0) { throw "Could not create the local PLANCK repository for the verified synthetic bundle." }
        }
        git -C $planckWorktree bundle verify $planckBundle
        if ($LASTEXITCODE -ne 0) { throw "PLANCK synthetic bundle is not self-contained for an empty guest repository." }
        $planckHeads = @(git -C $planckWorktree bundle list-heads $planckBundle)
        if ($LASTEXITCODE -ne 0 -or $planckHeads.Count -ne 1) {
            throw "PLANCK synthetic bundle does not advertise exactly one required local-only commit."
        }
        $planckHeadParts = @($planckHeads[0] -split '\s+')
        if ($planckHeadParts.Count -ne 2 -or $planckHeadParts[0].Trim() -ne $planckSyntheticCommit -or
            $planckHeadParts[1].Trim() -ne $planckBundleRef) {
            throw "PLANCK synthetic bundle does not advertise exactly the required local-only commit."
        }
        git -C $planckWorktree fetch --no-tags --no-write-fetch-head $planckBundle "${planckBundleRef}:$planckTransferRef"
        if ($LASTEXITCODE -ne 0 -or (git -C $planckWorktree rev-parse "$planckTransferRef^{commit}").Trim() -ne $planckSyntheticCommit) {
            throw "Could not import the exact verified PLANCK synthetic commit."
        }
        git -C $planckWorktree cat-file -e "$planckBase^{commit}"
        if ($LASTEXITCODE -ne 0 -or (git -C $planckWorktree rev-parse "$planckSyntheticCommit^").Trim() -ne $planckBase) {
            throw "Verified PLANCK bundle does not retain the pinned base history."
        }
        git -C $planckWorktree checkout --detach $planckSyntheticCommit
        if ($LASTEXITCODE -ne 0) { throw "Could not checkout the exact PLANCK synthetic snapshot." }
    }
    $submoduleState = @(git submodule status --recursive)
    $unexpectedSubmoduleState = @($submoduleState | Where-Object {
        $_ -match '^[\-U]' -or ($_ -match '^\+' -and ($planckBundleSha256 -eq 'NOT_REQUIRED' -or $_ -notmatch [regex]::Escape($planckPath)))
    })
    if ($LASTEXITCODE -ne 0 -or $unexpectedSubmoduleState.Count -ne 0) {
        throw "Candidate patch left an unresolved or mismatched submodule state."
    }

    # Build from the imported root synthetic snapshot, not a guest-created
    # approximation.  This preserves the local PLANCK gitlink and auditable
    # source identity without touching any real branch.
    git checkout --detach $rootSyntheticCommit
    if ($LASTEXITCODE -ne 0) { throw "Could not checkout the exact root synthetic snapshot." }
    $candidateRevision = (git rev-parse HEAD).Trim()
    if ($candidateRevision -ne $rootSyntheticCommit -or (git rev-parse HEAD^{tree}).Trim() -ne $rootSyntheticTree) {
        throw "Candidate worktree is not at the exact imported root synthetic snapshot."
    }
    $rootPlanckGitlink = (git rev-parse ":$planckPath").Trim()
    if ($LASTEXITCODE -ne 0 -or $rootPlanckGitlink -ne $planckSyntheticCommit) {
        throw "Root synthetic snapshot does not pin the required PLANCK synthetic gitlink."
    }
    $finalPlanckHead = (git -C $planckWorktree rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $finalPlanckHead -ne $planckSyntheticCommit) {
        throw "Candidate worktree PLANCK checkout does not match the root synthetic snapshot gitlink."
    }
    $clean = @(git status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0 -or $clean.Count -ne 0) { throw "Ephemeral WinBoat candidate commit is unexpectedly dirty before the audited build." }

    $env:Path = "C:\Users\obesecatlord\AppData\Local\Microsoft\WinGet\Links;$env:Path"
    & .\BuildCompleteSkyrimTogetherVR-Windows.ps1 -HavokArchive $havokArchive -DependencyRoot $planckDependencyRoot -Configuration Release
    if ($LASTEXITCODE -ne 0) { throw "Complete patched-PLANCK gameplay candidate build failed with exit code $LASTEXITCODE." }

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
        "commonAncestor=$commonAncestor"
        "trustedRemoteCommit=$trustedRemoteCommit"
        "candidateBundleSha256=$candidateBundleSha256"
        "candidateRevision=$candidateRevision"
        "patchSha256=$patchSha256"
        "commonLibBase=$commonLibBase"
        "commonLibTarget=$commonLibTarget"
        "commonLibCommonAncestor=$commonLibCommonAncestor"
        "commonLibTrustedUpstream=$commonLibTrustedUpstream"
        "commonLibBundleSha256=$commonLibBundleSha256"
        "planckBase=$planckBase"
        "planckSyntheticCommit=$planckSyntheticCommit"
        "planckBundleSha256=$planckBundleSha256"
        "rootSyntheticCommit=$rootSyntheticCommit"
        "rootSyntheticTree=$rootSyntheticTree"
        "rootSnapshotBundleSha256=$rootSnapshotBundleSha256"
        "buildEvidence=$($evidence.Name)"
    ) | Set-Content -LiteralPath (Join-Path $result 'STVR_CandidateProvenance.txt') -Encoding ascii
    $buildSucceeded = $true
} finally {
    Remove-Item -LiteralPath $patch -Force -ErrorAction SilentlyContinue
    if (-not [string]::IsNullOrWhiteSpace($commonLibBundle)) {
        Remove-Item -LiteralPath $commonLibBundle -Force -ErrorAction SilentlyContinue
    }
    if (-not [string]::IsNullOrWhiteSpace($candidateBundle)) {
        Remove-Item -LiteralPath $candidateBundle -Force -ErrorAction SilentlyContinue
    }
    if (-not [string]::IsNullOrWhiteSpace($planckBundle)) { Remove-Item -LiteralPath $planckBundle -Force -ErrorAction SilentlyContinue }
    if (-not [string]::IsNullOrWhiteSpace($rootSnapshotBundle)) { Remove-Item -LiteralPath $rootSnapshotBundle -Force -ErrorAction SilentlyContinue }
    if (-not $buildSucceeded -and (Test-Path -LiteralPath $result)) {
        Remove-Item -LiteralPath $result -Recurse -Force -ErrorAction SilentlyContinue
    }
    if (-not [string]::IsNullOrWhiteSpace($commonLibWorktree) -and (Test-Path -LiteralPath $commonLibWorktree)) {
        if (-not [string]::IsNullOrWhiteSpace($commonLibTransferRef)) {
            Invoke-GitStatusProbe -Arguments @('-C', $commonLibWorktree, 'update-ref', '-d', $commonLibTransferRef) | Out-Null
        }
        if (-not [string]::IsNullOrWhiteSpace($commonLibTrustedRef)) {
            Invoke-GitStatusProbe -Arguments @('-C', $commonLibWorktree, 'update-ref', '-d', $commonLibTrustedRef) | Out-Null
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($planckWorktree) -and (Test-Path -LiteralPath $planckWorktree) -and
        -not [string]::IsNullOrWhiteSpace($planckTransferRef)) {
        Invoke-GitStatusProbe -Arguments @('-C', $planckWorktree, 'update-ref', '-d', $planckTransferRef) | Out-Null
    }
    Set-Location $repo
    if ($worktreeCreated) {
        Remove-CandidateWorktree -Path $build -FailIfActive $false
    }
    if (-not [string]::IsNullOrWhiteSpace($candidateTransferRef)) {
        Invoke-GitStatusProbe -Arguments @('-C', $repo, 'update-ref', '-d', $candidateTransferRef) | Out-Null
    }
    if (-not [string]::IsNullOrWhiteSpace($trustedRemoteRef)) {
        Invoke-GitStatusProbe -Arguments @('-C', $repo, 'update-ref', '-d', $trustedRemoteRef) | Out-Null
    }
    if (-not [string]::IsNullOrWhiteSpace($rootSnapshotTransferRef)) {
        Invoke-GitStatusProbe -Arguments @('-C', $repo, 'update-ref', '-d', $rootSnapshotTransferRef) | Out-Null
    }
    Invoke-GitStatusProbe -Arguments @('-C', $repo, 'worktree', 'prune') | Out-Null
    $normalizedBuildSucceeded = if ($buildSucceeded) { "true" } else { "false" }
    "STVR_CANDIDATE_BASE=$baseCommit"
    "STVR_CANDIDATE_COMMON_ANCESTOR=$commonAncestor"
    "STVR_CANDIDATE_BUNDLE_SHA256=$candidateBundleSha256"
    "STVR_CANDIDATE_TRUSTED_REMOTE_COMMIT=$trustedRemoteCommit"
    "STVR_CANDIDATE_EPHEMERAL_REVISION=$candidateRevision"
    "STVR_CANDIDATE_BUILD_SUCCESS=$normalizedBuildSucceeded"
    "STVR_CANDIDATE_GUEST_RESULT=$result"
    "STVR_CANDIDATE_COMMONLIB_BASE=$commonLibBase"
    "STVR_CANDIDATE_COMMONLIB_TARGET=$commonLibTarget"
    "STVR_CANDIDATE_COMMONLIB_COMMON_ANCESTOR=$commonLibCommonAncestor"
    "STVR_CANDIDATE_COMMONLIB_TRUSTED_UPSTREAM=$commonLibTrustedUpstream"
    "STVR_CANDIDATE_COMMONLIB_BUNDLE_SHA256=$commonLibBundleSha256"
    "STVR_CANDIDATE_PLANCK_BASE=$planckBase"
    "STVR_CANDIDATE_PLANCK_SYNTHETIC_COMMIT=$planckSyntheticCommit"
    "STVR_CANDIDATE_PLANCK_BUNDLE_SHA256=$planckBundleSha256"
    "STVR_CANDIDATE_ROOT_SYNTHETIC_COMMIT=$rootSyntheticCommit"
    "STVR_CANDIDATE_ROOT_SYNTHETIC_TREE=$rootSyntheticTree"
    "STVR_CANDIDATE_ROOT_SNAPSHOT_BUNDLE_SHA256=$rootSnapshotBundleSha256"
}
POWERSHELL

powershell_payload=${powershell_payload//__WINBOAT_REPO__/$winboat_repo}
powershell_payload=${powershell_payload//__WINBOAT_BUILD__/$winboat_build}
powershell_payload=${powershell_payload//__GUEST_PATCH__/$guest_patch}
powershell_payload=${powershell_payload//__BASE_COMMIT__/$base_commit}
powershell_payload=${powershell_payload//__CANDIDATE_COMMON_ANCESTOR__/$candidate_common_ancestor}
powershell_payload=${powershell_payload//__HAS_CANDIDATE_BUNDLE__/$candidate_bundle_required}
powershell_payload=${powershell_payload//__GUEST_CANDIDATE_BUNDLE__/$guest_candidate_bundle}
powershell_payload=${powershell_payload//__CANDIDATE_BUNDLE_SHA256__/$candidate_bundle_sha256}
powershell_payload=${powershell_payload//__GUEST_CANDIDATE_TRANSFER_REF__/$guest_candidate_transfer_ref}
powershell_payload=${powershell_payload//__GUEST_TRUSTED_REMOTE_REF__/$guest_trusted_remote_ref}
powershell_payload=${powershell_payload//__PATCH_SHA256__/$patch_sha256}
powershell_payload=${powershell_payload//__PLANCK_BASE__/$planck_base_commit}
powershell_payload=${powershell_payload//__PLANCK_SYNTHETIC_COMMIT__/$planck_synthetic_commit}
powershell_payload=${powershell_payload//__GUEST_PLANCK_BUNDLE__/$guest_planck_bundle}
powershell_payload=${powershell_payload//__PLANCK_BUNDLE_SHA256__/$planck_bundle_sha256}
powershell_payload=${powershell_payload//__PLANCK_BUNDLE_REF__/$planck_snapshot_ref}
powershell_payload=${powershell_payload//__GUEST_PLANCK_TRANSFER_REF__/$guest_planck_transfer_ref}
powershell_payload=${powershell_payload//__ROOT_SYNTHETIC_COMMIT__/$root_synthetic_commit}
powershell_payload=${powershell_payload//__ROOT_SYNTHETIC_TREE__/$root_synthetic_tree}
powershell_payload=${powershell_payload//__GUEST_ROOT_SNAPSHOT_BUNDLE__/$guest_root_snapshot_bundle}
powershell_payload=${powershell_payload//__ROOT_SNAPSHOT_BUNDLE_SHA256__/$root_snapshot_bundle_sha256}
powershell_payload=${powershell_payload//__ROOT_SNAPSHOT_BUNDLE_REF__/$root_snapshot_ref}
powershell_payload=${powershell_payload//__ROOT_SNAPSHOT_TRANSFER_REF__/$guest_root_snapshot_transfer_ref}
powershell_payload=${powershell_payload//__HAS_COMMONLIB_GITLINK__/$commonlib_gitlink_changed}
powershell_payload=${powershell_payload//__HAS_COMMONLIB_BUNDLE__/$commonlib_bundle_required}
powershell_payload=${powershell_payload//__COMMONLIB_BASE__/$commonlib_base_commit}
powershell_payload=${powershell_payload//__COMMONLIB_TARGET__/$commonlib_target_commit}
powershell_payload=${powershell_payload//__COMMONLIB_COMMON_ANCESTOR__/$commonlib_common_ancestor}
powershell_payload=${powershell_payload//__COMMONLIB_TRUSTED_UPSTREAM__/$commonlib_trusted_upstream_commit}
powershell_payload=${powershell_payload//__COMMONLIB_TRUSTED_UPSTREAM_REQUIRED__/$commonlib_trusted_upstream_match_required}
powershell_payload=${powershell_payload//__COMMONLIB_TRUSTED_UPSTREAM_URL__/$commonlib_trusted_upstream_url}
powershell_payload=${powershell_payload//__GUEST_COMMONLIB_BUNDLE__/$guest_commonlib_bundle}
powershell_payload=${powershell_payload//__COMMONLIB_BUNDLE_SHA256__/$commonlib_bundle_sha256}
powershell_payload=${powershell_payload//__GUEST_COMMONLIB_TRANSFER_REF__/$guest_commonlib_transfer_ref}
powershell_payload=${powershell_payload//__GUEST_COMMONLIB_TRUSTED_REF__/$guest_commonlib_trusted_ref}
powershell_payload=${powershell_payload//__GUEST_RESULT__/$guest_result}
powershell_payload=${powershell_payload//__GUEST_HAVOK_ARCHIVE__/$guest_havok_archive}
powershell_payload=${powershell_payload//__GUEST_PLANCK_DEPENDENCY_ROOT__/$guest_planck_dependency_root}

payload_file=$(mktemp "${TMPDIR:-/tmp}/stvr-winboat-candidate-${short_commit}-XXXXXX.ps1")
guest_report_file=$(mktemp "${TMPDIR:-/tmp}/stvr-winboat-candidate-${short_commit}-XXXXXX.log")
printf '%s\n' "$powershell_payload" >"$payload_file"
"$winboat_scp" to-guest "$patch_file" "$guest_patch"
if [[ $candidate_bundle_required == true ]]; then
    "$winboat_scp" to-guest "$candidate_bundle_file" "$guest_candidate_bundle"
fi
if [[ $commonlib_bundle_required == true ]]; then
    "$winboat_scp" to-guest "$commonlib_bundle_file" "$guest_commonlib_bundle"
fi
if [[ $planck_dirty == true ]]; then
    "$winboat_scp" to-guest "$planck_bundle_file" "$guest_planck_bundle"
fi
"$winboat_scp" to-guest "$root_snapshot_bundle_file" "$guest_root_snapshot_bundle"
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
guest_candidate_common_ancestor=$(awk -F= '/^STVR_CANDIDATE_COMMON_ANCESTOR=/ { value = $2 } END { print value }' "$guest_report_file")
guest_candidate_bundle_sha256=$(awk -F= '/^STVR_CANDIDATE_BUNDLE_SHA256=/ { value = $2 } END { print value }' "$guest_report_file")
guest_trusted_remote_commit=$(awk -F= '/^STVR_CANDIDATE_TRUSTED_REMOTE_COMMIT=/ { value = $2 } END { print value }' "$guest_report_file")
guest_candidate_common_ancestor=${guest_candidate_common_ancestor//$'\r'/}
guest_candidate_bundle_sha256=${guest_candidate_bundle_sha256//$'\r'/}
guest_trusted_remote_commit=${guest_trusted_remote_commit//$'\r'/}
guest_commonlib_base=$(awk -F= '/^STVR_CANDIDATE_COMMONLIB_BASE=/ { value = $2 } END { print value }' "$guest_report_file")
guest_commonlib_target=$(awk -F= '/^STVR_CANDIDATE_COMMONLIB_TARGET=/ { value = $2 } END { print value }' "$guest_report_file")
guest_commonlib_common_ancestor=$(awk -F= '/^STVR_CANDIDATE_COMMONLIB_COMMON_ANCESTOR=/ { value = $2 } END { print value }' "$guest_report_file")
guest_commonlib_trusted_upstream=$(awk -F= '/^STVR_CANDIDATE_COMMONLIB_TRUSTED_UPSTREAM=/ { value = $2 } END { print value }' "$guest_report_file")
guest_commonlib_bundle_sha256=$(awk -F= '/^STVR_CANDIDATE_COMMONLIB_BUNDLE_SHA256=/ { value = $2 } END { print value }' "$guest_report_file")
guest_planck_base=$(awk -F= '/^STVR_CANDIDATE_PLANCK_BASE=/ { value = $2 } END { print value }' "$guest_report_file")
guest_planck_synthetic_commit=$(awk -F= '/^STVR_CANDIDATE_PLANCK_SYNTHETIC_COMMIT=/ { value = $2 } END { print value }' "$guest_report_file")
guest_planck_bundle_sha256=$(awk -F= '/^STVR_CANDIDATE_PLANCK_BUNDLE_SHA256=/ { value = $2 } END { print value }' "$guest_report_file")
guest_root_synthetic_commit=$(awk -F= '/^STVR_CANDIDATE_ROOT_SYNTHETIC_COMMIT=/ { value = $2 } END { print value }' "$guest_report_file")
guest_root_synthetic_tree=$(awk -F= '/^STVR_CANDIDATE_ROOT_SYNTHETIC_TREE=/ { value = $2 } END { print value }' "$guest_report_file")
guest_root_snapshot_bundle_sha256=$(awk -F= '/^STVR_CANDIDATE_ROOT_SNAPSHOT_BUNDLE_SHA256=/ { value = $2 } END { print value }' "$guest_report_file")
guest_commonlib_base=${guest_commonlib_base//$'\r'/}
guest_commonlib_target=${guest_commonlib_target//$'\r'/}
guest_commonlib_common_ancestor=${guest_commonlib_common_ancestor//$'\r'/}
guest_commonlib_trusted_upstream=${guest_commonlib_trusted_upstream//$'\r'/}
guest_commonlib_bundle_sha256=${guest_commonlib_bundle_sha256//$'\r'/}
guest_planck_base=${guest_planck_base//$'\r'/}
guest_planck_synthetic_commit=${guest_planck_synthetic_commit//$'\r'/}
guest_planck_bundle_sha256=${guest_planck_bundle_sha256//$'\r'/}
guest_root_synthetic_commit=${guest_root_synthetic_commit//$'\r'/}
guest_root_synthetic_tree=${guest_root_synthetic_tree//$'\r'/}
guest_root_snapshot_bundle_sha256=${guest_root_snapshot_bundle_sha256//$'\r'/}
if ((guest_status != 0)); then
    exit "$guest_status"
fi
if [[ $guest_candidate_common_ancestor != "$candidate_common_ancestor" || \
      $guest_candidate_bundle_sha256 != "$candidate_bundle_sha256" || \
      ! $guest_trusted_remote_commit =~ ^[0-9a-fA-F]{40}$ ]]; then
    echo "WinBoat candidate build did not report the verified base-transfer identity." >&2
    exit 2
fi
if [[ $guest_commonlib_base != "$commonlib_base_commit" || \
      $guest_commonlib_target != "$commonlib_target_commit" || \
      $guest_commonlib_common_ancestor != "$commonlib_common_ancestor" || \
      $guest_commonlib_bundle_sha256 != "$commonlib_bundle_sha256" ]]; then
    echo "WinBoat candidate build did not report the verified CommonLib transfer identity." >&2
    exit 2
fi
if [[ $commonlib_trusted_upstream_match_required == true ]]; then
    if [[ $guest_commonlib_trusted_upstream != "$commonlib_trusted_upstream_commit" ]]; then
        echo "WinBoat candidate build did not report the trusted CommonLib upstream from the Linux network probe." >&2
        exit 2
    fi
elif [[ ! $guest_commonlib_trusted_upstream =~ ^[0-9a-fA-F]{40}$ ]]; then
    echo "WinBoat candidate build did not independently verify a trusted CommonLib upstream commit." >&2
    exit 2
fi
if [[ $guest_planck_base != "$planck_base_commit" || $guest_planck_synthetic_commit != "$planck_synthetic_commit" || \
      $guest_planck_bundle_sha256 != "$planck_bundle_sha256" || $guest_root_synthetic_commit != "$root_synthetic_commit" || \
      $guest_root_synthetic_tree != "$root_synthetic_tree" || $guest_root_snapshot_bundle_sha256 != "$root_snapshot_bundle_sha256" ]]; then
    echo "WinBoat candidate build did not report the exact PLANCK/root synthetic snapshot provenance." >&2
    exit 2
fi
if [[ $candidate_ephemeral_revision != "$root_synthetic_commit" ]]; then
    echo "WinBoat candidate build was not performed from the imported root synthetic snapshot." >&2
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
if ! python3 "$repo_root/Tools/SkyrimVR/audit_built_package.py" \
    --package "$linux_result_path/result/gameplay" --gameplay --require-patched-planck-interface002; then
    echo "Transferred WinBoat candidate package failed the patched PLANCK interface002 audit." >&2
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
