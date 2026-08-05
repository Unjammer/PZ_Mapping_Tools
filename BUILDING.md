# Building PZ Mapping Tools

This guide describes the maintainer build used for the portable PZWorldEd,
TileZed, and BuildingEd release. It deliberately separates source, build, and
deployment directories so that an old executable or DLL cannot be mistaken
for the current build.

## Supported release target

The currently supported and release-tested target is:

- Windows x64;
- Qt 5.14.2, `msvc2017_64` package;
- qmake;
- Microsoft Visual C++ x64 (`nmake`);
- the maintained sources under `WorldEd` and `TileZed`.

Visual Studio 2022 can build the Qt `msvc2017_64` package when the Desktop
development with C++ workload and a Windows SDK are installed. The current
maintainer machine uses:

```text
C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin\qmake.exe
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat
```

Do not build the historical `repos`, `reference`, or `recovery` copies when
preparing a release.

## Directory layout

The examples assume:

```text
C:\pz\
├── integration\WorldEd\       maintained PZWorldEd source
├── integration\TileZed\       maintained TileZed/BuildingEd source
└── build\
    ├── worlded-streetnames\    out-of-source WorldEd build
    ├── tilezed-night\          out-of-source TileZed/BuildingEd build
    └── PZTools-Qt5-Latest\     only final deployment
```

Build outside the source directories. For a reproducible clean build, create
new empty build directories instead of deleting or modifying source files.

## Configure and build WorldEd

Open `cmd.exe`, initialize the Visual Studio x64 environment, then run qmake
from the WorldEd build directory:

```bat
call C:\PROGRA~1\MICROS~1\2022\COMMUN~1\Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64
mkdir C:\pz\build\worlded-streetnames
cd /d C:\pz\build\worlded-streetnames
C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin\qmake.exe C:\pz\integration\WorldEd\PZWorldEd.pro -spec win32-msvc CONFIG+=release
nmake
```

For an already configured build tree, initialize the same Visual Studio
environment and run only `nmake`. Rerun qmake after changing a `.pro`, `.pri`,
resource, form, or source-list file.

The principal output is:

```text
C:\pz\build\worlded-streetnames\PZWorldEd.exe
```

Keep its matching PDB when the linker produces one.

## Configure and build TileZed and BuildingEd

TileZed and BuildingEd are produced by the same top-level project:

```bat
call C:\PROGRA~1\MICROS~1\2022\COMMUN~1\Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64
mkdir C:\pz\build\tilezed-night
cd /d C:\pz\build\tilezed-night
C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin\qmake.exe C:\pz\integration\TileZed\tiled.pro -spec win32-msvc CONFIG+=release
nmake
```

This build also produces the shared Tiled libraries, format plugins, Lua
tools, and bundled zlib used by the editors. The main outputs are:

```text
C:\pz\build\tilezed-night\TileZed.exe
C:\pz\build\tilezed-night\BuildingEd.exe
```

## Incremental versus clean builds

Use an incremental `nmake` while developing. Before a public release, use a
fresh empty build directory or verify that qmake has regenerated every nested
Makefile. A successful link alone does not prove that the deployed directory
contains the new executable.

Do not copy an entire old build tree over the release. Deploy the newly linked
executables and their PDBs, then copy a DLL or plugin only when that output was
actually rebuilt or its hash changed.

## Assemble the portable release

The final deployment root is:

```text
C:\pz\build\PZTools-Qt5-Latest
```

Copy the current outputs into its `bin` directory:

- `PZWorldEd.exe` and its PDB;
- `TileZed.exe` and its PDB when produced;
- `BuildingEd.exe` and its PDB when produced;
- rebuilt shared DLLs or plugins whose content changed.

For a brand-new portable directory, use Qt's `windeployqt` on the three
executables, then add the project data directories (`config`, `lua`,
`brushes`, `themes`, `translations`, `docs`) and the shared plugins required
by the tools. A release must also retain:

- `COPYING.txt`;
- `THIRD_PARTY_NOTICES.txt`;
- `SOURCE-OFFER.txt`;
- `AUTHORS.txt`;
- `UPSTREAM-HISTORY.md`;
- the complete `licenses` directory.

The exact corresponding source must be committed, tagged, and made available
before a public binary release. Do not present a stale ZIP already present in
the deployment directory as the current build.

## Verify deployment

Compare every copied binary with its build output:

```powershell
Get-FileHash C:\pz\build\worlded-streetnames\PZWorldEd.exe -Algorithm SHA256
Get-FileHash C:\pz\build\PZTools-Qt5-Latest\bin\PZWorldEd.exe -Algorithm SHA256

Get-FileHash C:\pz\build\tilezed-night\TileZed.exe -Algorithm SHA256
Get-FileHash C:\pz\build\PZTools-Qt5-Latest\bin\TileZed.exe -Algorithm SHA256

Get-FileHash C:\pz\build\tilezed-night\BuildingEd.exe -Algorithm SHA256
Get-FileHash C:\pz\build\PZTools-Qt5-Latest\bin\BuildingEd.exe -Algorithm SHA256
```

Each source/deployed pair must match. Test only the deployed executables,
because that directory contains the Qt runtime and plugins users receive.

Run at least:

```powershell
Set-Location C:\pz\build\PZTools-Qt5-Latest\bin
.\TileZed.exe --validate-tileset-catalog
.\TileZed.exe --validate-automapper-rules
.\TileZed.exe --validate-brush-performance
.\BuildingEd.exe --validate-building-categories
.\PZWorldEd.exe --validate-preview-overlays
.\PZWorldEd.exe --validate-native-256-lot-geometry
.\PZWorldEd.exe --validate-tileset-cleanup
```

These are Windows GUI-subsystem applications. PowerShell can return before a
validator exits, so wait for the exact process and confirm the explicit
`PASS` or `FAIL` line in the newest application log under
`settings\logs`. Also perform one normal startup of each editor and ensure no
hidden test process remains running.

The complete validator and issue-report reference is in
[`docs/Diagnostics-and-Logs.md`](docs/Diagnostics-and-Logs.md).

## Linux and macOS

The qmake projects contain useful cross-platform branches, but Windows x64 is
the only release-tested target today. The old distribution scripts are not a
supported release recipe. Linux and macOS need portability and packaging work
before their binaries can be advertised.

See [`PLATFORM-BUILD-AUDIT.md`](PLATFORM-BUILD-AUDIT.md) for the verified
blockers, recommended build targets, and validation plan.
