param(
    [Parameter(Mandatory)][string]$JdkDirectory,
    [Parameter(Mandatory)][string]$GameDirectory,
    [string]$OutputDirectory = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'
$javac = Join-Path $JdkDirectory 'bin/javac.exe'
$jar = Join-Path $JdkDirectory 'bin/jar.exe'
$gameJar = Join-Path $GameDirectory 'projectzomboid.jar'
$classes = Join-Path $OutputDirectory ('classes-' + [guid]::NewGuid())
$target = Join-Path $OutputDirectory 'PZDirectMapPreview.jar'
$sources = Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'java') -File -Recurse -Filter '*.java'
New-Item -ItemType Directory -Path $classes -Force | Out-Null
try {
    $arguments = @('-encoding', 'UTF-8', '-cp', $gameJar, '-d', $classes) + @($sources.FullName)
    & $javac @arguments
    if ($LASTEXITCODE -ne 0) { throw "javac failed ($LASTEXITCODE)." }

    $manifest = Join-Path $classes 'preview.mf'
    [IO.File]::WriteAllText($manifest, "Premain-Class: pztools.preview.DirectMapAgent`r`n`r`n")
    $built = Join-Path $classes 'preview.jar'
    & $jar --create --file $built --manifest $manifest -C $classes pztools
    if ($LASTEXITCODE -ne 0) { throw "jar failed ($LASTEXITCODE)." }
    Copy-Item -LiteralPath $built -Destination $target -Force
    Write-Output (Get-Item -LiteralPath $target).FullName
} finally {
    $resolved = (Resolve-Path -LiteralPath $classes).Path
    $base = (Resolve-Path -LiteralPath $OutputDirectory).Path
    if ([IO.Path]::GetDirectoryName($resolved) -eq $base -and
        [IO.Path]::GetFileName($resolved) -like 'classes-*') {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
