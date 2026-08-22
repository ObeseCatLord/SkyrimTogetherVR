#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$repo_root"

usage() {
    printf 'Usage: %s [--runtime-evidence ZIP] [--skip-handoff] [--timeout-seconds SECONDS] [<commit>]\n' "${0##*/}" >&2
}

skip_handoff=0
runtime_evidence=${STVR_RUNTIME_EVIDENCE:-}
timeout_seconds=${STVR_WINBOAT_BUILD_TIMEOUT_SECONDS:-14400}
poll_seconds=${STVR_WINBOAT_BUILD_POLL_SECONDS:-15}
task_grace_seconds=${STVR_WINBOAT_TASK_GRACE_SECONDS:-120}
revision=HEAD
revision_set=0
while (($# > 0)); do
    case $1 in
        --skip-handoff)
            skip_handoff=1
            ;;
        --runtime-evidence)
            if (($# < 2)); then
                echo "Missing ZIP path after --runtime-evidence." >&2
                usage
                exit 2
            fi
            runtime_evidence=$2
            shift
            ;;
        --timeout-seconds)
            if (($# < 2)); then
                echo "Missing value after --timeout-seconds." >&2
                usage
                exit 2
            fi
            timeout_seconds=$2
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        --)
            shift
            if (($# > 1)); then
                usage
                exit 2
            fi
            if (($# == 1)); then
                revision=$1
            fi
            break
            ;;
        -*)
            echo "Unknown option: $1" >&2
            usage
            exit 2
            ;;
        *)
            if ((revision_set)); then
                usage
                exit 2
            fi
            revision=$1
            revision_set=1
            ;;
    esac
    shift
done

if [[ ! $timeout_seconds =~ ^[0-9]+$ || ! $poll_seconds =~ ^[1-9][0-9]*$ || ! $task_grace_seconds =~ ^[1-9][0-9]*$ ]] ||
   ((poll_seconds > 60 || task_grace_seconds > 600)); then
    echo "WinBoat timeout must be a non-negative integer, poll interval 1-60 seconds, and task grace 1-600 seconds." >&2
    exit 2
fi

if ((skip_handoff == 0)); then
    if [[ -z $runtime_evidence ]]; then
        echo "Runtime evidence is required for handoff generation. Use --runtime-evidence ZIP after live gameplay-bootstrap acceptance, or use --skip-handoff for build-only and run finalize_local_agent_handoff.sh after live acceptance." >&2
        exit 2
    fi
    if [[ ! -f $runtime_evidence ]]; then
        echo "Runtime evidence ZIP does not exist: $runtime_evidence. Use --skip-handoff for build-only and run finalize_local_agent_handoff.sh after live acceptance." >&2
        exit 2
    fi
    runtime_evidence=$(realpath -e -- "$runtime_evidence")
fi

build_lock_file="${XDG_RUNTIME_DIR:-/tmp}/skyrim-together-vr-build-active.lock"
exec 8>"$build_lock_file"
if ! flock -n 8; then
    echo "Another Skyrim Together VR build or cleanup is active; refusing to overlap it." >&2
    exit 2
fi
export STVR_BUILD_LOCK_HELD=1

import_root=""
payload_file=""
launcher_file=""
winboat_powershell=""
cleanup_runtime() {
    local status=$?
    trap - EXIT
    [[ -z $payload_file ]] || rm -f -- "$payload_file"
    [[ -z $launcher_file ]] || rm -f -- "$launcher_file"
    [[ -z $import_root ]] || rm -rf -- "$import_root"
    exit "$status"
}
trap cleanup_runtime EXIT

# The Linux submodule worktree is not transferred to WinBoat; the guest checks
# out the committed gitlink independently. Preserve local mod-development dirt
# while still rejecting every superproject and untracked change. The committed
# gitlink itself is resolved and checked in the fresh WinBoat worktree.
if [[ -n $(git status --porcelain=v1 --untracked-files=all --ignore-submodules=all) ]]; then
    echo "Refusing WinBoat build from a dirty Linux worktree." >&2
    exit 2
fi

commit=$(git rev-parse --verify "${revision}^{commit}")
short_commit=${commit:0:8}
git fetch --force --tags github main
if ! git merge-base --is-ancestor "$commit" FETCH_HEAD; then
    echo "Commit $commit is not reachable from github/main. Push it before building." >&2
    exit 2
fi

winboat_powershell=${WINBOAT_POWERSHELL:-$HOME/.codex/skills/winboat-ssh/scripts/winboat-powershell}
winboat_scp=${WINBOAT_SCP:-$HOME/.codex/skills/winboat-ssh/scripts/winboat-scp}
if [[ ! -x $winboat_powershell ]]; then
    echo "WinBoat PowerShell helper is not executable: $winboat_powershell" >&2
    exit 2
fi
if [[ ! -x $winboat_scp ]]; then
    echo "WinBoat SCP helper is not executable: $winboat_scp" >&2
    exit 2
fi
run_winboat_powershell() {
    # Windows PowerShell 5.1 cannot reliably parse multiline function blocks
    # from OpenSSH stdin, and encoded commands hit cmd.exe's length ceiling.
    # Transfer an ephemeral script and invoke it with one short expression.
    local local_script guest_script output status=0
    local_script=$(mktemp "${TMPDIR:-/tmp}/stvr-winboat-task-XXXXXX.ps1")
    guest_script="C:/Users/${winboat_windows_user}/AppData/Local/Temp/$(basename "$local_script")"
    printf '%s\n' "$1" >"$local_script"
    "$winboat_scp" to-guest "$local_script" "$guest_script"
    output=$("$winboat_powershell" \
        "Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force; & '$guest_script'") || status=$?
    rm -f -- "$local_script"
    "$winboat_powershell" "Remove-Item -LiteralPath '$guest_script' -Force -ErrorAction SilentlyContinue" \
        >/dev/null || true
    output=${output//$'\r'/}
    printf '%s\n' "$output"
    return "$status"
}

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
winboat_windows_user=${WINBOAT_WINDOWS_USER:-obesecatlord}
job_identity=${STVR_WINBOAT_JOB_ID:-$commit}
if [[ ! $winboat_windows_user =~ ^[a-zA-Z0-9_.-]+$ || ! $job_identity =~ ^[a-zA-Z0-9_.-]+$ ]]; then
    echo "WinBoat job identity and Windows user may contain only letters, digits, dots, underscores, and hyphens." >&2
    exit 2
fi
guest_job_name="stvr-gameplay-${short_commit}-${job_identity}"
guest_jobs_root="C:/Users/${winboat_windows_user}/AppData/Local/SkyrimTogetherVR/WinBoatJobs"
guest_job_root="${guest_jobs_root}/${guest_job_name}"
guest_payload="${guest_job_root}/build.ps1"
guest_launcher="${guest_job_root}/launcher.ps1"
guest_state="${guest_job_root}/state.txt"
guest_exit="${guest_job_root}/exit.txt"
guest_log="${guest_job_root}/build.log"
guest_result_record="${guest_job_root}/result.txt"
winboat_task_name="STVR-SkyrimTogetherVR-Gameplay-${short_commit}-${job_identity}"
task_description="STVR SkyrimTogetherVR gameplay build ${job_identity} ${commit}"
task_executable='powershell.exe'
task_arguments="-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"${guest_launcher}\""
guest_mutex_name='Global\STVR-SkyrimTogetherVR-Gameplay-JobLifecycle'
timestamp=$(date -u +%Y%m%d%H%M%SZ)
winboat_build="${winboat_repo}-build-${short_commit}-${job_identity}"
winboat_result="${winboat_repo}-build-results\\${short_commit}-${job_identity}"

for value in "$winboat_repo" "$winboat_build" "$winboat_result" "$guest_jobs_root" "$guest_job_root" "$guest_payload" "$guest_launcher" "$guest_state" "$guest_exit" "$guest_log" "$guest_result_record" "$winboat_task_name" "$task_description" "$task_arguments" "$guest_havok_archive" "$guest_planck_dependency_root"; do
    if [[ $value == *"'"* ]]; then
        echo "WinBoat paths containing a single quote are not supported." >&2
        exit 2
    fi
done

read -r -d '' task_preflight_payload <<'POWERSHELL' || true
$ErrorActionPreference = "Stop"
$taskName = '__TASK_NAME__'
$description = '__TASK_DESCRIPTION__'
$jobRoot = '__GUEST_JOB_ROOT__'
$identityPath = Join-Path $jobRoot 'identity.json'
$statePath = '__GUEST_STATE__'
$exitPath = '__GUEST_EXIT__'
$resultPath = '__GUEST_RESULT_RECORD__'
$launcher = '__GUEST_LAUNCHER__'
$jobIdentity = '__JOB_IDENTITY__'
$commit = '__COMMIT__'
$shortCommit = '__SHORT_COMMIT__'
$windowsUser = '__WINDOWS_USER__'
$taskExecutable = '__TASK_EXECUTABLE__'
$taskArguments = '__TASK_ARGUMENTS__'
$graceSeconds = __TASK_GRACE_SECONDS__
$repo = '__WINBOAT_REPO__'
$build = '__WINBOAT_BUILD__'
$expectedResult = '__WINBOAT_RESULT__'

function Assert-JobIdentity {
    if (-not (Test-Path -LiteralPath $identityPath -PathType Leaf)) {
        throw "Scheduled task identity record is missing: $identityPath"
    }
    $identity = Get-Content -LiteralPath $identityPath -Raw | ConvertFrom-Json
    if ($identity.jobIdentity -ne $jobIdentity -or $identity.commit -ne $commit -or
        $identity.taskName -ne $taskName -or $identity.launcher -ne $launcher -or
        $identity.windowsUser -ine $windowsUser -or $identity.taskExecutable -cne $taskExecutable -or
        $identity.taskArguments -cne $taskArguments -or $identity.shortCommit -cne $shortCommit -or
        $identity.repo -cne $repo -or $identity.buildPath -cne $build -or
        $identity.resultPath -cne $expectedResult) {
        throw "Existing WinBoat job identity does not match the requested commit/job."
    }
}

function Assert-TaskContract {
    param($Task)
    if ($Task.Description -ne $description -or $Task.Actions.Count -ne 1 -or
        $Task.Actions[0].Execute -cne $taskExecutable -or $Task.Actions[0].Arguments -cne $taskArguments -or
        $Task.Principal.UserId -ine $windowsUser -or $Task.Principal.LogonType.ToString() -cne 'S4U') {
        throw "Existing scheduled task does not match the requested WinBoat job identity."
    }
}

function Test-ValidResult {
    if (-not (Test-Path -LiteralPath $exitPath -PathType Leaf) -or
        (Get-Content -LiteralPath $exitPath -Raw).Trim() -ne '0' -or
        -not (Test-Path -LiteralPath $resultPath -PathType Leaf)) { return $false }
    $record = @(Get-Content -LiteralPath $resultPath)
    return $record -contains "STVR_BUILD_COMMIT=$commit" -and $record -contains "STVR_BUILD_RESULT=$expectedResult"
}

$task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
$createdJobRoot = $false
if ($null -ne $task) {
    Assert-JobIdentity
    Assert-TaskContract $task
} elseif (-not (Test-Path -LiteralPath $jobRoot)) {
    New-Item -ItemType Directory -Path $jobRoot -Force | Out-Null
    $createdJobRoot = $true
    [pscustomobject]@{
        jobIdentity = $jobIdentity
        commit = $commit
        taskName = $taskName
        launcher = $launcher
        windowsUser = $windowsUser
        taskExecutable = $taskExecutable
        taskArguments = $taskArguments
        shortCommit = $shortCommit
        repo = $repo
        buildPath = $build
        resultPath = $expectedResult
        createdAtUtc = (Get-Date).ToUniversalTime().ToString('o')
    } | ConvertTo-Json -Compress | Set-Content -LiteralPath $identityPath -Encoding UTF8 -NoNewline
} else {
    Assert-JobIdentity
}

$state = if (Test-Path -LiteralPath $statePath -PathType Leaf) {
    (Get-Content -LiteralPath $statePath -Raw).Trim().ToUpperInvariant()
} else { '' }
if (Test-ValidResult) {
    $status = 'COMPLETE'
} elseif (Test-Path -LiteralPath $exitPath -PathType Leaf) {
    $status = 'FAILED'
} elseif ($null -eq $task) {
    $status = if ($createdJobRoot -or $state -eq 'STAGED') { 'STAGED' } else { 'FAILED' }
} elseif ($task.State -eq 'Running') {
    $status = 'RUNNING'
} else {
    $taskInfo = Get-ScheduledTaskInfo -TaskName $taskName -ErrorAction Stop
    $referenceTime = $taskInfo.LastRunTime.ToUniversalTime()
    if ($referenceTime.Year -lt 2000 -and (Test-Path -LiteralPath $statePath -PathType Leaf)) {
        $referenceTime = (Get-Item -LiteralPath $statePath).LastWriteTimeUtc
    }
    $withinGrace = $referenceTime.Year -ge 2000 -and (((Get-Date).ToUniversalTime() - $referenceTime).TotalSeconds -le $graceSeconds)
    $transientResult = [int64]($taskInfo.LastTaskResult) -in @(0, 267008, 267009, 267011)
    $status = if ($withinGrace -and $transientResult -and $state -in @('STARTING', 'RUNNING', 'COMPLETE')) { 'STARTING' } elseif ($state -eq 'STAGED') { 'STAGED' } else { 'FAILED' }
}
"STVR_JOB_STATUS=$status"
"STVR_JOB_STATE_PATH=$statePath"
"STVR_JOB_EXIT_PATH=$exitPath"
"STVR_JOB_LOG_PATH=__GUEST_LOG__"
"STVR_JOB_RESULT_PATH=$resultPath"
POWERSHELL

task_preflight_payload=${task_preflight_payload//__TASK_NAME__/$winboat_task_name}
task_preflight_payload=${task_preflight_payload//__TASK_DESCRIPTION__/$task_description}
task_preflight_payload=${task_preflight_payload//__GUEST_JOB_ROOT__/$guest_job_root}
task_preflight_payload=${task_preflight_payload//__GUEST_STATE__/$guest_state}
task_preflight_payload=${task_preflight_payload//__GUEST_EXIT__/$guest_exit}
task_preflight_payload=${task_preflight_payload//__GUEST_RESULT_RECORD__/$guest_result_record}
task_preflight_payload=${task_preflight_payload//__GUEST_LAUNCHER__/$guest_launcher}
task_preflight_payload=${task_preflight_payload//__JOB_IDENTITY__/$job_identity}
task_preflight_payload=${task_preflight_payload//__COMMIT__/$commit}
task_preflight_payload=${task_preflight_payload//__SHORT_COMMIT__/$short_commit}
task_preflight_payload=${task_preflight_payload//__GUEST_LOG__/$guest_log}
task_preflight_payload=${task_preflight_payload//__WINDOWS_USER__/$winboat_windows_user}
task_preflight_payload=${task_preflight_payload//__TASK_EXECUTABLE__/$task_executable}
task_preflight_payload=${task_preflight_payload//__TASK_ARGUMENTS__/$task_arguments}
task_preflight_payload=${task_preflight_payload//__TASK_GRACE_SECONDS__/$task_grace_seconds}
task_preflight_payload=${task_preflight_payload//__WINBOAT_REPO__/$winboat_repo}
task_preflight_payload=${task_preflight_payload//__WINBOAT_BUILD__/$winboat_build}
task_preflight_payload=${task_preflight_payload//__WINBOAT_RESULT__/$winboat_result}

task_preflight_output=$(run_winboat_powershell "$task_preflight_payload")
job_status=$(sed -n 's/^STVR_JOB_STATUS=//p' <<<"$task_preflight_output" | tail -n 1)
if [[ -z $job_status ]]; then
    echo "WinBoat job preflight did not report a scheduled-task state." >&2
    exit 2
fi

if [[ $job_status == RUNNING || $job_status == STARTING ]]; then
    echo "Reattaching to active WinBoat scheduled task $winboat_task_name."
elif [[ $job_status == COMPLETE ]]; then
    echo "Reusing completed WinBoat scheduled task $winboat_task_name."
elif [[ $job_status == FAILED ]]; then
    echo "Restarting failed WinBoat scheduled task $winboat_task_name; prior log is retained at $guest_log."
elif [[ $job_status != STAGED ]]; then
    echo "WinBoat scheduled task $winboat_task_name has unsupported state: $job_status" >&2
    exit 2
fi

read -r -d '' guest_maintenance_payload <<'POWERSHELL' || true
$ErrorActionPreference = 'Stop'
$jobsRoot = '__GUEST_JOBS_ROOT__'
$currentTaskName = '__TASK_NAME__'
$windowsUser = '__WINDOWS_USER__'
$expectedRepo = '__WINBOAT_REPO__'
$mutexName = '__GUEST_MUTEX_NAME__'
$successCutoff = (Get-Date).ToUniversalTime().AddDays(-7)
$failureCutoff = (Get-Date).ToUniversalTime().AddDays(-30)

function Get-ValidatedStvrJob {
    param([System.IO.DirectoryInfo]$Root)
    $identityPath = Join-Path $Root.FullName 'identity.json'
    if (-not (Test-Path -LiteralPath $identityPath -PathType Leaf)) { return $null }
    try { $identity = Get-Content -LiteralPath $identityPath -Raw | ConvertFrom-Json } catch { return $null }
    if ($identity.commit -notmatch '^[0-9a-f]{40}$' -or $identity.shortCommit -notmatch '^[0-9a-f]{8}$' -or
        -not $identity.commit.StartsWith($identity.shortCommit, [System.StringComparison]::Ordinal) -or
        $identity.jobIdentity -notmatch '^[A-Za-z0-9_.-]+$') { return $null }
    $expectedName = "stvr-gameplay-$($identity.shortCommit)-$($identity.jobIdentity)"
    if ($Root.Name -cne $expectedName) { return $null }
    $expectedLauncher = (Join-Path $Root.FullName 'launcher.ps1').Replace('\', '/')
    $expectedTaskName = "STVR-SkyrimTogetherVR-Gameplay-$($identity.shortCommit)-$($identity.jobIdentity)"
    $expectedDescription = "STVR SkyrimTogetherVR gameplay build $($identity.jobIdentity) $($identity.commit)"
    $expectedBuild = "$($identity.repo)-build-$($identity.shortCommit)-$($identity.jobIdentity)"
    $expectedResult = "$($identity.repo)-build-results\$($identity.shortCommit)\$($identity.jobIdentity)"
    if ($identity.taskName -cne $expectedTaskName -or $identity.repo -cne $expectedRepo -or
        -not (Test-Path -LiteralPath $identity.repo -PathType Container) -or
        $identity.launcher.Replace('\', '/') -cne $expectedLauncher -or
        $identity.buildPath -cne $expectedBuild -or $identity.resultPath -cne $expectedResult -or
        $identity.windowsUser -ine $windowsUser) { return $null }
    $task = Get-ScheduledTask -TaskName $identity.taskName -ErrorAction SilentlyContinue
    if ($null -eq $task -or $task.Description -cne $expectedDescription -or $task.Actions.Count -ne 1 -or
        $task.Actions[0].Execute -cne 'powershell.exe' -or
        $task.Actions[0].Arguments -cne ('-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + $identity.launcher + '"') -or
        $task.Principal.UserId -ine $windowsUser -or $task.Principal.LogonType.ToString() -cne 'S4U') { return $null }
    [pscustomobject]@{
        Root = $Root
        Identity = $identity
        Task = $task
        Repo = $identity.repo
        BuildPath = $expectedBuild
        ResultPath = $expectedResult
    }
}

$mutex = New-Object System.Threading.Mutex($false, $mutexName)
$held = $false
try {
    $held = $mutex.WaitOne(0)
    if (-not $held) { 'STVR_GUEST_JOB_CLEANUP=SKIPPED_MUTEX_BUSY'; exit 0 }
    $jobs = @()
    if (Test-Path -LiteralPath $jobsRoot -PathType Container) {
        $jobs = @(Get-ChildItem -LiteralPath $jobsRoot -Directory -ErrorAction SilentlyContinue |
            ForEach-Object { Get-ValidatedStvrJob $_ } | Where-Object { $null -ne $_ })
    }
    $activeJobs = @($jobs | Where-Object { $_.Task.State -eq 'Running' })
    if ($activeJobs.Count -ne 0) {
        'STVR_GUEST_JOB_CLEANUP=SKIPPED_ACTIVE_TASK'
        "STVR_GUEST_ACTIVE_TASK=$($activeJobs[0].Identity.taskName)"
        exit 0
    }
    $removed = 0
    foreach ($job in $jobs) {
        if ($job.Identity.taskName -eq $currentTaskName) { continue }
        $exitPath = Join-Path $job.Root.FullName 'exit.txt'
        $resultPath = Join-Path $job.Root.FullName 'result.txt'
        $successful = (Test-Path -LiteralPath $exitPath -PathType Leaf) -and
            (Get-Content -LiteralPath $exitPath -Raw).Trim() -eq '0' -and
            (Test-Path -LiteralPath $resultPath -PathType Leaf)
        $cutoff = if ($successful) { $successCutoff } else { $failureCutoff }
        if ($job.Root.LastWriteTimeUtc -gt $cutoff) { continue }
        if (Test-Path -LiteralPath $job.Repo -PathType Container) {
            if (Test-Path -LiteralPath $job.BuildPath) {
                git -C $job.Repo worktree remove --force $job.BuildPath
                if ($LASTEXITCODE -ne 0) {
                    Remove-Item -LiteralPath $job.BuildPath -Recurse -Force -ErrorAction Stop
                }
            }
            git -C $job.Repo worktree prune
            if ($LASTEXITCODE -ne 0) { throw "Could not prune stale worktrees for $($job.Repo)." }
        }
        if (Test-Path -LiteralPath $job.ResultPath) {
            Remove-Item -LiteralPath $job.ResultPath -Recurse -Force -ErrorAction Stop
        }
        Unregister-ScheduledTask -TaskName $job.Identity.taskName -Confirm:$false -ErrorAction Stop
        Remove-Item -LiteralPath $job.Root.FullName -Recurse -Force -ErrorAction Stop
        $removed++
    }
    "STVR_GUEST_JOB_CLEANUP=REMOVED_$removed"
} finally {
    if ($held) { [void]$mutex.ReleaseMutex() }
    $mutex.Dispose()
}
POWERSHELL
guest_maintenance_payload=${guest_maintenance_payload//__GUEST_JOBS_ROOT__/$guest_jobs_root}
guest_maintenance_payload=${guest_maintenance_payload//__TASK_NAME__/$winboat_task_name}
guest_maintenance_payload=${guest_maintenance_payload//__WINDOWS_USER__/$winboat_windows_user}
guest_maintenance_payload=${guest_maintenance_payload//__WINBOAT_REPO__/$winboat_repo}
guest_maintenance_payload=${guest_maintenance_payload//__GUEST_MUTEX_NAME__/$guest_mutex_name}
guest_maintenance_output=$(run_winboat_powershell "$guest_maintenance_payload")
printf '%s\n' "$guest_maintenance_output"
guest_cleanup_status=$(sed -n 's/^STVR_GUEST_JOB_CLEANUP=//p' <<<"$guest_maintenance_output" | tail -n 1)
guest_active_task=$(sed -n 's/^STVR_GUEST_ACTIVE_TASK=//p' <<<"$guest_maintenance_output" | tail -n 1)
if [[ $job_status == STAGED || $job_status == FAILED ]] &&
   [[ $guest_cleanup_status == SKIPPED_ACTIVE_TASK || $guest_cleanup_status == SKIPPED_MUTEX_BUSY ]]; then
    echo "Another validated WinBoat gameplay task is active${guest_active_task:+: $guest_active_task}; leaving $winboat_task_name staged. Rerun this command to start or reattach safely." >&2
    exit 2
fi

read -r -d '' powershell_payload <<'POWERSHELL' || true
$ErrorActionPreference = "Stop"
$repo = '__WINBOAT_REPO__'
$build = '__WINBOAT_BUILD__'
$result = '__WINBOAT_RESULT__'
$commit = '__COMMIT__'
$havokArchive = '__GUEST_HAVOK_ARCHIVE__'
$planckDependencyRoot = '__GUEST_PLANCK_DEPENDENCY_ROOT__'
$resultRecord = '__GUEST_RESULT_RECORD__'
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
    & .\BuildCompleteSkyrimTogetherVR-Windows.ps1 -HavokArchive $havokArchive -DependencyRoot $planckDependencyRoot -Configuration Release
    if ($LASTEXITCODE -ne 0) { throw "Complete patched-PLANCK gameplay build failed with exit code $LASTEXITCODE." }

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

    @(
        "STVR_BUILD_COMMIT=$commit"
        "STVR_BUILD_RESULT=$result"
        "STVR_GAMEPLAY_PACKAGE=$resultPackage"
        "STVR_BUILD_EVIDENCE=$resultEvidence"
    ) | Set-Content -LiteralPath $resultRecord -Encoding UTF8

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
powershell_payload=${powershell_payload//__GUEST_HAVOK_ARCHIVE__/$guest_havok_archive}
powershell_payload=${powershell_payload//__GUEST_PLANCK_DEPENDENCY_ROOT__/$guest_planck_dependency_root}
powershell_payload=${powershell_payload//__GUEST_RESULT_RECORD__/$guest_result_record}

read -r -d '' launcher_payload <<'POWERSHELL' || true
$ErrorActionPreference = "Stop"
$payload = '__GUEST_PAYLOAD__'
$statePath = '__GUEST_STATE__'
$exitPath = '__GUEST_EXIT__'
$logPath = '__GUEST_LOG__'
$resultPath = '__GUEST_RESULT_RECORD__'
$mutexName = '__GUEST_MUTEX_NAME__'
$exitCode = 1
$mutex = New-Object System.Threading.Mutex($false, $mutexName)
$held = $false
try {
    $held = $mutex.WaitOne([TimeSpan]::FromSeconds(300))
    if (-not $held) { throw "Timed out waiting for WinBoat job lifecycle mutex: $mutexName" }
    Set-Content -LiteralPath $statePath -Value 'RUNNING' -Encoding ASCII -NoNewline
    & $payload *>&1 | Tee-Object -FilePath $logPath -Append
    if ($LASTEXITCODE -ne 0) { throw "Build payload failed with exit code $LASTEXITCODE." }
    if (-not (Test-Path -LiteralPath $resultPath -PathType Leaf)) { throw "Build payload did not produce $resultPath." }
    $exitCode = 0
    Set-Content -LiteralPath $statePath -Value 'COMPLETE' -Encoding ASCII -NoNewline
} catch {
    $_ | Out-String | Tee-Object -FilePath $logPath -Append | Out-Null
    Set-Content -LiteralPath $statePath -Value 'FAILED' -Encoding ASCII -NoNewline
} finally {
    Set-Content -LiteralPath $exitPath -Value $exitCode -Encoding ASCII -NoNewline
    if ($held) { [void]$mutex.ReleaseMutex() }
    $mutex.Dispose()
}
exit $exitCode
POWERSHELL

launcher_payload=${launcher_payload//__GUEST_PAYLOAD__/$guest_payload}
launcher_payload=${launcher_payload//__GUEST_STATE__/$guest_state}
launcher_payload=${launcher_payload//__GUEST_EXIT__/$guest_exit}
launcher_payload=${launcher_payload//__GUEST_LOG__/$guest_log}
launcher_payload=${launcher_payload//__GUEST_RESULT_RECORD__/$guest_result_record}
launcher_payload=${launcher_payload//__GUEST_MUTEX_NAME__/$guest_mutex_name}

if [[ $job_status == STAGED || $job_status == FAILED ]]; then
    payload_file=$(mktemp "${TMPDIR:-/tmp}/stvr-winboat-build-${short_commit}-${job_identity}-XXXXXX.ps1")
    launcher_file=$(mktemp "${TMPDIR:-/tmp}/stvr-winboat-launcher-${short_commit}-${job_identity}-XXXXXX.ps1")
    printf '%s\n' "$powershell_payload" >"$payload_file"
    printf '%s\n' "$launcher_payload" >"$launcher_file"
    "$winboat_scp" to-guest "$payload_file" "$guest_payload"
    "$winboat_scp" to-guest "$launcher_file" "$guest_launcher"

    read -r -d '' task_start_payload <<'POWERSHELL' || true
$ErrorActionPreference = "Stop"
$taskName = '__TASK_NAME__'
$description = '__TASK_DESCRIPTION__'
$launcher = '__GUEST_LAUNCHER__'
$statePath = '__GUEST_STATE__'
$exitPath = '__GUEST_EXIT__'
$resultPath = '__GUEST_RESULT_RECORD__'
$windowsUser = '__WINDOWS_USER__'
$taskExecutable = '__TASK_EXECUTABLE__'
$taskArguments = '__TASK_ARGUMENTS__'
$mutexName = '__GUEST_MUTEX_NAME__'
$jobStatus = '__JOB_STATUS__'
$build = '__WINBOAT_BUILD__'
$result = '__WINBOAT_RESULT__'

function Assert-TaskContract {
    param($Task)
    if ($Task.Description -ne $description -or $Task.Actions.Count -ne 1 -or
        $Task.Actions[0].Execute -cne $taskExecutable -or $Task.Actions[0].Arguments -cne $taskArguments -or
        $Task.Principal.UserId -ine $windowsUser -or $Task.Principal.LogonType.ToString() -cne 'S4U') {
        throw "Existing scheduled task does not match the requested WinBoat job identity."
    }
}

$task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($null -ne $task) {
    Assert-TaskContract $task
} else {
    if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) { throw "Durable WinBoat launcher is missing: $launcher" }
    $action = New-ScheduledTaskAction -Execute $taskExecutable -Argument $taskArguments
    $principal = New-ScheduledTaskPrincipal -UserId $windowsUser -LogonType S4U -RunLevel Limited
    $settings = New-ScheduledTaskSettingsSet -MultipleInstances IgnoreNew -StartWhenAvailable
    Register-ScheduledTask -TaskName $taskName -Action $action -Principal $principal -Settings $settings -Description $description -Force | Out-Null
    $task = Get-ScheduledTask -TaskName $taskName -ErrorAction Stop
    Assert-TaskContract $task
}
if ($jobStatus -eq 'FAILED') {
    $mutex = New-Object System.Threading.Mutex($false, $mutexName)
    $held = $false
    try {
        $held = $mutex.WaitOne(0)
        if (-not $held) { throw "Another validated WinBoat task is active; rerun to reattach instead of restarting $taskName." }
        Remove-Item -LiteralPath $build, $result -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $exitPath, $resultPath -Force -ErrorAction SilentlyContinue
    } finally {
        if ($held) { [void]$mutex.ReleaseMutex() }
        $mutex.Dispose()
    }
}
if (($jobStatus -eq 'STAGED' -or $jobStatus -eq 'FAILED') -and $task.State -ne 'Running') {
    Set-Content -LiteralPath $statePath -Value 'STARTING' -Encoding ASCII -NoNewline
    Start-ScheduledTask -TaskName $taskName
}
POWERSHELL
    task_start_payload=${task_start_payload//__TASK_NAME__/$winboat_task_name}
    task_start_payload=${task_start_payload//__TASK_DESCRIPTION__/$task_description}
    task_start_payload=${task_start_payload//__GUEST_LAUNCHER__/$guest_launcher}
    task_start_payload=${task_start_payload//__GUEST_STATE__/$guest_state}
    task_start_payload=${task_start_payload//__GUEST_EXIT__/$guest_exit}
    task_start_payload=${task_start_payload//__GUEST_RESULT_RECORD__/$guest_result_record}
    task_start_payload=${task_start_payload//__WINDOWS_USER__/$winboat_windows_user}
    task_start_payload=${task_start_payload//__TASK_EXECUTABLE__/$task_executable}
    task_start_payload=${task_start_payload//__TASK_ARGUMENTS__/$task_arguments}
    task_start_payload=${task_start_payload//__GUEST_MUTEX_NAME__/$guest_mutex_name}
    task_start_payload=${task_start_payload//__JOB_STATUS__/$job_status}
    task_start_payload=${task_start_payload//__WINBOAT_BUILD__/$winboat_build}
    task_start_payload=${task_start_payload//__WINBOAT_RESULT__/$winboat_result}
    run_winboat_powershell "$task_start_payload"
fi

read -r -d '' task_status_payload <<'POWERSHELL' || true
$ErrorActionPreference = "Stop"
$taskName = '__TASK_NAME__'
$description = '__TASK_DESCRIPTION__'
$identityPath = '__GUEST_JOB_ROOT__/identity.json'
$statePath = '__GUEST_STATE__'
$exitPath = '__GUEST_EXIT__'
$resultPath = '__GUEST_RESULT_RECORD__'
$logPath = '__GUEST_LOG__'
$jobIdentity = '__JOB_IDENTITY__'
$commit = '__COMMIT__'
$windowsUser = '__WINDOWS_USER__'
$taskExecutable = '__TASK_EXECUTABLE__'
$taskArguments = '__TASK_ARGUMENTS__'
$graceSeconds = __TASK_GRACE_SECONDS__
$expectedResult = '__WINBOAT_RESULT__'
if (-not (Test-Path -LiteralPath $identityPath -PathType Leaf)) { throw "WinBoat job identity record is missing: $identityPath" }
$identity = Get-Content -LiteralPath $identityPath -Raw | ConvertFrom-Json
if ($identity.jobIdentity -ne $jobIdentity -or $identity.commit -ne $commit -or $identity.taskName -ne $taskName -or
    $identity.windowsUser -ine $windowsUser -or $identity.taskExecutable -cne $taskExecutable -or
    $identity.taskArguments -cne $taskArguments) {
    throw "WinBoat job identity changed while polling."
}

function Assert-TaskContract {
    param($Task)
    if ($Task.Description -ne $description -or $Task.Actions.Count -ne 1 -or $Task.Actions[0].Execute -cne $taskExecutable -or
        $Task.Actions[0].Arguments -cne $taskArguments -or $Task.Principal.UserId -ine $windowsUser -or
        $Task.Principal.LogonType.ToString() -cne 'S4U') {
        throw "WinBoat scheduled task contract changed while polling."
    }
}

function Test-ValidResult {
    if (-not (Test-Path -LiteralPath $exitPath -PathType Leaf) -or
        (Get-Content -LiteralPath $exitPath -Raw).Trim() -ne '0' -or
        -not (Test-Path -LiteralPath $resultPath -PathType Leaf)) { return $false }
    $record = @(Get-Content -LiteralPath $resultPath)
    return $record -contains "STVR_BUILD_COMMIT=$commit" -and $record -contains "STVR_BUILD_RESULT=$expectedResult"
}

$task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($null -ne $task) { Assert-TaskContract $task }
$state = if (Test-Path -LiteralPath $statePath -PathType Leaf) {
    (Get-Content -LiteralPath $statePath -Raw).Trim().ToUpperInvariant()
} else { '' }
if (Test-ValidResult) {
    $status = 'COMPLETE'
} elseif (Test-Path -LiteralPath $exitPath -PathType Leaf) {
    $status = 'FAILED'
} elseif ($null -eq $task) {
    $status = 'FAILED'
} elseif ($task.State -eq 'Running') {
    $status = 'RUNNING'
} else {
    $taskInfo = Get-ScheduledTaskInfo -TaskName $taskName -ErrorAction Stop
    $referenceTime = $taskInfo.LastRunTime.ToUniversalTime()
    if ($referenceTime.Year -lt 2000 -and (Test-Path -LiteralPath $statePath -PathType Leaf)) {
        $referenceTime = (Get-Item -LiteralPath $statePath).LastWriteTimeUtc
    }
    $withinGrace = $referenceTime.Year -ge 2000 -and (((Get-Date).ToUniversalTime() - $referenceTime).TotalSeconds -le $graceSeconds)
    $transientResult = [int64]($taskInfo.LastTaskResult) -in @(0, 267008, 267009, 267011)
    $status = if ($withinGrace -and $transientResult -and $state -in @('STARTING', 'RUNNING', 'COMPLETE')) { 'STARTING' } else { 'FAILED' }
}
"STVR_JOB_STATUS=$status"
"STVR_JOB_STATE_PATH=$statePath"
"STVR_JOB_EXIT_PATH=$exitPath"
"STVR_JOB_LOG_PATH=$logPath"
"STVR_JOB_RESULT_PATH=$resultPath"
POWERSHELL
task_status_payload=${task_status_payload//__TASK_NAME__/$winboat_task_name}
task_status_payload=${task_status_payload//__TASK_DESCRIPTION__/$task_description}
task_status_payload=${task_status_payload//__GUEST_JOB_ROOT__/$guest_job_root}
task_status_payload=${task_status_payload//__GUEST_STATE__/$guest_state}
task_status_payload=${task_status_payload//__GUEST_EXIT__/$guest_exit}
task_status_payload=${task_status_payload//__GUEST_RESULT_RECORD__/$guest_result_record}
task_status_payload=${task_status_payload//__GUEST_LOG__/$guest_log}
task_status_payload=${task_status_payload//__JOB_IDENTITY__/$job_identity}
task_status_payload=${task_status_payload//__COMMIT__/$commit}
task_status_payload=${task_status_payload//__WINDOWS_USER__/$winboat_windows_user}
task_status_payload=${task_status_payload//__TASK_EXECUTABLE__/$task_executable}
task_status_payload=${task_status_payload//__TASK_ARGUMENTS__/$task_arguments}
task_status_payload=${task_status_payload//__TASK_GRACE_SECONDS__/$task_grace_seconds}
task_status_payload=${task_status_payload//__WINBOAT_RESULT__/$winboat_result}

poll_started=$SECONDS
while :; do
    task_status_output=$(run_winboat_powershell "$task_status_payload")
    job_status=$(sed -n 's/^STVR_JOB_STATUS=//p' <<<"$task_status_output" | tail -n 1)
    case $job_status in
        COMPLETE)
            break
            ;;
        FAILED)
            echo "WinBoat scheduled task $winboat_task_name failed; inspect $guest_log." >&2
            exit 1
            ;;
        RUNNING|STARTING|STAGED)
            if ((SECONDS - poll_started >= timeout_seconds)); then
                echo "Timed out waiting for WinBoat scheduled task $winboat_task_name after ${timeout_seconds}s; rerun this command to reattach. Guest log: $guest_log" >&2
                exit 1
            fi
            sleep "$poll_seconds"
            ;;
        *)
            echo "WinBoat scheduled task $winboat_task_name returned no usable state while polling." >&2
            exit 2
            ;;
    esac
done

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
mkdir -p "$package_dir" "$evidence_dir"
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

if ((skip_handoff)); then
    printf 'STVR_LINUX_GAMEPLAY_PACKAGE=%s\n' "$package_zip"
    printf 'STVR_LINUX_BUILD_EVIDENCE=%s\n' "$evidence_copy"
    printf 'STVR_LOCAL_HANDOFF=SKIPPED (--skip-handoff: no handoff archive was created, overwritten, regenerated, audited, or unzipped)\n'
else
    handoff_dir="$repo_root/artifacts/SkyrimTogetherVR/review-handoff"
    mkdir -p "$handoff_dir"
    handoff_zip="$handoff_dir/SkyrimTogetherVR-local-agent-complete-handoff-${short_commit}-${timestamp}.zip"
    "$repo_root/Tools/SkyrimVR/finalize_local_agent_handoff.sh" \
        --gameplay-package "$package_zip" \
        --build-evidence "$evidence_copy" \
        --runtime-evidence "$runtime_evidence" \
        --output "$handoff_zip" \
        --upload-target "${STVR_HANDOFF_UPLOAD_TARGET:-foundry:videos/}"
    printf 'STVR_LINUX_GAMEPLAY_PACKAGE=%s\n' "$package_zip"
    printf 'STVR_LINUX_BUILD_EVIDENCE=%s\n' "$evidence_copy"
    printf 'STVR_LOCAL_HANDOFF=%s\n' "$handoff_zip"
fi
