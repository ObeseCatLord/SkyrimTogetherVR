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
        "SkyrimTogetherVRTickBridge",
        "SkyrimVRImmersiveLauncher",
        "ImmersiveElf",
        "TPProcess"
    ),

    [switch]$SkipConfigure,

    [switch]$NoPackage,

    [switch]$AllowDirtySource,

    [switch]$SkipGameFiles,

    [switch]$SkipCompanionPanel,

    [switch]$BuildUi,

    [switch]$CompilePapyrus,

    [switch]$SkipPapyrusCompile,

    [string]$PapyrusCompiler = "",

    [string]$SkseVrSdkRoot = "",

    [string]$Python = "",

    [switch]$PreflightOnly,

    [string]$GameFilesRoot = "GameFiles\SkyrimVR",

    [string]$CompanionToolRoot = "Tools\SkyrimVR",

    [string]$CefRuntimeDirectory = "",

    [string]$PackageRoot = "artifacts\SkyrimTogetherVR"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $RepoRoot
$ResolvedPapyrusCompiler = ""
$CefRuntimeVersion = "141.0.11"
$CefRuntimeRequiredFiles = @(
    "chrome_elf.dll",
    "d3dcompiler_47.dll",
    "dxcompiler.dll",
    "dxil.dll",
    "libcef.dll",
    "libEGL.dll",
    "libGLESv2.dll",
    "v8_context_snapshot.bin",
    "vk_swiftshader.dll",
    "vulkan-1.dll",
    "chrome_100_percent.pak",
    "chrome_200_percent.pak",
    "icudtl.dat",
    "resources.pak",
    "locales\en-US.pak"
)

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

function Get-SourceTreeSha256 {
    $temporaryPath = [System.IO.Path]::GetTempFileName()
    $excludedDirectoryNames = @(".git", ".xmake", ".sdk", "artifacts", "build", "node_modules", "review-handoff")
    try {
        $manifestStream = [System.IO.File]::Open($temporaryPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
        try {
            $sourceFiles = @(
                Get-ChildItem -LiteralPath $RepoRoot -Recurse -Force -File -ErrorAction SilentlyContinue | Where-Object {
                    $relativePath = $_.FullName.Substring($RepoRoot.Length).TrimStart([char[]]@('\', '/'))
                    $pathSegments = $relativePath -split '[\\/]'
                    -not ($pathSegments | Where-Object { $excludedDirectoryNames -contains $_ })
                } | Sort-Object FullName
            )
            foreach ($sourceFile in $sourceFiles) {
                $relativePath = $sourceFile.FullName.Substring($RepoRoot.Length).TrimStart([char[]]@('\', '/')).Replace('\', '/')
                $pathBytes = [System.Text.Encoding]::UTF8.GetBytes($relativePath + "`0")
                $manifestStream.Write($pathBytes, 0, $pathBytes.Length)

                $sourceStream = [System.IO.File]::OpenRead($sourceFile.FullName)
                try {
                    $sourceStream.CopyTo($manifestStream)
                }
                finally {
                    $sourceStream.Dispose()
                }

                $separator = [byte[]]@(0)
                $manifestStream.Write($separator, 0, $separator.Length)
            }
        }
        finally {
            $manifestStream.Dispose()
        }

        return (Get-FileHash -LiteralPath $temporaryPath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    finally {
        Remove-Item -Force -LiteralPath $temporaryPath -ErrorAction SilentlyContinue
    }
}

function Get-SourceProvenance {
    $gitCommand = Get-Command "git" -ErrorAction SilentlyContinue
    if ($null -eq $gitCommand) {
        throw "Git is required to package SkyrimTogetherVR so the build manifest has an immutable source revision."
    }

    $revision = & $gitCommand.Source -C $RepoRoot rev-parse HEAD 2>$null
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($revision)) {
        throw "Could not resolve the Git HEAD revision for the SkyrimTogetherVR build manifest."
    }

    $revision = $revision.Trim()
    if ($revision -notmatch '^[0-9a-fA-F]{40}$') {
        throw "Git returned an invalid HEAD revision for the SkyrimTogetherVR build manifest: $revision"
    }

    $status = & $gitCommand.Source -C $RepoRoot status --porcelain=v1 --untracked-files=all 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "Could not determine whether the SkyrimTogetherVR source tree is clean."
    }
    $isDirty = @($status).Count -gt 0
    if ($isDirty -and -not $AllowDirtySource) {
        throw "The SkyrimTogetherVR source tree is dirty. Commit or stash the changes, or pass -AllowDirtySource to create an explicitly marked developer package."
    }

    $sourceTreeSha256 = Get-SourceTreeSha256
    $sourceRevision = if ($isDirty) { "$revision-dirty-$sourceTreeSha256" } else { $revision }
    return [ordered]@{
        revision = $revision
        sourceTreeSha256 = $sourceTreeSha256
        dirty = [bool]$isDirty
        dirtyApproved = [bool]($isDirty -and $AllowDirtySource)
        sourceRevision = $sourceRevision
    }
}

function Get-PackageFileSha256 {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PackageDirectory,

        [Parameter(Mandatory = $true)]
        [string]$ManifestPath
    )

    $manifestFullPath = [System.IO.Path]::GetFullPath($ManifestPath)
    $packageFileSha256 = [ordered]@{}
    Get-ChildItem -LiteralPath $PackageDirectory -Recurse -Force -File | Sort-Object FullName | ForEach-Object {
        if ([System.IO.Path]::GetFullPath($_.FullName) -ne $manifestFullPath) {
            $relativePath = $_.FullName.Substring($PackageDirectory.Length).TrimStart([char[]]@('\', '/')).Replace('\', '/')
            $packageFileSha256[$relativePath] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    return $packageFileSha256
}

function Resolve-CefRuntimeDirectory {
    param(
        [string]$RequestedDirectory
    )

    $candidateDirectories = New-Object 'System.Collections.Generic.List[string]'
    $requestedPaths = if (-not [string]::IsNullOrWhiteSpace($RequestedDirectory)) {
        @($RequestedDirectory)
    }
    else {
        @($env:STVR_CEF_RUNTIME_DIR)
    }
    foreach ($requestedPath in $requestedPaths) {
        if ([string]::IsNullOrWhiteSpace($requestedPath)) {
            continue
        }

        $resolvedPath = Resolve-PathAgainstRepo -Path $requestedPath
        if (-not (Test-Path -LiteralPath $resolvedPath)) {
            throw "Requested CEF runtime directory does not exist: $resolvedPath"
        }
        [void]$candidateDirectories.Add($resolvedPath)
    }

    if ($candidateDirectories.Count -eq 0) {
        if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
            throw "LOCALAPPDATA is not set. Pass -CefRuntimeDirectory pointing at xmake's CEF bin directory."
        }

        $cefPackageRoot = Join-Path $env:LOCALAPPDATA (".xmake\packages\c\cef\{0}" -f $CefRuntimeVersion)
        if (-not (Test-Path -LiteralPath $cefPackageRoot)) {
            throw "CEF $CefRuntimeVersion is not installed under $cefPackageRoot. Run xmake require/install for the launcher target, or pass -CefRuntimeDirectory."
        }

        Get-ChildItem -LiteralPath $cefPackageRoot -Directory | ForEach-Object {
            $binDirectory = Join-Path $_.FullName "bin"
            if (Test-Path -LiteralPath $binDirectory) {
                [void]$candidateDirectories.Add($binDirectory)
            }
        }
    }

    $validDirectories = New-Object 'System.Collections.Generic.List[string]'
    foreach ($candidateDirectory in $candidateDirectories) {
        $binDirectory = if (Test-Path -LiteralPath (Join-Path $candidateDirectory "libcef.dll")) {
            $candidateDirectory
        }
        else {
            Join-Path $candidateDirectory "bin"
        }
        if (-not (Test-Path -LiteralPath $binDirectory)) {
            continue
        }

        $missing = @($CefRuntimeRequiredFiles | Where-Object {
            -not (Test-Path -LiteralPath (Join-Path $binDirectory $_))
        })
        if ($missing.Count -eq 0) {
            [void]$validDirectories.Add([System.IO.Path]::GetFullPath($binDirectory))
        }
    }

    $validDirectories = @($validDirectories | Sort-Object -Unique)
    if ($validDirectories.Count -eq 0) {
        throw "No complete CEF $CefRuntimeVersion runtime was found. Required files include libcef.dll, resources.pak, and locales\en-US.pak. Pass -CefRuntimeDirectory to the xmake CEF bin directory."
    }
    if ($validDirectories.Count -gt 1) {
        throw "Multiple complete CEF $CefRuntimeVersion runtimes were found: $($validDirectories -join '; '). Pass -CefRuntimeDirectory to select one."
    }

    return $validDirectories[0]
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

function Test-SkseVrSdkRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    # Pin the exact official SDK sources compiled by SkyrimTogetherVRTickBridge.
    # This also makes an explicit SDK root fail closed instead of silently
    # accepting a different legacy ABI.
    $expectedFiles = @{
        "src\common\IPrefix.h" = "79e8b01a653213f6e9eb50213175bb002e5e1c37dfbfab1c1736ab1a8942ceab"
        "src\common\ITypes.h" = "2cd96cd31945fcc906c59fa1d0d7324bd8f0e36f246611e65703f94d95aa8a06"
        "src\sksevr\skse64\PluginAPI.h" = "d7d47ec8e6643cff46e1d31a2988cb4a46f9a71d81f68296aef9d6a620a80a87"
        "src\sksevr\skse64\PapyrusArgs.h" = "a7d11a1a487a6e7f5d3238b52ba3f0be9b5b2ef4dce0d9019853c9c5496c354b"
        "src\sksevr\skse64\PapyrusNativeFunctions.h" = "61ab1aad9c232d2a921a8f387f2e51f0ffec9cfc6a2ff1040b2b0d2f5f9373c2"
        "src\sksevr\skse64\GameAPI.cpp" = "abb7908c9e865f4416aab6d83f0fc16a6eedebe3d041e6815ef89b197a7a9407"
        "src\sksevr\skse64_common\Relocation.cpp" = "050655be00cff2fe688997f5f6d49aabac5040fec2b3c6846cacaf36cc71ea79"
        "src\sksevr\skse64_common\skse_version.h" = "3a8fb69ccca94e0e7b3a0e41d772292c1a6de0933811873d27cd3d4a1b704f7b"
    }

    foreach ($entry in $expectedFiles.GetEnumerator()) {
        $path = Join-Path $Root $entry.Key
        if (-not (Test-Path -LiteralPath $path)) {
            return $false
        }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value) {
            return $false
        }
    }

    return $true
}

function Resolve-SkseVrSdk {
    param(
        [string]$RequestedRoot
    )

    foreach ($candidate in @($RequestedRoot, $env:SKSEVR_SDK_ROOT)) {
        if (-not [string]::IsNullOrWhiteSpace($candidate)) {
            $candidateRoot = [System.IO.Path]::GetFullPath($candidate)
            if (-not (Test-SkseVrSdkRoot -Root $candidateRoot)) {
                throw "SKSEVR SDK root '$candidateRoot' does not match the pinned official 2.0.12 bridge sources. Remove it or provide the verified SDK."
            }
            $env:SKSEVR_SDK_ROOT = $candidateRoot
            return $candidateRoot
        }
    }

    $sdkCache = Join-Path $RepoRoot "Tools\SkyrimVR\.sdk"
    $resolvedRoot = Join-Path $sdkCache "sksevr_2_00_12"
    if (Test-SkseVrSdkRoot -Root $resolvedRoot) {
        $env:SKSEVR_SDK_ROOT = $resolvedRoot
        return $resolvedRoot
    }

    $archivePath = Join-Path $sdkCache "sksevr_2_00_12.7z"
    $downloadUrl = "https://skse.silverlock.org/beta/sksevr_2_00_12.7z"
    $expectedSha256 = "f03df5d8663f2c9a781f830fb0809c63a9a0e3b626d6d1a96e38493f81a3c9ad"
    New-Item -ItemType Directory -Force -Path $sdkCache | Out-Null

    if (-not (Test-Path -LiteralPath $archivePath)) {
        Write-Host "Downloading the official SKSEVR 2.0.12 SDK: $downloadUrl"
        Invoke-WebRequest -UseBasicParsing -Uri $downloadUrl -OutFile $archivePath
    }

    $actualSha256 = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualSha256 -ne $expectedSha256) {
        throw "SKSEVR SDK checksum mismatch for $archivePath. Expected $expectedSha256, got $actualSha256."
    }

    $sevenZipCandidates = New-Object 'System.Collections.Generic.List[string]'
    foreach ($candidate in @(
        (Get-Command "7z.exe" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1),
        (Get-Command "7zz.exe" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1),
        "C:\Program Files\7-Zip\7z.exe",
        (Join-Path ${env:ProgramFiles} "7-Zip\7z.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "7-Zip\7z.exe")
    )) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate)) {
            [void]$sevenZipCandidates.Add($candidate)
        }
    }
    if ($sevenZipCandidates.Count -eq 0) {
        throw "The SKSEVR SDK is not extracted and 7-Zip was not found. Install 7-Zip, set SKSEVR_SDK_ROOT to an extracted official sksevr_2_00_12 SDK, or rerun the build."
    }
    $sevenZip = $sevenZipCandidates[0]

    Write-Host "Extracting the verified SKSEVR SDK to $sdkCache"
    & $sevenZip x -y -aoa "-o$sdkCache" $archivePath
    if ($LASTEXITCODE -ne 0) {
        throw "7-Zip failed while extracting the SKSEVR SDK."
    }

    if (-not (Test-SkseVrSdkRoot -Root $resolvedRoot)) {
        throw "The extracted SKSEVR SDK does not match the pinned official 2.0.12 bridge sources."
    }

    $env:SKSEVR_SDK_ROOT = $resolvedRoot
    return $resolvedRoot
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

if (-not $SkipPapyrusCompile -and -not $NoPackage -and -not $SkipGameFiles) {
    $CompilePapyrus = $true
}

if ($SkipPapyrusCompile -and -not $NoPackage -and -not $SkipGameFiles -and $Targets -contains "SkyrimTogetherVRTickBridge") {
    throw "Cannot package SkyrimTogetherVRTickBridge with -SkipPapyrusCompile; SkyrimTogetherVRTickBridge.pex must be regenerated with the bridge DLL."
}

if ($Targets -contains "SkyrimTogetherVRTickBridge") {
    $resolvedSkseVrSdk = Resolve-SkseVrSdk -RequestedRoot $SkseVrSdkRoot
    Write-Host "Using SKSEVR 2.0.12 SDK: $resolvedSkseVrSdk"
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
$requiredTargets = @("SkyrimTogetherVRClient", "SkyrimTogetherVRVrikBridge", "SkyrimTogetherVRHiggsBridge", "SkyrimTogetherVRPlanckBridge", "SkyrimTogetherVRTickBridge", "SkyrimVRImmersiveLauncher", "ImmersiveElf", "TPProcess")
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

    $launcherTargets = @(
        "SkyrimVRImmersiveLauncher",
        "SkyrimVRImmersiveLauncherAvatarSync",
        "SkyrimVRImmersiveLauncherGameplay"
    )
    if (@($Targets | Where-Object { $launcherTargets -contains $_ }).Count -gt 0) {
        $cefRuntimeDir = Resolve-CefRuntimeDirectory -RequestedDirectory $CefRuntimeDirectory
        Write-Host "Found complete CEF $CefRuntimeVersion runtime: $cefRuntimeDir"
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
    if (Test-Path -LiteralPath $packageDir) {
        Remove-Item -Recurse -Force -LiteralPath $packageDir
    }
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
        "SkyrimTogetherVRTickBridge",
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
        "SkyrimTogetherVRTickBridge",
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
            "SkyrimTogetherVRTickBridge" { $expectedArtifactNames.Add("SkyrimTogetherVRTickBridge.dll") }
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
            if ($file.BaseName -eq "SkyrimTogetherVRVrikBridge" -or $file.BaseName -eq "SkyrimTogetherVRHiggsBridge" -or $file.BaseName -eq "SkyrimTogetherVRPlanckBridge" -or $file.BaseName -eq "SkyrimTogetherVRTickBridge") {
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
    $dllOnlyPackage = ($targetSet.Count -eq 5)
    foreach ($dllOnlyTarget in @("SkyrimTogetherVRVrikBridge", "SkyrimTogetherVRHiggsBridge", "SkyrimTogetherVRPlanckBridge", "SkyrimTogetherVRTickBridge", "ImmersiveElf")) {
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

    $launcherTargets = @(
        "SkyrimVRImmersiveLauncher",
        "SkyrimVRImmersiveLauncherAvatarSync",
        "SkyrimVRImmersiveLauncherGameplay"
    )
    $requiresCefRuntime = @($launcherTargets | Where-Object { $targetSet.Contains($_) }).Count -gt 0
    $cefRuntimeManifest = $null
    if ($requiresCefRuntime) {
        $cefRuntimeDir = Resolve-CefRuntimeDirectory -RequestedDirectory $CefRuntimeDirectory
        Get-ChildItem -LiteralPath $cefRuntimeDir -Force | ForEach-Object {
            Copy-Item -Recurse -Force -LiteralPath $_.FullName -Destination $packageDir
        }
        $cefRuntimeFiles = @(
            Get-ChildItem -LiteralPath $cefRuntimeDir -Recurse -File | ForEach-Object {
                $_.FullName.Substring($cefRuntimeDir.Length).TrimStart("\\") -replace "\\", "/"
            } | Sort-Object
        )
        $cefRuntimeManifest = [ordered]@{
            version = $CefRuntimeVersion
            files = $cefRuntimeFiles
        }
        Write-Host "Copied complete CEF $CefRuntimeVersion runtime ($($cefRuntimeFiles.Count) files) to $packageDir"
    }

    $artifactSha256 = [ordered]@{}
    foreach ($artifactName in @($copied | Sort-Object -Unique)) {
        if ($artifactName -like "SkyrimTogetherVR*Bridge.*") {
            $artifactPath = Join-Path $packageDir (Join-Path "Data\SKSE\Plugins" $artifactName)
        }
        else {
            $artifactPath = Join-Path $packageDir $artifactName
        }
        if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
            throw "Cannot hash copied artifact because it is missing from the package: $artifactPath"
        }
        $artifactSha256[$artifactName] = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash.ToLowerInvariant()
    }

    $packageSnapshotDir = Join-Path $RepoRoot (Join-Path $PackageRoot (Join-Path "packages" $packageFlavor))
    $manifestPath = Join-Path $packageDir "SkyrimTogetherVR_BuildManifest.json"
    $sourceProvenance = Get-SourceProvenance
    $packageFileSha256 = Get-PackageFileSha256 -PackageDirectory $packageDir -ManifestPath $manifestPath
    $packageManifest = [ordered]@{
        schema = "skyrim_together_vr_build_package_v2"
        mode = $Mode
        platform = "windows"
        arch = "x64"
        avatarSync = [bool]$avatarSyncPackage
        gameplay = [bool]$gameplayPackage
        packageFlavor = $packageFlavor
        targets = @($Targets)
        copiedArtifacts = @($copied | Sort-Object -Unique)
        expectedArtifacts = @($expectedArtifactNames | Sort-Object -Unique)
        artifactSha256 = $artifactSha256
        packageFileSha256 = $packageFileSha256
        sourceRevision = $sourceProvenance["sourceRevision"]
        sourceProvenance = $sourceProvenance
        packageRoot = $packageDir
        packageSnapshotRoot = $packageSnapshotDir
        stagedGameFiles = [bool](-not $SkipGameFiles)
        papyrusCompiled = [bool]$CompilePapyrus
        papyrusCompiler = $ResolvedPapyrusCompiler
        companionPanel = [bool](-not $SkipCompanionPanel)
        cefRuntime = $cefRuntimeManifest
        generatedAtUtc = [DateTime]::UtcNow.ToString("o")
    }
    $manifestJson = $packageManifest | ConvertTo-Json -Depth 5
    $utf8NoBom = New-Object System.Text.UTF8Encoding -ArgumentList $false
    [System.IO.File]::WriteAllText($manifestPath, $manifestJson + [Environment]::NewLine, $utf8NoBom)
    Write-Host "Wrote SkyrimTogetherVR build manifest to $manifestPath"

    if (Test-Path -LiteralPath $packageSnapshotDir) {
        Remove-Item -Recurse -Force -LiteralPath $packageSnapshotDir
    }
    $robocopy = Join-Path $env:SystemRoot "System32\robocopy.exe"
    & $robocopy $packageDir $packageSnapshotDir /E /COPY:DAT /R:5 /W:1 /NFL /NDL /NJH /NJS /NP
    if ($LASTEXITCODE -gt 7) {
        throw "Could not copy the SkyrimTogetherVR package snapshot; robocopy exited with code $LASTEXITCODE."
    }
    Write-Host "Copied SkyrimTogetherVR package snapshot to $packageSnapshotDir"
}

Write-Host "SkyrimTogetherVR Windows build script completed."
