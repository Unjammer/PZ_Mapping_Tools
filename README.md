# PZTools Unofficial

An unofficial, maintained Qt 5 edition of the Project Zomboid mapping tools:
**WorldEd**, **TileZed**, and **BuildingEd**.

This repository continues the work in Tim Baker's
[pzworlded](https://github.com/timbaker/pzworlded) and
[tiled](https://github.com/timbaker/tiled) repositories. It starts from the
upstream `basements` work, preserves basement and negative-level support, and
adds a portable Windows distribution, native 256-cell workflows, image editing,
mapping automation, stability fixes, and current Project Zomboid data support.

This is a community project. It is not an official The Indie Stone release.
Project Zomboid game assets are not included.

## The three editors

| Application | Main purpose | Notable additions in this fork |
|---|---|---|
| **WorldEd** | Assemble the world, assign TMX maps to cells, edit roads and zones, and export map data | Per-project 300/256 grids, terrain and vegetation PNG editor, procedural terrain tools, improved thumbnails, Biomemap and Zombie Heatmap tools, safer InGameMap export |
| **TileZed** | Edit TMX maps, tilesets, TileDefs, Automapper rules, and run Lua mapping tools | Complete tileset catalog preload, 2x/custom-only support, restored Automapper, expanded Lua API, pack and tileset utilities, clearer tile status and diagnostics |
| **BuildingEd** | Create and edit TBX buildings, rooms, furniture, roofs, grime, and building metadata | Standalone executable, complete Tile/Furniture/Object palettes, category validation, templates, autosave, and transactional Lua building scripts |

## Highlights

### Portable, shared configuration

- No installer, Registry configuration, `%APPDATA%` dependency, or
  standalone `config.exe`.
- The first editor started opens **PZTools Initial Setup** only when no valid
  Tiles directory can be detected.
- WorldEd, TileZed, and BuildingEd share the same paths through
  `settings/PZTools.ini`.
- Packaged catalogs remain in `config`; runtime preferences and logs remain in
  `settings`.
- Selecting `Tiles`, `Tiles/1x`, `Tiles/2x`, or `Tiles/custom` is normalized
  automatically. A `1x` directory is not required.
- Settings are schema-versioned. Incompatible UI state can be reset during an
  upgrade while valid shared paths are preserved.
- Unicode paths and portable side-by-side installations are supported.

### Predictable tileset availability

Every catalog tileset is decoded during application startup and remains
resident for the editing session. This makes the complete catalog immediately
available to palettes, building categories, Automapper, and Lua scripts without
changing behavior depending on which map was opened first.

The trade-off is deliberate: startup can take tens of seconds and several
gigabytes of memory with a current 2x Tiles installation. The progress dialog
and `settings/logs` report the total, loaded, missing, and unresolved entries.
Missing or corrupt images use an explicit placeholder instead of causing a
silent empty palette.

### 300 × 300 and 256 × 256 projects

World geometry is stored per project as either:

- **300 × 300 (Legacy)** for the historical mapping workflow; or
- **256 × 256 (Native)** for current native cells.

The selected geometry is used consistently by world and cell views, coordinate
conversion, thumbnails, BMP/TMX conversion, building import, procedural image
tools, LOT generation, InGameMap export, and Lua placement. Existing 300-cell
projects remain supported.

## WorldEd

### Terrain and vegetation image editor

WorldEd contains a lightweight image editor for the project `Map.png` and
`Map_veg.png` pair. The command is enabled after a WorldEd project is loaded,
because palette colors, cell dimensions, and output paths belong to that
project.

- Ground and vegetation colors come from `Rules.txt`.
- Unknown colors are reported with their RGB value, pixel coordinate, and
  affected file.
- Images can cover multiple 300 × 300 or 256 × 256 cells.
- Brush, fill, color picker, vegetation eraser, zoom, composite preview,
  undo, and redo are included.
- PNG saves are atomic and checked before the original is replaced.
- The working-memory limit is configurable from **128 MiB to 64 GiB**; the
  default is **512 MiB**.
- Procedural tools can generate terrain patches, vegetation, lakes, rivers,
  and roads continuously across cell boundaries.

### World and export tools

- Safe trimming of empty cells from the outer world rectangle, including
  project-origin and road-coordinate updates.
- Correct thumbnails for both grid formats, selection-only rebuilds, and
  configurable thumbnail resolution and grid appearance.
- Restored and optimized Biomemap generation using both ground and vegetation
  images, with explicit unmapped-color and zone-type diagnostics.
- Editable Zombie Heatmap with 300-cell and native 256-cell geometry, brush
  controls, undo, atomic save, and a safety backup.
- Restored InGameMap road generation for roads, trails, and railways.
- XML and binary InGameMap outputs are validated and committed as one
  recoverable pair.
- Complete map-mod export using the current 8 × 8 layout.
- BMP→TMX files are relinked to the shared catalog by tileset name, including
  installations where only the 2x image exists.
- An unclean shutdown no longer creates an automatic project-restore crash
  loop; the following start skips restoration and explains how to recover.

## TileZed

### Tiles, TileDefs, and Automapper

- Import Project Zomboid tilesets directly from PNG.
- Custom tile dimensions up to 4096 pixels and Jumbo tiles in pack tools.
- Restored advanced Pack Extractor, tileset identification, ID reconstruction,
  and tile-to-PNG export utilities.
- Tile names, numeric IDs, resolution, and loaded/missing status are visible in
  editor palettes.
- B42 TileDef properties were audited against game sources and supplemented
  with English descriptions and contextual tooltips.
- The restored Automapping panel accepts TMX rule maps and nested TXT rule
  lists, detects recursion and duplicate loading, and supports Object Groups,
  property wildcards, and object add/change/remove triggers.

### Lua mapping automation

The portable `lua` directory includes the common mapping scripts and examples.
The current API keeps existing scripts working while adding:

- UTF-8 console output, tracebacks, progress reporting, and cooperative cancel;
- negative levels and WorldEd cell coordinates;
- Object Group and RoomDef access, creation, modification, and removal;
- tile lookup by exact name and layer lookup by `(level, baseName)`;
- exact-position and map-wide tile deletion and replacement;
- whole-layer removal and named placement with automatic tileset attachment;
- Undo integration for map changes; and
- a restricted `app` interface for documented editor actions.

`LuaTools.txt` can be supplied by a separate user catalog. Portable installs
detect when the user and application catalog are the same file and load it only
once.


## BuildingEd

- Standalone application with the same portable settings and external QSS
  themes as WorldEd and TileZed.
- Full tileset catalog is loaded before a building, template, or palette is
  displayed.
- Tile mode selects an available tileset by default instead of opening on an
  apparently empty palette.
- Ortho and Iso object modes refresh all 17 tile categories and all Furniture
  groups after `BuildingFurniture.txt` is loaded.
- Stable palettes for Roof Caps, roofs, walls, windows, grime, furniture, and
  every other category.
- Templates, RoomTone, Attribute mode, basements, negative floors, grime,
  unlit-room inspection, autosave, and binary/TMX export are retained.
- Transactional Lua 5.2 scripts provide building inspection, room fill,
  tile placement/removal/replacement, Undo, and restricted save/export actions.
- `BuildingEd.exe --validate-building-categories` validates the complete
  tileset catalog, all 26 templates, Tile mode, 175 Furniture groups, and both
  the Ortho and Iso category panels.

## Installing a compiled release

1. Extract the complete archive to a writable directory.
2. Keep the directory structure intact. Do not move an executable out of
   `bin`.
3. Extract or copy the Project Zomboid mapping Tiles separately. They are not
   redistributed with PZTools.
4. Start `PZWorldEd.exe`, `TileZed.exe`, or `BuildingEd.exe` from `bin`.
5. If **PZTools Initial Setup** appears, select the release `config` directory
   and your extracted Tiles directory.

A typical portable layout is:

```text
PZTools/
├── bin/             applications, Qt libraries, and Qt runtime plugins
├── config/          Rules.txt, Tilesets.txt, TMXConfig.txt, Building*.txt
├── docs/            packaged user documentation
├── lua/             TileZed and BuildingEd scripts
├── plugins/         Tiled format plugins
├── themes/          external QSS themes
├── translations/
├── settings/        created at runtime; shared INI files and logs
└── Tiles/           optional local game Tiles; not included
    ├── 1x/          optional
    ├── 2x/          optional
    └── custom/      optional
```

If the Tiles directory is moved later, use **Change Shared Paths** from WorldEd
or TileZed. BuildingEd consumes the same shared setting.

Logs are written to `settings/logs`. A useful issue report includes:

- application name and exact steps to reproduce;
- the newest matching log file;
- whether the Tiles directory contains `1x`, `2x`, and/or `custom` images;
- the relevant PZW, TMX, TBX, PNG, rules, or Lua file when it can be shared;
- the release date or commit used; and
- a screenshot of the visible result or error.

For a Windows **Bad Image** error after extracting a release, compare the
reported DLL with the archive or extract the archive again before changing
editor settings. A damaged or partially copied runtime file fails before the
application can create its normal log.

## Building from source

The supported source target is Windows x64 with qmake and Qt 5.14.2. The
current release is tested with the `msvc2017_64` Qt package and a compatible
MSVC x64 toolchain.

From a Visual Studio x64 Native Tools command prompt:

```bat
mkdir build-worlded
cd build-worlded
qmake ..\WorldEd\PZWorldEd.pro -spec win32-msvc CONFIG+=release
nmake
```

```bat
mkdir build-tilezed
cd build-tilezed
qmake ..\TileZed\tiled.pro -spec win32-msvc CONFIG+=release
nmake
```

The TileZed build also produces BuildingEd and the Tiled format plugins. Keep
executables and their matching libraries together when assembling a release.

## Repository layout

```text
.
├── WorldEd/                 WorldEd source tree
├── TileZed/                 TileZed and BuildingEd source tree
├── CHANGELOG-PZTOOLS.md     detailed differences from upstream
└── README.md
```
