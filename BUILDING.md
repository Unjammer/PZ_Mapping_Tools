# How to build PZ Mapping Tools

This guide explains how to arrange the source, create out-of-source build
directories, compile PZWorldEd, TileZed, and BuildingEd, and prepare a
redistributable application directory on Windows, Linux, and macOS.

Windows x64 with Qt 5.14.2 and MSVC is the currently tested release target.
The Linux and macOS sections describe the intended qmake build flow. Builds
for those systems must be compiled and tested on the target operating system
before they are published as supported releases.

## Required source layout

Keep the complete `WorldEd` and `TileZed` trees from the same source revision.
BuildingEd is part of the TileZed tree and is not a separate repository.

The source, compiler output, and packaged application must be separate:

```text
PZ_Mapping_Tools/
├── source/
│   ├── WorldEd/
│   │   ├── PZWorldEd.pro
│   │   ├── PZWorldEd.pri
│   │   ├── initvars.pro
│   │   └── src/
│   ├── TileZed/
│   │   ├── tiled.pro
│   │   ├── tiled.pri
│   │   ├── initvars.pro
│   │   └── src/
│   ├── docs/
│   ├── licenses/
│   ├── README.md
│   └── BUILDING.md
├── build/
│   ├── worlded/
│   └── tilezed/
└── package/
```

In the repository itself, `WorldEd` and `TileZed` may be directly below the
repository root. In that case, use the repository root in place of `source`
in the commands below.

Do not copy only selected `src` folders. The top-level `.pro`, `.pri`,
`.qmake.cache.in`, resources, translations, Lua scripts, documentation, and
configuration files are part of the build or package input.

The projects build their bundled Lua, zlib, QuaZip, libtiled, and editor
components in dependency order. Qt and the native compiler toolchain must be
installed separately.

## Common build rules

- Use one empty build directory for WorldEd.
- Use a second empty build directory for TileZed and BuildingEd.
- Run qmake from inside each build directory.
- Pass the absolute path to the matching top-level project file.
- Use the qmake executable from the Qt installation that will provide the
  runtime libraries.
- Rerun qmake after changing a `.pro`, `.pri`, form, resource, translation, or
  source-list file.
- Use an incremental native build command while developing.
- Use fresh build directories for a release build.
- Never build inside the source directory.

## Windows x64

### Prerequisites

Install:

- Qt 5.14.2 for MSVC 2017 64-bit
- Visual Studio 2022 with Desktop development with C++
- a Windows SDK

The Qt 5.14.2 `msvc2017_64` package can be built with the Visual Studio 2022
x64 compiler environment. The maintainer paths used below are examples:

```text
C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin\qmake.exe
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat
```

### Build WorldEd

Open `cmd.exe` and run:

```bat
call C:\PROGRA~1\MICROS~1\2022\COMMUN~1\Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64
mkdir C:\PZ_Mapping_Tools\build\worlded
cd /d C:\PZ_Mapping_Tools\build\worlded
C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin\qmake.exe C:\PZ_Mapping_Tools\source\WorldEd\PZWorldEd.pro -spec win32-msvc CONFIG+=release
nmake
```

The main output is:

```text
C:\PZ_Mapping_Tools\build\worlded\PZWorldEd.exe
```

### Build TileZed and BuildingEd

Open `cmd.exe` and run:

```bat
call C:\PROGRA~1\MICROS~1\2022\COMMUN~1\Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64
mkdir C:\PZ_Mapping_Tools\build\tilezed
cd /d C:\PZ_Mapping_Tools\build\tilezed
C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin\qmake.exe C:\PZ_Mapping_Tools\source\TileZed\tiled.pro -spec win32-msvc CONFIG+=release
nmake
```

The main outputs are:

```text
C:\PZ_Mapping_Tools\build\tilezed\TileZed.exe
C:\PZ_Mapping_Tools\build\tilezed\BuildingEd.exe
```

Keep the matching PDB files when the linker creates them.

## Linux x86_64

### Prerequisites

Use a native Linux build host with:

- a C++17-capable GCC or Clang toolchain
- GNU Make
- Qt 5.15.x development tools with qmake
- Qt Core, Gui, Widgets, XML, SVG, OpenGL, and development headers
- the normal XCB runtime plugins required by Qt applications

The source contains Unix branches and origin-relative library search paths.
Use the qmake binary from the Qt installation selected for the package.

### Build all three applications

From a terminal, with `/opt/Qt/5.15.2/gcc_64` replaced by the actual Qt
location:

```sh
mkdir -p "$HOME/PZ_Mapping_Tools/build/worlded"
mkdir -p "$HOME/PZ_Mapping_Tools/build/tilezed"

cd "$HOME/PZ_Mapping_Tools/build/worlded"
/opt/Qt/5.15.2/gcc_64/bin/qmake \
  "$HOME/PZ_Mapping_Tools/source/WorldEd/PZWorldEd.pro" \
  CONFIG+=release
make -j"$(nproc)"

cd "$HOME/PZ_Mapping_Tools/build/tilezed"
/opt/Qt/5.15.2/gcc_64/bin/qmake \
  "$HOME/PZ_Mapping_Tools/source/TileZed/tiled.pro" \
  CONFIG+=release
make -j"$(nproc)"
```

Expected executable locations are below the selected build roots, normally
under `bin`. Confirm the actual paths after qmake completes:

```sh
find "$HOME/PZ_Mapping_Tools/build" -type f \
  \( -name PZWorldEd -o -name TileZed -o -name BuildingEd \)
```

Before packaging, confirm that the executables locate the bundled libraries
through an origin-relative RPATH and that Qt platform plugins are included.
Run the applications on a clean user account without a developer Qt
installation. Test both XCB and Wayland where the selected Qt version supports
them.

## macOS

### Prerequisites

Use a real macOS build host with:

- Xcode and the Xcode command-line tools
- a Qt installation that includes qmake, macdeployqt, Widgets, XML, SVG, and
  OpenGL support
- an Apple Silicon Qt kit for an arm64 build
- an Intel Qt kit for an x86_64 build

The current maintained Windows release uses Qt 5.14.2. A public Apple Silicon
release should complete and validate the remaining Qt and OpenGL compatibility
work with the Qt version selected for macOS. Do not claim an arm64 or universal
release solely because qmake generated the project.

### Build all three applications

From Terminal, with `QMAKE` set to the selected Qt qmake:

```sh
export QMAKE="$HOME/Qt/5.15.2/clang_64/bin/qmake"

mkdir -p "$HOME/PZ_Mapping_Tools/build/worlded"
mkdir -p "$HOME/PZ_Mapping_Tools/build/tilezed"

cd "$HOME/PZ_Mapping_Tools/build/worlded"
"$QMAKE" "$HOME/PZ_Mapping_Tools/source/WorldEd/PZWorldEd.pro" \
  CONFIG+=release
make -j"$(sysctl -n hw.ncpu)"

cd "$HOME/PZ_Mapping_Tools/build/tilezed"
"$QMAKE" "$HOME/PZ_Mapping_Tools/source/TileZed/tiled.pro" \
  CONFIG+=release
make -j"$(sysctl -n hw.ncpu)"
```

For an explicit single-architecture build, add the architecture accepted by
the selected Qt kit:

```sh
"$QMAKE" /absolute/path/to/PZWorldEd.pro CONFIG+=release \
  QMAKE_APPLE_DEVICE_ARCHS=arm64
```

The expected application bundles are `PZWorldEd.app`, `TileZed.app`, and
`BuildingEd.app`. Inspect the generated build tree if a selected Qt version
places a helper executable or bundle in a different subdirectory.

Use `macdeployqt` separately on every application bundle. Then verify bundled
framework and plugin paths with `otool -L`. A public macOS package also needs
a stable bundle identifier, icons, version metadata, signing of nested
frameworks and plugins, notarization, and testing on a clean macOS account.

## Package the applications

The compiler output is not the final application package. Create a separate
package root:

```text
PZTools/
├── bin/             executables and native runtime libraries
├── brushes/
├── config/
├── docs/
├── licenses/
├── lua/
├── plugins/
├── themes/
├── translations/
├── AUTHORS.txt
├── COPYING.txt
├── SOURCE-OFFER.txt
├── THIRD_PARTY_NOTICES.txt
└── UPSTREAM-HISTORY.md
```

The exact internal arrangement may use `.app` bundles on macOS and a standard
`usr/bin`, `usr/lib`, and `usr/share` staging tree on Linux. Keep the project
data directories available through the paths compiled by qmake or through a
documented package-relative lookup.

On Windows, copy the three newly linked executables to `bin` and run
`windeployqt` against them. Copy rebuilt shared DLLs and plugins only when
their content changed.

On Linux, deploy the Qt libraries and platform plugins with a maintained
deployment tool or distribution package recipe. Preserve origin-relative
library lookup and install writable settings and logs under the user's
standard application-data locations.

On macOS, place configuration, documentation, Lua, themes, translations, and
plugins in the application bundle locations expected by the qmake projects.
Use `macdeployqt`, then sign and notarize the completed bundles.

Do not include Project Zomboid Tiles, textures, or other game assets. Users
must supply those separately.

## Verify a build

For every platform:

1. Record the exact source commit or tag.
2. Confirm a clean qmake configure and native compilation.
3. Verify that PZWorldEd, TileZed, and BuildingEd start without a developer Qt
   installation.
4. Confirm that settings and logs use a writable location.
5. Confirm sibling-editor launching.
6. Load complete 1x, 2x, and custom Tiles catalogues.
7. Test raster and OpenGL rendering.
8. Open and save representative PZW, TMX, and TBX projects.
9. Run the validators supported by the target platform.
10. Compare packaged executables with the compiler outputs by SHA-256.

Windows hash example:

```powershell
Get-FileHash C:\PZ_Mapping_Tools\build\worlded\PZWorldEd.exe -Algorithm SHA256
Get-FileHash C:\PZ_Mapping_Tools\package\bin\PZWorldEd.exe -Algorithm SHA256

Get-FileHash C:\PZ_Mapping_Tools\build\tilezed\TileZed.exe -Algorithm SHA256
Get-FileHash C:\PZ_Mapping_Tools\package\bin\TileZed.exe -Algorithm SHA256

Get-FileHash C:\PZ_Mapping_Tools\build\tilezed\BuildingEd.exe -Algorithm SHA256
Get-FileHash C:\PZ_Mapping_Tools\package\bin\BuildingEd.exe -Algorithm SHA256
```

Linux and macOS hash example:

```sh
shasum -a 256 /path/to/compiler/output
shasum -a 256 /path/to/packaged/output
```

The corresponding source and required license files must be available for
every public binary release.
