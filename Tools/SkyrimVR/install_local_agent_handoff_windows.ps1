<#
Install an extracted private local-agent handoff into a second Windows client.

The command is a dry run unless -Install is supplied. It never replaces
SkyrimVR.exe and only copies the Windows-portable overlay subset.
#>
[CmdletBinding()]
param(
    [string]$GameDir,
    [switch]$Install,
    [switch]$EnableProfile,
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$SupportedSkyrimVrSha256 = "6961efb4f4775a307b0fc9a3d637542c1e090be207d3b09467eab216b7f87971"
$ManifestSchema = "skyrim_together_vr_local_agent_handoff_v1"
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
    $plan = @()
    foreach ($source in $overlayFiles | Sort-Object Relative) {
        $relative = $source.Relative
        if ($relative -ieq "SkyrimVR.exe") {
            $skyrimExeCount++
            continue
        }
        if ($relative.StartsWith("Data/", [System.StringComparison]::OrdinalIgnoreCase)) {
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
        if ($relative -ieq "SkyrimVR.exe") {
            throw "gameplay package must not replace the legal SkyrimVR.exe"
        }
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
        }
    }
    return $plan
}

function Invoke-CopyPlan {
    param([object[]]$Plan)

    foreach ($item in $Plan) {
        $destination = Get-SafeTargetPath $item.TargetRoot $item.TargetRelative
        $parent = Split-Path -LiteralPath $destination -Parent
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
        $target = Get-Item -LiteralPath $destination -Force -ErrorAction SilentlyContinue
        if ($null -ne $target -and (Test-IsReparsePoint $target)) {
            throw "refusing to replace reparse target: $destination"
        }
        Copy-Item -LiteralPath $item.Source -Destination $destination -Force
    }
}

function Install-GameplayPackage {
    param([string]$PackagePath, [object[]]$Plan)

    Invoke-ValidatedZip $PackagePath {
        param($entries)
        foreach ($item in $Plan) {
            $entry = Get-ZipEntry $entries $item.Entry
            $destination = Get-SafeTargetPath $item.TargetRoot $item.TargetRelative
            $parent = Split-Path -LiteralPath $destination -Parent
            New-Item -ItemType Directory -Path $parent -Force | Out-Null
            $target = Get-Item -LiteralPath $destination -Force -ErrorAction SilentlyContinue
            if ($null -ne $target -and (Test-IsReparsePoint $target)) {
                throw "refusing to replace reparse target: $destination"
            }
            $input = $entry.Open()
            $output = [System.IO.File]::Open($destination, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
            try {
                $input.CopyTo($output)
            }
            finally {
                $output.Dispose()
                $input.Dispose()
            }
        }
    }
}

function Invoke-SelfTest {
    $temp = Join-Path ([System.IO.Path]::GetTempPath()) ("stvr-windows-handoff-" + [guid]::NewGuid().ToString("N"))
    try {
        $root = Join-Path $temp "handoff"
        $game = Join-Path $temp "game"
        New-Item -ItemType Directory -Path "$root/dependencies/current-game-overlay/Data/Test", $game -Force | Out-Null
        Set-Content -LiteralPath "$root/dependencies/current-game-overlay/SkyrimVR.exe" -Value "preserved" -NoNewline -Encoding ASCII
        Set-Content -LiteralPath "$root/dependencies/current-game-overlay/Data/Test/portable.txt" -Value "portable" -NoNewline -Encoding ASCII
        $plan = @(Get-WindowsOverlayPlan $root $game)
        if ($plan.Count -ne 1 -or (Test-Path -LiteralPath "$game/Data/Test/portable.txt")) {
            throw "dry-run plan mutated the game directory"
        }
        $singleTarget = Get-SafeTargetPath $game "single.txt"
        if ($singleTarget -ne (Join-Path $game "single.txt") -or (Test-Path -LiteralPath $singleTarget)) {
            throw "one-segment dry-run target handling failed"
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
    $root = Get-SafeDirectory $PSScriptRoot "extracted handoff root"
    $game = Get-SafeDirectory $GameDir "-GameDir"
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
    if ($Install) {
        Invoke-CopyPlan $overlayPlan
        Install-GameplayPackage $gameplayPackage.File.FullName $packagePlan
        if ($EnableProfile) {
            Invoke-CopyPlan $profilePlan
        }
        $profileState = if ($EnableProfile) { "; profile enabled" } else { "" }
        Write-Output "installed: $($overlayPlan.Count) portable overlay files, $($packagePlan.Count) gameplay files$profileState; build $($buildRevision.Substring(0, 8))"
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
