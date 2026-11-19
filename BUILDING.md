# Build PZ Mapping Tools on Windows

The maintained target is Windows x64 with Qt 5.14.2 and MSVC. A complete
checkout builds WorldEd, TileZed and BuildingEd. The portable deployment
script assembles their runtime and initial editor catalogues from that
checkout and the compiler outputs.

## Get the source and dependencies

1. Clone the repository or extract its source archive. For a published
   build, use the tag or commit identified with that release. Keep
   `WorldEd`, `TileZed`, `shared`, `tools`, `docs`, `licenses` and the
   root files together. BuildingEd is a target inside `TileZed`.
2. Install Visual Studio 2022 with **Desktop development with C++**, the x64
   MSVC toolset and a Windows SDK.
3. Install **Qt 5.14.2 for MSVC 2017 64-bit**, including Qt SVG and the
   Qt Linguist tools. Locate `qmake.exe`, `lrelease.exe` and
   `windeployqt.exe` in the same Qt installation.

The projects build their included Lua, zlib, QuaZip and libtiled components.
The C++ build uses the Java agent already present in the checkout. The
optional instructions below cover rebuilding that agent.

## Compile the editors

Open **x64 Native Tools Command Prompt for VS 2022** in the checkout
directory containing `BUILDING.md`. These commands use a sibling directory
for compiler outputs. Adjust the Qt path to your installation.

```bat
set "PZ_SOURCE=%CD%"
set "PZ_BUILD=%CD%\..\PZTools-build"
set "PZ_QT_BIN=C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin"
where cl
where nmake
"%PZ_QT_BIN%\qmake.exe" -v
"%PZ_QT_BIN%\lrelease.exe" -version
```

Use separate empty build directories for the initial build. Configure each
top-level qmake project and compile it in the same command prompt.

```bat
mkdir "%PZ_BUILD%\worlded"
pushd "%PZ_BUILD%\worlded"
"%PZ_QT_BIN%\qmake.exe" "%PZ_SOURCE%\WorldEd\PZWorldEd.pro" -spec win32-msvc CONFIG+=release && nmake
popd

mkdir "%PZ_BUILD%\tilezed"
pushd "%PZ_BUILD%\tilezed"
"%PZ_QT_BIN%\qmake.exe" "%PZ_SOURCE%\TileZed\tiled.pro" -spec win32-msvc CONFIG+=release && nmake
popd
```

WorldEd produces `worlded\PZWorldEd.exe`. The other build produces
`tilezed\TileZed.exe`, `tilezed\BuildingEd.exe`, the TileZed plugins and
`tilezed\translations\tiled_*.qm`. Keep the matching executables, libraries
and PDB files together. For incremental changes, run `nmake` again in the
relevant build directory. Rerun qmake after changing the project structure or
toolchain. Use fresh build directories for a release.

## Assemble and run the portable tools

For a fresh package, provide the OpenSSL 1.1 DLLs compatible with this Qt
build, `libssl-1_1-x64.dll` and `libcrypto-1_1-x64.dll`, from a source you
can redistribute. Keep their license notice. Run the following in
**PowerShell**, from the source root, after both builds succeed. Adjust the
Qt and OpenSSL paths. The example uses the sibling build directory above.

```powershell
$sourceRoot = (Get-Location).Path
$workspace = Split-Path -Parent $sourceRoot
$buildRoot = Join-Path $workspace 'PZTools-build'
$packageRoot = Join-Path $workspace 'PZTools-portable'
& .\tools\Deploy-WindowsPortableRuntime.ps1 `
    -TileZedBuildDirectory (Join-Path $buildRoot 'tilezed') `
    -WorldEdBuildDirectory (Join-Path $buildRoot 'worlded') `
    -DestinationDirectory $packageRoot `
    -QtBinDirectory 'C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin' `
    -OpenSslDirectory 'C:\Libraries\OpenSSL-1.1-x64\bin'
```

The script runs `windeployqt` and installs the application DLLs, plugins,
compiled translations, manuals, credits and licenses. It selects a matching
Microsoft-signed x64 Visual C++ runtime from the local package cache or
Visual Studio redist directory. To supply the runtime explicitly, use
`-VcRuntimeDirectory` with a Microsoft.VC*.CRT directory or
`-VcRuntimeMinimumMsi` with an official x64 minimum-runtime MSI. Select one
runtime source. On an existing installation, you can omit
`-OpenSslDirectory` if both matching OpenSSL DLLs are already in `bin`.

A fresh package receives the source defaults in `config`, `brushes` and
`lua`. The script creates `settings` for the editors to populate on first
run and `themes` for optional external stylesheets. The editors also provide
built-in themes. On an existing package it adds missing defaults and
preserves existing catalogues and Lua tools. To ship revised defaults in a
fork, assemble a fresh package from your revised source. Start a public
release in an empty destination so local settings, logs and user additions
stay on the working machine.

The mapper-facing `tools` directory contains these optional resources.

| Location | Contents |
|---|---|
| `tools/DirectMapPreview/example` | `DirectPreview.pzw`, `PreviewCell.tmx` and `ShoreHouse.tbx` form one Play Map Source example. Keep them together. |
| `tools/TileDefs` | `pz42-window-supports.patch.tiles` and its editable `.tiles.txt` companion supply the optional window support patch. |

WorldEd embeds the preview agent in its executable and extracts it into the
temporary playtest profile at launch. The standalone source JAR is a build
input. The BuildingEd manual comes from
`TileZed/src/tiled/BuildingEditor/manual` and is installed in
`docs/BuildingEd`.

Launch `bin\PZWorldEd.exe`, `bin\TileZed.exe` or `bin\BuildingEd.exe` in
the package. Select your mapping Tiles directory on first use. Players and
mappers supply game assets from their own installation. See the
[portable installation guide](docs/TileZed/PZToolsGuide.html#installation).

## Rebuild the Play Map Source agent

After editing the Java agent, use a Windows JDK 25 and the supported
Project Zomboid 42.22.2 installation. Run the command from the source root,
then rebuild WorldEd so the updated JAR becomes part of its executable.

```powershell
.\tools\DirectMapPreview\Build-PreviewAgent.ps1 `
    -JdkDirectory 'C:\Java\jdk-25' `
    -GameDirectory 'C:\Games\Project Zomboid'
```

## Redistribution

Keep the credits, license texts, third-party notices and corresponding
source information with a release. See the [editor license](licenses/GPL-2.0.txt),
[third-party notices](THIRD_PARTY_NOTICES.txt) and
[corresponding source](SOURCE-OFFER.txt). The script installs the licensing
overview as `COPYING.txt` in the portable directory.

The Windows package uses an app-local Visual C++ runtime. Wine and CrossOver
remain experimental. Native Linux and macOS packaging paths are outside the
maintained target.
