# How to build PZ Mapping Tools

This guide explains how to arrange the source, create out-of-source build
directories, compile PZWorldEd, TileZed, and BuildingEd, and prepare a
redistributable application directory on Windows x64.

Windows x64 with Qt 5.14.2 and MSVC is the maintained release target.
Native Linux and macOS build and packaging paths are not included.

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
experimental and unsupported until tested on the target systems. This does
not provide native macOS application bundles.

Do not include Project Zomboid Tiles, textures, or other game assets. Users
must supply those separately.
