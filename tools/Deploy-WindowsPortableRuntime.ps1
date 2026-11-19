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
    param([string]$Source, [string]$Destination, [hashtable]$LinkReplacements)

    $sourcePath = Resolve-ExistingFile $Source 'Source'
    $destinationParent = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $destinationParent -PathType Container)) {
        New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null
    }

    if ($LinkReplacements) {
        $content = [System.IO.File]::ReadAllText($sourcePath)
        foreach ($replacement in $LinkReplacements.GetEnumerator()) {
            $content = $content.Replace($replacement.Key, $replacement.Value)
        }
        if ((Test-Path -LiteralPath $Destination -PathType Leaf) -and
            [string]::Equals([System.IO.File]::ReadAllText($Destination), $content, [System.StringComparison]::Ordinal)) {
            return $false
        }
        [System.IO.File]::WriteAllText($Destination, $content, [System.Text.UTF8Encoding]::new($false))
        return $true
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
    param([string]$SourceDirectory, [string]$DestinationDirectory, [hashtable]$LinkReplacementsByPath = @{})

    $sourcePath = Resolve-ExistingDirectory $SourceDirectory 'Source'
    $count = 0
    foreach ($file in Get-ChildItem -LiteralPath $sourcePath -File -Recurse) {
        $relativePath = $file.FullName.Substring($sourcePath.Length).TrimStart('\')
        if (Copy-FileWhenChanged $file.FullName (Join-Path $DestinationDirectory $relativePath) $LinkReplacementsByPath[$relativePath]) {
            ++$count
        }
    }
    return $count
}

function Install-PortableDefaults {
    param([string]$SourceRoot, [string]$Destination)

    $defaults = [ordered]@{
        'TileZed' = @('Curbs.txt', 'Edges.txt', 'Fences.txt', 'LuaTools.txt', 'Rearrange.txt',
            'RearrangeGrid.txt', 'RoomNames.txt', 'RoomTone.txt', 'TileProperties.txt', 'Tilesets.txt')
        'TileZed\src\tiled\BuildingEditor' = @('BuildingFurniture.txt', 'BuildingTemplates.txt',
            'BuildingTiles.txt', 'TMXConfig.txt')
        'WorldEd' = @('Blends.txt', 'MapBaseXML.txt', 'MapToPNG.txt', 'Roads.txt', 'Rules.txt', 'WorldDefaults.txt')
    }
    $files = [ordered]@{}
    foreach ($dir in $defaults.Keys) {
        foreach ($name in $defaults[$dir]) {
            $files["config\$name"] = Join-Path (Join-Path $SourceRoot $dir) $name
        }
    }
    $files['brushes\README.txt'] = Join-Path $SourceRoot 'TileZed\brushes\README.txt'
    $luaFiles = @(
        '_add_RoomDefs.lua', 'add_default_layers.lua', 'blend.lua', 'convert-selection-to-lot.lua',
        'dashed-line.lua', 'dashed-white-line.lua', 'example-building-fill-room.lua',
        'example-building-inspect.lua', 'example-fill-selection.lua', 'example-inspect-map.lua',
        'example-roomdefs.lua', 'fence-tool.png', 'fix-sand.lua', 'fix_tree_and_plant.lua',
        'fix_two_plants.lua', 'make-street.lua', 'parking-stall.lua', 'perlin_trees.lua',
        'rearrange_location_restaurant_bar.lua', 'rearrange_location_restaurant_pie.lua',
        'reload-rules.lua', 'remove-blend-tiles.lua', 'remove-overlapping-blend-tiles.lua',
        'remove-unknown-colors.lua', 'replace_floors_exterior_street.lua',
        'replace_floors_exterior_street_to_veg.lua', 'replace_vegetation_groundcover.lua',
        'replace_vegetation_ornamental.lua', 'tiles-to-rules.lua', 'tool-curb-data.lua',
        'tool-curb.lua', 'tool-curb.png', 'tool-edge-data.lua', 'tool-edge.lua', 'tool-edge.png',
        'tool-fence-data.lua', 'tool-fence.lua', 'tool-fence.png', 'tool-four-directions.lua',
        'tool-mailbox-rural.lua', 'tool-parking-stall.lua', 'tool-parking-stall.png',
        'tool-random-houseplant.lua', 'tool-random-houseplant.png', 'tool-stop-sign.lua',
        'tool-street-decoration-data.lua', 'tool-street-decoration.lua', 'tool-street-decoration.png',
        'west-parking-stalls.lua', 'wooden-fence-1.lua', 'wooden-fence-2.lua'
    )
    foreach ($name in $luaFiles) {
        $files["lua\$name"] = Join-Path $SourceRoot "TileZed\lua\$name"
    }
    $added = 0
    foreach ($path in $files.Keys) {
        $target = Join-Path $Destination $path
        if (-not (Test-Path -LiteralPath $target) -and (Copy-FileWhenChanged $files[$path] $target)) {
            ++$added
        }
    }
    foreach ($name in @('settings', 'themes')) {
        New-Item -ItemType Directory -Path (Join-Path $Destination $name) -Force | Out-Null
    }
    return $added
}

function Install-EditorTranslations {
    param([string]$SourceRoot, [string]$TileZedBuild, [string]$Destination)

    $count = 0
    foreach ($ts in Get-ChildItem -LiteralPath (Join-Path $SourceRoot 'TileZed\translations') -Filter 'tiled_*.ts' -File) {
        $relative = "translations\$($ts.BaseName).qm"
        $compiled = Join-Path $TileZedBuild $relative
        $target = Join-Path $Destination $relative
        if (Test-Path -LiteralPath $compiled -PathType Leaf) {
            if (Copy-FileWhenChanged $compiled $target) {
                ++$count
            }
        } elseif (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
            throw "Missing translation $compiled. Build TileZed's top-level tiled.pro with Qt Linguist lrelease installed."
        }
    }
    return $count
}

function Test-EmbeddedPreviewAgent {
    param([string]$WorldEdExecutable, [string]$AgentJar)

    $jar = [System.IO.File]::ReadAllBytes($AgentJar)
    $length = [BitConverter]::GetBytes([uint32]$jar.Length)
    if ([BitConverter]::IsLittleEndian) {
        [array]::Reverse($length)
    }
    $encoding = [System.Text.Encoding]::GetEncoding(28591)
    $data = $encoding.GetString($length) + $encoding.GetString($jar)
    $name = $encoding.GetString([System.Text.Encoding]::BigEndianUnicode.GetBytes('PZDirectMapPreview.jar'))
    $image = $encoding.GetString([System.IO.File]::ReadAllBytes($WorldEdExecutable))
    return $image.Contains($name) -and $image.Contains($data)
}

function Remove-EmbeddedAgentDuplicate {
    param([string]$SourceRoot, [string]$Destination)

    $relative = 'tools\DirectMapPreview\PZDirectMapPreview.jar'
    $copy = Join-Path $Destination $relative
    if (-not (Test-Path -LiteralPath $copy -PathType Leaf)) {
        return 0
    }
    $item = Get-Item -LiteralPath $copy -Force
    $directory = $item.Directory
    while ($null -ne $directory) {
        if ($directory.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
            return 0
        }
        if ($directory.FullName.TrimEnd('\') -eq $Destination.TrimEnd('\')) {
            break
        }
        $directory = $directory.Parent
    }
    if ($null -eq $directory -or ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
        return 0
    }
    $sourceJar = Join-Path $SourceRoot $relative
    if (-not (Test-Path -LiteralPath $sourceJar -PathType Leaf)) {
        return 0
    }
    $embeddedHash = (Get-FileHash -LiteralPath $sourceJar -Algorithm SHA256).Hash
    $exe = Join-Path $Destination 'bin\PZWorldEd.exe'
    if ((Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -eq $embeddedHash -and
        (Test-Path -LiteralPath $exe -PathType Leaf) -and (Test-EmbeddedPreviewAgent $exe $sourceJar)) {
        Remove-Item -LiteralPath $copy
        return 1
    }
    return 0
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
    $sourceRoot = Split-Path -Parent $PSScriptRoot
    if ((Test-Path -LiteralPath (Join-Path $destination 'WorldEd\PZWorldEd.pro')) -or
        $destination.TrimEnd('\') -in @($tileZedBuild, $worldEdBuild, $qtBin)) {
        throw "Choose a package directory separate from the sources, compiler output and Qt installation."
    }
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

    $editorPlugins = @('flare', 'droidcraft', 'json', 'lot', 'lua', 'tengine', 'tmw', 'replicaisland')
    foreach ($plugin in $editorPlugins) {
        $relative = "plugins\tiled\$plugin.dll"
        if (Copy-FileWhenChanged (Join-Path $tileZedBuild $relative) (Join-Path $destination $relative)) {
            ++$changedFiles
        }
    }

    $previousDllNames = @(Get-ChildItem -LiteralPath $bin -Filter '*.dll' -File | ForEach-Object { $_.Name })
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
    foreach ($plugin in $editorPlugins) {
        $linkerVersions += Get-PeLinkerVersion (Join-Path $tileZedBuild "plugins\tiled\$plugin.dll")
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

    foreach ($file in Get-ChildItem -LiteralPath $bin -Filter '*.dll' -File) {
        if ($file.Name -notin $previousDllNames -and
            ($file.Name -in $unusedVcRuntimeFiles -or $file.Name -eq 'ucrtbase.dll' -or $file.Name -like 'api-ms-win-*.dll')) {
            $signature = Get-AuthenticodeSignature -LiteralPath $file.FullName
            if ($signature.Status -eq [System.Management.Automation.SignatureStatus]::Valid -and
                $signature.SignerCertificate.Subject -match 'Microsoft Corporation') {
                Remove-Item -LiteralPath $file.FullName -Force
                ++$removedFiles
            }
        }
    }

    $changedFiles += Install-PortableDefaults $sourceRoot $destination
    $changedFiles += Install-EditorTranslations $sourceRoot $tileZedBuild $destination
    $documentFiles = @(
        [pscustomobject]@{ Source = 'AUTHORS.txt'; Destination = 'AUTHORS.txt' },
        [pscustomobject]@{ Source = 'BUILDING.md'; Destination = 'BUILDING.md' },
        [pscustomobject]@{ Source = 'COPYING'; Destination = 'COPYING.txt' },
        [pscustomobject]@{ Source = 'FEATURE_PROVENANCE.md'; Destination = 'FEATURE_PROVENANCE.md' },
        [pscustomobject]@{ Source = 'README.md'; Destination = 'README.md' },
        [pscustomobject]@{ Source = 'RELEASE_CHANGELOG.md'; Destination = 'RELEASE_CHANGELOG.md' },
        [pscustomobject]@{ Source = 'SOURCE-OFFER.txt'; Destination = 'SOURCE-OFFER.txt' },
        [pscustomobject]@{ Source = 'THIRD_PARTY_NOTICES.txt'; Destination = 'THIRD_PARTY_NOTICES.txt' },
        [pscustomobject]@{ Source = 'UPSTREAM-HISTORY.md'; Destination = 'UPSTREAM-HISTORY.md' }
    )
    foreach ($document in $documentFiles) {
        if (Copy-FileWhenChanged (Join-Path $sourceRoot $document.Source) (Join-Path $destination $document.Destination)) {
            ++$changedFiles
        }
    }
    $sourceManual = 'TileZed/src/tiled/BuildingEditor/manual/'
    $rootManualLinks = @{ ('href="../' + $sourceManual) = 'href="BuildingEd/' }
    $nestedManualLinks = @{ ('href="../../' + $sourceManual) = 'href="../BuildingEd/' }
    $documentationLinks = @{
        'index.html' = $rootManualLinks
        'TileZed\LuaScripting.html' = $nestedManualLinks
        'TileZed\PZToolsGuide.html' = $nestedManualLinks
    }
    $changedFiles += Copy-DirectoryFilesWhenChanged (Join-Path $sourceRoot 'docs') (Join-Path $destination 'docs') $documentationLinks
    $changedFiles += Copy-DirectoryFilesWhenChanged (Join-Path $sourceRoot 'TileZed\src\tiled\BuildingEditor\manual') (Join-Path $destination 'docs\BuildingEd')
    $indexLinks = @{ ('](' + $sourceManual) = '](docs/BuildingEd/' }
    if (Copy-FileWhenChanged (Join-Path $sourceRoot 'DOCUMENTATION.md') (Join-Path $destination 'DOCUMENTATION.md') $indexLinks) {
        ++$changedFiles
    }
    $changedFiles += Copy-DirectoryFilesWhenChanged (Join-Path $sourceRoot 'licenses') (Join-Path $destination 'licenses')
    $mapperFiles = @(
        'tools\DirectMapPreview\example\DirectPreview.pzw',
        'tools\DirectMapPreview\example\PreviewCell.tmx',
        'tools\DirectMapPreview\example\ShoreHouse.tbx',
        'tools\TileDefs\pz42-window-supports.patch.tiles',
        'tools\TileDefs\pz42-window-supports.patch.tiles.txt'
    )
    foreach ($relative in $mapperFiles) {
        if (Copy-FileWhenChanged (Join-Path $sourceRoot $relative) (Join-Path $destination $relative)) {
            ++$changedFiles
        }
    }

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
    ) + $mapperFiles
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

    $removedFiles += Remove-EmbeddedAgentDuplicate $sourceRoot $destination
    Write-Host "Portable runtime deployed successfully"
    Write-Host "Destination: $destination"
    Write-Host "MSVC runtime: $runtimeVersion"
    Write-Host "Minimum linker version: $minimumRuntimeVersion"
    Write-Host "Changed files: $changedFiles"
    Write-Host "Removed redundant files: $removedFiles"
}
finally {
    Remove-SafeTemporaryDirectory $temporaryRuntimeRoot
}
