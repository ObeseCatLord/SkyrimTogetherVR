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

cleanup_after_build() {
    "$repo_root/Tools/SkyrimVR/cleanup_build_storage.sh" \
        --scheduled --max-age-days 2 --skip-local-artifacts --temp-artifacts || true
}

import_root=""
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
    [[ -z $import_root ]] || rm -rf -- "$import_root"
    cleanup_after_build
    exit "$status"
}
trap cleanup_runtime EXIT

if [[ -n $(git status --porcelain=v1 --untracked-files=all) ]]; then
    echo "Refusing WinBoat build from a dirty Linux worktree." >&2
    exit 2
fi

revision=${1:-HEAD}
commit=$(git rev-parse --verify "${revision}^{commit}")
short_commit=${commit:0:8}
git fetch --force --tags github main
if ! git merge-base --is-ancestor "$commit" FETCH_HEAD; then
    echo "Commit $commit is not reachable from github/main. Push it before building." >&2
    exit 2
fi

winboat_powershell=${WINBOAT_POWERSHELL:-$HOME/.codex/skills/winboat-ssh/scripts/winboat-powershell}
winboat_ssh=${WINBOAT_SSH:-$HOME/.codex/skills/winboat-ssh/scripts/winboat-ssh}
winboat_scp=${WINBOAT_SCP:-$HOME/.codex/skills/winboat-ssh/scripts/winboat-scp}
if [[ ! -x $winboat_powershell ]]; then
    echo "WinBoat PowerShell helper is not executable: $winboat_powershell" >&2
    exit 2
fi
if [[ ! -x $winboat_ssh ]]; then
    echo "WinBoat SSH helper is not executable: $winboat_ssh" >&2
    exit 2
fi
if [[ ! -x $winboat_scp ]]; then
    echo "WinBoat SCP helper is not executable: $winboat_scp" >&2
    exit 2
fi

"$repo_root/Tools/SkyrimVR/cleanup_build_storage.sh" \
    --max-age-days 0 --skip-local-artifacts --local-build-output --temp-artifacts

winboat_repo=${STVR_WINBOAT_REPO:-'C:\Users\obesecatlord\Documents\Codex\SkyrimTogetherVR'}
timestamp=$(date -u +%Y%m%d%H%M%SZ)
winboat_build="${winboat_repo}-build-${short_commit}-${timestamp}"
winboat_result="${winboat_repo}-build-results\\${short_commit}-${timestamp}"

for value in "$winboat_repo" "$winboat_build" "$winboat_result"; do
    if [[ $value == *"'"* ]]; then
        echo "WinBoat paths containing a single quote are not supported." >&2
        exit 2
    fi
done

read -r -d '' powershell_payload <<'POWERSHELL' || true
$ErrorActionPreference = "Stop"
$repo = '__WINBOAT_REPO__'
$build = '__WINBOAT_BUILD__'
$result = '__WINBOAT_RESULT__'
$commit = '__COMMIT__'
$worktreeCreated = $false

try {
    git -C $repo fetch --force --tags origin main
    if ($LASTEXITCODE -ne 0) { throw "Could not fetch origin/main in the WinBoat checkout." }
    git -C $repo cat-file -e "$commit`^{commit}"
    if ($LASTEXITCODE -ne 0) { throw "Commit $commit is unavailable in the WinBoat checkout." }
    if (Test-Path -LiteralPath $build) { throw "Fresh build worktree already exists: $build" }
    if (Test-Path -LiteralPath $result) { throw "Fresh build result directory already exists: $result" }

    git -C $repo worktree add --detach $build $commit
    if ($LASTEXITCODE -ne 0) { throw "Could not create detached WinBoat worktree." }
    $worktreeCreated = $true

    Set-Location $build
    git submodule sync --recursive
    if ($LASTEXITCODE -ne 0) { throw "Could not synchronize submodule URLs." }
    git submodule update --init --recursive --checkout
    if ($LASTEXITCODE -ne 0) { throw "Could not initialize pinned submodules." }

    $dirty = @(git status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0 -or $dirty.Count -ne 0) { throw "Fresh WinBoat worktree is unexpectedly dirty." }

    $env:Path = "C:\Users\obesecatlord\AppData\Local\Microsoft\WinGet\Links;$env:Path"
    & .\BuildAuditCollectSkyrimTogetherVR-Windows.bat --gameplay
    if ($LASTEXITCODE -ne 0) { throw "Audited gameplay build failed with exit code $LASTEXITCODE." }

    $package = Join-Path $build 'artifacts\SkyrimTogetherVR\packages\gameplay'
    $evidence = Get-ChildItem -LiteralPath (Join-Path $build 'artifacts\SkyrimTogetherVR\build-evidence') -Filter 'SkyrimTogetherVR-build-evidence-gameplay-*.zip' |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if (-not (Test-Path -LiteralPath $package)) { throw "Gameplay package was not created: $package" }
    if ($null -eq $evidence) { throw "Gameplay build evidence archive was not created." }

    $resultPackage = Join-Path $result 'gameplay'
    New-Item -ItemType Directory -Path $resultPackage -Force | Out-Null
    Copy-Item -Path (Join-Path $package '*') -Destination $resultPackage -Recurse -Force
    $resultEvidence = Join-Path $result $evidence.Name
    Copy-Item -LiteralPath $evidence.FullName -Destination $resultEvidence -Force

    "STVR_BUILD_COMMIT=$commit"
    "STVR_BUILD_WORKTREE_REMOVED=$build"
    "STVR_BUILD_RESULT=$result"
    "STVR_GAMEPLAY_PACKAGE=$resultPackage"
    "STVR_BUILD_EVIDENCE=$resultEvidence"
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
powershell_payload=${powershell_payload//__WINBOAT_RESULT__/$winboat_result}
powershell_payload=${powershell_payload//__COMMIT__/$commit}

payload_file=$(mktemp "${TMPDIR:-/tmp}/stvr-winboat-build-${short_commit}-${timestamp}-XXXXXX.ps1")
guest_payload="C:/Users/obesecatlord/AppData/Local/Temp/stvr-winboat-build-${short_commit}-${timestamp}.ps1"
printf '%s\n' "$powershell_payload" >"$payload_file"
"$winboat_scp" to-guest "$payload_file" "$guest_payload"
guest_payload_uploaded=1
"$winboat_ssh" powershell.exe -NoLogo -NoProfile -NonInteractive \
    -ExecutionPolicy Bypass -File "$guest_payload"

import_root=$(mktemp -d "${TMPDIR:-/tmp}/stvr-winboat-import-${short_commit}-${timestamp}-XXXXXX")

winboat_result_scp=${winboat_result//\\//}
"$winboat_scp" from-guest "$winboat_result_scp" "$import_root/result" --recursive

gameplay_dir=$(find "$import_root/result" -type f -name SkyrimTogetherVR_BuildManifest.json -printf '%h\n' -quit)
evidence_path=$(find "$import_root/result" -type f -name 'SkyrimTogetherVR-build-evidence-gameplay-*.zip' -print -quit)
if [[ -z $gameplay_dir || -z $evidence_path ]]; then
    echo "Transferred WinBoat result is missing the gameplay package or build evidence." >&2
    exit 2
fi

package_dir="$repo_root/artifacts/SkyrimTogetherVR/packages"
evidence_dir="$repo_root/artifacts/SkyrimTogetherVR/build-evidence"
handoff_dir="$repo_root/artifacts/SkyrimTogetherVR/review-handoff"
mkdir -p "$package_dir" "$evidence_dir" "$handoff_dir"
package_zip="$package_dir/SkyrimTogetherVR-gameplay-${short_commit}-${timestamp}.zip"
evidence_copy="$evidence_dir/$(basename "$evidence_path")"
python3 "$repo_root/Tools/SkyrimVR/archive_gameplay_package.py" \
    "$gameplay_dir" "$package_zip" --expected-revision "$commit"
cp -- "$evidence_path" "$evidence_copy"

python3 - "$repo_root" "$package_zip" "$evidence_copy" "$commit" <<'PY'
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(sys.argv[1]) / "Tools/SkyrimVR"))
from local_handoff_artifacts import validate_artifact_pair

validate_artifact_pair(pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3]), sys.argv[4])
PY

handoff_zip="$handoff_dir/SkyrimTogetherVR-local-agent-complete-handoff-${short_commit}-${timestamp}.zip"
python3 "$repo_root/Tools/SkyrimVR/create_local_agent_handoff.py" \
    --repo "$repo_root" \
    --gameplay-package "$package_zip" \
    --build-evidence "$evidence_copy" \
    --output "$handoff_zip"
python3 "$repo_root/Tools/SkyrimVR/audit_local_agent_handoff.py" "$handoff_zip"
unzip -tq "$handoff_zip"

printf 'STVR_LINUX_GAMEPLAY_PACKAGE=%s\n' "$package_zip"
printf 'STVR_LINUX_BUILD_EVIDENCE=%s\n' "$evidence_copy"
printf 'STVR_LOCAL_HANDOFF=%s\n' "$handoff_zip"
