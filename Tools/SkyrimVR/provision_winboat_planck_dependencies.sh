#!/usr/bin/env bash
set -euo pipefail

havok_sha256=7349946401a820784fc86aa13bc667def6c409ed938865b01c8e6c3d86692555
sksevr_source_sha256=edbb4945544718054279c9f949ac689e735b13c8efcd3272b6f74e2398dd5d53
default_havok_archive=/home/obesecatlord/Backup/Downloads/hk2010_2_0_r1.7z
default_sksevr_source=/home/obesecatlord/Documents/SkyrimModding/_deps/planck/sksevr-2.0.12/sksevr_2_00_12
default_guest_root='C:\Users\obesecatlord\AppData\Local\SkyrimTogetherVR\planck-build'

usage() {
    printf 'Usage: %s [--self-check] [--winboat-powershell PATH --winboat-scp PATH]\n' "${0##*/}" >&2
}

winboat_powershell=""
winboat_scp=""
self_check=false
while (($# > 0)); do
    case $1 in
        --winboat-powershell)
            (($# >= 2)) || { usage; exit 2; }
            winboat_powershell=$2
            shift 2
            ;;
        --winboat-scp)
            (($# >= 2)) || { usage; exit 2; }
            winboat_scp=$2
            shift 2
            ;;
        --self-check)
            self_check=true
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            usage
            exit 2
            ;;
    esac
done

if [[ $self_check != true ]]; then
    for helper in "$winboat_powershell" "$winboat_scp"; do
        if [[ ! -x $helper ]]; then
            echo "Required WinBoat helper is not executable: $helper" >&2
            exit 2
        fi
    done
fi

havok_archive=${STVR_HAVOK_ARCHIVE:-$default_havok_archive}
sksevr_source=${STVR_SKSEVR_SOURCE:-$default_sksevr_source}
guest_root=${STVR_WINBOAT_PLANCK_ROOT:-$default_guest_root}
for value in "$havok_archive" "$sksevr_source" "$guest_root"; do
    if [[ $value == *"'"* || $value == *$'\n'* || $value == *$'\r'* ]]; then
        echo "PLANCK dependency paths containing quotes or newlines are not supported." >&2
        exit 2
    fi
done

if [[ ! -f $havok_archive || -L $havok_archive ]]; then
    echo "Havok archive must be a regular, non-symlink file: $havok_archive" >&2
    exit 2
fi
actual_havok_sha256=$(sha256sum -- "$havok_archive" | awk '{print $1}')
if [[ $actual_havok_sha256 != "$havok_sha256" ]]; then
    echo "Havok archive SHA-256 mismatch. Expected $havok_sha256, got $actual_havok_sha256." >&2
    exit 2
fi

if [[ ! -d $sksevr_source || -L $sksevr_source ]]; then
    echo "Pinned SKSEVR source tree is missing or is a symlink: $sksevr_source" >&2
    exit 2
fi
for required in src/sksevr/skse64/PluginAPI.h src/sksevr/skse64_common/Relocation.h \
    src/sksevr/skse64/skse64.vcxproj src/sksevr/skse64_common/skse64_common.vcxproj src/common/IPrefix.h; do
    if [[ ! -f $sksevr_source/$required || -L $sksevr_source/$required ]]; then
        echo "Pinned SKSEVR source tree is incomplete: $sksevr_source/$required" >&2
        exit 2
    fi
done
hash_helper=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/hash_source_tree.py
if [[ ! -x $hash_helper ]]; then
    echo "Deterministic source-tree hash helper is not executable: $hash_helper" >&2
    exit 2
fi
hash_result=$(python3 "$hash_helper" "$sksevr_source" --expect "$sksevr_source_sha256")
read -r actual_sksevr_sha256 actual_sksevr_file_count <<<"$hash_result"
if [[ $actual_sksevr_sha256 != "$sksevr_source_sha256" || $actual_sksevr_file_count != 403 ]]; then
    echo "Pinned SKSEVR self-check did not return the expected digest and 403-file count: $hash_result" >&2
    exit 2
fi

if [[ $self_check == true ]]; then
    printf 'STVR_PLANCK_DEPENDENCY_SELF_CHECK=ok\n'
    printf 'STVR_HAVOK_ARCHIVE_SHA256=%s\n' "$actual_havok_sha256"
    printf 'STVR_SKSEVR_SOURCE_SHA256=%s\n' "$actual_sksevr_sha256"
    printf 'STVR_SKSEVR_SOURCE_FILE_COUNT=%s\n' "$actual_sksevr_file_count"
    exit 0
fi

guest_archive_dir="$guest_root\archives"
guest_dependency_root="$guest_root\dependencies"
guest_havok_archive="$guest_archive_dir\hk2010_2_0_r1.7z"
guest_sksevr_destination="$guest_dependency_root\sksevr_2_00_12"
transfer_nonce="$$-$(date -u +%Y%m%d%H%M%SZ)"
guest_havok_stage="$guest_archive_dir\.hk2010_2_0_r1.$transfer_nonce.tmp"
guest_sksevr_stage="$guest_dependency_root\.sksevr_2_00_12.$transfer_nonce.tmp"

read -r -d '' prepare_guest <<'POWERSHELL' || true
$ErrorActionPreference = 'Stop'
$root = '__GUEST_ROOT__'
$archiveDir = '__ARCHIVE_DIR__'
$dependencyRoot = '__DEPENDENCY_ROOT__'
$localAppData = [System.IO.Path]::GetFullPath($env:LOCALAPPDATA).TrimEnd('\')
$rootFull = [System.IO.Path]::GetFullPath($root).TrimEnd('\')
if (-not $rootFull.StartsWith($localAppData + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Durable PLANCK dependency root must remain under the guest user's LOCALAPPDATA: $rootFull"
}
foreach ($path in @($rootFull, $archiveDir, $dependencyRoot)) {
    if (-not (Test-Path -LiteralPath $path)) { New-Item -ItemType Directory -Path $path | Out-Null }
    $item = Get-Item -LiteralPath $path -Force
    if (-not $item.PSIsContainer -or ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "PLANCK dependency path is not a private regular directory: $path"
    }
}
POWERSHELL
prepare_guest=${prepare_guest//__GUEST_ROOT__/$guest_root}
prepare_guest=${prepare_guest//__ARCHIVE_DIR__/$guest_archive_dir}
prepare_guest=${prepare_guest//__DEPENDENCY_ROOT__/$guest_dependency_root}
"$winboat_powershell" "$prepare_guest" >/dev/null

guest_archive_state=$("$winboat_powershell" "if (Test-Path -LiteralPath '$guest_havok_archive' -PathType Leaf) { \$item = Get-Item -LiteralPath '$guest_havok_archive' -Force; if ((\$item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Durable Havok archive is a reparse point.' }; (Get-FileHash -LiteralPath \$item.FullName -Algorithm SHA256).Hash.ToLowerInvariant() } else { 'MISSING' }")
guest_archive_state=${guest_archive_state//$'\r'/}
if [[ $guest_archive_state == MISSING ]]; then
    "$winboat_scp" to-guest "$havok_archive" "${guest_havok_stage//\\//}"
    "$winboat_powershell" "if ((Get-FileHash -LiteralPath '$guest_havok_stage' -Algorithm SHA256).Hash.ToLowerInvariant() -ne '$havok_sha256') { throw 'Transferred Havok archive hash mismatch.' }; Move-Item -LiteralPath '$guest_havok_stage' -Destination '$guest_havok_archive'" >/dev/null
elif [[ $guest_archive_state != "$havok_sha256" ]]; then
    echo "Durable guest Havok archive has untrusted provenance: $guest_havok_archive" >&2
    exit 2
fi

read -r -d '' verify_sksevr <<'POWERSHELL' || true
$ErrorActionPreference = 'Stop'
$root = '__SKSEVR_ROOT__'
if (-not (Test-Path -LiteralPath $root -PathType Container)) { 'MISSING'; exit 0 }
$item = Get-Item -LiteralPath $root -Force
if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Durable SKSEVR source root is a reparse point.' }
$rootFull = $item.FullName.TrimEnd('\')
foreach ($directory in Get-ChildItem -LiteralPath $root -Recurse -Force -Directory) {
    if (($directory.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { throw "SKSEVR source contains a reparse directory: $($directory.FullName)" }
}
$relativePaths = [System.Collections.Generic.List[string]]::new()
$filesByRelativePath = [System.Collections.Generic.Dictionary[string,string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -Force -File) {
    if (($file.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { throw "SKSEVR source contains a reparse file: $($file.FullName)" }
    $relative = $file.FullName.Substring($rootFull.Length).TrimStart('\').Replace('\', '/')
    foreach ($character in $relative.ToCharArray()) {
        if ([int]$character -gt 127 -or $character -eq "`0" -or $character -eq "`r" -or $character -eq "`n") {
            throw "SKSEVR source path is outside the canonical ASCII manifest format: $relative"
        }
    }
    if ($filesByRelativePath.ContainsKey($relative)) { throw "SKSEVR source contains a case-aliased path: $relative" }
    $filesByRelativePath.Add($relative, $file.FullName)
    $relativePaths.Add($relative)
}
$relativePaths.Sort([System.StringComparer]::Ordinal)
$records = [System.Collections.Generic.List[string]]::new()
foreach ($relative in $relativePaths) {
    $hash = (Get-FileHash -LiteralPath $filesByRelativePath[$relative] -Algorithm SHA256).Hash.ToLowerInvariant()
    $records.Add("$hash  $relative`n")
}
$bytes = [System.Text.UTF8Encoding]::new($false).GetBytes(($records -join ''))
$digest = [System.Security.Cryptography.SHA256]::Create()
try {
    $treeHash = ([System.BitConverter]::ToString($digest.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    "$treeHash  $($relativePaths.Count)"
}
finally { $digest.Dispose() }
POWERSHELL
verify_sksevr=${verify_sksevr//__SKSEVR_ROOT__/$guest_sksevr_destination}
guest_sksevr_state=$("$winboat_powershell" "$verify_sksevr")
guest_sksevr_state=${guest_sksevr_state//$'\r'/}
if [[ $guest_sksevr_state == MISSING ]]; then
    "$winboat_scp" to-guest "$sksevr_source" "${guest_sksevr_stage//\\//}" --recursive
    # Bash pattern substitution treats backslashes in the pattern/replacement as
    # escapes, and the guest paths are backslash-separated. Escape them so the
    # staged verify targets the just-transferred stage directory, not the
    # not-yet-created destination.
    staged_destination_pattern=${guest_sksevr_destination//\\/\\\\}
    staged_stage_replacement=${guest_sksevr_stage//\\/\\\\}
    staged_verify=${verify_sksevr//$staged_destination_pattern/$staged_stage_replacement}
    staged_verify_file=$(mktemp "${TMPDIR:-/tmp}/stvr-winboat-sksevr-verify-XXXXXX.ps1")
    guest_verify_script="$guest_dependency_root\.verify-sksevr-$transfer_nonce.ps1"
    printf '%s\n' "$staged_verify" >"$staged_verify_file"
    "$winboat_scp" to-guest "$staged_verify_file" "${guest_verify_script//\\//}"
    staged_status=0
    staged_result=$("$winboat_powershell" "& '$guest_verify_script'") || staged_status=$?
    rm -f -- "$staged_verify_file"
    "$winboat_powershell" "Remove-Item -LiteralPath '$guest_verify_script' -Force -ErrorAction SilentlyContinue" >/dev/null || true
    if ((staged_status != 0)); then
        "$winboat_powershell" "Remove-Item -LiteralPath '$guest_sksevr_stage' -Recurse -Force -ErrorAction SilentlyContinue" >/dev/null || true
        echo "Transferred SKSEVR source-tree verification failed." >&2
        exit "$staged_status"
    fi
    staged_result=${staged_result//$'\r'/}
    read -r staged_hash staged_file_count <<<"$staged_result"
    if [[ $staged_hash != "$sksevr_source_sha256" || $staged_file_count != 403 ]]; then
        "$winboat_powershell" "Remove-Item -LiteralPath '$guest_sksevr_stage' -Recurse -Force -ErrorAction SilentlyContinue" >/dev/null || true
        echo "Transferred SKSEVR source-tree provenance mismatch: $staged_result" >&2
        exit 2
    fi
    "$winboat_powershell" "if (Test-Path -LiteralPath '$guest_sksevr_destination') { throw 'SKSEVR destination appeared during transfer.' }; Move-Item -LiteralPath '$guest_sksevr_stage' -Destination '$guest_sksevr_destination'" >/dev/null
else
    read -r guest_sksevr_hash guest_sksevr_file_count <<<"$guest_sksevr_state"
    if [[ $guest_sksevr_hash != "$sksevr_source_sha256" || $guest_sksevr_file_count != 403 ]]; then
        echo "Durable guest SKSEVR source tree has untrusted provenance: $guest_sksevr_destination ($guest_sksevr_state)" >&2
        exit 2
    fi
fi

printf 'STVR_GUEST_HAVOK_ARCHIVE=%s\n' "$guest_havok_archive"
printf 'STVR_GUEST_PLANCK_DEPENDENCY_ROOT=%s\n' "$guest_dependency_root"
printf 'STVR_HAVOK_ARCHIVE_SHA256=%s\n' "$havok_sha256"
printf 'STVR_SKSEVR_SOURCE_SHA256=%s\n' "$sksevr_source_sha256"
printf 'STVR_SKSEVR_SOURCE_FILE_COUNT=%s\n' "$actual_sksevr_file_count"
