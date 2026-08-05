# PZTools documentation index

This is the source-tree index for the maintained WorldEd, TileZed, and
BuildingEd documentation. A compiled release contains the same entry points
under `docs/index.html`.

## Start here

- [PZTools user guide](docs/TileZed/PZToolsGuide.html): installation,
  shared paths, projects, major workflows, and troubleshooting.
- [Logs, diagnostics, and issue reports](docs/Diagnostics-and-Logs.md):
  log names and retention, reading severity and thread fields, regression
  commands, and the information needed for a useful report.
- [User-facing feature reference](docs/Feature-Reference.md): menu/location,
  prerequisites, inputs, output ownership, and current scope for each major
  function.
- [Configuration files](docs/PZTools-Configuration-Files.md): which catalogue
  each tool uses, the audited Build 42.20 state, and safe customization rules.
- [Current release changes](RELEASE_CHANGELOG.md): user-facing release notes.
- [Complete unofficial changelog](CHANGELOG-PZTOOLS.md): detailed differences
  from the maintained Tim Baker baselines.
- [Building PZ Mapping Tools](BUILDING.md): supported Windows build,
  deployment, hash verification, and regression procedure.
- [Linux and macOS build feasibility audit](PLATFORM-BUILD-AUDIT.md):
  confirmed portability blockers and validation gates.
- [Upstream history](UPSTREAM-HISTORY.md): source provenance and selectively
  ported upstream work.

## WorldEd

| Subject | Reference |
|---|---|
| Guided PZW/TMX/TBX health check, stale tilesets, path repair, backups, and TBX ID remapping | [Project Doctor: tiles and paths](docs/PZ-Project-Doctor-Tiles-and-Paths.md) |
| Shared configuration catalogues, Tiles resolution, audited counts, and safe customization | [PZTools configuration files](docs/PZTools-Configuration-Files.md) |
| WorldGen biomes, 2x2-chunk preview, biome features, static prefabs, TMX/TBX conversion, and staging | [Build 42.20 WorldGen editor](docs/PZ-B42.20-WorldGen-Editor-and-Prefabs.md) |
| Jumbo, XL, and XXL tree behavior | [Deterministic Jumbo trees](docs/PZ-B42.20-Jumbo-Trees.md) |
| Terrain/vegetation image editor, project grids, thumbnails, Biomemap, Zombie Heatmap, InGameMap, and export | [PZTools user guide](docs/TileZed/PZToolsGuide.html) |

## TileZed

| Subject | Reference |
|---|---|
| Automapper concepts, manifest, layer rules, examples, interactive mode, and troubleshooting | [Automapper](docs/TileZed/Automapper.html) |
| Build 42 procedural loot viewer/editor shared with BuildingEd | [Procedural loot editor](docs/PZ-B42.20-Procedural-Loot-Editor.md) |
| Advanced `.pack` comparison, hashes, previews, extraction, and JSON provenance | [Pack comparator and extractor](docs/PZ-Pack-Comparator-and-Extractor.md) |
| `.tiles` comparison and Snow/Burnt/custom replacement editing | [TileDef comparator and Snow editor](docs/PZ-TileDef-Comparator-and-Snow-Editor.md) |
| TileDef property meanings | [Tile properties](docs/TileZed/TileProperties/index.html) |
| Terrain BMP tools inside TileZed | [BMP tools](docs/TileZed/BMPTools.html) |
| Lua batch/interactive mapping API and examples | [Lua scripting](docs/TileZed/LuaScripting.html) |
| RoomDef tools | [RoomDefecator](docs/TileZed/RoomDefecator.html) |
| Mapping-controlled spawn data | [Mapping spawn control](docs/TileZed/MappingSpawnControl.html) |

The complete Automapper example is under
`TileZed/examples/sewer_automap`.

## BuildingEd

The maintained manual lives under
`TileZed/src/tiled/BuildingEditor/manual` and is installed as
`docs/BuildingEd` in the portable release.

- [BuildingEd manual index](TileZed/src/tiled/BuildingEditor/manual/index.html)
- [BuildingEd Lua scripting](TileZed/src/tiled/BuildingEditor/manual/LuaScripting.html)
- [BuildingEd tools](TileZed/src/tiled/BuildingEditor/manual/Tools.html)
- [Tile mode](TileZed/src/tiled/BuildingEditor/manual/TileEditingMode.html)
- [Tiles dialog](TileZed/src/tiled/BuildingEditor/manual/TilesDialog.html)

The procedural-loot editor is shared with TileZed and uses the same loader,
project-output format, validation rules, and documentation.

## Documentation evidence labels

The maintained documents use these distinctions:

- **Game-confirmed** means the behavior or structure was checked against the
  local Project Zomboid 42.20 Lua/Java/data reference.
- **Tool-enforced** means it is an editor validation or safety policy and is
  not presented as an engine limit.
- **Representative preview** means the display is useful for authoring but is
  not claimed to reproduce a particular game save exactly.
- **Out of scope** identifies data that the current editor deliberately does
  not read, write, package, or simulate.

When one of these distinctions affects a workflow, the relevant document
states it next to the feature or limit.

## Documentation maintenance

User-visible behavior changes are not complete until all applicable locations
are updated:

1. the dedicated feature reference;
2. the user guide or documentation index;
3. `README.md`;
4. `RELEASE_CHANGELOG.md`;
5. `CHANGELOG-PZTOOLS.md`;
6. the copies installed in the portable distribution.

Before a release, internal links are checked and duplicate packaged/source
copies are compared by SHA-256.
