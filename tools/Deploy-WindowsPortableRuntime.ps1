[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$TileZedBuildDirectory,

    [Parameter(Mandatory = $true)]
    [string]$WorldEdBuildDirectory,

    [Parameter(Mandatory = $true)]
    [string]$DestinationDirectory,

    [Parameter(Mandatory = $true)]
    [string]$QtBinDirectory,

    [string]$VcRuntimeDirectory,
    [string]$VcRuntimeMinimumMsi,
    [string]$OpenSslDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$vcRuntimeFiles = @(
    'msvcp140.dll',
    'vcruntime140.dll',
    'vcruntime140_1.dll'
)

$unusedVcRuntimeFiles = @(
    'concrt140.dll',
    'msvcp140_1.dll',
    'msvcp140_2.dll',
    'msvcp140_atomic_wait.dll',
    'msvcp140_codecvt_ids.dll',
    'vcamp140.dll',
    'vccorlib140.dll',
    'vcomp140.dll',
    'vcruntime140_threads.dll'
)

function Resolve-ExistingDirectory {
    param([string]$Path, [string]$Label)

    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Label directory does not exist: $Path"
    }

    return (Resolve-Path -LiteralPath $Path).Path
}

function Resolve-ExistingFile {
    param([string]$Path, [string]$Label)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label file does not exist: $Path"
    }

    return (Resolve-Path -LiteralPath $Path).Path
}

function Copy-FileWhenChanged {
    param([string]$Source, [string]$Destination)

    $sourcePath = Resolve-ExistingFile $Source 'Source'
    $destinationParent = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $destinationParent -PathType Container)) {
        New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null
    }

    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        $sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
        $destinationHash = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash
        if ($sourceHash -eq $destinationHash) {
            return $false
        }
    }

    Copy-Item -LiteralPath $sourcePath -Destination $Destination -Force
    return $true
}

function Copy-DirectoryFilesWhenChanged {
    param([string]$SourceDirectory, [string]$DestinationDirectory)

    $sourcePath = Resolve-ExistingDirectory $SourceDirectory 'Source'
    $count = 0
    foreach ($file in Get-ChildItem -LiteralPath $sourcePath -File -Recurse) {
        $relativePath = $file.FullName.Substring($sourcePath.Length).TrimStart('\')
        if (Copy-FileWhenChanged $file.FullName (Join-Path $DestinationDirectory $relativePath)) {
            ++$count
        }
    }
    return $count
}

function Get-PeLinkerVersion {
    param([string]$Path)

    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $reader = [System.IO.BinaryReader]::new($stream)
        $stream.Position = 0x3c
        $peOffset = $reader.ReadInt32()
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "Not a PE image: $Path"
        }
        $stream.Position = $peOffset + 26
        $major = $reader.ReadByte()
        $minor = $reader.ReadByte()
        return [version]::new($major, $minor)
    }
    finally {
        $stream.Dispose()
    }
}

function Get-VersionFromRuntimePath {
    param([string]$Path)

    $match = [regex]::Match($Path, 'v(?<version>14\.[0-9]+\.[0-9]+)', [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if ($match.Success) {
        return [version]$match.Groups['version'].Value
    }
    return [version]'0.0'
}

function Expand-VcRuntimeMinimumMsi {
    param([string]$MsiPath)

    $resolvedMsi = Resolve-ExistingFile $MsiPath 'VC runtime MSI'
    $root = Join-Path ([System.IO.Path]::GetTempPath()) ('PZTools-vcrt-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $root | Out-Null
    $process = Start-Process -FilePath 'msiexec.exe' -ArgumentList '/a', ('"' + $resolvedMsi + '"'), ('TARGETDIR="' + $root + '"'), '/qn' -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        throw "VC runtime MSI extraction failed with exit code $($process.ExitCode)"
    }

    $systemDirectory = Join-Path $root 'System64'
    if (-not (Test-Path -LiteralPath $systemDirectory -PathType Container)) {
        throw "VC runtime MSI did not contain a System64 payload: $resolvedMsi"
    }

    $script:temporaryRuntimeRoot = $root
    return $systemDirectory
}

function Resolve-VcRuntimePayload {
    if ($VcRuntimeDirectory) {
        return Resolve-ExistingDirectory $VcRuntimeDirectory 'VC runtime'
    }

    if ($VcRuntimeMinimumMsi) {
        return Expand-VcRuntimeMinimumMsi $VcRuntimeMinimumMsi
    }

    $candidates = @()
    $visualStudioRoot = Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio'
    if (Test-Path -LiteralPath $visualStudioRoot -PathType Container) {
        foreach ($file in Get-ChildItem -LiteralPath $visualStudioRoot -Filter 'vcruntime140_1.dll' -File -Recurse -ErrorAction SilentlyContinue) {
            if ($file.FullName -match '\\VC\\Redist\\MSVC\\[^\\]+\\x64\\Microsoft\.VC[0-9]+\.CRT\\vcruntime140_1\.dll$') {
                $candidates += [pscustomobject]@{
                    Kind = 'Directory'
                    Path = $file.DirectoryName
                    Version = [version]$file.VersionInfo.ProductVersion
                }
            }
        }
    }

    $packageCache = Join-Path $env:ProgramData 'Package Cache'
    if (Test-Path -LiteralPath $packageCache -PathType Container) {
        foreach ($file in Get-ChildItem -LiteralPath $packageCache -Filter 'vc_runtimeMinimum_x64.msi' -File -Recurse -ErrorAction SilentlyContinue) {
            $candidates += [pscustomobject]@{
                Kind = 'Msi'
                Path = $file.FullName
                Version = Get-VersionFromRuntimePath $file.FullName
            }
        }
    }

    $candidate = $candidates | Sort-Object Version -Descending | Select-Object -First 1
    if (-not $candidate) {
        throw 'No x64 Microsoft Visual C++ runtime payload was found. Supply -VcRuntimeDirectory or -VcRuntimeMinimumMsi.'
    }

    if ($candidate.Kind -eq 'Msi') {
        return Expand-VcRuntimeMinimumMsi $candidate.Path
    }
    return $candidate.Path
}

function Remove-SafeTemporaryDirectory {
    param([string]$Path)

    if (-not $Path) {
        return
    }

    $resolvedPath = [System.IO.Path]::GetFullPath($Path)
    $temporaryRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    $leaf = Split-Path -Leaf $resolvedPath
    if (-not $resolvedPath.StartsWith($temporaryRoot, [System.StringComparison]::OrdinalIgnoreCase) -or -not $leaf.StartsWith('PZTools-vcrt-', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove unexpected temporary directory: $resolvedPath"
    }

    if (Test-Path -LiteralPath $resolvedPath -PathType Container) {
        Remove-Item -LiteralPath $resolvedPath -Recurse -Force
    }
}

$temporaryRuntimeRoot = $null

try {
    $tileZedBuild = Resolve-ExistingDirectory $TileZedBuildDirectory 'TileZed build'
    $worldEdBuild = Resolve-ExistingDirectory $WorldEdBuildDirectory 'WorldEd build'
    $destination = [System.IO.Path]::GetFullPath($DestinationDirectory)
    $qtBin = Resolve-ExistingDirectory $QtBinDirectory 'Qt bin'
    $bin = Join-Path $destination 'bin'
    New-Item -ItemType Directory -Path $bin -Force | Out-Null
    $removedFiles = 0

    $buildFiles = @(
        [pscustomobject]@{ Source = Join-Path $tileZedBuild 'TileZed.exe'; Destination = Join-Path $bin 'TileZed.exe' },
        [pscustomobject]@{ Source = Join-Path $tileZedBuild 'TileZed.pdb'; Destination = Join-Path $bin 'TileZed.pdb' },
        [pscustomobject]@{ Source = Join-Path $tileZedBuild 'BuildingEd.exe'; Destination = Join-Path $bin 'BuildingEd.exe' },
        [pscustomobject]@{ Source = Join-Path $tileZedBuild 'BuildingEd.pdb'; Destination = Join-Path $bin 'BuildingEd.pdb' },
        [pscustomobject]@{ Source = Join-Path $tileZedBuild 'tiled.dll'; Destination = Join-Path $bin 'tiled.dll' },
        [pscustomobject]@{ Source = Join-Path $tileZedBuild 'zlib1.dll'; Destination = Join-Path $bin 'zlib1.dll' },
        [pscustomobject]@{ Source = Join-Path $tileZedBuild 'automappingconverter.exe'; Destination = Join-Path $bin 'automappingconverter.exe' },
        [pscustomobject]@{ Source = Join-Path $tileZedBuild 'tmxviewer.exe'; Destination = Join-Path $bin 'tmxviewer.exe' },
        [pscustomobject]@{ Source = Join-Path $worldEdBuild 'PZWorldEd.exe'; Destination = Join-Path $bin 'PZWorldEd.exe' },
        [pscustomobject]@{ Source = Join-Path $worldEdBuild 'PZWorldEd.pdb'; Destination = Join-Path $bin 'PZWorldEd.pdb' },
        [pscustomobject]@{ Source = Join-Path $worldEdBuild 'worlded.dll'; Destination = Join-Path $bin 'worlded.dll' }
    )

    $changedFiles = 0
    foreach ($file in $buildFiles) {
        if (Copy-FileWhenChanged $file.Source $file.Destination) {
            ++$changedFiles
        }
    }

    $pluginSource = Join-Path $tileZedBuild 'plugins\tiled'
    $pluginDestination = Join-Path $destination 'plugins\tiled'
    foreach ($plugin in Get-ChildItem -LiteralPath $pluginSource -Filter '*.dll' -File) {
        if (Copy-FileWhenChanged $plugin.FullName (Join-Path $pluginDestination $plugin.Name)) {
            ++$changedFiles
        }
    }

    $windeployqt = Resolve-ExistingFile (Join-Path $qtBin 'windeployqt.exe') 'windeployqt'
    & $windeployqt --release --no-compiler-runtime --no-translations --dir $bin (Join-Path $bin 'TileZed.exe') (Join-Path $bin 'BuildingEd.exe') (Join-Path $bin 'PZWorldEd.exe')
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed with exit code $LASTEXITCODE"
    }

    if ($OpenSslDirectory) {
        $openSsl = Resolve-ExistingDirectory $OpenSslDirectory 'OpenSSL'
        foreach ($name in @('libcrypto-1_1-x64.dll', 'libssl-1_1-x64.dll')) {
            if (Copy-FileWhenChanged (Join-Path $openSsl $name) (Join-Path $bin $name)) {
                ++$changedFiles
            }
        }
    }

    $linkerVersions = foreach ($file in $buildFiles) {
        if ([System.IO.Path]::GetExtension($file.Source) -in @('.exe', '.dll')) {
            Get-PeLinkerVersion $file.Source
        }
    }
    foreach ($plugin in Get-ChildItem -LiteralPath $pluginSource -Filter '*.dll' -File) {
        $linkerVersions += Get-PeLinkerVersion $plugin.FullName
    }
    $minimumRuntimeVersion = $linkerVersions | Sort-Object -Descending | Select-Object -First 1

    $runtimeSource = Resolve-VcRuntimePayload
    $runtimeVersions = @()
    foreach ($name in $vcRuntimeFiles) {
        $source = Resolve-ExistingFile (Join-Path $runtimeSource $name) 'VC runtime'
        $signature = Get-AuthenticodeSignature -LiteralPath $source
        if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid -or $signature.SignerCertificate.Subject -notmatch 'Microsoft Corporation') {
            throw "VC runtime file does not have a valid Microsoft signature: $source"
        }
        $runtimeVersions += [version](Get-Item -LiteralPath $source).VersionInfo.ProductVersion
    }

    $runtimeVersion = $runtimeVersions | Sort-Object -Descending | Select-Object -First 1
    $oldestRuntimeVersion = $runtimeVersions | Sort-Object | Select-Object -First 1
    if ($runtimeVersion -ne $oldestRuntimeVersion) {
        throw "VC runtime payload mixes versions $oldestRuntimeVersion and $runtimeVersion"
    }
    if ($runtimeVersion -lt $minimumRuntimeVersion) {
        throw "VC runtime $runtimeVersion is older than linker $minimumRuntimeVersion"
    }

    foreach ($name in $vcRuntimeFiles) {
        if (Copy-FileWhenChanged (Join-Path $runtimeSource $name) (Join-Path $bin $name)) {
            ++$changedFiles
        }
    }

    foreach ($name in $unusedVcRuntimeFiles + @('ucrtbase.dll')) {
        $path = Join-Path $bin $name
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Remove-Item -LiteralPath $path -Force
            ++$removedFiles
        }
    }
    foreach ($file in Get-ChildItem -LiteralPath $bin -Filter 'api-ms-win-*.dll' -File) {
        Remove-Item -LiteralPath $file.FullName -Force
        ++$removedFiles
    }

    $sourceRoot = Split-Path -Parent $PSScriptRoot
    $documentFiles = @(
        [pscustomobject]@{ Source = 'AUTHORS.txt'; Destination = 'AUTHORS.txt' },
        [pscustomobject]@{ Source = 'BUILDING.md'; Destination = 'BUILDING.md' },
        [pscustomobject]@{ Source = 'COPYING'; Destination = 'COPYING.txt' },
        [pscustomobject]@{ Source = 'DOCUMENTATION.md'; Destination = 'DOCUMENTATION.md' },
        [pscustomobject]@{ Source = 'FEATURE_PROVENANCE.md'; Destination = 'FEATURE_PROVENANCE.md' },
        [pscustomobject]@{ Source = 'README.md'; Destination = 'README.md' },
        [pscustomobject]@{ Source = 'RELEASE_CHANGELOG.md'; Destination = 'RELEASE_CHANGELOG.md' },
        [pscustomobject]@{ Source = 'SOURCE-OFFER.txt'; Destination = 'SOURCE-OFFER.txt' },
        [pscustomobject]@{ Source = 'THIRD_PARTY_NOTICES.txt'; Destination = 'THIRD_PARTY_NOTICES.txt' },
        [pscustomobject]@{ Source = 'UPSTREAM-HISTORY.md'; Destination = 'UPSTREAM-HISTORY.md' }
        [pscustomobject]@{ Source = 'tools\Setup-macOS.command'; Destination = 'tools\Setup-macOS.command' }
    )
    foreach ($document in $documentFiles) {
        if (Copy-FileWhenChanged (Join-Path $sourceRoot $document.Source) (Join-Path $destination $document.Destination)) {
            ++$changedFiles
        }
    }
    $changedFiles += Copy-DirectoryFilesWhenChanged (Join-Path $sourceRoot 'docs') (Join-Path $destination 'docs')
    $changedFiles += Copy-DirectoryFilesWhenChanged (Join-Path $sourceRoot 'TileZed\src\tiled\BuildingEditor\manual') (Join-Path $destination 'docs\BuildingEd')
    $changedFiles += Copy-DirectoryFilesWhenChanged (Join-Path $sourceRoot 'licenses') (Join-Path $destination 'licenses')

    $requiredPaths = @(
        'bin\Qt5Core.dll',
        'bin\Qt5Gui.dll',
        'bin\Qt5Network.dll',
        'bin\Qt5Svg.dll',
        'bin\Qt5Widgets.dll',
        'bin\platforms\qwindows.dll',
        'bin\imageformats\qsvg.dll',
        'bin\iconengines\qsvgicon.dll',
        'bin\styles\qwindowsvistastyle.dll',
        'bin\libcrypto-1_1-x64.dll',
        'bin\libssl-1_1-x64.dll',
        'brushes',
        'config',
        'docs',
        'docs\BuildingEd\index.html',
        'licenses',
        'lua',
        'plugins\tiled',
        'settings',
        'themes',
        'translations',
        'AUTHORS.txt',
        'COPYING.txt',
        'SOURCE-OFFER.txt',
        'THIRD_PARTY_NOTICES.txt',
        'UPSTREAM-HISTORY.md'
    )
    foreach ($path in $requiredPaths) {
        $resolved = Join-Path $destination $path
        if (-not (Test-Path -LiteralPath $resolved)) {
            throw "Portable distribution is missing required content: $resolved"
        }
    }

    foreach ($name in $vcRuntimeFiles) {
        $deployed = Get-Item -LiteralPath (Join-Path $bin $name)
        if ([version]($deployed.VersionInfo.ProductVersion) -ne $runtimeVersion) {
            throw "Deployed VC runtime version mismatch: $($deployed.FullName)"
        }
    }

    Write-Host "Portable runtime deployed successfully"
    Write-Host "Destination: $destination"
    Write-Host "MSVC runtime: $runtimeVersion"
    Write-Host "Minimum linker version: $minimumRuntimeVersion"
    Write-Host "Changed files: $changedFiles"
    Write-Host "Removed unused runtime files: $removedFiles"
}
finally {
    Remove-SafeTemporaryDirectory $temporaryRuntimeRoot
}
