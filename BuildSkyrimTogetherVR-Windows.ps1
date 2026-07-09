#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet("debug", "releasedbg", "release")]
    [string]$Mode = "releasedbg",

    [string]$Xmake = "xmake",

    [string]$Toolchain = "",

    [string[]]$Targets = @(
        "SkyrimTogetherVRClient",
        "SkyrimTogetherVRVrikBridge",
        "SkyrimTogetherVRHiggsBridge",
        "SkyrimTogetherVRPlanckBridge",
        "SkyrimVRImmersiveLauncher",
        "ImmersiveElf",
        "TPProcess"
    ),

    [switch]$SkipConfigure,

    [switch]$NoPackage,

    [switch]$SkipGameFiles,

    [switch]$SkipCompanionPanel,

    [switch]$BuildUi,

    [switch]$CompilePapyrus,

    [string]$PapyrusCompiler = "",

    [string]$Python = "",

    [switch]$PreflightOnly,

    [string]$GameFilesRoot = "GameFiles\SkyrimVR",

    [string]$CompanionToolRoot = "Tools\SkyrimVR",

    [string]$PackageRoot = "artifacts\SkyrimTogetherVR"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $RepoRoot
$ResolvedPapyrusCompiler = ""

$Targets = @(
    $Targets | ForEach-Object {
        $_ -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_.Length -gt 0 }
    }
)

function Invoke-Xmake {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    Write-Host "> $Xmake $($Arguments -join ' ')"
    & $Xmake @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "xmake failed with exit code $LASTEXITCODE while running: xmake $($Arguments -join ' ')"
    }
}

function Copy-MatchingArtifact {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.FileInfo]$File,

        [Parameter(Mandatory = $true)]
        [string]$Destination
    )

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Copy-Item -Force -LiteralPath $File.FullName -Destination $Destination
}

function Resolve-PathAgainstRepo {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

function Resolve-StagedVrGameFilesRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $expectedGameFilesDir = Resolve-PathAgainstRepo -Path "GameFiles\SkyrimVR"
    $gameFilesDir = Resolve-PathAgainstRepo -Path $Path
    if (-not [string]::Equals($gameFilesDir, $expectedGameFilesDir, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "SkyrimTogetherVR packages must stage game files from GameFiles\SkyrimVR. Refusing GameFilesRoot '$Path' resolved to '$gameFilesDir'."
    }

    return $gameFilesDir
}

function Resolve-PythonCommandPrefix {
    param(
        [string]$RequestedPython
    )

    if ($RequestedPython.Length -gt 0) {
        return @($RequestedPython)
    }

    if (Get-Command "py" -ErrorAction SilentlyContinue) {
        return @("py", "-3")
    }

    if (Get-Command "python" -ErrorAction SilentlyContinue) {
        return @("python")
    }

    if (Get-Command "python3" -ErrorAction SilentlyContinue) {
        return @("python3")
    }

    throw "Python was not found. Install Python 3 or pass -Python C:\path\to\python.exe."
}

function Invoke-Python {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$PythonCommandPrefix,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $pythonExe = $PythonCommandPrefix[0]
    $pythonArgs = @()
    if ($PythonCommandPrefix.Count -gt 1) {
        $pythonArgs += $PythonCommandPrefix[1..($PythonCommandPrefix.Count - 1)]
    }
    $pythonArgs += $Arguments

    Write-Host "> $pythonExe $($pythonArgs -join ' ')"
    & $pythonExe @pythonArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Python command failed with exit code $LASTEXITCODE while running: $pythonExe $($pythonArgs -join ' ')"
    }
}

function Resolve-PapyrusCompilerForPreflight {
    param(
        [string]$RequestedCompiler
    )

    if ($RequestedCompiler.Length -gt 0) {
        $compilerPath = Resolve-PathAgainstRepo -Path $RequestedCompiler
        if (-not (Test-Path -LiteralPath $compilerPath)) {
            throw "Requested Papyrus compiler does not exist: $compilerPath"
        }
        return $compilerPath
    }

    if ($env:CAPRICA) {
        $compilerPath = Resolve-PathAgainstRepo -Path $env:CAPRICA
        if (-not (Test-Path -LiteralPath $compilerPath)) {
            throw "CAPRICA points to a missing Papyrus compiler: $compilerPath"
        }
        return $compilerPath
    }

    $defaultCompilers = @(
        (Join-Path $RepoRoot "..\_refs\Caprica-release\extracted\Caprica.exe"),
        (Join-Path $RepoRoot "..\_refs\Caprica\build\Caprica\Caprica.exe"),
        (Join-Path $RepoRoot "..\_refs\Caprica\build\Caprica\Caprica")
    )
    foreach ($compilerPath in $defaultCompilers) {
        $resolvedCompilerPath = [System.IO.Path]::GetFullPath($compilerPath)
        if (Test-Path -LiteralPath $resolvedCompilerPath) {
            return $resolvedCompilerPath
        }
    }

    throw "Papyrus compile was requested, but no Caprica compiler was found. Pass -PapyrusCompiler C:\path\to\Caprica.exe or set CAPRICA."
}

function Invoke-PapyrusCompile {
    param(
        [string]$GameFilesDir
    )

    $compileScript = Join-Path $RepoRoot "Tools\SkyrimVR\compile_papyrus.py"
    if (-not (Test-Path -LiteralPath $compileScript)) {
        throw "Expected Papyrus compile wrapper does not exist: $compileScript"
    }

    $pythonPrefix = Resolve-PythonCommandPrefix -RequestedPython $Python
    $compilerPath = Resolve-PapyrusCompilerForPreflight -RequestedCompiler $PapyrusCompiler
    $script:ResolvedPapyrusCompiler = $compilerPath
    $outputDir = Join-Path $GameFilesDir "scripts"

    Invoke-Python -PythonCommandPrefix $pythonPrefix -Arguments @(
        $compileScript,
        "--caprica",
        $compilerPath,
        "--output",
        $outputDir
    )
}

function Get-LocalPythonImportModules {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $modules = New-Object 'System.Collections.Generic.HashSet[string]'
    $lines = Get-Content -LiteralPath $Path -Encoding UTF8
    foreach ($line in $lines) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^import\s+(.+)$') {
            foreach ($importPart in ($Matches[1] -split ",")) {
                $moduleName = (($importPart.Trim() -split "\s+as\s+")[0] -split "\.")[0]
                if ($moduleName.Length -gt 0) {
                    [void]$modules.Add($moduleName)
                }
            }
        }
        elseif ($trimmed -match '^from\s+([A-Za-z_][A-Za-z0-9_\.]*)\s+import\s+') {
            $moduleName = ($Matches[1] -split "\.")[0]
            if ($moduleName.Length -gt 0) {
                [void]$modules.Add($moduleName)
            }
        }
    }

    return @($modules | Sort-Object)
}

function Resolve-PackagedPythonHelpers {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ToolDirectory,

        [Parameter(Mandatory = $true)]
        [string[]]$SeedNames
    )

    $required = New-Object 'System.Collections.Generic.HashSet[string]'
    $visited = New-Object 'System.Collections.Generic.HashSet[string]'
    $queue = New-Object 'System.Collections.Generic.Queue[string]'
    foreach ($seedName in $SeedNames) {
        [void]$required.Add($seedName)
        $queue.Enqueue($seedName)
    }

    while ($queue.Count -gt 0) {
        $helperName = $queue.Dequeue()
        if (-not $visited.Add($helperName)) {
            continue
        }

        $helperPath = Join-Path $ToolDirectory $helperName
        if (-not (Test-Path -LiteralPath $helperPath)) {
            throw "Expected packaged Python helper does not exist: $helperPath"
        }

        foreach ($moduleName in (Get-LocalPythonImportModules -Path $helperPath)) {
            $candidateName = "$moduleName.py"
            $candidatePath = Join-Path $ToolDirectory $candidateName
            if ((Test-Path -LiteralPath $candidatePath) -and $required.Add($candidateName)) {
                $queue.Enqueue($candidateName)
            }
        }
    }

    return @($required | Sort-Object)
}

if ($env:OS -ne "Windows_NT") {
    Write-Warning "This script is intended to run from Windows with Visual Studio/MSVC available."
}

if (-not (Get-Command $Xmake -ErrorAction SilentlyContinue)) {
    throw "Could not find xmake. Install xmake first or pass -Xmake C:\path\to\xmake.exe."
}

if (-not $SkipConfigure) {
    $configureArgs = @("f", "-p", "windows", "-a", "x64", "-m", $Mode, "-y")
    if ($Toolchain.Length -gt 0) {
        $configureArgs += "--toolchain=$Toolchain"
    }

    Invoke-Xmake $configureArgs
}

Write-Host "> $Xmake show -l targets"
$targetList = & $Xmake show -l targets 2>&1
if ($LASTEXITCODE -ne 0) {
    $targetText = ($targetList | Out-String)
    throw "xmake target listing failed.`n$targetText"
}

$targetListText = ($targetList | Out-String)
$requiredTargets = @("SkyrimTogetherVRClient", "SkyrimTogetherVRVrikBridge", "SkyrimTogetherVRHiggsBridge", "SkyrimTogetherVRPlanckBridge", "SkyrimVRImmersiveLauncher", "ImmersiveElf", "TPProcess")
$requiredTargets += @("SkyrimTogetherVRClientAvatarSync", "SkyrimVRImmersiveLauncherAvatarSync", "SkyrimTogetherVRGameplayClient", "SkyrimVRImmersiveLauncherGameplay")
foreach ($requiredTarget in $requiredTargets) {
    if ($targetListText -notmatch [regex]::Escape($requiredTarget)) {
        throw "Required Windows target '$requiredTarget' is not visible. Check that xmake configured with -p windows -a x64."
    }
}

if ($PreflightOnly) {
    Write-Host "Running SkyrimTogetherVR Windows build preflight only; no targets will be built."

    if ($BuildUi -and -not (Get-Command "pnpm" -ErrorAction SilentlyContinue)) {
        throw "BuildUi was requested, but pnpm was not found in PATH."
    }

    if (-not $SkipGameFiles) {
        $gameFilesDir = Resolve-StagedVrGameFilesRoot -Path $GameFilesRoot
        if (-not (Test-Path -LiteralPath $gameFilesDir)) {
            throw "Expected staged VR game files directory does not exist: $gameFilesDir"
        }
        if ($CompilePapyrus) {
            $pythonPrefix = Resolve-PythonCommandPrefix -RequestedPython $Python
            $compilerPath = Resolve-PapyrusCompilerForPreflight -RequestedCompiler $PapyrusCompiler
            $compileScript = Join-Path $RepoRoot "Tools\SkyrimVR\compile_papyrus.py"
            if (-not (Test-Path -LiteralPath $compileScript)) {
                throw "Expected Papyrus compile wrapper does not exist: $compileScript"
            }
            Write-Host "Papyrus compile preflight:"
            Write-Host "  Python: $($pythonPrefix -join ' ')"
            Write-Host "  Compiler: $compilerPath"
            Write-Host "  Output: $(Join-Path $gameFilesDir 'scripts')"
        }
        Write-Host "Found staged VR game files: $gameFilesDir"
    }

    if (-not $SkipCompanionPanel) {
        $companionToolDir = Join-Path $RepoRoot $CompanionToolRoot
        $packagedPythonHelperNames = @(
            "vr_handoff.py",
            "vr_paths.py",
            "audit_runtime_handoff.py",
            "collect_runtime_evidence.py",
            "audit_runtime_evidence_zip.py"
        )
        $packagedPythonHelperNames = Resolve-PackagedPythonHelpers -ToolDirectory $companionToolDir -SeedNames $packagedPythonHelperNames
        $companionLauncher = Join-Path $RepoRoot "LaunchSkyrimTogetherVRCompanion.bat"
        $runtimeAuditLauncher = Join-Path $RepoRoot "AuditSkyrimTogetherVRRuntime-Windows.bat"
        $avatarRuntimeAuditLauncher = Join-Path $RepoRoot "AuditSkyrimTogetherVRAvatarSyncRuntime-Windows.bat"
        $gameplayRuntimeAuditLauncher = Join-Path $RepoRoot "AuditSkyrimTogetherVRGameplayRuntime-Windows.bat"
        $runtimeEvidenceLauncher = Join-Path $RepoRoot "CollectSkyrimTogetherVREvidence-Windows.bat"
        $avatarRuntimeEvidenceLauncher = Join-Path $RepoRoot "CollectSkyrimTogetherVRAvatarSyncEvidence-Windows.bat"
        $gameplayRuntimeEvidenceLauncher = Join-Path $RepoRoot "CollectSkyrimTogetherVRGameplayEvidence-Windows.bat"
        $runtimeEvidenceAuditLauncher = Join-Path $RepoRoot "AuditSkyrimTogetherVREvidence-Windows.bat"
        $avatarRuntimeEvidenceAuditLauncher = Join-Path $RepoRoot "AuditSkyrimTogetherVRAvatarSyncEvidence-Windows.bat"
        $gameplayRuntimeEvidenceAuditLauncher = Join-Path $RepoRoot "AuditSkyrimTogetherVRGameplayEvidence-Windows.bat"
        foreach ($launcherPath in @(
            $companionLauncher,
            $runtimeAuditLauncher,
            $avatarRuntimeAuditLauncher,
            $gameplayRuntimeAuditLauncher,
            $runtimeEvidenceLauncher,
            $avatarRuntimeEvidenceLauncher,
            $gameplayRuntimeEvidenceLauncher,
            $runtimeEvidenceAuditLauncher,
            $avatarRuntimeEvidenceAuditLauncher,
            $gameplayRuntimeEvidenceAuditLauncher
        )) {
            if (-not (Test-Path -LiteralPath $launcherPath)) {
                throw "Expected packaged launcher does not exist: $launcherPath"
            }
        }
        Write-Host "Resolved packaged Python helper closure:"
        $packagedPythonHelperNames | ForEach-Object { Write-Host "  Tools\SkyrimVR\$_" }
    }

    Write-Host "Preflight completed; no targets were built."
    return
}

if ($CompilePapyrus) {
    $gameFilesDir = Resolve-StagedVrGameFilesRoot -Path $GameFilesRoot
    if (-not (Test-Path -LiteralPath $gameFilesDir)) {
        throw "Expected staged VR game files directory does not exist: $gameFilesDir"
    }
    Invoke-PapyrusCompile -GameFilesDir $gameFilesDir
}

foreach ($target in $Targets) {
    Invoke-Xmake @("build", "-y", $target)
}

if ($BuildUi) {
    if (-not (Get-Command "pnpm" -ErrorAction SilentlyContinue)) {
        throw "BuildUi was requested, but pnpm was not found in PATH."
    }

    Push-Location (Join-Path $RepoRoot "Code\skyrim_ui")
    try {
        Write-Host "> pnpm install"
        & pnpm install
        if ($LASTEXITCODE -ne 0) {
            throw "pnpm install failed with exit code $LASTEXITCODE"
        }

        Write-Host "> pnpm deploy:production"
        & pnpm deploy:production
        if ($LASTEXITCODE -ne 0) {
            throw "pnpm deploy:production failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
}

if (-not $NoPackage) {
    $buildDir = Join-Path $RepoRoot ("build\windows\x64\{0}" -f $Mode)
    if (-not (Test-Path -LiteralPath $buildDir)) {
        throw "Expected build output directory does not exist: $buildDir"
    }

    $packageDir = Join-Path $RepoRoot (Join-Path $PackageRoot $Mode)
    New-Item -ItemType Directory -Force -Path $packageDir | Out-Null

    $staleRuntimeArtifactBaseNames = @(
        "SkyrimTogetherVR",
        "SkyrimTogetherVRAvatarSync",
        "SkyrimTogetherVRGameplay",
        "SkyrimTogetherVRClient",
        "SkyrimTogetherVRClientAvatarSync",
        "SkyrimTogetherVRGameplayClient",
        "SkyrimTogetherVRVrikBridge",
        "SkyrimTogetherVRHiggsBridge",
        "SkyrimTogetherVRPlanckBridge",
        "EarlyLoad",
        "TPProcess"
    )
    $staleRuntimeArtifactExtensions = @(".exe", ".dll", ".pdb", ".lib", ".exp")
    $staleRuntimeArtifactDirs = @(
        $packageDir,
        (Join-Path $packageDir "Data\SKSE\Plugins")
    )
    foreach ($staleArtifactDir in $staleRuntimeArtifactDirs) {
        foreach ($staleArtifactBaseName in $staleRuntimeArtifactBaseNames) {
            foreach ($staleArtifactExtension in $staleRuntimeArtifactExtensions) {
                $staleArtifactPath = Join-Path $staleArtifactDir ($staleArtifactBaseName + $staleArtifactExtension)
                if (Test-Path -LiteralPath $staleArtifactPath) {
                    Remove-Item -Force -LiteralPath $staleArtifactPath -ErrorAction SilentlyContinue
                }
            }
        }
    }

    if (-not $SkipGameFiles) {
        $gameFilesDir = Resolve-StagedVrGameFilesRoot -Path $GameFilesRoot
        if (-not (Test-Path -LiteralPath $gameFilesDir)) {
            throw "Expected staged VR game files directory does not exist: $gameFilesDir"
        }

        $staleRootGameFilePaths = @(
            "SkyrimTogether.esp",
            "scripts",
            "meshes",
            "SkyrimTogetherRebornBehaviors"
        )
        foreach ($relativeStalePath in $staleRootGameFilePaths) {
            $stalePath = Join-Path $packageDir $relativeStalePath
            if (Test-Path -LiteralPath $stalePath) {
                Remove-Item -Recurse -Force -LiteralPath $stalePath -ErrorAction SilentlyContinue
            }
        }

        $packageDataDir = Join-Path $packageDir "Data"
        New-Item -ItemType Directory -Force -Path $packageDataDir | Out-Null
        Get-ChildItem -LiteralPath $gameFilesDir -Force | ForEach-Object {
            if ($_.PSIsContainer -and $_.Name -ieq "Data") {
                Get-ChildItem -LiteralPath $_.FullName -Force | ForEach-Object {
                    Copy-Item -Recurse -Force -LiteralPath $_.FullName -Destination $packageDataDir
                }
            }
            else {
                Copy-Item -Recurse -Force -LiteralPath $_.FullName -Destination $packageDataDir
            }
        }
        Write-Host "Copied staged VR game files from $gameFilesDir"
    }

    if (-not $SkipCompanionPanel) {
        $companionToolDir = Join-Path $RepoRoot $CompanionToolRoot
        $packagedPythonHelperNames = @(
            "vr_handoff.py",
            "vr_paths.py",
            "audit_runtime_handoff.py",
            "collect_runtime_evidence.py",
            "audit_runtime_evidence_zip.py"
        )
        $packagedPythonHelperNames = Resolve-PackagedPythonHelpers -ToolDirectory $companionToolDir -SeedNames $packagedPythonHelperNames
        $companionLauncher = Join-Path $RepoRoot "LaunchSkyrimTogetherVRCompanion.bat"
        $runtimeAuditLauncher = Join-Path $RepoRoot "AuditSkyrimTogetherVRRuntime-Windows.bat"
        $avatarRuntimeAuditLauncher = Join-Path $RepoRoot "AuditSkyrimTogetherVRAvatarSyncRuntime-Windows.bat"
        $gameplayRuntimeAuditLauncher = Join-Path $RepoRoot "AuditSkyrimTogetherVRGameplayRuntime-Windows.bat"
        $runtimeEvidenceLauncher = Join-Path $RepoRoot "CollectSkyrimTogetherVREvidence-Windows.bat"
        $avatarRuntimeEvidenceLauncher = Join-Path $RepoRoot "CollectSkyrimTogetherVRAvatarSyncEvidence-Windows.bat"
        $gameplayRuntimeEvidenceLauncher = Join-Path $RepoRoot "CollectSkyrimTogetherVRGameplayEvidence-Windows.bat"
        $runtimeEvidenceAuditLauncher = Join-Path $RepoRoot "AuditSkyrimTogetherVREvidence-Windows.bat"
        $avatarRuntimeEvidenceAuditLauncher = Join-Path $RepoRoot "AuditSkyrimTogetherVRAvatarSyncEvidence-Windows.bat"
        $gameplayRuntimeEvidenceAuditLauncher = Join-Path $RepoRoot "AuditSkyrimTogetherVRGameplayEvidence-Windows.bat"
        if (-not (Test-Path -LiteralPath $companionLauncher)) {
            throw "Expected VR companion launcher does not exist: $companionLauncher"
        }
        if (-not (Test-Path -LiteralPath $runtimeAuditLauncher)) {
            throw "Expected VR runtime audit launcher does not exist: $runtimeAuditLauncher"
        }
        if (-not (Test-Path -LiteralPath $avatarRuntimeAuditLauncher)) {
            throw "Expected VR avatar-sync runtime audit launcher does not exist: $avatarRuntimeAuditLauncher"
        }
        if (-not (Test-Path -LiteralPath $gameplayRuntimeAuditLauncher)) {
            throw "Expected VR gameplay runtime audit launcher does not exist: $gameplayRuntimeAuditLauncher"
        }
        if (-not (Test-Path -LiteralPath $runtimeEvidenceLauncher)) {
            throw "Expected VR runtime evidence collector launcher does not exist: $runtimeEvidenceLauncher"
        }
        if (-not (Test-Path -LiteralPath $avatarRuntimeEvidenceLauncher)) {
            throw "Expected VR avatar-sync runtime evidence collector launcher does not exist: $avatarRuntimeEvidenceLauncher"
        }
        if (-not (Test-Path -LiteralPath $gameplayRuntimeEvidenceLauncher)) {
            throw "Expected VR gameplay runtime evidence collector launcher does not exist: $gameplayRuntimeEvidenceLauncher"
        }
        if (-not (Test-Path -LiteralPath $runtimeEvidenceAuditLauncher)) {
            throw "Expected VR runtime evidence audit launcher does not exist: $runtimeEvidenceAuditLauncher"
        }
        if (-not (Test-Path -LiteralPath $avatarRuntimeEvidenceAuditLauncher)) {
            throw "Expected VR avatar-sync runtime evidence audit launcher does not exist: $avatarRuntimeEvidenceAuditLauncher"
        }
        if (-not (Test-Path -LiteralPath $gameplayRuntimeEvidenceAuditLauncher)) {
            throw "Expected VR gameplay runtime evidence audit launcher does not exist: $gameplayRuntimeEvidenceAuditLauncher"
        }

        $packageToolDir = Join-Path $packageDir $CompanionToolRoot
        New-Item -ItemType Directory -Force -Path $packageToolDir | Out-Null
        foreach ($helperName in $packagedPythonHelperNames) {
            Copy-Item -Force -LiteralPath (Join-Path $companionToolDir $helperName) -Destination $packageToolDir
        }
        Copy-Item -Force -LiteralPath $companionLauncher -Destination $packageDir
        Copy-Item -Force -LiteralPath $runtimeAuditLauncher -Destination $packageDir
        Copy-Item -Force -LiteralPath $avatarRuntimeAuditLauncher -Destination $packageDir
        Copy-Item -Force -LiteralPath $gameplayRuntimeAuditLauncher -Destination $packageDir
        Copy-Item -Force -LiteralPath $runtimeEvidenceLauncher -Destination $packageDir
        Copy-Item -Force -LiteralPath $avatarRuntimeEvidenceLauncher -Destination $packageDir
        Copy-Item -Force -LiteralPath $gameplayRuntimeEvidenceLauncher -Destination $packageDir
        Copy-Item -Force -LiteralPath $runtimeEvidenceAuditLauncher -Destination $packageDir
        Copy-Item -Force -LiteralPath $avatarRuntimeEvidenceAuditLauncher -Destination $packageDir
        Copy-Item -Force -LiteralPath $gameplayRuntimeEvidenceAuditLauncher -Destination $packageDir
        Write-Host "Copied VR companion panel helper import closure, companion launcher, runtime audit launchers, and evidence collector/audit launchers"
        $packagedPythonHelperNames | ForEach-Object { Write-Host "  Tools\SkyrimVR\$_" }
    }

    $artifactNames = @(
        "SkyrimTogetherVR",
        "SkyrimTogetherVRAvatarSync",
        "SkyrimTogetherVRGameplay",
        "SkyrimTogetherVRVrikBridge",
        "SkyrimTogetherVRHiggsBridge",
        "SkyrimTogetherVRPlanckBridge",
        "SkyrimTogetherVRClient",
        "SkyrimTogetherVRClientAvatarSync",
        "SkyrimTogetherVRGameplayClient",
        "EarlyLoad",
        "TPProcess"
    )
    $artifactExtensions = @(".exe", ".dll", ".pdb", ".lib", ".exp")

    $expectedArtifactNames = New-Object System.Collections.Generic.List[string]
    foreach ($target in $Targets) {
        switch ($target) {
            "SkyrimTogetherVRClient" { $expectedArtifactNames.Add("SkyrimTogetherVRClient.lib") }
            "SkyrimTogetherVRClientAvatarSync" { $expectedArtifactNames.Add("SkyrimTogetherVRClientAvatarSync.lib") }
            "SkyrimTogetherVRGameplayClient" { $expectedArtifactNames.Add("SkyrimTogetherVRGameplayClient.lib") }
            "SkyrimTogetherVRVrikBridge" { $expectedArtifactNames.Add("SkyrimTogetherVRVrikBridge.dll") }
            "SkyrimTogetherVRHiggsBridge" { $expectedArtifactNames.Add("SkyrimTogetherVRHiggsBridge.dll") }
            "SkyrimTogetherVRPlanckBridge" { $expectedArtifactNames.Add("SkyrimTogetherVRPlanckBridge.dll") }
            "SkyrimVRImmersiveLauncher" { $expectedArtifactNames.Add("SkyrimTogetherVR.exe") }
            "SkyrimVRImmersiveLauncherAvatarSync" { $expectedArtifactNames.Add("SkyrimTogetherVRAvatarSync.exe") }
            "SkyrimVRImmersiveLauncherGameplay" { $expectedArtifactNames.Add("SkyrimTogetherVRGameplay.exe") }
            "ImmersiveElf" { $expectedArtifactNames.Add("EarlyLoad.dll") }
            "TPProcess" { $expectedArtifactNames.Add("TPProcess.exe") }
        }
    }

    $allowedArtifactBaseNames = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($expectedArtifactName in $expectedArtifactNames) {
        [void]$allowedArtifactBaseNames.Add([System.IO.Path]::GetFileNameWithoutExtension($expectedArtifactName))
    }

    $copied = New-Object System.Collections.Generic.List[string]
    Get-ChildItem -LiteralPath $buildDir -Recurse -File | ForEach-Object {
        $file = $_
        if ($allowedArtifactBaseNames.Contains($file.BaseName) -and $artifactExtensions -contains $file.Extension.ToLowerInvariant()) {
            if ($file.BaseName -eq "SkyrimTogetherVRVrikBridge" -or $file.BaseName -eq "SkyrimTogetherVRHiggsBridge" -or $file.BaseName -eq "SkyrimTogetherVRPlanckBridge") {
                Copy-MatchingArtifact -File $file -Destination (Join-Path $packageDir "Data\SKSE\Plugins")
            }
            else {
                Copy-MatchingArtifact -File $file -Destination $packageDir
            }
            $copied.Add($file.Name)
        }
    }

    if ($BuildUi) {
        $uiDist = Join-Path $RepoRoot "Code\skyrim_ui\dist"
        if (Test-Path -LiteralPath $uiDist) {
            Copy-Item -Recurse -Force -LiteralPath $uiDist -Destination (Join-Path $packageDir "ui")
        }
    }

    if ($copied.Count -eq 0) {
        Write-Warning "No matching VR artifacts were found under $buildDir."
    }
    else {
        Write-Host "Copied VR artifacts to $packageDir"
        $copied | Sort-Object -Unique | ForEach-Object { Write-Host "  $_" }
    }

    $missingExpectedArtifacts = @()
    foreach ($expectedArtifactName in $expectedArtifactNames) {
        if ($copied -notcontains $expectedArtifactName) {
            $missingExpectedArtifacts += $expectedArtifactName
        }
    }

    if ($missingExpectedArtifacts.Count -gt 0) {
        throw "Expected VR artifacts were not found or copied: $($missingExpectedArtifacts -join ', ')"
    }

    $targetSet = New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($target in $Targets) {
        [void]$targetSet.Add($target)
    }
    $avatarSyncPackage = (
        $targetSet.Contains("SkyrimTogetherVRClientAvatarSync") -or
        $targetSet.Contains("SkyrimVRImmersiveLauncherAvatarSync")
    )
    $gameplayPackage = (
        $targetSet.Contains("SkyrimTogetherVRGameplayClient") -or
        $targetSet.Contains("SkyrimVRImmersiveLauncherGameplay")
    )
    $dllOnlyPackage = ($targetSet.Count -eq 4)
    foreach ($dllOnlyTarget in @("SkyrimTogetherVRVrikBridge", "SkyrimTogetherVRHiggsBridge", "SkyrimTogetherVRPlanckBridge", "ImmersiveElf")) {
        if (-not $targetSet.Contains($dllOnlyTarget)) {
            $dllOnlyPackage = $false
        }
    }
    if ($gameplayPackage) {
        $packageFlavor = "gameplay"
    }
    elseif ($avatarSyncPackage) {
        $packageFlavor = "avatar-sync"
    }
    elseif ($dllOnlyPackage) {
        $packageFlavor = "dll-only"
    }
    else {
        $packageFlavor = "default"
    }
    $packageSnapshotDir = Join-Path $RepoRoot (Join-Path $PackageRoot (Join-Path "packages" $packageFlavor))
    $packageManifest = [ordered]@{
        schema = "skyrim_together_vr_build_package_v1"
        mode = $Mode
        platform = "windows"
        arch = "x64"
        avatarSync = [bool]$avatarSyncPackage
        gameplay = [bool]$gameplayPackage
        packageFlavor = $packageFlavor
        targets = @($Targets)
        copiedArtifacts = @($copied | Sort-Object -Unique)
        expectedArtifacts = @($expectedArtifactNames | Sort-Object -Unique)
        packageRoot = $packageDir
        packageSnapshotRoot = $packageSnapshotDir
        stagedGameFiles = [bool](-not $SkipGameFiles)
        papyrusCompiled = [bool]$CompilePapyrus
        papyrusCompiler = $ResolvedPapyrusCompiler
        companionPanel = [bool](-not $SkipCompanionPanel)
        generatedAtUtc = [DateTime]::UtcNow.ToString("o")
    }
    $manifestPath = Join-Path $packageDir "SkyrimTogetherVR_BuildManifest.json"
    $manifestJson = $packageManifest | ConvertTo-Json -Depth 5
    $utf8NoBom = New-Object System.Text.UTF8Encoding -ArgumentList $false
    [System.IO.File]::WriteAllText($manifestPath, $manifestJson + [Environment]::NewLine, $utf8NoBom)
    Write-Host "Wrote SkyrimTogetherVR build manifest to $manifestPath"

    if (Test-Path -LiteralPath $packageSnapshotDir) {
        Remove-Item -Recurse -Force -LiteralPath $packageSnapshotDir
    }
    New-Item -ItemType Directory -Force -Path $packageSnapshotDir | Out-Null
    Get-ChildItem -LiteralPath $packageDir -Force | ForEach-Object {
        Copy-Item -Recurse -Force -LiteralPath $_.FullName -Destination $packageSnapshotDir
    }
    Write-Host "Copied SkyrimTogetherVR package snapshot to $packageSnapshotDir"
}

Write-Host "SkyrimTogetherVR Windows build script completed."
