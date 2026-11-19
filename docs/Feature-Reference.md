# PZTools feature reference

| Task | Where to start |
|---|---|
| Generate or edit LOT data | [Editable LOT sources](#editable-lot-sources), [partial chunks](#partial-chunk-lot-export), [chunk-data overrides](#native256-chunk-data-overrides) |
| Preview authored cells or vehicle zones | [Play Map Source](#play-map-source), [Cell View vehicle previews](#cell-view-vehicle-previews) |
| Place basement lots or export zones | [Building lots and basement access](#building-lots-and-basement-access), [spawn points and Lua zones](#spawn-points-and-lua-zone-export), [fishing zones](#fishing-zones) |
| Work with the in-game map | [World-map XML overlays](#world-map-xml-overlays), [linked projects](PZWorldEd-Linked-World-Projects.md) |
| Paint, pick or reuse tiles | [Tile palettes](#tile-palettes-in-tilezed-and-buildinged), [selection and tile tools](#selection-and-tile-tools), [BuildingEd editing and inspection](#buildinged-editing-and-inspection) |

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

**Preferences > Window Setup** can apply a temporary window size to the
current tool or launch all three at that size. A normal launch uses 1920 ×
1080 fitted to the desktop and restores each application's dock layout.
Use **Close** or **Esc** in tool windows. Choose **Save**, **Discard** or
**Cancel** when prompted about pending edits. In **Keyboard Shortcuts**, use
**Reload From Disk** to read the saved bindings again. In **Tilesets** and
**Building Tiles**, **OK** applies the edits.

Each editor has an autosave interval in Preferences. WorldEd and TileZed
autosave existing, modified PZW or TMX documents. BuildingEd keeps a
recoverable `.autosave` copy. Give an untitled document a path with **Save**.
Autosave waits for cut, paste, paint and resize operations to finish.

## PZWorldEd

| Function | Menu or location | Main result |
|---|---|---|
| New 300 or 256 world | File > New World | PZW project with the selected native grid |
| Cell Copy and Paste | Edit menu or Ctrl+C and Ctrl+V | Complete cell data with a pointer-following preview, occupied-target warning, one placement per confirmation, and one Undo transaction |
| OpenStreetMap project | Terrain & Environment | New project with terrain, vegetation, roads, buildings, streets, and zones |
| [Terrain and vegetation editor](TileZed/PZToolsGuide.html#terrain-editor) | Terrain & Environment | Edits the saved project's Map and vegetation images in PNG or BMP format |
| BMP to TMX | Conversion & Export | Project-sized TMX cells with validation, repair, and metadata-only Rules/Blends synchronization |
| Project Doctor | Project Utilities | Read-only health report and backed-up project corrections |
| WorldGen biome editor | WorldGen | Project biome and feature Lua |
| WorldGen prefab editor | WorldGen | Static prefab Lua from supported sources |
| Street Names | Street Names dock | Version 1 `streets.xml` definitions |
| Regions | Regions dock | Independent rectangular `regions.lua` definitions |
| Biomemap Generator | Conversion & Export | Biome and zone channel images |
| Biomemap painting | Biomemap view | Separate red biome and green zone channel edits |
| [Zombie Heatmap](TileZed/PZToolsGuide.html#heatmap) | ZombieMap view | Editable heatmap with Undo and backup |
| Hole Detection | World tools | Missing level-zero tile detection and optional repair |
| Generate Lots | File menu | Legacy300 or one-to-one Native256 LOT output |
| Linked World Projects | World menu | Validated read-only PZW references aligned by World origin |
| InGameMap Forest | InGameMap menu | Forest XML, binary, PNG, and pyramid |
| Complete mod export | File menu | Map mod directory with generated map data |
| Basement entrance preview | Selected Basement zone | Translucent editable TBX or TMX entrance overlay |
| Integrated building basement | Placed lot context menu | Vertical lot placement, selected buried-level outlines, and confirmed ground opening at detected basement stairs |

In World View, use **Ctrl+A** to select all cells. With **Ctrl+C** and
**Ctrl+V**, green outlines show empty paste targets and orange outlines
show occupied ones. Click to place the cells and confirm before replacing
occupied content. Moving cells also moves their street points, region
anchors, road endpoints and fully covered terrain-image placements in the
same Undo operation. In the LotPack Viewer, **File > Save Screenshot** or
**F12** captures the current viewport, zoom, visible levels and RoomDefs
overlay to a PNG.

- [Project Doctor](PZ-Project-Doctor-Tiles-and-Paths.md)
- [WorldGen editor](PZ-B42.20-WorldGen-Editor-and-Prefabs.md)
- [Jumbo trees](PZ-B42.20-Jumbo-Trees.md)
- [BiomeMapConfig](PZ-B42.20-BiomeMapConfig.md)
- [Linked World Projects](PZWorldEd-Linked-World-Projects.md)

### Editable LOT sources

In **File > Generate Lots**, select **Export .source text files alongside
generated LOT files** to keep editable room and building metadata
(`.lotheader.source`), tile placements (`.lotpack.source`) and ground-level
navigation (`chunkdata_X_Y.bin.source`). The option is saved in the PZW and
starts off. To rebuild binaries, back up the lots directory, select it in
Generate Lots and use **Import .source files from lots directory**. Confirm
replacement of the matching binaries. Ordinary import uses the navigation
already in the source. Keep the PZW, TMX and TBX files for further map editing.

To export or import complete cell triplets from PowerShell, run the
corresponding command from the distribution's `bin` directory. Conversion checks the directory
before replacing files and preserves identical binaries and their timestamps.

```powershell
.\PZWorldEd.exe --export-lot-sources="C:\Maps\Lots"
.\PZWorldEd.exe --import-lot-sources="C:\Maps\Lots"
```

**Recalculate navigation when importing .source files** derives initial
level-zero collision from edited placements, room rectangles and TileDefs.
It preserves manual navigation corrections and partial export coverage.
Use the definitions from the original export, including mods. In TileZed
Preferences, place **Additional Tile Property Files** in the game's mod and
`tiledef=` order. Put each patch after the definition it changes. WorldEd
uses discovered regular definitions in the selected directory, the explicit
mod list in order, then remaining discovered patches. Selecting a text
definition explicitly takes precedence over its binary companion.

Keep the following files alongside the three sources when sharing or
rebuilding them.

| File | Used for |
|---|---|
| `chunkdata_X_Y.bin.source.baseline` and `.source.previous` | Original automatic and previously exported navigation. Older sources without these references require the original binary triplet and matching TileDefs for the first recalculation. |
| `chunkdata_X_Y.override.png` | Explicit corrections, including opaque zero. Navigation-text edits and opaque correction pixels survive recalculation. An additional project mask takes precedence at opaque pixels. |
| `chunkdata_X_Y.bin.source.dependencies` and neighboring `.lotheader.source.context` files | Rooms intersecting the output cell, including buildings owned several cells away. Keep the referenced neighboring headers. Text headers take precedence over binaries. Header-only neighbors supply context. |

Missing TileDefs or required neighbor headers stop the operation before
replacement. Recalculation updates navigation, its text source, both
references and the saved correction mask.
`--source-tiledef` accepts a directory, binary file or `.tiles.txt`. Repeat
`--source-override=x,y::path.png` to supply masks for additional output cells.

```powershell
.\PZWorldEd.exe --rebuild-lot-source-navigation="C:\Maps\Lots" --source-tiledef="C:\Game\media\tile_definitions" --source-tiledef="C:\Mods\Tiles\42\media\custom.tiles.txt" --source-override="17,54::C:\Maps\Masks\pump.png"
```

Generate Lots also offers **Bypass jumbo rules (preserve placed trees)** for
Legacy300 and Native256 projects. Enable it to export trees as placed,
including XL and XXL tiles. The PZW retains the setting. For a missing TMX
that is the only content of an otherwise empty source cell, WorldEd offers
an undoable cleanup after confirmation and saves a dated PZW snapshot below
`.pztools-backups`. [Jumbo tree rules](PZ-B42.20-Jumbo-Trees.md) ·
[Project Doctor backups](PZ-Project-Doctor-Tiles-and-Paths.md#backups-and-recovery)

### Partial chunk LOT export

Open a Native256 cell TMX in TileZed or open its cell view in WorldEd.
Enable **Partial Chunks** from its menu or toolbar. Each of the 32 × 32
grid squares covers one 8 × 8 game chunk.

- Click to toggle a chunk. Drag a rectangle to include or omit an area
  according to the starting chunk. The preview applies on release.
- While the mode is active, **Ctrl+A** includes all 1,024 chunks.
  **Clear Chunk Selection** omits all of them. Included chunks are tinted
  with the grid color and omitted chunks appear darker.
- Disable the mode for ordinary editing. Enable it again before generating
  a partial LOT. Keep `map-name.tmx.pzchunks` beside the TMX when moving
  or sharing the map. A map without an enabled sidecar exports a full cell.
- **How Partial Chunks Works...** in the menu opens the in-app guide.

The mask restricts LOT tiles, RoomDefs, room objects, tileset usage and
zombie intensity. **Overwrite SpawnMap** gives omitted chunks zero intensity.
An included chunk can be empty. An omitted chunk exports as absent, with
null navigation, even if its TMX contains tiles. Hole Detection and
automatic hole filling are disabled for that cell while the mask is enabled.

### Native256 chunk-data overrides

Select one Native256 cell and choose **Tools > Terrain & Environment >
Native256 Chunk Data Overrides**. Each pixel in its 256 × 256 RGBA PNG
represents one square at level zero. Leave pixels transparent for automatic
generation. Paint opaque pixels to override a value, or right-drag to
restore transparency. Set green and blue to zero and store the combined
flags in red.

| Red value | Meaning |
|---:|---|
| 0 | Explicitly clear all five flags |
| 1 | Solid square |
| 2 | Block the north edge |
| 4 | Block the west edge |
| 8 | Water |
| 16 | Inside a room |

Add the values to combine flags. For example, use 17 for solid and inside a
room, or 22 for both wall edges and inside a room. Automatic generation
accounts for stairs, water and trees, explicit `collideN` and `collideW`,
door frames, windows and hoppable edges. Omitted partial chunks always
remain absent. The PNG changes initial ground-level navigation metadata.
The game recalculates flags when the actual chunk loads or a square changes.

Keep the PNG at the path recorded in the PZW. Moving or copying a cell
preserves this reference. Generate Lots checks attached PNGs and reports
the cell and reason if the file, dimensions or channel values are invalid.

### Play Map Source

1. Save a Native256 PZW with an assigned TMX, placed TBX lots, or both.
   Set the mapping **Tiles** directory in WorldEd Preferences. The supported
   game is the original Windows Project Zomboid 42.22.2 installation with
   base-game tiles. If prompted, select the folder containing
   `ProjectZomboid64.json`, `projectzomboid.jar` and `jre64`.
2. In World View select the cells to visit, or press **Ctrl+A** for the
   entire project. Choose **File > Play Map Source...**. In Cell View the
   command previews the open cell.
3. Keep WorldEd open and complete any first-launch prompts in the game.
   The solo session starts in the first selected cell with a walkable
   authored square. A valid SpawnPoint there takes precedence. Exit the
   game to finish.

To try the supplied example from a PowerShell prompt in the distribution's
`bin` directory, use the following command. Each `--preview-cell` takes
nonnegative project-local coordinates and can be repeated.

```powershell
.\PZWorldEd.exe --play-map-source "..\tools\DirectMapPreview\example\DirectPreview.pzw" --preview-cell 0,0
```

WorldEd streams terrain, buildings and rooms into the game in memory, using
its embedded agent and the game's Java runtime. Selected authored cells
replace the corresponding base-game cells for the session. The game can
fill unpainted ground in sparse cells procedurally. Outside the preview
cells, the game uses its own map.
Exiting removes the temporary game profile. Use normal map export to test
exported zones, custom Lua or compiled chunk-data overrides.

### Cell View vehicle previews

Set the game installation in WorldEd Preferences, open a cell and enable
**View > Show Vehicle Mesh Previews**. ParkingStall and rectangular or
polyline TrafficJam zones use their vehicle distributions. Sparse stalls
can legitimately stay empty. The preview keeps each zone's model, position
and heading stable while editing. In-game spawning also depends on sandbox
settings and random choices.

In **Preferences > Vehicles**, **Vehicle preview size** changes display
scale around the zone anchor (0.25× to 4×, default 1×). **Vehicle preview
render quality** changes resolution and memory use (0.25× to 4×, default
2.5×). Effective quality rises to the preview size when necessary. **Rebuild
Atlas** regenerates 16 directions from the selected game's zone definitions,
vehicle scripts, meshes, wheels and textures. The cache is stored under
`settings/cache/vehicle-preview-atlas` by installation assets and quality.
Cancellation keeps finished sprites. Older eight-direction atlases use the
nearest stored direction until rebuilt.

### Building lots and basement access

Right-click a selected building lot and choose **Vertical Placement** to
see its source floor range and resulting world levels. Confirm the offset
to move the entire lot, including floors, rooms, stairs and collision. A
source with floors already below zero normally stays at lot level 0.
Dragging a lot to a different level group also requires confirmation.
Generation reports placements outside the supported world range.

For an entrance preview, put editable TBX or TMX sources under
`pzby_tbx/basement_access` beside `bin`, and compiled PZBY files under
`pzby_tbx/binmap`. Right-click the **Basement** zone with
**Select** or **Create Object** and choose **Choose Basement Access...**.
WorldEd displays the selected source aligned to the trace origin. It picks
up files added to either directory during the session.

### Spawn points and Lua zone export

Use **Create Object** to choose the group and type. Right-click the zone
with **Select** or **Create Object** to edit its relevant fields. The
Properties dock exposes the full set.

| Object | Creation and export |
|---|---|
| SpawnPoint | New points use `unemployed`. The exporter maps the older `Professions = all` to `unemployed` and preserves other explicit choices once per profession. Valid rectangular points retain their original position and level, including underground positions. |
| WaterFlow | Starts as one square with `WaterDirection = 0` and `WaterSpeed = 0.0`. |
| WaterZone | Starts with `WaterGround = false` and `WaterShore = true`. Larger valid water areas retain their dimensions. |
| RoomTone | Starts as one square with `RoomTone = Generic`. Its origin selects the room. Set `EntireBuilding = true` to cover the whole building. |
| SpawnOrigin | Use a nonempty rectangle at nonnegative whole map-square coordinates. The type has no runtime properties. |

The world origin plus the local cell coordinate, multiplied by cell size,
gives the absolute start of a cell. WorldEd adds the point's local offset.
For example, Native256 world cell `17,54` and local square `31,123`
produce `{ posX = 4383, posY = 13947, posZ = 0 }` at ground level.
`spawnpoints.lua` keeps the `SpawnPoints()` function and profession lists.
Invalid zone records are reported and omitted from Lua. Correct the
geometry or properties in the saved PZW, then save or export again.
In the Regions editor, `NoGas`, `NoPower`, `NoWater` and `NoPowerOrWater`
are utility zones without additional properties.

### Fishing zones

In Cell View at level 0, choose **Create Object**, select `NoFishZone` or
`RiverZone` and the matching type under **New Object Defaults**, then draw
a rectangle. Click for a single square. `NoFishZone` blocks rod casts aimed
inside it. `RiverZone` selects the river habitat for rod catches. Vanilla
42.22.2 has the same eligible species in both habitats. Fishing nets have
separate placement and catch rules, and clicking inside a no-fishing zone
can hide the menu for an existing net.

Choose **File > Export Fishing Zones**, select the mod folder and enter the
map folder name. WorldEd writes valid zones from all cells with the project
origin and grid size applied. The output is
`common/media/lua/shared/<Map Name>/WorldEdFishingZones.lua` inside the mod.
**File > Export Complete Mod (8x8)** includes the same script. Re-export
after editing or deleting the last zone. Keep custom Lua in a separate
file because export replaces the generated script.

### World-map XML overlays

Use **View > World Map Overlays** to view read-only references in World
View. Loading `worldmap.xml` also loads an adjacent `worldmap-forest.xml`
if present. Show, hide or clear each independently. Forest appears green.
The overlay uses absolute map-square positions on either project grid.
Older overlay XML defaults to 256-square cells, with clear 300-square
geometry detected from local coordinates.

To replace the project's **editable** features instead, choose
**InGameMap > Read Features XML**. The replacement supports Undo. On a
read error the previous features and file selection are retained. This
import follows the open project's grid when the input omits `cellSize`.
**Write Worldmap** and **Write Worldmap-Forest** write XML and binary data
on a 256-square grid and split features at output boundaries. For complex
roads, the exporter first simplifies shapes, then omits the smallest
internal fragments if required by the renderer's index limit. The log
lists affected cells and road classes. The editable source remains intact.

Use **InGameMap > Generate Building Features** for surface outlines from
placed TBX lots or RoomDefs embedded in TMX. Regenerate and export after
changing the source geometry. For **InGameMap > Create World Image**, use
the packaged `MapToPNG.txt` color rules. The annotation editor opens from
**InGameMap**, its toolbar button or **Ctrl+Alt+A**.
[InGameMap feature generation](TileZed/PZToolsGuide.html#features)

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

- [Automapper](TileZed/Automapper.html)
- [TileSetZ](TileSetZ.md)
- [Procedural loot](PZ-B42.20-Procedural-Loot-Editor.md)
- [Pack tools](PZ-Pack-Comparator-and-Extractor.md)
- [TileDef tools](PZ-TileDef-Comparator-and-Snow-Editor.md)
- [Lua scripting](TileZed/LuaScripting.html)
- [BMP tools](TileZed/BMPTools.html)

### Selection and tile tools

With **Select Tiles**, choose the layers and levels to copy or cut.
**Ctrl+V** starts a translucent placement preview. Click to place it.
Current-layer, current-level copies go onto the selected destination
layer. Wider scopes preserve the source layer names and relative levels,
even when only one layer contains tiles. In the Layers list, use **Check
All** or **Uncheck All** to change visibility. Each map retains its own
visibility settings while switching tabs.

Right-click a tile in the tileset grid to open its defining TileDef at
the exact index. The TileDefs editor reads `.tiles` and `.tiles.txt` and
saves both. Automatic discovery prefers the binary when both exist.
Select the text file explicitly to edit from the companion. **Edit >
Review Unknown Properties...** removes only the names checked, on all
tiles in the open file. Leave mod keys you need unchecked. The removal
supports Undo. [Checking a property catalogue](PZ-TileDef-Comparator-and-Snow-Editor.md#checking-a-property-catalogue)

**Depth Map Editor** uses blue for near (0) and red for far (255).
Start with a ground or vertical estimate, fit a box to the sprite, drag
it along the ground, and use **Shift+drag** to raise it. Inspect the live
volume preview, then use **Generate All** to apply it to the depth image.
In **Create .pack**, **Apply** saves the current output path without
building a pack. Pack profiles with their own TileDef list use that
order for additions. [Pack tools](PZ-Pack-Comparator-and-Extractor.md)

Tilesheets are resolved recursively with readable 2× sheets preferred,
including nested packs. A readable 1× or custom sheet supplies a missing
scale. A loose PNG at the root of `2x` can override the extracted pack
copies. The editors retain ordered TMX headers and referenced sprite IDs
for older maps and neighboring cells. A red missing-source marker means
the sheet could not be resolved. A crossed eye in BuildingEd marks a
transparent or explicitly invisible tile. Use the `invisible` TileDef
property on intentional transparent placeholders for **Check Maps**.
[Tileset resolution](PZTools-Configuration-Files.md#tileset-resolution)

### Tile palettes in TileZed and BuildingEd

Click **Tile Palettes** on the toolbar to open a floating thumbnail grid.
In BuildingEd first open a building in Tile mode. Hover over a thumbnail
to see its tileset and local tile number. Click an entry to use it as a
brush. In TileZed, choosing a tile from an installed sheet absent from the
current map adds the sheet to that map as an undoable action.

- **Favorites** holds named palettes. Choose **New Palette**, select one
  tile in the editor and click **Add current tile**. You can also
  right-click a tile in the tileset grid and choose **Add to Tile Palette**.
- **Recently Used** holds the last 32 distinct tiles actually placed with
  a single-tile brush, newest first. Select an entry and choose **Add to
  favorites** to keep it. Recording starts when the window is first opened
  and continues while hidden. Clear **Record recently used tiles** to pause.
- TileZed and BuildingEd keep separate palettes. **Export** writes the
  selected palette to JSON. **Import** opens it in either editor and gives
  a duplicate palette name a numbered suffix. The JSON stores sheet names
  and local tile IDs. Install the matching PNG sheets on the destination
  computer.

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

### BuildingEd editing and inspection

TBX stores reusable buildings with floors starting at local level 0. A
cell TMX can also contain a building directly. Use the lot's vertical
placement in WorldEd to position a building below ground.
[Building lots and basement access](#building-lots-and-basement-access)

| Task | Gesture or command |
|---|---|
| Pick a room or remove an object | In Iso, right-click with **Pencil** to pick a room. With **Furniture** or **Roof**, right-click removes that object. **Shift+right-click** opens the context menu in isometric modes. |
| Reuse a tile stamp | In Tile mode, right-drag with **Draw Tiles** to copy a rectangle. A single right-click releases the stamp. |
| Inspect a stack of tiles | In Tile mode open **View > Views > Tile Layers at Cursor**. **Shift+click** with **Pick Tiles** holds the square. Double-click a row to activate its layer and reveal its tile. |
| Find a transparent gap | Turn on **View > Gap Check Background** in Iso, Tile or Attributes mode. Hide lower floors or backing layers if they cover the opening. Exposed gaps show magenta. |
| See behind walls | **View > Show Walls** or **Ctrl+Shift+W** toggles walls, trim, grime, roof caps, frames, windows, curtains and doors in Iso, Tile and Attributes modes. Furniture remains visible. The toggle preserves individual layer settings. |
| Hold a room highlight | Use **Pin Room Highlight** in the context menu or **Ctrl+Shift+H**. Use **Release Room Highlight** to follow the pointer again. Changing floor, resizing or rotating releases the pin. |

Four independent controls in **Edit > Preferences > Editing** start off.

- **Enable furniture copy and paste in Ortho and Iso** enables group
  placement in the same building. Select furniture with **Select and Move
  Objects**, press **Ctrl+C**, switch floors if needed, then press
  **Ctrl+V**. Click once per room to place another copy. Right-click to
  finish. Each placement has its own Undo. **Ctrl+drag** duplicates a
  selection on its current floor.
- **Show the tileset palette when picking a tile** shows a hidden palette
  and clears a filter if it hides the selected sheet. **Pick Tiles** also
  finds visible pixels of tall and custom sprites.
- **Show wall and trim variant details in tooltips** displays directional
  tiles or offsets for similar entries.
- **Allow Lua scripts to register Floor Grime brushes** enables
  `app:registerFloorGrime` for the current session's brush materials.
  [BuildingEd manual and Lua scripting](../DOCUMENTATION.md#buildinged)

Use **All Layers** when copying or cutting a room layout and its RoomDefs
with the painted tiles. **Current Layer**, **Visible Layers** and **Specific
Layers** operate on the selected explicit tile layers. Paste without a
destination selection to preview placement in Ortho or Iso. A paste can
expand the building up to 300 × 300. If a paste would exceed that limit,
BuildingEd asks before cropping the out-of-bounds clipboard content. One
Undo restores the prior state. Resizing an existing building or TMX map
also asks before cropping content.

Building rotations and mirrors move painted tiles, square attributes and
selections together. Directional artwork keeps its sprite IDs and may
need adjustment after the transform. **Building > Remove Tilesheet from
Building** removes the selected sheet from the open TBX, including room
materials, furniture, object and painted-tile references. Use Undo to
restore it. Shared catalogues remain available to other buildings.

When a TBX is placed on a TMX, the initial run of TMX layers beginning
with `Floor`, including `FloorOverlay` in that run, shows beneath the lot
until covered by a TBX floor. Floorless walls, finishes, doors, windows
and frames can leave underlying vegetation visible. Furniture on the
same square can cover it. `AboveLot` layers draw over the building.
[BuildingEd tools and tile-mode manual](../DOCUMENTATION.md#buildinged)

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
