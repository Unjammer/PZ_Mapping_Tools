# How to build PZ Mapping Tools

This guide explains how to arrange the source, create out-of-source build
directories, compile PZWorldEd, TileZed, and BuildingEd, and prepare a
redistributable application directory on Windows, Linux, and macOS.

Windows x64 with Qt 5.14.2 and MSVC is the currently tested release target.
The Linux and macOS sections describe the intended qmake build flow. Builds
for those systems must be compiled and tested on the target operating system
before they are published as supported releases.

## Required source layout

Keep the complete `WorldEd`, `TileZed`, and `shared` trees from the same source revision.
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
│   ├── shared/
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
- an OpenSSL 1.1 runtime compatible with the exact Qt 5.14.2 build, including
  its redistributable license text

The Qt 5.14.2 `msvc2017_64` package can be built with the Visual Studio 2022
x64 compiler environment. The paths used below are examples:

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
- Qt Core, Gui, Widgets, XML, SVG, OpenGL, Network, Concurrent, and
  development headers
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

### Guided setup

Run the [macOS setup script](tools/Setup-macOS.command) from Terminal:

```sh
/bin/bash tools/Setup-macOS.command
```

The script selects the native Apple Silicon or Intel architecture, checks
Xcode and its SDK, installs Homebrew and Qt 5.15 when missing, obtains the
source, checks the compiler with a small Qt program, and builds all three
applications. Xcode installation and license acceptance use Apple's normal
interface. Homebrew retains its own installation confirmations.

When the script is inside a complete source tree, it uses that copy without
changing it. A standalone copy downloads the `main` branch of the public
repository into `~/Developer/PZTools/source`. Sources that lack the macOS
build configuration are rejected with instructions to select a newer source
copy. A branch or tag can be selected with `--ref` when creating a clone.

Useful options:

```sh
/bin/bash tools/Setup-macOS.command --dependencies-only
/bin/bash tools/Setup-macOS.command --no-build
/bin/bash tools/Setup-macOS.command --source-dir "$HOME/PZ_Mapping_Tools/source"
/bin/bash tools/Setup-macOS.command --directory "$HOME/Developer/PZTools" --jobs 4
/bin/bash tools/Setup-macOS.command --update
```

Rerunning the script preserves existing sources and compiler outputs. Only
`--update` updates the downloaded clone, using a fast-forward merge after
checking its origin, branch, and local changes. A source selected with
`--source-dir` is never updated. Changing the compiler, Qt, SDK path, source
directory, or architecture selects a separate build directory. The script
does not delete earlier builds or alter shell startup files.

Builds are under `~/Developer/PZTools/build`, with a directory for the selected
toolchain. Each run keeps its complete log below `~/Developer/PZTools/logs`.
The script stops on errors and reports the failed step and log location.
After compilation, it checks that all three expected executables exist and
contain the selected architecture. These are development applications that
use the local Qt installation and their build libraries. Packaging, signing,
and native graphical validation are separate from this setup command.

### Prerequisites

Use a real macOS build host with:

- Xcode and the Xcode command-line tools
- a Qt installation that includes qmake, macdeployqt, Widgets, XML, SVG, and
  OpenGL support
- an Apple Silicon Qt kit for an arm64 build
- an Intel Qt kit for an x86_64 build

On macOS Tahoe, Homebrew provides Qt 5.15 with Apple Silicon packages. Install
Xcode with the macOS SDK, select it, then install Qt 5:

```sh
sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer
sudo xcodebuild -runFirstLaunch
brew install qt@5
export PZ_QT="$(brew --prefix qt@5)"
export PATH="$PZ_QT/bin:$PATH"
"$PZ_QT/bin/qmake" -v
xcrun --sdk macosx --show-sdk-path
```

Install Homebrew using its [installation instructions](https://docs.brew.sh/Installation)
if `brew` is unavailable. Use a native terminal and Qt kit matching the Mac's
architecture. The [Qt 5 formula](https://formulae.brew.sh/formula/qt@5) lists
available binary packages and its deprecation status. A source installation
of Qt requires the full Xcode application.

The current maintained Windows release uses Qt 5.14.2. A public Apple Silicon
release should complete and validate the remaining Qt and OpenGL compatibility
work with the Qt version selected for macOS. Do not claim an arm64 or universal
release solely because qmake generated the project.

### Build all three applications

From Terminal, with `QMAKE` set to the selected Qt qmake:

```sh
export QMAKE="$(brew --prefix qt@5)/bin/qmake"
export PZ_ARCH="$(uname -m)"

mkdir -p "$HOME/PZ_Mapping_Tools/build/worlded"
mkdir -p "$HOME/PZ_Mapping_Tools/build/tilezed"

cd "$HOME/PZ_Mapping_Tools/build/worlded"
"$QMAKE" "$HOME/PZ_Mapping_Tools/source/WorldEd/PZWorldEd.pro" \
  -spec macx-clang CONFIG+=release "QMAKE_APPLE_DEVICE_ARCHS=$PZ_ARCH"
make -j"$(sysctl -n hw.ncpu)"

cd "$HOME/PZ_Mapping_Tools/build/tilezed"
"$QMAKE" "$HOME/PZ_Mapping_Tools/source/TileZed/tiled.pro" \
  -spec macx-clang CONFIG+=release "QMAKE_APPLE_DEVICE_ARCHS=$PZ_ARCH"
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

The macOS projects enable C++17 and place their application bundles under
each build root's `bin` directory. The corresponding `lib` directory holds
the bundled build libraries. Relative runtime search paths cover this build
layout and the `Contents/Frameworks` directory used by deployed bundles.

Qmake copies the shared configuration catalogues, documentation, BuildingEd
manual, and Lua scripts into each application's `Contents/Config`,
`Contents/Docs`, and `Contents/Lua` directories. TileZed and BuildingEd have
separate executable identities, document associations, and translations.

For editor-to-editor commands, place the three deployed application bundles
beside each other. Copy the TileZed format plugins to BuildingEd's
`Contents/PlugIns` directory as well. Retain the matching libraries from each
build when deploying the bundles. The former Qt 4 distribution scripts are
not part of the current build workflow.

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

On Windows, use the maintained deployment script after both builds complete:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File C:\PZ_Mapping_Tools\source\tools\Deploy-WindowsPortableRuntime.ps1 `
  -TileZedBuildDirectory C:\PZ_Mapping_Tools\build\tilezed `
  -WorldEdBuildDirectory C:\PZ_Mapping_Tools\build\worlded `
  -DestinationDirectory C:\PZ_Mapping_Tools\package\PZTools `
  -QtBinDirectory C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin
```

The destination must already contain the maintained application data
directories described above. The script copies changed executables, PDBs,
shared libraries, plugins, documentation, and licenses. It runs
`windeployqt` without the compiler-runtime option, then supplies one coherent
app-local Microsoft Visual C++ x64 runtime. Only `msvcp140.dll`,
`vcruntime140.dll`, and `vcruntime140_1.dll` are copied because those are the
runtime files imported by the current applications, Qt libraries, and
plugins. Every selected runtime file is verified as Microsoft-signed and must
be at least as new as the newest linker version found in the deployed
binaries.

The BuildingEd manual is maintained in
`TileZed/src/tiled/BuildingEditor/manual`. The deployment script installs it
under `docs/BuildingEd`, where the release documentation links resolve.

By default, the script selects the newest x64 Visual C++ minimum-runtime MSI
from the build machine's package cache or a suitable Visual Studio
redistributable directory. `-VcRuntimeDirectory` accepts an extracted
`Microsoft.VC*.CRT` directory. `-VcRuntimeMinimumMsi` accepts an official
`vc_runtimeMinimum_x64.msi` directly. Do not combine runtime DLLs from
different releases.

`windeployqt` does not supply the OpenSSL runtime used by Qt Network. Place
compatible `libssl-1_1-x64.dll` and `libcrypto-1_1-x64.dll` files beside the
executables before running the script, or pass their directory through
`-OpenSslDirectory`. Retain `licenses/OpenSSL-1.1.1.txt` and the matching
entry in `THIRD_PARTY_NOTICES.txt`.

The resulting Windows directory does not require a system Visual C++ runtime
installation. The same package may be tested through Wine or CrossOver on
macOS without installing a separate `vcrun` component because Wine provides
the Universal CRT system implementation. Wine and macOS use remains
experimental and unsupported until tested on the target systems. A native
macOS release requires the separate application bundle workflow above.

On Linux, deploy the Qt libraries and platform plugins with a maintained
deployment tool or distribution package recipe. Preserve origin-relative
library lookup and install writable settings and logs under the user's
standard application-data locations.

On macOS, place configuration, documentation, Lua, themes, translations, and
plugins in the application bundle locations expected by the qmake projects.
Use `macdeployqt`, then sign and notarize the completed bundles.

Do not include Project Zomboid Tiles, textures, or other game assets. Users
must supply those separately.
