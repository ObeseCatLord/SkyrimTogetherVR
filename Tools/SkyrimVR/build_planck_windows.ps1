[CmdletBinding()]
param(
    [string]$RepoRoot = (Join-Path $PSScriptRoot '..\..'),
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$HavokArchive,
    [string]$DependencyRoot,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$SkyrimVRPath,
    [string]$SKSEVRArchive,
    [string]$SevenZipPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

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

function Get-DependencyTreeManifest {
    param([Parameter(Mandatory = $true)][string]$Root, [Parameter(Mandatory = $true)][string]$Label)

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

function Resolve-DependencyProvenance {
    param([Parameter(Mandatory = $true)][string]$Root)

    $path = Join-Path $Root '.stvr-planck-dependency-provenance.json'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "PLANCK dependency preparation did not write its provenance manifest: $path"
    }
    $provenanceItem = Get-Item -LiteralPath $path -Force
    if (($provenanceItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "PLANCK dependency provenance must be a regular file, not a reparse point: $path"
    }
    try { $provenance = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json }
    catch { throw "PLANCK dependency provenance is not valid JSON: $path ($($_.Exception.Message))" }
    if ($provenance.schema -ne 'stvr_planck_dependency_provenance_v1') {
        throw "PLANCK dependency provenance has an unsupported schema: $path"
    }
    foreach ($field in @('havokArchiveSha256', 'havokSourceTreeSha256', 'sksevrArchiveSha256', 'sksevrSourceTreeSha256')) {
        $fieldValue = $provenance.PSObject.Properties[$field].Value
        if ($fieldValue -notmatch '^[0-9a-fA-F]{64}$') {
            throw "PLANCK dependency provenance field '$field' is missing or invalid: $path"
        }
    }
    foreach ($field in @('havokSourceFileCount', 'sksevrSourceFileCount')) {
        $parsedCount = 0L
        $fieldValue = $provenance.PSObject.Properties[$field].Value
        if (-not [long]::TryParse([string]$fieldValue, [ref]$parsedCount) -or $parsedCount -le 0) {
            throw "PLANCK dependency provenance field '$field' is missing or invalid: $path"
        }
    }
    return $provenance
}

function Assert-DependencyTreesMatchProvenance {
    param([Parameter(Mandatory = $true)][object]$Provenance, [Parameter(Mandatory = $true)][string]$Root)

    $treeChecks = @(
        @{ Label = 'Havok'; Root = (Join-Path $Root 'havok2010_2_0_r1\Source'); Hash = $Provenance.havokSourceTreeSha256; Count = $Provenance.havokSourceFileCount },
        @{ Label = 'SKSEVR'; Root = (Join-Path $Root 'sksevr_2_00_12'); Hash = $Provenance.sksevrSourceTreeSha256; Count = $Provenance.sksevrSourceFileCount }
    )
    foreach ($check in $treeChecks) {
        $actual = Get-DependencyTreeManifest -Root $check.Root -Label $check.Label
        if ($actual.sha256 -ne $check.Hash.ToLowerInvariant() -or $actual.fileCount -ne $check.Count) {
            throw "$($check.Label) dependency tree no longer matches verified provenance. Expected $($check.Hash) ($($check.Count) files), got $($actual.sha256) ($($actual.fileCount) files)."
        }
    }
}

function Get-MSBuildPath {
    $vswhereCandidates = @()
    $pathCommand = Get-Command vswhere.exe -ErrorAction SilentlyContinue
    if ($pathCommand) {
        $vswhereCandidates += $pathCommand.Source
    }
    $vswhereInstallCandidates = @(${env:ProgramFiles(x86)}, ${env:ProgramFiles}) |
        Where-Object { $_ } |
        ForEach-Object { Join-Path $_ 'Microsoft Visual Studio\Installer\vswhere.exe' }
    foreach ($candidate in $vswhereInstallCandidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            $vswhereCandidates += $candidate
        }
    }

    foreach ($vswhere in ($vswhereCandidates | Select-Object -Unique)) {
        $msbuild = @(& $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1)
        if ($LASTEXITCODE -eq 0 -and $msbuild.Count -eq 1 -and (Test-Path -LiteralPath $msbuild[0] -PathType Leaf)) {
            return $msbuild[0]
        }
    }

    throw 'MSBuild was not found through vswhere. Install Visual Studio Build Tools with the MSBuild and C++ desktop workload components.'
}

function Get-LatestWindowsSdkVersion {
    # The pinned SKSEVR vc14 projects hardcode WindowsTargetPlatformVersion 8.1,
    # which is not installed on current build hosts. Resolve the newest installed
    # Windows 10/11 SDK so the command-line property override below retargets the
    # build without modifying the hash-verified SKSEVR source tree.
    $includeRoot = 'C:\Program Files (x86)\Windows Kits\10\Include'
    if (-not (Test-Path -LiteralPath $includeRoot -PathType Container)) {
        throw "Windows SDK include root was not found: $includeRoot"
    }
    $versions = @(Get-ChildItem -LiteralPath $includeRoot -Directory |
        Where-Object { $_.Name -match '^10\.0\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending |
        Select-Object -ExpandProperty Name)
    if (-not $versions -or $versions.Count -eq 0) {
        throw "No Windows 10/11 SDK version was found under $includeRoot"
    }
    return $versions[0]
}

function Get-PlanckSourceTreeSha256 {
    param([Parameter(Mandatory = $true)][string]$SourceRoot)

    $temporaryPath = [System.IO.Path]::GetTempFileName()
    $excludedDirectoryNames = @('.git', '.vs', 'x64', 'build', 'artifacts', '__pycache__')
    try {
        $manifestStream = [System.IO.File]::Open($temporaryPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
        try {
            Get-ChildItem -LiteralPath $SourceRoot -Recurse -Force -File | Where-Object {
                $relativePath = $_.FullName.Substring($SourceRoot.Length).TrimStart([char[]]@('\', '/'))
                -not (($relativePath -split '[\\/]') | Where-Object { $excludedDirectoryNames -contains $_ })
            } | Sort-Object FullName | ForEach-Object {
                $relativePath = $_.FullName.Substring($SourceRoot.Length).TrimStart([char[]]@('\', '/')).Replace('\', '/')
                $pathBytes = [System.Text.Encoding]::UTF8.GetBytes($relativePath + "`0")
                $manifestStream.Write($pathBytes, 0, $pathBytes.Length)
                $sourceStream = [System.IO.File]::OpenRead($_.FullName)
                try { $sourceStream.CopyTo($manifestStream) }
                finally { $sourceStream.Dispose() }
                $manifestStream.Write([byte[]]@(0), 0, 1)
            }
        }
        finally { $manifestStream.Dispose() }
        return (Get-FileHash -LiteralPath $temporaryPath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    finally {
        Remove-Item -Force -LiteralPath $temporaryPath -ErrorAction SilentlyContinue
    }
}

$RepoRoot = Get-FullPath $RepoRoot
if (-not (Test-Path -LiteralPath (Join-Path $RepoRoot '.git'))) {
    throw "RepoRoot is not a repository root: $RepoRoot"
}
if (-not $DependencyRoot) {
    $DependencyRoot = Join-Path (Split-Path -Parent $RepoRoot) 'SkyrimTogetherVR-planck-dependencies'
}
$DependencyRoot = Get-FullPath $DependencyRoot
if (Test-PathWithin $DependencyRoot $RepoRoot) {
    throw "DependencyRoot must be outside RepoRoot so SDK content is never stored in git: $DependencyRoot"
}

$project = Join-Path $RepoRoot 'Libraries\activeragdoll\activeragdoll.vcxproj'
if (-not (Test-Path -LiteralPath $project -PathType Leaf)) {
    throw "PLANCK project was not found: $project"
}

$prepareArguments = @{
    HavokArchive = $HavokArchive
    DependencyRoot = $DependencyRoot
}
if ($SKSEVRArchive) { $prepareArguments.SKSEVRArchive = $SKSEVRArchive }
if ($SevenZipPath) { $prepareArguments.SevenZipPath = $SevenZipPath }
# Fresh archive extraction verifies every dependency input file before a forced
# rebuild, including durable roots that otherwise look complete.
& (Join-Path $PSScriptRoot 'prepare_planck_dependencies.ps1') @prepareArguments
$dependencyProvenance = Resolve-DependencyProvenance -Root $DependencyRoot
Assert-DependencyTreesMatchProvenance -Provenance $dependencyProvenance -Root $DependencyRoot

$msbuild = Get-MSBuildPath
$projectDirectory = Split-Path -Parent $project
$planckSourceRoot = $projectDirectory
$havokSource = Join-Path $DependencyRoot 'havok2010_2_0_r1\Source'
$sksevrSource = Join-Path $DependencyRoot 'sksevr_2_00_12\src\sksevr'
$skseCommonSource = Join-Path $DependencyRoot 'sksevr_2_00_12\src\common'

$msbuildArguments = @(
    $project,
    '/t:Rebuild',
    "/p:Configuration=$Configuration",
    '/p:Platform=x64',
    "/p:WindowsTargetPlatformVersion=$(Get-LatestWindowsSdkVersion)",
    '/p:PlatformToolset=v143',
    "/p:SolutionDir=$sksevrSource\",
    "/p:Havok2010Source=$havokSource",
    "/p:SKSEVRSourceRoot=$sksevrSource",
    "/p:SKSECommonSourceRoot=$skseCommonSource"
)
if ($SkyrimVRPath) {
    $msbuildArguments += "/p:SkyrimVRPath=$(Get-FullPath $SkyrimVRPath)"
}

$artifact = Join-Path $projectDirectory "x64\$Configuration\activeragdoll.dll"
$provenancePath = "$artifact.stvr-planck-provenance.json"
$git = Get-Command git -ErrorAction Stop
$planckCommit = (& $git.Source -C $planckSourceRoot rev-parse --verify 'HEAD^{commit}').Trim()
if ($LASTEXITCODE -ne 0 -or $planckCommit -notmatch '^[0-9a-fA-F]{40}$') {
    throw "Could not resolve the exact PLANCK source commit: $planckSourceRoot"
}
$planckSourceTreeSha256 = Get-PlanckSourceTreeSha256 -SourceRoot $planckSourceRoot
if (Test-Path -LiteralPath $artifact) {
    $existingArtifact = Get-Item -LiteralPath $artifact -Force
    if ($existingArtifact.PSIsContainer -or ($existingArtifact.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Expected PLANCK output is not a regular file: $artifact"
    }
    Remove-Item -LiteralPath $artifact -Force
}
if (Test-Path -LiteralPath $artifact) {
    throw "Could not delete the exact PLANCK output before forced rebuild: $artifact"
}
Remove-Item -LiteralPath $provenancePath -Force -ErrorAction SilentlyContinue
$buildStartedUtc = [DateTime]::UtcNow

Write-Host "Force-rebuilding PLANCK with $msbuild"
& $msbuild @msbuildArguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
    throw "MSBuild succeeded but the expected PLANCK artifact was not found: $artifact"
}
$builtArtifact = Get-Item -LiteralPath $artifact -Force
if (($builtArtifact.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0 -or $builtArtifact.Length -le 0) {
    throw "Forced PLANCK rebuild produced an invalid artifact: $artifact"
}
if ($builtArtifact.LastWriteTimeUtc -le $buildStartedUtc) {
    throw "Forced PLANCK rebuild produced an unchanged or stale artifact timestamp: $artifact"
}
if ($builtArtifact.LastWriteTimeUtc -gt [DateTime]::UtcNow.AddMinutes(5)) {
    throw "Forced PLANCK rebuild produced a future-dated artifact: $artifact"
}

if ((& $git.Source -C $planckSourceRoot rev-parse --verify 'HEAD^{commit}').Trim() -ne $planckCommit -or
    (Get-PlanckSourceTreeSha256 -SourceRoot $planckSourceRoot) -ne $planckSourceTreeSha256) {
    throw "PLANCK source identity changed during forced rebuild; refusing to package a raced artifact."
}
Assert-DependencyTreesMatchProvenance -Provenance $dependencyProvenance -Root $DependencyRoot
$provenance = [ordered]@{
    schema = 'stvr_planck_forced_build_v2'
    artifactName = 'activeragdoll.dll'
    artifactSha256 = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
    planckCommit = $planckCommit.ToLowerInvariant()
    planckSourceTreeSha256 = $planckSourceTreeSha256
    havokArchiveSha256 = $dependencyProvenance.havokArchiveSha256.ToLowerInvariant()
    havokSourceTreeSha256 = $dependencyProvenance.havokSourceTreeSha256.ToLowerInvariant()
    havokSourceFileCount = [long]$dependencyProvenance.havokSourceFileCount
    sksevrArchiveSha256 = $dependencyProvenance.sksevrArchiveSha256.ToLowerInvariant()
    sksevrSourceTreeSha256 = $dependencyProvenance.sksevrSourceTreeSha256.ToLowerInvariant()
    sksevrSourceFileCount = [long]$dependencyProvenance.sksevrSourceFileCount
    msbuildTarget = 'Rebuild'
    buildStartedUtc = $buildStartedUtc.ToString('o')
    artifactLastWriteUtc = $builtArtifact.LastWriteTimeUtc.ToString('o')
}
[System.IO.File]::WriteAllText($provenancePath, ($provenance | ConvertTo-Json -Depth 4) + [Environment]::NewLine, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "PLANCK_ARTIFACT=$artifact"
Write-Host "PLANCK_PROVENANCE=$provenancePath"
