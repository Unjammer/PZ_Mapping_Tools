# PZTools documentation

This index covers the public documentation shipped with PZWorldEd, TileZed,
and BuildingEd.

## Start here

- [PZTools user guide](docs/TileZed/PZToolsGuide.html)
- [Feature reference](docs/Feature-Reference.md)
- [Troubleshooting FAQ](docs/FAQ.md)
- [Logs and useful issue reports](docs/Diagnostics-and-Logs.md)
- [Configuration files](docs/PZTools-Configuration-Files.md)
- [Current release changes](RELEASE_CHANGELOG.md)
- [Build instructions](BUILDING.md)
- [Feature-level provenance](FEATURE_PROVENANCE.md)
- [Upstream history and source provenance](UPSTREAM-HISTORY.md)

## WorldEd

| Subject | Reference |
|---|---|
| Project health, missing tilesets, paths, backups, and TBX IDs | [Project Doctor](docs/PZ-Project-Doctor-Tiles-and-Paths.md) |
| WorldGen biomes, previews, features, and static prefabs | [WorldGen editor](docs/PZ-B42.20-WorldGen-Editor-and-Prefabs.md) |
| Jumbo, XL, and XXL trees | [Jumbo trees](docs/PZ-B42.20-Jumbo-Trees.md) |
| Build 42.20 Biomemap values and channels | [BiomeMapConfig](docs/PZ-B42.20-BiomeMapConfig.md) |
| Read-only adjacent PZW projects and `otherworld` compatibility | [Linked World Projects](docs/PZWorldEd-Linked-World-Projects.md) |
| Terrain images, Biomemap, Zombie Heatmap, InGameMap, and export | [PZTools user guide](docs/TileZed/PZToolsGuide.html) |

## TileZed

| Subject | Reference |
|---|---|
| Automapper rules and interactive mode | [Automapper](docs/TileZed/Automapper.html) |
| Tile drawing, zoomable masks and wall texture precutting | [TileSetZ](docs/TileSetZ.md) |
| Procedural loot | [Procedural loot editor](docs/PZ-B42.20-Procedural-Loot-Editor.md) |
| Texture packs and extraction | [Pack comparator and extractor](docs/PZ-Pack-Comparator-and-Extractor.md) |
| TileDefs, snow, burnt tiles, and replacements | [TileDef tools](docs/PZ-TileDef-Comparator-and-Snow-Editor.md) |
| Tile property meanings | [Tile properties](docs/TileZed/TileProperties/index.html) |
| Terrain BMP tools | [BMP tools](docs/TileZed/BMPTools.html) |
| Lua mapping API | [Lua scripting](docs/TileZed/LuaScripting.html) |
| RoomDef tools | [RoomDefecator](docs/TileZed/RoomDefecator.html) |
| Mapping-controlled spawns | [Mapping spawn control](docs/TileZed/MappingSpawnControl.html) |

The complete Automapper example is under `TileZed/examples/sewer_automap`.

## BuildingEd

The BuildingEd manual is installed under `docs/BuildingEd` in the portable
release.

- [BuildingEd manual](TileZed/src/tiled/BuildingEditor/manual/index.html)
- [Lua scripting](TileZed/src/tiled/BuildingEditor/manual/LuaScripting.html)
- [Tools](TileZed/src/tiled/BuildingEditor/manual/Tools.html)
- [Tile mode](TileZed/src/tiled/BuildingEditor/manual/TileEditingMode.html)
- [Tiles dialog](TileZed/src/tiled/BuildingEditor/manual/TilesDialog.html)

## Terminology

- **Game-confirmed** describes behavior checked against Project Zomboid data
  or runtime code.
- **Tool-enforced** describes an editor validation or safety policy.
- **Representative preview** describes an authoring aid that does not claim
  to reproduce a particular game save exactly.
- **Out of scope** identifies data the current release does not read, write,
  package, or simulate.
