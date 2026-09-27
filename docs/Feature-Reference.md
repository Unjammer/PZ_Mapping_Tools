# PZTools feature reference

This page routes users to the major functions available in the current
PZWorldEd, TileZed, and BuildingEd release.

## Shared suite behavior

| Function | Location | Effect |
|---|---|---|
| Initial setup | First startup | Selects shared Tiles and configuration paths |
| Project Zomboid installation | Preferences | Supplies read-only game TileDefs, packs, WorldGen, and loot data |
| Basement access resources | Portable `pzby_tbx` beside `bin` | Reads editable sources from `basement_access` and compiled PZBY files from `binmap` |
| Window setup | Preferences | All three tools start at a centered 1920 x 1080 size fitted to the desktop. Temporary custom dimensions can be applied to the current application or used to open all three, without rewriting another application's INI |
| Reset interface | Preferences | Restores the current application's default window and dock layout |
| Complete tileset discovery | All applications | Resolves every valid PNG recursively with 2x priority and 1x or custom fallback |
| New Folder | File browsers | Creates a directory from a compact button or the browser context menu |
| Render diagnostics | View menu | Shows FPS, render time, RAM, zoom, renderer, viewport, and drawn content |
| Portable logs | Every run | Writes support logs below `settings/logs` |

See [configuration files](PZTools-Configuration-Files.md),
[logs and issue reports](Diagnostics-and-Logs.md), and the
[complete user guide](TileZed/PZToolsGuide.html).

## PZWorldEd

| Function | Menu or location | Main result |
|---|---|---|
| New 300 or 256 world | File > New World | PZW project with the selected native grid |
| Cell Copy and Paste | Edit menu or Ctrl+C and Ctrl+V | Complete cell data with a pointer-following preview, occupied-target warning, one placement per confirmation, and one Undo transaction |
| OpenStreetMap project | Terrain & Environment | New project with terrain, vegetation, roads, buildings, streets, and zones |
| Terrain and vegetation editor | Terrain & Environment | Atomic Map and vegetation PNG editing |
| BMP to TMX | Conversion & Export | Project-sized TMX cells with validation, repair, and metadata-only Rules/Blends synchronization |
| Project Doctor | Project Utilities | Read-only health report and backed-up project corrections |
| WorldGen biome editor | WorldGen | Project biome and feature Lua |
| WorldGen prefab editor | WorldGen | Static prefab Lua from supported sources |
| Street Names | Street Names dock | Version 1 `streets.xml` definitions |
| Regions | Regions dock | Independent rectangular `regions.lua` definitions |
| Biomemap Generator | Conversion & Export | Biome and zone channel images |
| Biomemap painting | Biomemap view | Separate red biome and green zone channel edits |
| Zombie Heatmap | ZombieMap view | Editable heatmap PNG with Undo and backup |
| Hole Detection | World tools | Missing level-zero tile detection and optional repair |
| Generate Lots | File menu | Legacy300 or one-to-one Native256 LOT output |
| Linked World Projects | World menu | Validated read-only PZW references aligned by World origin |
| InGameMap Forest | InGameMap menu | Forest XML, binary, PNG, and pyramid |
| Complete mod export | File menu | Map mod directory with generated map data |
| Basement entrance preview | Selected Basement zone | Translucent editable TBX or TMX entrance overlay |
| Integrated building basement | Placed lot context menu | Vertical lot placement, selected buried-level outlines, and confirmed ground opening at detected basement stairs |

Detailed references:

- [Project Doctor](PZ-Project-Doctor-Tiles-and-Paths.md)
- [WorldGen editor](PZ-B42.20-WorldGen-Editor-and-Prefabs.md)
- [Jumbo trees](PZ-B42.20-Jumbo-Trees.md)
- [BiomeMapConfig](PZ-B42.20-BiomeMapConfig.md)
- [Linked World Projects](PZWorldEd-Linked-World-Projects.md)

## TileZed

| Function | Menu or location | Main result |
|---|---|---|
| TMX editing | Main editor | Tile, object, layer, and RoomDef changes with Undo |
| Tileset palette | Tilesets dock | Complete catalogue with source and scale tags, optional tile numbers in the context menu |
| Tile Definitions | Tools | Binary and text TileDef property editing |
| TileDef comparison | Tools | Structural comparison and controlled property merge |
| Snow and replacements | Tools | SnowTile, BurntTile, and custom replacement properties |
| Pack extraction | Tools | Individual tiles, tilesets, pages, or assembled objects |
| Pack comparison | Tools | Pixel, metadata, and file comparison |
| Depth Map Editor | Tools | Color depth painting, ground and vertical estimates, fitted volumes with live previews and Undo |
| TileSetZ | Tools or tile context menu | Pixel drawing and shapes, zoomable tilesheets and masks, automatic wall texture precutting with assembly check and TMX export, trim colors and Undo |
| Automapper | Automapping dock | Full or interactive rules applied through Undo |
| BMP Tools | Dockable BMP window | Terrain brush, fill, select, blending, and layer conversion |
| Select Tiles | Main tools | Current-layer selections paste onto the destination layer, broader scopes preserve layer names and relative levels |
| Procedural loot | Tools | Project loot manifest and generated post-merge Lua |
| Lua mapping | Tools and console | Transactional map, object, and RoomDef changes |

Detailed references:

- [Automapper](TileZed/Automapper.html)
- [TileSetZ](TileSetZ.md)
- [Procedural loot](PZ-B42.20-Procedural-Loot-Editor.md)
- [Pack tools](PZ-Pack-Comparator-and-Extractor.md)
- [TileDef tools](PZ-TileDef-Comparator-and-Snow-Editor.md)
- [Lua scripting](TileZed/LuaScripting.html)
- [BMP tools](TileZed/BMPTools.html)

## BuildingEd

| Function | Location | Main result |
|---|---|---|
| Building and room editing | Main editor | TBX rooms, floors, walls, roofs, objects, and metadata |
| Tile mode | Tile mode | Direct user-tile layer editing |
| Select Tiles | Main tools | Layer-aware and floor-aware copy and cut, followed by a movable paste preview |
| Object palettes | Ortho and Iso modes | Category-driven walls, furniture, and objects |
| Templates | Building dialogs | Reusable building defaults and furniture groups |
| Basement placement | WorldEd lot placement | Places the building's local floors below ground |
| Building transforms | Building menu | Reversible rotations and mirrors retain painted tile positions and attributes |
| Procedural loot | Building menu | Room-aware project loot overrides |
| Lua automation | Building menu and console | Transactional building changes with Undo |
| Autosave | Automatic | Recoverable copy of modified building data |

The BuildingEd manual documents its editing
modes and tools.

## File ownership

| Data | Owner | Policy |
|---|---|---|
| Game Tiles, WorldGen, and loot | Project Zomboid installation | Read-only reference |
| Basement access TBX, TMX, and PZBY | Portable `pzby_tbx` beside `bin` | Read-only source and compiled access catalogue |
| PZW, TMX, TBX, and source PNG | Mapper project | Written only through explicit editor actions |
| Project WorldGen | Project `media/lua/server/WorldGen` | Atomic project output |
| Project loot | Project `media/lua/server/Items` | Project JSON and generated Lua |
| Catalogues | Portable `config` | Shared application data |
| Preferences and logs | Portable `settings` | Never used as the catalogue directory |
| Extracted pack data | User-selected directory | Safe rename unless overwrite is selected |

## Current limits

- WorldGen preview output is representative rather than save-identical.
- Static WorldGen prefabs use the supported level-zero schematic format.
- Loot editing does not define items, icons, sandbox settings, or vehicles.
- Pack tools do not merge or patch source packs.
- TileDef merge does not invent missing tilesets or resize source sheets.
