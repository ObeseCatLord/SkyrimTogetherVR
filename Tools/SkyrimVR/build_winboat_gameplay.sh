#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$repo_root"

if [[ -n $(git status --porcelain=v1 --untracked-files=all) ]]; then
    echo "Refusing WinBoat build from a dirty Linux worktree." >&2
    exit 2
fi

revision=${1:-HEAD}
commit=$(git rev-parse --verify "${revision}^{commit}")
short_commit=${commit:0:8}
git fetch github main
if ! git merge-base --is-ancestor "$commit" FETCH_HEAD; then
    echo "Commit $commit is not reachable from github/main. Push it before building." >&2
    exit 2
fi

winboat_powershell=${WINBOAT_POWERSHELL:-$HOME/.codex/skills/winboat-ssh/scripts/winboat-powershell}
if [[ ! -x $winboat_powershell ]]; then
    echo "WinBoat PowerShell helper is not executable: $winboat_powershell" >&2
    exit 2
fi

"$repo_root/Tools/SkyrimVR/cleanup_build_storage.sh" --max-age-days 0 --skip-local-artifacts

winboat_repo=${STVR_WINBOAT_REPO:-'C:\Users\obesecatlord\Documents\Codex\SkyrimTogetherVR'}
timestamp=$(date -u +%Y%m%d%H%M%SZ)
winboat_build="${winboat_repo}-build-${short_commit}-${timestamp}"

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

git -C $repo fetch origin main
if ($LASTEXITCODE -ne 0) { throw "Could not fetch origin/main in the WinBoat checkout." }
git -C $repo cat-file -e "$commit`^{commit}"
if ($LASTEXITCODE -ne 0) { throw "Commit $commit is unavailable in the WinBoat checkout." }
if (Test-Path -LiteralPath $build) { throw "Fresh build worktree already exists: $build" }

git -C $repo worktree add --detach $build $commit
if ($LASTEXITCODE -ne 0) { throw "Could not create detached WinBoat worktree." }

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

"STVR_BUILD_COMMIT=$commit"
"STVR_BUILD_WORKTREE=$build"
"STVR_GAMEPLAY_PACKAGE=$package"
"STVR_BUILD_EVIDENCE=$($evidence.FullName)"
POWERSHELL

powershell_payload=${powershell_payload//__WINBOAT_REPO__/$winboat_repo}
powershell_payload=${powershell_payload//__WINBOAT_BUILD__/$winboat_build}
powershell_payload=${powershell_payload//__COMMIT__/$commit}

"$winboat_powershell" "$powershell_payload"
