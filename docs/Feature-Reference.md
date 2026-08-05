# PZTools user-facing feature reference

This page is a routing table for the maintained PZWorldEd, TileZed, and
BuildingEd release. It covers major existing, restored, and newly added
functions. Detailed interaction instructions remain in the linked manuals.

## Shared suite behavior

| Function | Where | Reads | Writes / effect | Details |
|---|---|---|---|---|
| Initial setup | First editor startup | Extracted Tiles tree and packaged catalog directory | `settings/PZTools.ini` | Paths are shared by all three applications. |
| Change Shared Paths | WorldEd/TileZed preferences | Tiles parent and `config` | Shared portable settings | Restart other open editors after changing a shared path. |
| Audited configuration catalogues | Portable `config` | Build 42.20 data and configured Tiles tree | Shared definitions for all three applications | [File-by-file configuration reference](PZTools-Configuration-Files.md); project overrides stay outside the game and tool directories. |
| External themes | Preferences | `themes/*.qss` | Per-application theme choice | Theme changes widgets, not map rendering. |
| Complete tileset discovery | All three applications | Every valid PNG below 2x, then 1x/custom fallback | In-memory catalogue and startup log | 2x wins when both scales exist; 1x-only and 2x-only sheets remain valid. |
| Portable logs | Every run | Runtime messages | `settings/logs/<Application>-<timestamp>-<pid>.log` | Newest 20 files per application are retained. |
| Session/layout persistence | Every application | Portable application INI | Window, dock, splitter, recent-file, and selected-tool state | An unclean previous run skips automatic document restoration. |

See [Logs, diagnostics, and useful issue reports](Diagnostics-and-Logs.md) and
the [complete user guide](TileZed/PZToolsGuide.html).

### Common interaction model

New guided tools are designed so that a mapper can use them without first
learning the internal file formats:

1. the editor identifies the current project and states what it will inspect;
2. **Check** is read-only and produces a plain-language status;
3. one recommended action is presented when a safe correction exists;
4. changes are validated and backed up automatically;
5. file paths, ordered IDs, parser detail, and logs stay in an optional
   technical view for support.

An error should explain what is wrong, which file is involved, what remains
safe, and what the mapper can do next. Compatibility and file ownership are
shown in the workflow instead of being assumed knowledge.

## PZWorldEd

| Function | Menu / location | Availability | Main input | Main output or effect |
|---|---|---|---|---|
| New 300/256 world | File > New World | Always | Grid, dimensions, paths | `.pzw` project with project-owned grid format |
| Terrain/vegetation image editor | Tools > Terrain / Vegetation Image Editor... | Saved project loaded | `Map.png`, `Map_veg.png`, project `Rules.txt` | Atomic PNG pair and optional project attachment |
| BMP to TMX | World/BMP generation workflow | Project loaded | Main/veg images, WorldEd `Rules.txt`, `Blends.txt`, `MapBaseXML.txt` | Project-sized TMX cells |
| Project Doctor: Tiles and Paths | Tools > Project Doctor: Tiles and Paths... | Project loaded or project folder selected | PZW paths plus recursive TMX/TBX files | Plain-language summary table, optional support details, or backed-up atomic cleanup with stable TBX ID remapping |
| WorldGen biome editor/preview | Tools > WorldGen Biome Editor / Preview... | Saved project loaded | Read-only game WorldGen plus project definitions | Project biome/features Lua and representative 16x16 preview |
| WorldGen prefab editor | Tools > WorldGen Prefab Editor... | Saved project loaded | Game/project prefab or strict z=0 TMX/TBX | Project prefab Lua and optional staged override |
| Street Names | Street Names dock | World project loaded | `streets.xml` | Validated version-1 street definitions |
| Cell thumbnails | World cell view | World project loaded | Assigned TMX and shared Tiles | Cached project thumbnails for 300/256 grids |
| Remove empty border cells | World tools | Rectangular empty border exists | Current world | Smaller world rectangle with corrected origin/roads |
| Biomemap Generator | World tools | Project loaded | Main/veg images and seven supported zone types or Zone PNG | `biome.png` / `biomemap_X_Y.png` data |
| Biomemap painting | View > Show Biomemap, paint tool | Editable biomemap loaded | Biome-channel image | Atomic channel edit with Undo |
| Zombie Heatmap | View > ZombieMap, paint tool | Heatmap loaded/created | Zombie intensity image | Atomic image, backup, Undo |
| InGameMap features | Generate Road Features and feature preferences | Project/vector data loaded | Roads, trails, rails, geometry | Validated XML/binary world-map features |
| Native 256 LOT export | File > Generate Lots 8x8 | Native project with exact 256x256 TMX cells | PZW, TMX, RoomDefs, lots/prefabs, objects | One output cell per project cell with unchanged coordinates and 32x32 chunks of 8x8 |
| Complete map-mod export | File > Export Complete Mod (8x8)... | Project ready for LOT export | PZW, TMX, map metadata | Mod directory with map files and metadata |
| Open in TileZed | Cell command | Assigned TMX and sibling TileZed executable | Selected cell | Opens the exact TMX in the deployed TileZed |
| Raster/OpenGL selection | Preferences / command line | Renderer available | Editor preference | Same project through selected render path |

WorldGen details: [Build 42.20 WorldGen editor](PZ-B42.20-WorldGen-Editor-and-Prefabs.md).
Jumbo behavior: [Build 42.20 Jumbo trees](PZ-B42.20-Jumbo-Trees.md).
Project cleanup details:
[Project Doctor: TMX, TBX, tiles, and paths](PZ-Project-Doctor-Tiles-and-Paths.md).

## TileZed

| Function | Menu / location | Availability | Main input | Main output or effect |
|---|---|---|---|---|
| TMX map editing | File/Open and map canvas | TMX loaded | Project Zomboid TMX/TMX 2.0 | Tile/Object/RoomDef changes with Undo |
| Tileset palette/status | Tilesets dock | Map or catalogue loaded | Complete Tiles catalogue and TMX header | Visible scale, source, used/missing state, selection |
| Import PZ tileset PNG | Tileset tools | Readable PNG | 1x, 2x, or custom sheet | Catalogue/Tileset metadata with validated geometry |
| Tile Definitions editor | Tools / Tile properties | TileDef source selected | `.tiles` / `.tiles.txt` and PNGs | Tile properties and validated binary/text definitions |
| Compare `.tiles` files | Tools > Compare .tiles files... | Two files selected | Binary/text TileDefs | Structural/property report and controlled property merge |
| Snow / Replacement editor | Tools > Snow Editor... | TileDef loaded | Target/source tilesets | `SnowTile`, `BurntTile`, or custom replacement properties |
| Pack viewer/extractor | Tools > .pack Viewer / Extractor... | Pack loaded | `.pack` v0 or PZPK v1 | Reconstructed PNGs, sheets/pages, optional JSON manifest |
| Advanced pack comparator | Tools > Advanced .pack Comparator... | Two packs selected | Two `.pack` files | Pixel/metadata/file hashes, previews, difference report, CSV |
| Depth Map Editor | Tools > Depth Map Editor... | Tileset selected | `tileGeometry.txt`, source tiles, `DEPTH_*.png` | Geometry plus full depth atlas |
| Automapper | View > Automapping | Saved TMX and manifest | `automapping-rules.txt`, rule TMX/list files | Full or interactive rule application as Undo command |
| Lua mapping automation | Tools/Lua menu and console | Map/current directory/world available | Lua 5.2 editor scripts | Transactional map/object/RoomDef changes |
| BMP tools | BMP Tools window | Compatible map/image data | BMP layers and color rules | Brush/fill/select/prevent-blend/BMP-to-layers operations |
| Procedural loot | Tools > Procedural Loot Viewer / Editor... | Game Items path selected | Read-only game Lua plus project manifest | Project JSON and generated post-merge Lua |
| Mini-map | Mini-map dock | TMX loaded | Current map and shared ready tilesets | Background-rendered navigation preview |

Feature references:

- [Automapper](TileZed/Automapper.html)
- [Procedural loot](PZ-B42.20-Procedural-Loot-Editor.md)
- [Pack comparator and extractor](TileZed/PZ-Pack-Comparator-and-Extractor.md)
- [TileDef comparator and Snow/Replacement editor](TileZed/PZ-TileDef-Comparator-and-Snow-Editor.md)
- [Lua scripting](TileZed/LuaScripting.html)
- [BMP tools](TileZed/BMPTools.html)

## BuildingEd

| Function | Menu / location | Availability | Main input | Main output or effect |
|---|---|---|---|---|
| Building/room editing | Main canvas and room tools | Building open/new | `.tbx` or new dimensions/template | Rooms, floors, walls, objects, and building metadata |
| Object tools | Main toolbar | Compatible editing mode | Walls, doors, windows, stairs, roofs, furniture | Semantic BuildingEd objects and generated tile layers |
| Tile mode | Tile mode and tileset dock | Complete Tiles catalogue loaded | Selected tileset/tile | Direct user-tile layer editing |
| Ortho/Iso category palettes | Object modes | Building catalogs loaded | `BuildingTiles.txt`, `BuildingFurniture.txt` | Category-driven object/furniture selection |
| Templates and furniture catalogs | Building dialogs | Catalog directory valid | `BuildingTemplates.txt`, `BuildingFurniture.txt` | Reusable building defaults and furniture groups |
| Basements/negative floors | Floor/building controls | Format supports levels | Building floor structure | Below-ground floors retained in TBX/TMX export |
| Autosave | Automatic every 2.5 minutes | Unsaved changes | Current building | `.autosave` beside TBX or under portable settings |
| Lua building automation | Building > Run Lua Script... / Lua Console | Building document open | Lua 5.2 editor script | One transactional Undo command or no change on failure |
| Procedural loot | Building > Procedural Loot Viewer / Editor... | Game Items path selected | Current RoomDef plus game/project loot data | Project JSON and generated post-merge Lua |
| Category validator | Command line | Deployed build and Tiles configured | Full catalog | Template, Tile, Furniture, Ortho/Iso, Lua and Undo PASS/FAIL log |

The installed [BuildingEd manual](BuildingEd/index.html) documents tile mode,
the Tiles dialog, drawing/object tools, and Lua scripting.

## File and directory ownership

| Data | Owner/source | Editor policy |
|---|---|---|
| Base-game Tiles, WorldGen Lua, and loot Lua | Project Zomboid installation | Read-only reference |
| PZW/TMX/TBX and source PNGs | Mapper project | Edited only through explicit actions and normal save/Undo workflows |
| Project WorldGen | `<map-project>/media/lua/server/WorldGen` | Atomic project-owned output |
| Project loot | `<project-or-mod>/media/lua/server/Items` | Atomic JSON plus generated Lua |
| Staged WorldGen override | `<target>/media/maps/<MapName>/WorldGenOverride.lua` | Marked block replacement, unrelated content preserved |
| Catalogs | Portable `config` | Shared editable application data |
| Preferences/logs | Portable `settings` | Never use as the catalog directory |
| Extracted pack output | User-selected directory | Safe rename by default; overwrite only when explicitly selected |

## Known scope boundaries

- WorldGen roads are not exposed by the current preview/editor.
- Erosion is planned as a separate possible module.
- WorldGen preview output is representative rather than save-identical.
- Static prefabs are z=0 four-slot schematics, not multi-floor buildings.
- Loot editing does not define items, icons, sandbox settings, vehicles, or
  complete mod packaging.
- Pack comparison/extraction does not merge or patch source packs.
- TileDef property merge does not invent missing tilesets or silently resize
  changed sheets.
- The internal DAY/NIGHT prototype is hidden because it does not yet match the
  current game lighting renderer closely enough for authoring claims.
