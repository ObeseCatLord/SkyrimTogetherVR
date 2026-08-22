[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$HavokArchive,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$DependencyRoot,

    [string]$SKSEVRArchive,
    [string]$SevenZipPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$HavokSha256 = '7349946401a820784fc86aa13bc667def6c409ed938865b01c8e6c3d86692555'
$SKSEVRSha256 = 'f03df5d8663f2c9a781f830fb0809c63a9a0e3b626d6d1a96e38493f81a3c9ad'
$SKSEVRUrl = 'https://skse.silverlock.org/beta/sksevr_2_00_12.7z'

function Get-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath([Environment]::ExpandEnvironmentVariables($Path))
}

function Test-PathWithin([string]$Candidate, [string]$Parent) {
    $separators = [char[]]@([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
    $candidateFull = (Get-FullPath $Candidate).TrimEnd($separators)
    $parentFull = (Get-FullPath $Parent).TrimEnd($separators)
    return $candidateFull.Equals($parentFull, [System.StringComparison]::OrdinalIgnoreCase) -or
        $candidateFull.StartsWith($parentFull + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)
}

function Assert-ArchiveHash([string]$Path, [string]$ExpectedHash, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label archive was not found: $Path"
    }
    $item = Get-Item -LiteralPath $Path -Force
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label archive must be a regular file, not a reparse point: $Path"
    }

    $actualHash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $ExpectedHash) {
        throw "$Label archive SHA256 mismatch. Expected $ExpectedHash, got $actualHash: $Path"
    }
}

function Get-SevenZip([string]$RequestedPath) {
    if ($RequestedPath) {
        $resolved = Get-FullPath $RequestedPath
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            throw "SevenZipPath does not exist: $resolved"
        }
        return $resolved
    }

    $command = Get-Command 7z.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $sevenZipCandidates = @(${env:ProgramFiles}, ${env:ProgramFiles(x86)}) |
        Where-Object { $_ } |
        ForEach-Object { Join-Path $_ '7-Zip\7z.exe' }
    foreach ($candidate in $sevenZipCandidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return $candidate
        }
    }

    throw '7z.exe was not found. Install 7-Zip, add it to PATH, or pass -SevenZipPath C:\Path\To\7z.exe.'
}

function Invoke-SevenZipExtract([string]$Executable, [string]$Archive, [string]$Destination, [string[]]$Paths) {
    & $Executable x '-y' "-o$Destination" $Archive @Paths
    if ($LASTEXITCODE -ne 0) {
        throw "7-Zip failed extracting '$Archive' (exit code $LASTEXITCODE)."
    }
}

function Get-SourceTreeManifest([string]$Root, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        throw "$Label source-tree root is missing: $Root"
    }
    $rootItem = Get-Item -LiteralPath $Root -Force
    if (($rootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label source-tree root is a reparse point: $Root"
    }

    $rootFull = $rootItem.FullName.TrimEnd([char[]]@('\', '/'))
    foreach ($directory in Get-ChildItem -LiteralPath $rootFull -Recurse -Force -Directory) {
        if (($directory.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Label source tree contains a reparse directory: $($directory.FullName)"
        }
    }

    $relativePaths = [System.Collections.Generic.List[string]]::new()
    $filesByRelativePath = [System.Collections.Generic.Dictionary[string,string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($file in Get-ChildItem -LiteralPath $rootFull -Recurse -Force -File) {
        if (($file.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Label source tree contains a reparse file: $($file.FullName)"
        }
        $relative = $file.FullName.Substring($rootFull.Length).TrimStart([char[]]@('\', '/')).Replace('\', '/')
        foreach ($character in $relative.ToCharArray()) {
            if ([int]$character -gt 127 -or $character -eq "`0" -or $character -eq "`r" -or $character -eq "`n") {
                throw "$Label source-tree path is outside the canonical ASCII manifest format: $relative"
            }
        }
        if ($filesByRelativePath.ContainsKey($relative)) {
            throw "$Label source tree contains a case-aliased path: $relative"
        }
        $filesByRelativePath.Add($relative, $file.FullName)
        $relativePaths.Add($relative)
    }
    if ($relativePaths.Count -eq 0) {
        throw "$Label source tree is empty: $Root"
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
        return [pscustomobject]@{
            sha256 = ([System.BitConverter]::ToString($digest.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
            fileCount = $relativePaths.Count
        }
    }
    finally {
        $digest.Dispose()
    }
}

function Assert-SameSourceTreeManifest([object]$Expected, [object]$Actual, [string]$Label) {
    if ($Expected.sha256 -ne $Actual.sha256 -or $Expected.fileCount -ne $Actual.fileCount) {
        throw "$Label source-tree provenance mismatch. Expected $($Expected.sha256) ($($Expected.fileCount) files), got $($Actual.sha256) ($($Actual.fileCount) files)."
    }
}

$repoRoot = Get-FullPath (Join-Path $PSScriptRoot '..\..')
$DependencyRoot = Get-FullPath $DependencyRoot
if (Test-PathWithin $DependencyRoot $repoRoot) {
    throw "DependencyRoot must be outside the repository so SDK content is never stored in git. Choose a path outside '$repoRoot'."
}

$HavokArchive = Get-FullPath $HavokArchive
Assert-ArchiveHash $HavokArchive $HavokSha256 'Havok 2010.2'

New-Item -ItemType Directory -Force -Path $DependencyRoot | Out-Null
$dependencyRootItem = Get-Item -LiteralPath $DependencyRoot -Force
if (($dependencyRootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "DependencyRoot must be a private regular directory, not a reparse point: $DependencyRoot"
}
if ($SKSEVRArchive) {
    $SKSEVRArchive = Get-FullPath $SKSEVRArchive
} else {
    $downloadDirectory = Join-Path $DependencyRoot 'archives'
    $SKSEVRArchive = Join-Path $downloadDirectory 'sksevr_2_00_12.7z'
    if (-not (Test-Path -LiteralPath $SKSEVRArchive -PathType Leaf)) {
        New-Item -ItemType Directory -Force -Path $downloadDirectory | Out-Null
        Write-Host "Downloading official SKSEVR 2.0.12 source archive to $SKSEVRArchive"
        Invoke-WebRequest -UseBasicParsing -Uri $SKSEVRUrl -OutFile $SKSEVRArchive
    }
}
Assert-ArchiveHash $SKSEVRArchive $SKSEVRSha256 'SKSEVR 2.0.12'

$sevenZip = Get-SevenZip $SevenZipPath
$havokSource = Join-Path $DependencyRoot 'havok2010_2_0_r1\Source'
$sksevrSource = Join-Path $DependencyRoot 'sksevr_2_00_12\src\sksevr'
$skseCommonSource = Join-Path $DependencyRoot 'sksevr_2_00_12\src\common'
$havokDestination = Join-Path $DependencyRoot 'havok2010_2_0_r1'
$havokStage = Join-Path $DependencyRoot ('.planck-havok-stage-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $havokStage | Out-Null
    # Extract a fresh canonical copy on every run. The complete extracted build-input
    # tree, not a few representative headers, is then compared before reuse.
    Invoke-SevenZipExtract $sevenZip $HavokArchive $havokStage @('Source\Common\*', 'Source\Physics\*', 'Source\Animation\*')
    $freshHavokManifest = Get-SourceTreeManifest (Join-Path $havokStage 'Source') 'Fresh Havok'
    if (Test-Path -LiteralPath $havokDestination) {
        $havokDestinationItem = Get-Item -LiteralPath $havokDestination -Force
        if (($havokDestinationItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Existing Havok dependency root is a reparse point: $havokDestination"
        }
        $existingHavokManifest = Get-SourceTreeManifest $havokSource 'Existing Havok'
        Assert-SameSourceTreeManifest $freshHavokManifest $existingHavokManifest 'Existing Havok'
        Write-Host "Verified complete existing Havok build-input tree at $havokSource ($($freshHavokManifest.fileCount) files)"
    }
    else {
        New-Item -ItemType Directory -Path $havokDestination | Out-Null
        Move-Item -LiteralPath (Join-Path $havokStage 'Source') -Destination $havokSource
        Write-Host "Prepared complete Havok build-input tree at $havokSource ($($freshHavokManifest.fileCount) files)"
    }
}
finally {
    if (Test-Path -LiteralPath $havokStage) {
        Remove-Item -LiteralPath $havokStage -Recurse -Force
    }
}

$skseDestination = Join-Path $DependencyRoot 'sksevr_2_00_12'
$skseStage = Join-Path $DependencyRoot ('.planck-sksevr-stage-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $skseStage | Out-Null
    Invoke-SevenZipExtract $sevenZip $SKSEVRArchive $skseStage @('sksevr_2_00_12\src\*')
    $freshSkseManifest = Get-SourceTreeManifest (Join-Path $skseStage 'sksevr_2_00_12') 'Fresh SKSEVR'
    if (Test-Path -LiteralPath $skseDestination) {
        $existingSkseManifest = Get-SourceTreeManifest $skseDestination 'Existing SKSEVR'
        Assert-SameSourceTreeManifest $freshSkseManifest $existingSkseManifest 'Existing SKSEVR'
        Write-Host "Verified complete existing SKSEVR build-input tree at $skseDestination ($($freshSkseManifest.fileCount) files)"
    }
    else {
        Move-Item -LiteralPath (Join-Path $skseStage 'sksevr_2_00_12') -Destination $skseDestination
        Write-Host "Prepared complete SKSEVR build-input tree at $skseDestination ($($freshSkseManifest.fileCount) files)"
    }
}
finally {
    if (Test-Path -LiteralPath $skseStage) {
        Remove-Item -LiteralPath $skseStage -Recurse -Force
    }
}

$dependencyProvenancePath = Join-Path $DependencyRoot '.stvr-planck-dependency-provenance.json'
if (Test-Path -LiteralPath $dependencyProvenancePath) {
    $dependencyProvenanceItem = Get-Item -LiteralPath $dependencyProvenancePath -Force
    if (($dependencyProvenanceItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "PLANCK dependency provenance path is a reparse point: $dependencyProvenancePath"
    }
}
$dependencyProvenance = [ordered]@{
    schema = 'stvr_planck_dependency_provenance_v1'
    havokArchiveSha256 = $HavokSha256
    havokSourceTreeSha256 = $freshHavokManifest.sha256
    havokSourceFileCount = $freshHavokManifest.fileCount
    sksevrArchiveSha256 = $SKSEVRSha256
    sksevrSourceTreeSha256 = $freshSkseManifest.sha256
    sksevrSourceFileCount = $freshSkseManifest.fileCount
}
[System.IO.File]::WriteAllText($dependencyProvenancePath, ($dependencyProvenance | ConvertTo-Json -Depth 3) + [Environment]::NewLine, (New-Object System.Text.UTF8Encoding($false)))

Write-Host "PLANCK_HAVOK2010_SOURCE=$havokSource"
Write-Host "PLANCK_SKSEVR_SOURCE_ROOT=$sksevrSource"
Write-Host "PLANCK_SKSE_COMMON_SOURCE_ROOT=$skseCommonSource"
Write-Host "PLANCK_DEPENDENCY_PROVENANCE=$dependencyProvenancePath"
Write-Host "PLANCK_HAVOK_ARCHIVE_SHA256=$($dependencyProvenance.havokArchiveSha256)"
Write-Host "PLANCK_HAVOK_SOURCE_TREE_SHA256=$($dependencyProvenance.havokSourceTreeSha256)"
Write-Host "PLANCK_HAVOK_SOURCE_FILE_COUNT=$($dependencyProvenance.havokSourceFileCount)"
Write-Host "PLANCK_SKSEVR_ARCHIVE_SHA256=$($dependencyProvenance.sksevrArchiveSha256)"
Write-Host "PLANCK_SKSEVR_SOURCE_TREE_SHA256=$($dependencyProvenance.sksevrSourceTreeSha256)"
Write-Host "PLANCK_SKSEVR_SOURCE_FILE_COUNT=$($dependencyProvenance.sksevrSourceFileCount)"
