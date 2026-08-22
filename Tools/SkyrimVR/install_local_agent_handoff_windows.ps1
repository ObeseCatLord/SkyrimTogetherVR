<#
Install or uninstall an extracted private local-agent handoff on a Windows client.

The command is a dry run unless -Install is supplied. It never replaces
SkyrimVR.exe or openvr_api.dll and only copies the Windows-portable overlay subset.

Use -Uninstall to restore the pre-install files recorded in
.skyrim-together-vr-local-agent-handoff under the game root.  Pair it with
-Install to make changes; -Uninstall by itself reports what would be restored.
#>
[CmdletBinding()]
param(
    [string]$GameDir,
    [switch]$Install,
    [switch]$Uninstall,
    [switch]$EnableProfile,
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$SupportedSkyrimVrSha256 = "6961efb4f4775a307b0fc9a3d637542c1e090be207d3b09467eab216b7f87971"
$ManifestSchema = "skyrim_together_vr_local_agent_handoff_v1"
$InstallStateSchema = "skyrim_together_vr_local_agent_handoff_install_state_v1"
$InstallStateDirectoryName = ".skyrim-together-vr-local-agent-handoff"
$PortableOverlayRootFiles = @(
    "sksevr_loader.exe",
    "sksevr_1_4_15.dll",
    "sksevr_steam_loader.dll"
)
$NonPortableOverlayRootFiles = @(
    "SkyrimVR.exe",
    "SkyrimSE.exe",
    "openvr_api.dll",
    "launch-skyrim-together-vr.sh",
    "launch-skyrim-vr-offline.sh",
    "stvr-xrizer-input-compat.sh",
    "SkyrimTogetherVR_BuildManifest.json"
)

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Get-ObjectProperty {
    param([object]$Object, [string]$Name)

    if ($null -eq $Object) {
        return $null
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $null
    }
    return $property.Value
}

function Get-FileSha256 {
    param([string]$Path)

    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-BytesSha256 {
    param([byte[]]$Bytes)

    $digest = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([System.BitConverter]::ToString($digest.ComputeHash($Bytes))).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $digest.Dispose()
    }
}

function Test-IsJsonInteger {
    param([object]$Value)

    return (($Value -is [int] -or $Value -is [long]) -and $Value -ge 0)
}

function Test-IsReparsePoint {
    param([System.IO.FileSystemInfo]$Item)

    return (($Item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0)
}

function Get-SafeDirectory {
    param([string]$Path, [string]$Description)

    $item = Get-Item -LiteralPath $Path -Force
    if (-not $item.PSIsContainer -or (Test-IsReparsePoint $item)) {
        throw "$Description must be a non-reparse directory: $Path"
    }
    return [System.IO.Path]::GetFullPath($item.FullName).TrimEnd([char[]]@([char]92, [char]47))
}

function Get-SafeFile {
    param([string]$Path, [string]$Description)

    $item = Get-Item -LiteralPath $Path -Force
    if ($item.PSIsContainer -or (Test-IsReparsePoint $item)) {
        throw "$Description must be a regular, non-reparse file: $Path"
    }
    return $item
}

function Assert-SafeRelativePath {
    param([string]$Path, [string]$Description)

    if ([string]::IsNullOrWhiteSpace($Path) -or $Path.StartsWith("/") -or $Path.StartsWith(([char]92).ToString())) {
        throw "$Description is absolute or empty: $Path"
    }
    $parts = $Path.Split("/")
    foreach ($part in $parts) {
        if ([string]::IsNullOrEmpty($part) -or $part -eq "." -or $part -eq ".." -or
            $part.IndexOfAny([char[]]':*?<>|"') -ge 0 -or $part.IndexOfAny([char[]]"`0`n`r") -ge 0) {
            throw "$Description is unsafe: $Path"
        }
    }
    return $parts
}

function ConvertTo-SafeZipName {
    param([string]$Name)

    if ([string]::IsNullOrEmpty($Name) -or $Name.Contains(([char]92).ToString())) {
        throw "unsafe ZIP entry: $Name"
    }
    $trimmed = $Name.TrimEnd("/")
    if ([string]::IsNullOrEmpty($trimmed)) {
        throw "unsafe ZIP entry: $Name"
    }
    [void](Assert-SafeRelativePath $trimmed "ZIP entry")
    return $trimmed
}

function Get-SafeTargetPath {
    param([string]$Root, [string]$Relative)

    $rootPath = Get-SafeDirectory $Root "target root"
    $parts = @(Assert-SafeRelativePath $Relative "install path")
    $current = $rootPath
    for ($index = 0; $index -lt ($parts.Count - 1); $index++) {
        $current = Join-Path $current $parts[$index]
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (-not $item.PSIsContainer -or (Test-IsReparsePoint $item)) {
                throw "unsafe target directory: $current"
            }
        }
    }
    $destination = Join-Path $rootPath ([System.IO.Path]::Combine([string[]]$parts))
    if (Test-Path -LiteralPath $destination) {
        $item = Get-Item -LiteralPath $destination -Force
        if (Test-IsReparsePoint $item) {
            throw "refusing to replace reparse target: $destination"
        }
    }
    return $destination
}

function Get-SafeTreeFiles {
    param([string]$Root)

    $rootPath = Get-SafeDirectory $Root "handoff root"
    $pending = [System.Collections.Generic.Stack[System.IO.DirectoryInfo]]::new()
    $pending.Push((Get-Item -LiteralPath $rootPath -Force))
    $files = @()
    while ($pending.Count -gt 0) {
        $directory = $pending.Pop()
        foreach ($item in Get-ChildItem -LiteralPath $directory.FullName -Force) {
            if (Test-IsReparsePoint $item) {
                throw "handoff contains a reparse target: $($item.FullName)"
            }
            if ($item.PSIsContainer) {
                $pending.Push($item)
                continue
            }
            $relative = $item.FullName.Substring($rootPath.Length).TrimStart([char[]]@([char]92, [char]47)).Replace(([char]92).ToString(), "/")
            [void](Assert-SafeRelativePath $relative "handoff payload path")
            $files += [PSCustomObject]@{ Relative = $relative; Item = $item }
        }
    }
    return $files
}

function Invoke-ValidatedZip {
    param([string]$Path, [scriptblock]$Body)

    $zip = [System.IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $entries = @{}
        foreach ($entry in $zip.Entries) {
            $name = ConvertTo-SafeZipName $entry.FullName
            $unixType = (($entry.ExternalAttributes -shr 16) -band 0xF000)
            if ($unixType -eq 0xA000 -or ($entry.ExternalAttributes -band 0x400) -ne 0) {
                throw "ZIP entry is a symbolic link or reparse target: $($entry.FullName)"
            }
            if ($entry.FullName.EndsWith("/")) {
                continue
            }
            $key = $name.ToLowerInvariant()
            if ($entries.ContainsKey($key)) {
                throw "ZIP contains duplicate or case-aliased entry: $name"
            }
            $entries.Add($key, $entry)
        }
        & $Body $entries
    }
    finally {
        $zip.Dispose()
    }
}

function Get-ZipEntry {
    param([hashtable]$Entries, [string]$Name)

    $entry = $Entries[$Name.ToLowerInvariant()]
    if ($null -eq $entry) {
        throw "ZIP is missing required entry: $Name"
    }
    return $entry
}

function Read-ZipJson {
    param([hashtable]$Entries, [string]$Name)

    $entry = Get-ZipEntry $Entries $Name
    $stream = $entry.Open()
    $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::UTF8, $true)
    try {
        return $reader.ReadToEnd() | ConvertFrom-Json
    }
    finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

function Get-ZipEntrySha256 {
    param([System.IO.Compression.ZipArchiveEntry]$Entry)

    $stream = $Entry.Open()
    $digest = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([System.BitConverter]::ToString($digest.ComputeHash($stream))).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $digest.Dispose()
        $stream.Dispose()
    }
}

function ConvertTo-CanonicalValue {
    param([object]$Value)

    if ($null -eq $Value -or $Value -is [string] -or $Value -is [ValueType]) {
        return $Value
    }
    if ($Value -is [System.Collections.IEnumerable]) {
        $list = [System.Collections.ArrayList]::new()
        foreach ($item in $Value) {
            [void]$list.Add((ConvertTo-CanonicalValue $item))
        }
        return $list
    }
    $ordered = [ordered]@{}
    foreach ($property in @($Value.PSObject.Properties | Sort-Object Name)) {
        $ordered[$property.Name] = ConvertTo-CanonicalValue $property.Value
    }
    return [PSCustomObject]$ordered
}

function Get-CanonicalJson {
    param([object]$Value)

    $json = (ConvertTo-CanonicalValue $Value | ConvertTo-Json -Compress -Depth 100)
    if ($json -match "[^\x00-\x7F]") {
        throw "build manifest canonical JSON is not ASCII"
    }
    return $json
}

function Assert-BuildManifest {
    param([object]$Manifest, [string]$ExpectedRevision)

    if ((Get-ObjectProperty $Manifest "schema") -ne "skyrim_together_vr_build_package_v2" -or
        (Get-ObjectProperty $Manifest "packageFlavor") -ne "gameplay" -or
        (Get-ObjectProperty $Manifest "gameplay") -ne $true) {
        throw "nested build manifest is not a gameplay package"
    }
    if ((Get-ObjectProperty $Manifest "sourceRevision") -ne $ExpectedRevision) {
        throw "nested gameplay package source revision does not match LOCAL-MANIFEST.json"
    }
    $provenance = Get-ObjectProperty $Manifest "sourceProvenance"
    if ($null -eq $provenance -or (Get-ObjectProperty $provenance "dirty") -ne $false -or
        (Get-ObjectProperty $provenance "sourceRevision") -ne $ExpectedRevision) {
        throw "nested gameplay package does not report clean, matching source provenance"
    }
    $planck = Get-ObjectProperty $Manifest "patchedPlanckArtifact"
    if ($null -eq $planck -or (Get-ObjectProperty $planck "interface") -ne "interface002" -or
        (Get-ObjectProperty $planck "name") -ne "activeragdoll.dll" -or
        (Get-ObjectProperty $planck "packagePath") -ne "Data/SKSE/Plugins/activeragdoll.dll" -or
        (Get-ObjectProperty $planck "sha256") -notmatch "^[0-9a-f]{64}$" -or
        (Get-ObjectProperty $planck "forcedBuildArtifactSha256") -ne (Get-ObjectProperty $planck "sha256") -or
        (Get-ObjectProperty $planck "forcedBuildTarget") -ne "Rebuild" -or
        (Get-ObjectProperty $planck "planckCommit") -notmatch "^[0-9a-f]{40}$" -or
        (Get-ObjectProperty $planck "planckSourceTreeSha256") -notmatch "^[0-9a-f]{64}$") {
        throw "nested gameplay package is missing valid patched PLANCK interface002 forced-build provenance"
    }
    $packageHashes = Get-ObjectProperty $Manifest "packageFileSha256"
    if ($null -eq $packageHashes -or (Get-ObjectProperty $packageHashes "Data/SKSE/Plugins/activeragdoll.dll") -ne (Get-ObjectProperty $planck "sha256")) {
        throw "nested gameplay package patched PLANCK hash is not bound to its package path"
    }
}

function Validate-GameplayPackage {
    param([string]$PackagePath, [string]$ExpectedRevision)

    return Invoke-ValidatedZip $PackagePath {
        param($entries)
        $manifestName = "package/SkyrimTogetherVR_BuildManifest.json"
        $manifest = Read-ZipJson $entries $manifestName
        Assert-BuildManifest $manifest $ExpectedRevision

        $hashes = Get-ObjectProperty $manifest "packageFileSha256"
        if ($null -eq $hashes -or @($hashes.PSObject.Properties).Count -eq 0) {
            throw "gameplay package build manifest has no file hash map"
        }
        $expected = @{}
        $expected.Add($manifestName.ToLowerInvariant(), $true)
        foreach ($property in $hashes.PSObject.Properties) {
            $relative = [string]$property.Name
            [void](Assert-SafeRelativePath $relative "gameplay package manifest path")
            $expectedHash = [string]$property.Value
            if ($expectedHash -notmatch "^[0-9a-f]{64}$") {
                throw "gameplay package has a malformed SHA-256 for $relative"
            }
            $entryName = "package/$relative"
            $entry = Get-ZipEntry $entries $entryName
            if ((Get-ZipEntrySha256 $entry) -ne $expectedHash) {
                throw "gameplay package file hash mismatch: $relative"
            }
            $expected.Add($entryName.ToLowerInvariant(), $true)
        }
        $actual = @($entries.Keys | Sort-Object)
        $declared = @($expected.Keys | Sort-Object)
        if (($actual -join "\n") -ne ($declared -join "\n")) {
            throw "gameplay package member set differs from its build manifest"
        }
        return [PSCustomObject]@{
            Manifest = $manifest
            Entries = @($entries.Values | ForEach-Object { $_.FullName } | Sort-Object)
        }
    }
}

function Validate-BuildEvidence {
    param([string]$EvidencePath, [string]$ExpectedRevision, [object]$GameplayManifest)

    Invoke-ValidatedZip $EvidencePath {
        param($entries)
        $evidenceManifest = Read-ZipJson $entries "manifest.json"
        if ((Get-ObjectProperty $evidenceManifest "mode") -ne "gameplay" -or
            (Get-ObjectProperty $evidenceManifest "packageExists") -ne $true) {
            throw "build evidence does not describe a completed gameplay package"
        }
        $commands = @(Get-ObjectProperty $evidenceManifest "commands")
        if ($commands.Count -eq 0) {
            throw "build evidence has no command results"
        }
        $requiredCommands = @(
            "python-version", "xmake-version", "xmake-targets", "windows-build-audit", "built-package-audit-gameplay"
        )
        $seenCommands = @{}
        foreach ($command in $commands) {
            $name = Get-ObjectProperty $command "name"
            if ([string]::IsNullOrEmpty([string]$name) -or (Get-ObjectProperty $command "exitCode") -ne 0) {
                throw "build evidence contains a missing or failed command result"
            }
            $seenCommands[[string]$name] = $true
        }
        foreach ($required in $requiredCommands) {
            if (-not $seenCommands.ContainsKey($required)) {
                throw "build evidence is missing required command result: $required"
            }
        }
        $nestedManifest = Read-ZipJson $entries "package/SkyrimTogetherVR_BuildManifest.json"
        Assert-BuildManifest $nestedManifest $ExpectedRevision
        if ((Get-CanonicalJson $nestedManifest) -ne (Get-CanonicalJson $GameplayManifest)) {
            throw "gameplay package and build evidence contain different build manifests"
        }
    }
}

function Assert-ArtifactIdentity {
    param([object]$Metadata, [object]$GameplayManifest, [string]$ExpectedRevision, [string]$Label)

    $canonical = Get-CanonicalJson $GameplayManifest
    $expected = @{
        sourceRevision = $ExpectedRevision
        generatedAtUtc = [string](Get-ObjectProperty $GameplayManifest "generatedAtUtc")
        buildManifestSha256 = Get-BytesSha256 ([System.Text.Encoding]::ASCII.GetBytes($canonical))
    }
    foreach ($key in $expected.Keys) {
        if ((Get-ObjectProperty $Metadata $key) -ne $expected[$key]) {
            throw "$Label metadata does not match the nested gameplay identity: $key"
        }
    }
}

function Validate-HandoffManifest {
    param([string]$Root)

    $rootPath = Get-SafeDirectory $Root "handoff root"
    $manifestItem = Get-SafeFile (Join-Path $rootPath "LOCAL-MANIFEST.json") "LOCAL-MANIFEST.json"
    try {
        $manifest = Get-Content -LiteralPath $manifestItem.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    }
    catch {
        throw "invalid LOCAL-MANIFEST.json: $($_.Exception.Message)"
    }
    if ((Get-ObjectProperty $manifest "schema") -ne $ManifestSchema -or (Get-ObjectProperty $manifest "localOnly") -ne $true) {
        throw "not a private local-agent handoff"
    }
    $records = @(Get-ObjectProperty $manifest "records")
    if ($records.Count -eq 0) {
        throw "LOCAL-MANIFEST.json has no records"
    }
    $prefix = ([System.IO.Path]::GetFileName($rootPath)) + "/"
    $recordMap = @{}
    foreach ($record in $records) {
        $path = Get-ObjectProperty $record "path"
        $size = Get-ObjectProperty $record "size"
        $hash = Get-ObjectProperty $record "sha256"
        if ($path -isnot [string] -or -not $path.StartsWith($prefix, [System.StringComparison]::Ordinal) -or
            -not (Test-IsJsonInteger $size) -or $hash -isnot [string] -or $hash -notmatch "^[0-9a-f]{64}$") {
            throw "LOCAL-MANIFEST.json has a malformed record"
        }
        $relative = $path.Substring($prefix.Length)
        [void](Assert-SafeRelativePath $relative "LOCAL-MANIFEST record")
        if ($recordMap.ContainsKey($path)) {
            throw "LOCAL-MANIFEST.json duplicates a record: $path"
        }
        $recordMap.Add($path, $record)
    }
    $payloadFiles = @(Get-SafeTreeFiles $rootPath | Where-Object { $_.Relative -ne "LOCAL-MANIFEST.json" })
    $payloadMap = @{}
    foreach ($file in $payloadFiles) {
        $payloadMap.Add($prefix + $file.Relative, $file.Item)
    }
    if ($payloadMap.Count -ne $recordMap.Count -or @($payloadMap.Keys | Where-Object { -not $recordMap.ContainsKey($_) }).Count -ne 0) {
        throw "LOCAL-MANIFEST.json record set does not match the extracted handoff payload"
    }
    foreach ($path in $recordMap.Keys) {
        $file = $payloadMap[$path]
        $record = $recordMap[$path]
        if ($file.Length -ne (Get-ObjectProperty $record "size") -or
            (Get-FileSha256 $file.FullName) -ne (Get-ObjectProperty $record "sha256")) {
            throw "LOCAL-MANIFEST.json record does not match payload: $path"
        }
    }
    return [PSCustomObject]@{ Manifest = $manifest; Records = $recordMap; Root = $rootPath }
}

function Get-VerifiedArtifact {
    param([object]$ValidatedHandoff, [string]$Key)

    $metadata = Get-ObjectProperty $ValidatedHandoff.Manifest $Key
    $name = Get-ObjectProperty $metadata "name"
    $expectedHash = Get-ObjectProperty $metadata "sha256"
    if ($null -eq $metadata -or $name -isnot [string] -or $expectedHash -isnot [string]) {
        throw "LOCAL-MANIFEST.json has invalid $Key metadata"
    }
    [void](Assert-SafeRelativePath $name "$Key name")
    if ($name.Contains("/")) {
        throw "LOCAL-MANIFEST.json $Key name must not contain a path"
    }
    $relative = "build/$name"
    $recordPath = ([System.IO.Path]::GetFileName($ValidatedHandoff.Root)) + "/" + $relative
    $record = $ValidatedHandoff.Records[$recordPath]
    $path = Join-Path $ValidatedHandoff.Root $relative
    $file = Get-SafeFile $path $Key
    if ($null -eq $record -or (Get-ObjectProperty $record "sha256") -ne $expectedHash -or
        $file.Length -ne (Get-ObjectProperty $record "size") -or (Get-FileSha256 $file.FullName) -ne $expectedHash) {
        throw "$Key does not match LOCAL-MANIFEST.json"
    }
    return [PSCustomObject]@{ File = $file; Metadata = $metadata }
}

function Get-WindowsOverlayPlan {
    param([string]$Root, [string]$TargetGameDir)

    $overlay = Join-Path $Root "dependencies/current-game-overlay"
    $overlayFiles = @(Get-SafeTreeFiles $overlay)
    $portable = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $nonPortable = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($name in $PortableOverlayRootFiles) { [void]$portable.Add($name) }
    foreach ($name in $NonPortableOverlayRootFiles) { [void]$nonPortable.Add($name) }
    $skyrimExeCount = 0
    $openVrApiCount = 0
    $plan = @()
    foreach ($source in $overlayFiles | Sort-Object Relative) {
        $relative = $source.Relative
        if ($relative -ieq "SkyrimVR.exe") {
            $skyrimExeCount++
            continue
        }
        if ($relative -ieq "openvr_api.dll") {
            $openVrApiCount++
            continue
        }
        if ($relative.StartsWith("Data/", [System.StringComparison]::OrdinalIgnoreCase)) {
            Assert-NotInstallStatePath $relative
            if ($relative.StartsWith("Data/SkyrimTogetherReborn/SkyrimTogetherVR.", [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "current-game-overlay contains stale per-session Skyrim Together runtime data"
            }
            $plan += [PSCustomObject]@{
                Source = $source.Item.FullName
                Destination = Get-SafeTargetPath $TargetGameDir $relative
                TargetRoot = $TargetGameDir
                TargetRelative = $relative
            }
            continue
        }
        if ($portable.Contains($relative)) {
            Assert-NotInstallStatePath $relative
            $plan += [PSCustomObject]@{
                Source = $source.Item.FullName
                Destination = Get-SafeTargetPath $TargetGameDir $relative
                TargetRoot = $TargetGameDir
                TargetRelative = $relative
            }
            continue
        }
        if ($nonPortable.Contains($relative)) {
            continue
        }
        throw "current-game-overlay contains unsupported Windows root file: $relative"
    }
    if ($skyrimExeCount -ne 1) {
        throw "current-game-overlay must contain exactly one SkyrimVR.exe to preserve"
    }
    if ($openVrApiCount -ne 1) {
        throw "current-game-overlay must contain exactly one openvr_api.dll to preserve"
    }
    return $plan
}

function Get-GameplayPackagePlan {
    param([string]$PackagePath, [string]$TargetGameDir, [string[]]$Entries)

    $plan = @()
    foreach ($entry in $Entries) {
        if (-not $entry.StartsWith("package/", [System.StringComparison]::Ordinal) -or $entry -eq "package/") {
            throw "gameplay package has unexpected entry: $entry"
        }
        $relative = $entry.Substring("package/".Length)
        if ($relative -ieq "SkyrimVR.exe" -or $relative -ieq "openvr_api.dll") {
            throw "gameplay package must not replace preserved root file: $relative"
        }
        Assert-NotInstallStatePath $relative
        $plan += [PSCustomObject]@{
            Entry = $entry
            Destination = Get-SafeTargetPath $TargetGameDir $relative
            TargetRoot = $TargetGameDir
            TargetRelative = $relative
        }
    }
    return $plan
}

function Get-ProfilePlan {
    param([string]$Root)

    if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA) -or -not (Test-Path -LiteralPath $env:LOCALAPPDATA)) {
        throw "LOCALAPPDATA must name an existing directory before enabling the Windows profile"
    }
    $localAppData = Get-SafeDirectory $env:LOCALAPPDATA "LOCALAPPDATA"
    $plan = @()
    foreach ($name in @("Plugins.txt", "loadorder.txt")) {
        $source = Get-SafeFile (Join-Path $Root "profiles/direct-proton/$name") "bundled profile $name"
        $plan += [PSCustomObject]@{
            Source = $source.FullName
            Destination = Get-SafeTargetPath $localAppData "Skyrim VR/$name"
            TargetRoot = $localAppData
            TargetRelative = "Skyrim VR/$name"
            TargetScope = "profile"
        }
    }
    return $plan
}

function Assert-NotInstallStatePath {
    param([string]$Relative)

    $parts = @(Assert-SafeRelativePath $Relative "install path")
    if ($parts[0] -ieq $InstallStateDirectoryName) {
        throw "handoff payload may not target the installer state directory: $Relative"
    }
}

function Get-InstallStateDirectory {
    param([string]$GameRoot, [switch]$Create)

    $gamePath = Get-SafeDirectory $GameRoot "game root"
    $path = Join-Path $gamePath $InstallStateDirectoryName
    if (Test-Path -LiteralPath $path) {
        return Get-SafeDirectory $path "installer state directory"
    }
    if (-not $Create) {
        return $null
    }
    New-Item -ItemType Directory -Path $path -ErrorAction Stop | Out-Null
    return Get-SafeDirectory $path "installer state directory"
}

function Get-InstallStatePath {
    param([string]$StateDirectory)

    return (Join-Path (Get-SafeDirectory $StateDirectory "installer state directory") "state.json")
}

function Save-InstallState {
    param([string]$StateDirectory, [object]$State)

    $stateDirectory = Get-SafeDirectory $StateDirectory "installer state directory"
    $statePath = Get-InstallStatePath $stateDirectory
    if (Test-Path -LiteralPath $statePath) {
        [void](Get-SafeFile $statePath "installer state")
    }
    $temporary = Join-Path $stateDirectory (".state-" + [guid]::NewGuid().ToString("N") + ".tmp")
    $replaceBackup = $temporary + ".replace-backup"
    $json = $State | ConvertTo-Json -Depth 100
    try {
        [System.IO.File]::WriteAllText($temporary, $json, [System.Text.UTF8Encoding]::new($false))
        if (Test-Path -LiteralPath $statePath) {
            [System.IO.File]::Replace($temporary, $statePath, $replaceBackup)
            Remove-Item -LiteralPath $replaceBackup -Force
        }
        else {
            [System.IO.File]::Move($temporary, $statePath)
        }
    }
    finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
        if (Test-Path -LiteralPath $replaceBackup) {
            Remove-Item -LiteralPath $replaceBackup -Force
        }
    }
}

function Read-InstallState {
    param([string]$StateDirectory)

    if ($null -eq $StateDirectory) {
        return $null
    }
    $statePath = Get-InstallStatePath $StateDirectory
    if (-not (Test-Path -LiteralPath $statePath)) {
        return $null
    }
    $stateFile = Get-SafeFile $statePath "installer state"
    try {
        $state = Get-Content -LiteralPath $stateFile.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    }
    catch {
        throw "installer state is invalid: $($_.Exception.Message)"
    }
    if ((Get-ObjectProperty $state "schema") -ne $InstallStateSchema -or
        [string]::IsNullOrWhiteSpace([string](Get-ObjectProperty $state "status"))) {
        throw "installer state has an unsupported schema"
    }
    return $state
}

function Ensure-SafeTargetParent {
    param([string]$Root, [string]$Relative)

    $parts = @(Assert-SafeRelativePath $Relative "install path")
    $rootPath = Get-SafeDirectory $Root "target root"
    $current = $rootPath
    for ($index = 0; $index -lt ($parts.Count - 1); $index++) {
        $current = Join-Path $current $parts[$index]
        if (-not (Test-Path -LiteralPath $current)) {
            New-Item -ItemType Directory -Path $current -ErrorAction Stop | Out-Null
        }
        [void](Get-SafeDirectory $current "target directory")
    }
}

function Resolve-StateDestination {
    param([object]$Record, [string]$GameRoot)

    $scope = [string](Get-ObjectProperty $Record "targetScope")
    $relative = [string](Get-ObjectProperty $Record "targetRelative")
    [void](Assert-SafeRelativePath $relative "installer state target")
    if ($scope -eq "game") {
        Assert-NotInstallStatePath $relative
        return Get-SafeTargetPath $GameRoot $relative
    }
    if ($scope -eq "profile") {
        $recordedRoot = [string](Get-ObjectProperty $Record "targetRoot")
        if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
            throw "LOCALAPPDATA is required to restore the recorded Windows profile"
        }
        $profileRoot = Get-SafeDirectory $env:LOCALAPPDATA "LOCALAPPDATA"
        if (-not [string]::Equals($profileRoot, $recordedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "recorded profile root does not match the current LOCALAPPDATA"
        }
        return Get-SafeTargetPath $profileRoot $relative
    }
    throw "installer state has an unsupported target scope"
}

function New-TemporarySiblingPath {
    param([string]$Destination)

    $parent = Split-Path -Path $Destination -Parent
    [void](Get-SafeDirectory $parent "target directory")
    return (Join-Path $parent ("." + [System.IO.Path]::GetFileName($Destination) + ".stvr-" + [guid]::NewGuid().ToString("N") + ".tmp"))
}

function Invoke-AtomicFileWrite {
    param([string]$Destination, [scriptblock]$WriteTemporary, [string]$ExpectedSha256)

    $temporary = New-TemporarySiblingPath $Destination
    $replaceBackup = $temporary + ".replace-backup"
    try {
        & $WriteTemporary $temporary
        $temporaryFile = Get-SafeFile $temporary "temporary install file"
        if ((Get-FileSha256 $temporaryFile.FullName) -ne $ExpectedSha256) {
            throw "temporary install content hash mismatch: $Destination"
        }
        if (Test-Path -LiteralPath $Destination) {
            [void](Get-SafeFile $Destination "install target")
            [System.IO.File]::Replace($temporary, $Destination, $replaceBackup)
            Remove-Item -LiteralPath $replaceBackup -Force
        }
        else {
            [System.IO.File]::Move($temporary, $Destination)
        }
    }
    finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
        if (Test-Path -LiteralPath $replaceBackup) {
            Remove-Item -LiteralPath $replaceBackup -Force
        }
    }
}

function Write-OperationToTemporary {
    param([object]$Operation, [string]$Temporary)

    if ($Operation.kind -eq "copy") {
        $source = Get-SafeFile $Operation.source "install source"
        [System.IO.File]::Copy($source.FullName, $Temporary, $false)
        return
    }
    if ($Operation.kind -eq "zip") {
        Invoke-ValidatedZip $Operation.package {
            param($entries)
            $entry = Get-ZipEntry $entries $Operation.entry
            $input = $entry.Open()
            $output = [System.IO.File]::Open($Temporary, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write)
            try {
                $input.CopyTo($output)
            }
            finally {
                $output.Dispose()
                $input.Dispose()
            }
        }
        return
    }
    throw "unsupported install operation"
}

function New-InstallOperations {
    param([object[]]$OverlayPlan, [object[]]$PackagePlan, [object[]]$ProfilePlan, [string]$PackagePath)

    $operations = @()
    foreach ($item in $OverlayPlan + $ProfilePlan) {
        $source = Get-SafeFile $item.Source "install source"
        $operations += [PSCustomObject]@{
            kind = "copy"; source = $source.FullName; targetRoot = $item.TargetRoot
            targetRelative = $item.TargetRelative; targetScope = $(if ($item.PSObject.Properties["TargetScope"]) { $item.TargetScope } else { "game" })
            sha256 = Get-FileSha256 $source.FullName
        }
    }
    if ($PackagePlan.Count -gt 0) {
        Invoke-ValidatedZip $PackagePath {
            param($entries)
            foreach ($item in $PackagePlan) {
                $entry = Get-ZipEntry $entries $item.Entry
                $operations += [PSCustomObject]@{
                    kind = "zip"; package = $PackagePath; entry = $item.Entry; targetRoot = $item.TargetRoot
                    targetRelative = $item.TargetRelative; targetScope = "game"; sha256 = Get-ZipEntrySha256 $entry
                }
            }
        }
    }
    $deduplicated = New-Object 'System.Collections.Generic.List[object]'
    $indices = New-Object 'System.Collections.Generic.Dictionary[string,int]' ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($operation in $operations) {
        $key = "$($operation.targetScope)`0$($operation.targetRoot)`0$($operation.targetRelative)"
        if ($indices.ContainsKey($key)) {
            # Package operations are appended last and therefore replace an
            # overlay operation for the same Windows destination.
            $deduplicated[$indices[$key]] = $operation
        }
        else {
            $indices.Add($key, $deduplicated.Count)
            $deduplicated.Add($operation)
        }
    }
    return @($deduplicated)
}

function Test-InstallIsCurrent {
    param([object]$State, [object[]]$Operations, [string]$GameRoot)

    if ($null -eq $State -or $State.status -ne "committed" -or @($State.entries).Count -ne $Operations.Count) {
        return $false
    }
    for ($index = 0; $index -lt $Operations.Count; $index++) {
        $operation = $Operations[$index]
        $record = @($State.entries)[$index]
        if ($record.targetScope -ne $operation.targetScope -or $record.targetRelative -ne $operation.targetRelative -or
            $record.installedSha256 -ne $operation.sha256) {
            return $false
        }
        $destination = Resolve-StateDestination $record $GameRoot
        if (-not (Test-Path -LiteralPath $destination) -or
            (Get-FileSha256 (Get-SafeFile $destination "installed target").FullName) -ne $operation.sha256) {
            return $false
        }
    }
    return $true
}

function Restore-InstallRecord {
    param([object]$Record, [string]$GameRoot, [string]$StateDirectory)

    $destination = Resolve-StateDestination $Record $GameRoot
    $installedHash = [string](Get-ObjectProperty $Record "installedSha256")
    $hadOriginal = [bool](Get-ObjectProperty $Record "hadOriginal")
    $currentHash = $null
    if (Test-Path -LiteralPath $destination) {
        $current = Get-SafeFile $destination "installed target"
        $currentHash = Get-FileSha256 $current.FullName
    }
    $originalHash = [string](Get-ObjectProperty $Record "originalSha256")
    if ($hadOriginal -and $currentHash -eq $originalHash) {
        if ($null -ne (Get-ObjectProperty $Record "originalAttributes")) {
            (Get-Item -LiteralPath $destination -Force).Attributes = [System.IO.FileAttributes]([int]$Record.originalAttributes)
        }
        return
    }
    if (-not $hadOriginal -and $null -eq $currentHash) {
        return
    }
    if ($currentHash -ne $installedHash) {
        throw "refusing to restore a target changed outside this handoff: $destination"
    }
    if ($hadOriginal) {
        $backupRelative = [string](Get-ObjectProperty $Record "backupRelative")
        [void](Assert-SafeRelativePath $backupRelative "installer backup path")
        $backup = Join-Path (Get-SafeDirectory $StateDirectory "installer state directory") $backupRelative
        $backupFile = Get-SafeFile $backup "installer backup"
        if ((Get-FileSha256 $backupFile.FullName) -ne $originalHash) {
            throw "installer backup hash mismatch: $destination"
        }
        $targetRoot = if ($Record.targetScope -eq "game") { $GameRoot } else { Get-SafeDirectory $env:LOCALAPPDATA "LOCALAPPDATA" }
        Ensure-SafeTargetParent $targetRoot $Record.targetRelative
        Invoke-AtomicFileWrite $destination { param($temporary) [System.IO.File]::Copy($backupFile.FullName, $temporary, $false) } $originalHash
        if ($null -ne (Get-ObjectProperty $Record "originalAttributes")) {
            (Get-Item -LiteralPath $destination -Force).Attributes = [System.IO.FileAttributes]([int]$Record.originalAttributes)
        }
    }
    else {
        Remove-Item -LiteralPath $destination -Force
    }
}

function Complete-Uninstall {
    param([object]$State, [string]$GameRoot, [string]$StateDirectory)

    $State.status = "uninstalling"
    Save-InstallState $StateDirectory $State
    foreach ($record in @($State.entries | Sort-Object { [int]$_.sequence } -Descending)) {
        if (-not [bool](Get-ObjectProperty $record "restored")) {
            Restore-InstallRecord $record $GameRoot $StateDirectory
            $record.restored = $true
            Save-InstallState $StateDirectory $State
        }
    }
    $State.status = "uninstalled"
    $State.entries = @()
    Save-InstallState $StateDirectory $State
}

function Recover-InterruptedInstall {
    param([object]$State, [string]$GameRoot, [string]$StateDirectory)

    if ($State.status -eq "installing") {
        Complete-Uninstall $State $GameRoot $StateDirectory
    }
    elseif ($State.status -eq "uninstalling") {
        Complete-Uninstall $State $GameRoot $StateDirectory
    }
    elseif ($State.status -notin @("committed", "uninstalled")) {
        throw "installer state has an unsupported status"
    }
}

function Invoke-InstallTransaction {
    param([object[]]$Operations, [string]$GameRoot, [string]$StateDirectory)

    $state = [PSCustomObject]@{
        schema = $InstallStateSchema; status = "installing"; entries = @(); createdAtUtc = [DateTime]::UtcNow.ToString("o")
        transactionId = [guid]::NewGuid().ToString("N")
    }
    Save-InstallState $StateDirectory $state
    try {
        $sequence = 0
        foreach ($operation in $Operations) {
            $destination = Get-SafeTargetPath $operation.targetRoot $operation.targetRelative
            if ($operation.targetScope -eq "game") { Assert-NotInstallStatePath $operation.targetRelative }
            $record = [PSCustomObject]@{
                sequence = $sequence; targetScope = $operation.targetScope; targetRoot = $operation.targetRoot
                targetRelative = $operation.targetRelative; installedSha256 = $operation.sha256; hadOriginal = $false
                originalSha256 = $null; originalAttributes = $null; backupRelative = $null; mutationPrepared = $false; restored = $false
            }
            if (Test-Path -LiteralPath $destination) {
                $original = Get-SafeFile $destination "install target"
                $record.hadOriginal = $true
                $record.originalSha256 = Get-FileSha256 $original.FullName
                $record.originalAttributes = [int]$original.Attributes
                $backupRelative = "backups/" + $state.transactionId + "/" + $sequence.ToString("D6") + ".bin"
                $backup = Join-Path $StateDirectory $backupRelative
                $backupParent = Split-Path -Path $backup -Parent
                if (-not (Test-Path -LiteralPath $backupParent)) { New-Item -ItemType Directory -Path $backupParent -ErrorAction Stop | Out-Null }
                [void](Get-SafeDirectory $backupParent "installer backup directory")
                [System.IO.File]::Copy($original.FullName, $backup, $false)
                if ((Get-FileSha256 $backup) -ne $record.originalSha256) { throw "installer backup hash mismatch: $destination" }
                $record.backupRelative = $backupRelative
            }
            $state.entries += $record
            Save-InstallState $StateDirectory $state
            $record.mutationPrepared = $true
            Save-InstallState $StateDirectory $state
            Ensure-SafeTargetParent $operation.targetRoot $operation.targetRelative
            Invoke-AtomicFileWrite $destination { param($temporary) Write-OperationToTemporary $operation $temporary } $operation.sha256
            $sequence++
        }
        $state.status = "committed"
        Save-InstallState $StateDirectory $state
    }
    catch {
        $failure = $_
        try { Complete-Uninstall $state $GameRoot $StateDirectory }
        catch { throw "install failed and rollback also failed: $($failure.Exception.Message); $($_.Exception.Message)" }
        throw $failure
    }
}

function Invoke-InstallWithRecovery {
    param([object[]]$Operations, [string]$GameRoot, [string]$StateDirectory)

    $state = Read-InstallState $StateDirectory
    if ($null -ne $state) {
        Recover-InterruptedInstall $state $GameRoot $StateDirectory
        $state = Read-InstallState $StateDirectory
        if (Test-InstallIsCurrent $state $Operations $GameRoot) {
            return $true
        }
        if ($state.status -eq "committed") {
            Complete-Uninstall $state $GameRoot $StateDirectory
        }
    }
    Invoke-InstallTransaction $Operations $GameRoot $StateDirectory
    return $false
}

function Invoke-SelfTest {
    $temp = Join-Path ([System.IO.Path]::GetTempPath()) ("stvr-windows-handoff-" + [guid]::NewGuid().ToString("N"))
    try {
        $root = Join-Path $temp "handoff"
        $game = Join-Path $temp "game"
        New-Item -ItemType Directory -Path "$root/dependencies/current-game-overlay/Data/Test", $game -Force | Out-Null
        Set-Content -LiteralPath "$root/dependencies/current-game-overlay/SkyrimVR.exe" -Value "preserved" -NoNewline -Encoding ASCII
        Set-Content -LiteralPath "$root/dependencies/current-game-overlay/OPENVR_API.DLL" -Value "preserved-openvr" -NoNewline -Encoding ASCII
        Set-Content -LiteralPath "$root/dependencies/current-game-overlay/Data/Test/portable.txt" -Value "portable" -NoNewline -Encoding ASCII
        $plan = @(Get-WindowsOverlayPlan $root $game)
        if ($plan.Count -ne 1 -or (Test-Path -LiteralPath "$game/Data/Test/portable.txt") -or
            (Test-Path -LiteralPath "$game/openvr_api.dll")) {
            throw "dry-run plan mutated the game directory"
        }
        $singleTarget = Get-SafeTargetPath $game "single.txt"
        if ($singleTarget -ne (Join-Path $game "single.txt") -or (Test-Path -LiteralPath $singleTarget)) {
            throw "one-segment dry-run target handling failed"
        }
        New-Item -ItemType Directory -Path "$game/Data/Test" -Force | Out-Null
        Set-Content -LiteralPath "$game/Data/Test/portable.txt" -Value "original" -NoNewline -Encoding ASCII
        Set-Content -LiteralPath "$game/SkyrimVR.exe" -Value "player-exe" -NoNewline -Encoding ASCII
        Set-Content -LiteralPath "$game/openvr_api.dll" -Value "player-openvr" -NoNewline -Encoding ASCII
        $operation = [PSCustomObject]@{
            kind = "copy"; source = "$root/dependencies/current-game-overlay/Data/Test/portable.txt"; targetRoot = $game
            targetRelative = "Data/Test/portable.txt"; targetScope = "game"; sha256 = Get-FileSha256 "$root/dependencies/current-game-overlay/Data/Test/portable.txt"
        }
        $stateDirectory = Get-InstallStateDirectory $game -Create
        if (Invoke-InstallWithRecovery @($operation) $game $stateDirectory) {
            throw "fresh install was incorrectly considered current"
        }
        if ((Get-Content -LiteralPath "$game/Data/Test/portable.txt" -Raw) -ne "portable" -or
            (Get-Content -LiteralPath "$game/SkyrimVR.exe" -Raw) -ne "player-exe" -or
            (Get-Content -LiteralPath "$game/openvr_api.dll" -Raw) -ne "player-openvr") {
            throw "transactional install did not preserve root files or install payload"
        }
        if (-not (Invoke-InstallWithRecovery @($operation) $game $stateDirectory)) {
            throw "identical install was not idempotent"
        }
        $installedState = Read-InstallState $stateDirectory
        Complete-Uninstall $installedState $game $stateDirectory
        if ((Get-Content -LiteralPath "$game/Data/Test/portable.txt" -Raw) -ne "original" -or
            (Get-Content -LiteralPath "$game/SkyrimVR.exe" -Raw) -ne "player-exe" -or
            (Get-Content -LiteralPath "$game/openvr_api.dll" -Raw) -ne "player-openvr") {
            throw "uninstall did not restore payload or preserve root files"
        }
        Set-Content -LiteralPath "$root/rollback-first.txt" -Value "new-first" -NoNewline -Encoding ASCII
        Set-Content -LiteralPath "$game/rollback-first.txt" -Value "old-first" -NoNewline -Encoding ASCII
        $rollbackOperations = @(
            [PSCustomObject]@{ kind = "copy"; source = "$root/rollback-first.txt"; targetRoot = $game; targetRelative = "rollback-first.txt"; targetScope = "game"; sha256 = Get-FileSha256 "$root/rollback-first.txt" },
            [PSCustomObject]@{ kind = "copy"; source = "$root/missing-source.txt"; targetRoot = $game; targetRelative = "rollback-second.txt"; targetScope = "game"; sha256 = ("0" * 64) }
        )
        try {
            [void](Invoke-InstallWithRecovery $rollbackOperations $game $stateDirectory)
            throw "rollback test unexpectedly succeeded"
        }
        catch [System.Management.Automation.RuntimeException] {
            if ($_.Exception.Message -eq "rollback test unexpectedly succeeded") { throw }
        }
        if ((Get-Content -LiteralPath "$game/rollback-first.txt" -Raw) -ne "old-first" -or
            (Test-Path -LiteralPath "$game/rollback-second.txt")) {
            throw "failed install did not roll back changed targets"
        }
        $numericProbe = '{"small":7,"large":4294967296}' | ConvertFrom-Json
        if (-not (Test-IsJsonInteger $numericProbe.small) -or -not (Test-IsJsonInteger $numericProbe.large)) {
            throw "PowerShell JSON integer typing is unsupported"
        }
        try {
            [void](ConvertTo-SafeZipName "../escape")
            throw "unsafe ZIP traversal was accepted"
        }
        catch [System.Management.Automation.RuntimeException] {
            if ($_.Exception.Message -eq "unsafe ZIP traversal was accepted") { throw }
        }
        $manifestRoot = Join-Path $temp "manifest-handoff"
        New-Item -ItemType Directory -Path $manifestRoot -Force | Out-Null
        Set-Content -LiteralPath "$manifestRoot/payload.txt" -Value "payload" -NoNewline -Encoding ASCII
        $badManifest = [ordered]@{
            schema = $ManifestSchema
            localOnly = $true
            records = @([ordered]@{ path = "manifest-handoff/payload.txt"; size = 7; sha256 = ("0" * 64) })
        } | ConvertTo-Json -Depth 10
        Set-Content -LiteralPath "$manifestRoot/LOCAL-MANIFEST.json" -Value $badManifest -NoNewline -Encoding UTF8
        try {
            [void](Validate-HandoffManifest $manifestRoot)
            throw "mismatched LOCAL-MANIFEST record was accepted"
        }
        catch [System.Management.Automation.RuntimeException] {
            if ($_.Exception.Message -eq "mismatched LOCAL-MANIFEST record was accepted") {
                throw
            }
            if (-not $_.Exception.Message.StartsWith("LOCAL-MANIFEST.json record does not match payload:")) {
                throw
            }
        }
        Write-Output "Windows installer self-test passed"
    }
    finally {
        if (Test-Path -LiteralPath $temp) {
            Remove-Item -LiteralPath $temp -Recurse -Force
        }
    }
}

try {
    if ($SelfTest) {
        Invoke-SelfTest
        exit 0
    }
    if ([string]::IsNullOrWhiteSpace($GameDir)) {
        throw "-GameDir is required"
    }
    $game = Get-SafeDirectory $GameDir "-GameDir"
    if ($Uninstall) {
        if ($EnableProfile) {
            throw "-EnableProfile cannot be combined with -Uninstall"
        }
        $stateDirectory = Get-InstallStateDirectory $game
        $state = if ($null -eq $stateDirectory) { $null } else { Read-InstallState $stateDirectory }
        if ($null -eq $state -or $state.status -eq "uninstalled") {
            Write-Output "uninstall: no local-agent handoff state is present"
            exit 0
        }
        $recordCount = @($state.entries).Count
        if (-not $Install) {
            Write-Output "validated uninstall dry run; would restore $recordCount recorded targets"
            exit 0
        }
        Complete-Uninstall $state $game $stateDirectory
        Write-Output "uninstalled: restored $recordCount recorded targets"
        exit 0
    }

    $root = Get-SafeDirectory $PSScriptRoot "extracted handoff root"
    $gameExe = Get-SafeFile (Join-Path $game "SkyrimVR.exe") "SkyrimVR.exe"
    if ((Get-FileSha256 $gameExe.FullName) -ne $SupportedSkyrimVrSha256) {
        throw "SkyrimVR.exe is not the supported legal Skyrim VR 1.4.15 executable"
    }

    $handoff = Validate-HandoffManifest $root
    $gameplayPackage = Get-VerifiedArtifact $handoff "gameplayPackage"
    $buildEvidence = Get-VerifiedArtifact $handoff "buildEvidence"
    $buildRevision = Get-ObjectProperty $handoff.Manifest "buildSourceRevision"
    if ($buildRevision -isnot [string] -or $buildRevision -notmatch "^[0-9a-f]{40}$") {
        throw "LOCAL-MANIFEST.json is missing a valid build source revision"
    }
    $gameplay = Validate-GameplayPackage $gameplayPackage.File.FullName $buildRevision
    Validate-BuildEvidence $buildEvidence.File.FullName $buildRevision $gameplay.Manifest
    Assert-ArtifactIdentity $gameplayPackage.Metadata $gameplay.Manifest $buildRevision "gameplayPackage"
    Assert-ArtifactIdentity $buildEvidence.Metadata $gameplay.Manifest $buildRevision "buildEvidence"

    $overlayPlan = @(Get-WindowsOverlayPlan $root $game)
    $packagePlan = @(Get-GameplayPackagePlan $gameplayPackage.File.FullName $game $gameplay.Entries)
    $profilePlan = @()
    if ($EnableProfile) {
        $profilePlan = @(Get-ProfilePlan $root)
    }
    $operations = @(New-InstallOperations $overlayPlan $packagePlan $profilePlan $gameplayPackage.File.FullName)
    if ($Install) {
        $stateDirectory = Get-InstallStateDirectory $game -Create
        $alreadyInstalled = Invoke-InstallWithRecovery $operations $game $stateDirectory
        $profileState = if ($EnableProfile) { "; profile enabled" } else { "" }
        $action = if ($alreadyInstalled) { "already installed" } else { "installed" }
        Write-Output "${action}: $($overlayPlan.Count) portable overlay files, $($packagePlan.Count) gameplay files$profileState; build $($buildRevision.Substring(0, 8))"
    }
    else {
        $profileState = if ($EnableProfile) { "; profile would be enabled" } else { "" }
        Write-Output "validated (dry run; no target files changed): $($overlayPlan.Count) portable overlay files, $($packagePlan.Count) gameplay files$profileState; build $($buildRevision.Substring(0, 8))"
    }
}
catch {
    [Console]::Error.WriteLine("install failed: " + $_.Exception.Message)
    exit 1
}
