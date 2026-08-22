#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$HavokArchive,

    [string]$DependencyRoot = "",

    [string]$SKSEVRArchive = "",

    [string]$SevenZipPath = "",

    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidateSet("debug", "releasedbg", "release")]
    [string]$Mode = "releasedbg",

    [string]$SkyrimVR = "",

    [string]$GameFilesRoot = "",

    [string]$CefRuntimeDirectory = "",

    [string]$PapyrusCompiler = "",

    [string]$SkseVrSdkRoot = "",

    [string]$Python = "",

    [switch]$CompilePapyrus
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Invoke-CheckedBatch {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    # Invoke the batch through cmd /c with each argument quoted so paths and
    # switches are passed verbatim. Splatting an argument array directly onto a
    # .bat via the call operator mis-binds some arguments as a ScriptBlock.
    $quotedArguments = $Arguments | ForEach-Object { '"' + ($_ -replace '"', '\"') + '"' }
    $commandLine = '"' + $Path + '" ' + ($quotedArguments -join ' ')
    & cmd.exe /c $commandLine
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

function Invoke-StrictPackageAudit {
    param(
        [Parameter(Mandatory = $true)]
        [string]$AuditScript,

        [Parameter(Mandatory = $true)]
        [string]$Package,

        [string]$SkyrimVRPath,

        [string]$RequestedPython
    )

    $pythonCommand = $null
    if (-not [string]::IsNullOrWhiteSpace($RequestedPython)) {
        $pythonCommand = Get-Command $RequestedPython -ErrorAction Stop
    }
    else {
        $pythonCommand = Get-Command "py.exe" -ErrorAction SilentlyContinue
        if ($null -eq $pythonCommand) {
            $pythonCommand = Get-Command "python.exe" -ErrorAction Stop
        }
    }

    $arguments = @()
    if ([System.IO.Path]::GetFileName($pythonCommand.Source) -ieq "py.exe") {
        $arguments += "-3"
    }
    $arguments += @(
        $AuditScript,
        "--package", $Package,
        "--gameplay",
        "--require-patched-planck-interface002"
    )
    if (-not [string]::IsNullOrWhiteSpace($SkyrimVRPath)) {
        $arguments += @("--skyrim-vr", $SkyrimVRPath)
    }

    & $pythonCommand.Source @arguments
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$repoRoot = $PSScriptRoot
$planckBuilder = Join-Path $repoRoot "BuildPlanckSkyrimTogetherVR-Windows.bat"
$gameplayBuildAuditCollect = Join-Path $repoRoot "BuildAuditCollectSkyrimTogetherVR-Windows.bat"
$packageAudit = Join-Path $repoRoot "Tools\SkyrimVR\audit_built_package.py"

foreach ($requiredPath in @($planckBuilder, $gameplayBuildAuditCollect, $packageAudit)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required complete-build tool is missing: $requiredPath"
    }
}
if (-not (Test-Path -LiteralPath $HavokArchive -PathType Leaf)) {
    throw "HavokArchive must be supplied as a readable archive file: $HavokArchive"
}
$havokArchiveItem = Get-Item -LiteralPath $HavokArchive -Force
if (($havokArchiveItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "HavokArchive must be a regular file, not a reparse point: $HavokArchive"
}
$expectedHavokSha256 = "7349946401a820784fc86aa13bc667def6c409ed938865b01c8e6c3d86692555"
$actualHavokSha256 = (Get-FileHash -LiteralPath $HavokArchive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHavokSha256 -ne $expectedHavokSha256) {
    throw "HavokArchive SHA-256 mismatch. Expected ${expectedHavokSha256}, got ${actualHavokSha256}: ${HavokArchive}"
}

$planckArguments = @("-RepoRoot", $repoRoot, "-HavokArchive", $HavokArchive, "-Configuration", $Configuration)
if (-not [string]::IsNullOrWhiteSpace($DependencyRoot)) { $planckArguments += @("-DependencyRoot", $DependencyRoot) }
if (-not [string]::IsNullOrWhiteSpace($SKSEVRArchive)) { $planckArguments += @("-SKSEVRArchive", $SKSEVRArchive) }
if (-not [string]::IsNullOrWhiteSpace($SevenZipPath)) { $planckArguments += @("-SevenZipPath", $SevenZipPath) }

# This PLANCK build only produces its deterministic artifact; it never deploys to a game install.
Invoke-CheckedBatch -Path $planckBuilder -Arguments $planckArguments

$patchedPlanckArtifact = Join-Path $repoRoot ("Libraries\activeragdoll\x64\{0}\activeragdoll.dll" -f $Configuration)
$patchedPlanckProvenance = "$patchedPlanckArtifact.stvr-planck-provenance.json"
if (-not (Test-Path -LiteralPath $patchedPlanckArtifact -PathType Leaf)) {
    throw "PLANCK build completed without its deterministic artifact: $patchedPlanckArtifact"
}
if (-not (Test-Path -LiteralPath $patchedPlanckProvenance -PathType Leaf)) {
    throw "PLANCK build completed without forced-build provenance: $patchedPlanckProvenance"
}

$gameplayArguments = @("--gameplay")
if (-not [string]::IsNullOrWhiteSpace($SkyrimVR)) { $gameplayArguments += @("--skyrim-vr", $SkyrimVR) }
if ($CompilePapyrus) { $gameplayArguments += "--compile-papyrus" }
$gameplayArguments += "--"
$gameplayArguments += @("-Mode", $Mode, "-PatchedPlanckArtifact", $patchedPlanckArtifact, "-PatchedPlanckProvenance", $patchedPlanckProvenance)
if (-not [string]::IsNullOrWhiteSpace($GameFilesRoot)) { $gameplayArguments += @("-GameFilesRoot", $GameFilesRoot) }
if (-not [string]::IsNullOrWhiteSpace($CefRuntimeDirectory)) { $gameplayArguments += @("-CefRuntimeDirectory", $CefRuntimeDirectory) }
if (-not [string]::IsNullOrWhiteSpace($PapyrusCompiler)) { $gameplayArguments += @("-PapyrusCompiler", $PapyrusCompiler) }
if (-not [string]::IsNullOrWhiteSpace($SkseVrSdkRoot)) { $gameplayArguments += @("-SkseVrSdkRoot", $SkseVrSdkRoot) }
if (-not [string]::IsNullOrWhiteSpace($Python)) { $gameplayArguments += @("-Python", $Python) }

Invoke-CheckedBatch -Path $gameplayBuildAuditCollect -Arguments $gameplayArguments

$packageRoot = Join-Path $repoRoot ("artifacts\SkyrimTogetherVR\{0}" -f $Mode)
Invoke-StrictPackageAudit -AuditScript $packageAudit -Package $packageRoot -SkyrimVRPath $SkyrimVR -RequestedPython $Python

Write-Host "Complete gameplay package contains patched PLANCK interface002: $patchedPlanckArtifact"
