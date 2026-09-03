# PZTools troubleshooting FAQ

This FAQ covers the user-visible warnings and errors most commonly shown by
PZWorldEd, TileZed, and BuildingEd. Find the dialog title or the first line of
its message below.

An error normally cancels only the operation named in the dialog. It does not
mean that the whole project is damaged. Keep the source file, read the complete
message, and correct the path, input, or configuration before trying again.

## First checks for all three applications

### `VCRUNTIME140_1.dll` is missing or the application does not start through Wine

**What it means:** the application directory is incomplete or contains a
mixture of compiler-runtime versions from different releases.

**What to do:** restore the complete current portable release and launch the
executable from its `bin` directory. The release includes its own Microsoft
Visual C++ x64 runtime, so a separate Visual C++ Redistributable installation
or Wine `vcrun` component is not required.

**How to prevent it:** move or extract the complete release directory instead
of copying individual executables and DLLs. On macOS, Wine or CrossOver runs
the Windows build. It is not a native macOS application bundle.

### Where are the logs?

Each application creates a new log in `settings/logs` inside the portable
PZTools directory. Use the newest log whose name starts with the application
that displayed the error. See [Logs and useful issue reports](Diagnostics-and-Logs.md)
for the file names and the information to include in a report.

### Which directories must be configured?

- **Configuration directory** contains files such as `Tilesets.txt`,
  `BuildingTMX.txt`, `BuildingTiles.txt`, `FurnitureGroups.txt`, and
  `BuildingTemplates.txt`.
- **Tiles directory** contains the extracted mapping PNG sheets, either
  directly or in supported `1x`, `2x`, and custom subdirectories.
- **Project Zomboid installation** is optional. It provides game data such as
  TileDefs, packs, WorldGen data, and procedural loot. It does not replace the
  extracted mapping Tiles directory.

Set these paths in Preferences, then restart the application after changing a
core configuration or Tiles path.

### What do red question-mark tiles mean?

The TMX or TBX references a tileset name for which no readable PNG was found.
The project data is still present, but the source image is unresolved.

**What to do:** open Preferences and verify the shared Tiles directory. Check
that the expected PNG exists in the correct subdirectory and that it opens as
an image. In WorldEd, Project Doctor can remove references that no longer
resolve to a PNG.

**How to avoid it:** move the complete Tiles tree with a project and do not
rename PNG sheets after they have been referenced. Do not remove a tileset only
because it is currently unused if another project still needs it.

### What does the crossed-eye tile mean?

It is the diagnostic display for a valid fully transparent tile while **Show
Invisible Tiles** is enabled. It is not a missing tileset and does not need to
be repaired.

### Why does an application say that a file cannot be opened, read, saved, or written?

Titles include `Open Failed`, `Error Opening Map`, `Error Reading World`,
`Error reading building`, `Error Saving Map`, `Error Saving Building`,
`Save Failed`, and `Error writing file`.

**Why it appears:** the path no longer exists, the file is not in the expected
format, another program has locked it, the destination is read-only, or the
disk does not have enough free space. The detailed text normally names the
file and the parser or operating-system error.

**What to do:** verify the named file, copy it to a writable local directory,
close other programs that may hold it, and retry. If the message reports an XML
line, fix that line in a copy or restore a known-good backup.

**How to avoid it:** keep projects in a writable directory, let the application
finish saving before synchronization software moves the file, and retain
autosaves or versioned backups.

### What do `Configuration Error` and the configuration-specific titles mean?

Related titles include `Tileset Configuration Error`, `Building Configuration
Error`, `Building Tiles Error`, `Furniture Configuration Error`, `Building
Templates Error`, `Tile Properties Error`, and `Tileset Metadata Error`.

**Why it appears:** a required text file is absent, unreadable, malformed, or
refers to data that cannot be resolved. The message identifies the file being
read or written.

**What to do:** point Preferences to the packaged `config` directory. Restore
the named file from the same PZTools release if it was edited incorrectly. A
custom file should be compared with the packaged version before being restored.

**How to avoid it:** keep configuration files from one release together. Do
not mix a new executable with an incomplete older `config` directory. Back up
custom configuration before editing or updating it.

### What does `Invalid Project Zomboid Installation` mean?

**Why it appears:** the selected directory is neither the Project Zomboid
installation root nor its valid `media` directory.

**What to do:** browse to the installed game directory or its `media`
subdirectory. Clear the optional path if game data is not needed.

**How to avoid it:** select the installed game, not a Workshop mod, save folder,
or mapping project.

### Why is an image rejected?

Titles include `Error Loading Image`, `Invalid Images`, `Invalid Image Size`,
`Size Mismatch`, `Image Export Error`, and `Error Saving Image`.

**Why it appears:** the image is missing, damaged, encoded in an unsupported
form, has the wrong dimensions, or cannot be written to the destination.

**What to do:** open the file in an image editor, save it as a normal PNG, and
verify the dimensions required by the dialog. Paired map images must cover the
same area and have identical dimensions.

**How to avoid it:** keep one pixel per map square, use lossless PNG input, and
do not independently resize paired ground, vegetation, zone, or heatmap files.

### What should I do after an unexpected application closure?

Do not overwrite the original file while investigating. Restart the standalone
application from the release `bin` directory, check the Welcome page for an
autosave, and reproduce the shortest possible sequence once. Keep the newest
matching application log and the smallest source file that reproduces the
problem.

## PZWorldEd

### `Error Reading World`

**Why it appears:** the selected PZW could not be parsed. The detailed error
usually identifies invalid XML, an unsupported value, or a missing referenced
file.

**What to do:** open a backup of the PZW. If an XML line is reported, correct
that line only in a copy. Also verify that assigned TMX and TBX paths still
resolve relative to the project.

**How to avoid it:** save through WorldEd, keep the PZW with its project
folders, and avoid manual XML editing without a backup.

### `Defaults File Error`

**Why it appears:** the WorldEd defaults file is missing or malformed.

**What to do:** restore the packaged defaults file from the same release and
verify the configured application or configuration directory.

**How to avoid it:** do not replace individual configuration files with files
from unrelated releases.

### `TileZed not found` or `Unable to open map`

**Why it appears:** WorldEd cannot find `TileZed.exe` beside
`PZWorldEd.exe`, or TileZed could not be started with the selected TMX.

**What to do:** restore the complete portable release so both executables are
in the same `bin` directory. Verify that antivirus software did not quarantine
an executable, then open the TMX directly in TileZed to see any map error.

**How to avoid it:** distribute and update the complete `bin` directory rather
than copying a single WorldEd executable into another installation.

### `Lot Generation Error` before generation starts

**Why it appears:** a required input is absent. Common cases are an invalid LOT
output directory, no Zombie Spawn Map, no directory containing
`newtiledefinitions.tiles`, an invalid custom hole-fill tile, or incomplete mod
export information.

**What to do:** use the path named by the message. For a custom hole fill,
enter a complete tile name such as `blends_natural_01_0` and make sure its PNG
is available. For mod export, provide an existing parent directory, a mod name,
a valid mod ID, a valid map folder name, and an existing PNG poster when one is
selected.

**How to avoid it:** keep export presets inside the project workflow and verify
the Zombie Spawn Map and TileDefs path after moving a project to another
computer.

### `Lot Generation Failed!`, `Mod Export Failed!`, or `Overwrite SpawnMap Failed!`

**Why it appears:** validation passed, but a TMX, tileset, LOT file, header,
spawn map, or destination failed during generation. The detailed message is
the useful part of the error.

**What to do:** keep the source project unchanged, read the named file and cell
in the message, then correct that input. Run Project Doctor if the failure
mentions unresolved tilesets or invalid paths. Confirm that the export
directory is writable and has free space.

**How to avoid it:** resolve Project Doctor errors before a release export and
do not generate into a directory currently being synchronized or used by the
game.

### Why does Hole Detection report many holes?

Hole Detection treats a square as filled when a real tile is present. It does
not require the tile to have `solidfloor`. Water and custom building tiles are
valid coverage when they actually occupy the square.

**What to do:** inspect unexpected empty squares. During LOT generation,
enable automatic hole filling and choose the configured fallback or a valid
specific tile if the empty squares should be repaired on the generated map.

**How to avoid it:** do not leave accidental empty ground squares between
terrain operations. If intentional absence is required in a Native256 cell,
use Partial Chunks instead of filling the omitted chunks.

### `Hole Repair Failed` or `No detected hole could be repaired`

**Why it appears:** the selected repair mode has no valid fill tile, the target
map is unavailable, or the scan no longer contains a repairable empty square.

**What to do:** rescan, verify the assigned ground TMX, and select an available
tile. Save the repaired TMX before LOT generation.

**How to avoid it:** keep the assigned cell maps writable and rescan after any
manual terrain change.

### `Partial Chunks is supported only by Native 256 projects`

**Why it appears:** Partial Chunks was enabled for a legacy 300-square project.

**What to do:** use normal whole-cell export for that project. Partial Chunks
is available only when the project grid is Native256.

**How to avoid it:** choose Native256 when creating a project that is intended
to export selected 8 x 8-square chunks.

### `Map Generation Error` or `BMP To TMX Failed!`

**Why it appears:** BMP To TMX is missing `Rules.txt`, `Blends.txt`, the map
template, source images, or a writable output folder. A later failure can also
identify an invalid rule, color, TMX, or tileset.

**What to do:** choose the packaged terrain `Rules.txt` and `Blends.txt`, the
correct MapBase template, and the matching terrain images. If the output
folder does not exist, choose **Create Directory** or select another folder.
Use Validation and Repair for unknown bitmap colors when appropriate.

**How to avoid it:** keep all terrain source images at identical dimensions,
retain the project Rules and Blends files, and generate into a dedicated
writable directory.

### `Export Location Is Not a Folder`, `Create Export Directory?`, or `Could Not Create Export Directory`

**Why it appears:** the entered export path names a file, does not exist, or
cannot be created.

**What to do:** choose another folder or allow WorldEd to create the missing
directory. If creation fails, select a writable parent directory.

**How to avoid it:** use the browse button rather than typing a path and create
project export folders before batch generation.

### `TMX To BMP Failed!`

**Why it appears:** a TMX could not be read, did not contain the required map
data, or the output images could not be created.

**What to do:** open the TMX in TileZed first, resolve its missing tilesets, and
select a writable image output directory.

**How to avoid it:** keep the complete TMX tileset header and do not remove
embedded BMP metadata needed by the conversion workflow.

### `Error reading from-to file`, `Alias Fixup Error`, `From/To Failed`, or `Error writing TMX`

**Why it appears:** a FromTo definition is missing required fields, names an
unknown tileset or tile, contains no definitions, or its target TMX cannot be
written.

**What to do:** correct the exact line reported, add the required tileset
through TileZed when its PNG exists, and run the operation on a writable TMX
copy.

**How to avoid it:** keep FromTo files with the tileset configuration they were
written for and validate every referenced tile after renaming a sheet.

### `Tilesets.txt Update Failed` or Project Doctor cleanup failure

**Why it appears:** Project Doctor found a repair, but could not back up or
atomically update the tileset catalog or a TMX.

**What to do:** keep the scan results open, make the project and configuration
directories writable, close the affected TMX in other applications, and retry.
No source should be considered repaired until the success result is shown.

**How to avoid it:** let Project Doctor create its backup before cleanup and do
not run cleanup inside a read-only release or game directory.

### `Unable to Load World Map Overlay`

**Why it appears:** the selected `worldmap.xml` or `worldmap-forest.xml` is
missing, malformed, or does not contain readable overlay geometry.

**What to do:** select the XML file rather than its `.bin`, validate the XML,
and reload it from **View > World Map Overlays**.

**How to avoid it:** keep read-only overlay XML files separate from compiled
binary output and do not rename a binary as XML.

### `PNG Generation Failed` or thumbnail recreation errors

**Why it appears:** WorldEd cannot load a source map, initialize the thumbnail
renderer, allocate the requested image, or write the PNG.

**What to do:** close open cell documents to release memory, resolve missing
tilesets, select a writable output path, and retry. Restart WorldEd if the
message explicitly recommends it.

**How to avoid it:** regenerate thumbnails after the complete tileset catalogue
has loaded and avoid creating very large previews while multiple large cells
are open.

### `Zone Export Error`, `Zone Import Error`, or invalid property definitions

**Why it appears:** a zone image or definitions file contains an unknown type,
invalid color, missing required name, invalid enum, or malformed line.

**What to do:** correct the line, zone type, or property named by the message
and retry with a writable destination.

**How to avoid it:** use the editor's defined object and property types and
keep custom definitions with the project that uses them.

### `Error saving spawnpoints`, `Error saving objects`, or `Error saving RoomTone objects`

**Why it appears:** the related Lua output path is invalid, locked, or
read-only.

**What to do:** select or restore a writable project output directory and close
other programs using the Lua file.

**How to avoid it:** keep generated Lua outputs inside the writable project and
deploy copies only after WorldEd finishes saving.

### `Invalid zones skipped` or `Saved with invalid zones skipped`

**Why it appears:** WorldEd found a SpawnPoint, WaterFlow, WaterZone, or
RoomTone record that the Build 42 Lua loader cannot use safely. The detailed
list identifies missing properties, unsupported SpawnPoint profession `all`,
non-integral coordinates, an invalid geometry type, or an incorrect size.

**What to do:** open each listed object, correct its geometry and properties,
then save or export again. WaterFlow and RoomTone must be 1 x 1 rectangles.
WaterZone can cover a larger rectangle but requires boolean `WaterGround` and
`WaterShore`. SpawnPoint requires one or more explicit professions.

**How to avoid it:** create these records with their matching object group or
dedicated placement tool. WorldEd then supplies valid default properties.
Invalid records are omitted from the Lua file, but remain in the PZW and do
not disable LOT generation.

### Biomemap messages

| Message | Why it appears | What to do | How to avoid it |
|---|---|---|---|
| `Missing Input` | Map, vegetation, selected zone source, or output path is empty. | Select `Map.png`, `Map_veg.png`, the zone source, and an output directory. | Keep all source layers together. |
| `Size Mismatch` | Map and vegetation images have different dimensions. | Resize or restore the incorrect image so both cover the same squares. | Export paired layers together. |
| `Invalid Biomemap Size` | Dimensions are not exact multiples of the project cell size. | Use multiples of 256 for Native256 or the project cell size shown by the message. | Do not crop a layer independently. |
| `Biomemap Size Does Not Match Project` | WorldEd zone rasterization requires complete project coverage. | Supply images matching the full project width and height. | Create full-project layers before selecting WorldEd zones as the source. |
| `Invalid Zone Layer` | The zone image is missing or does not match `Map.png`. | Select the correct zone PNG or rasterize zones from the project. | Keep the zone layer aligned with the other images. |
| `Invalid Foraging Zone IDs` | The green channel contains values not mapped to a zone. | Repaint those values with supported zone IDs. | Use the generator's green-channel mode and the Build 42.20 Biomemap reference. |
| `Biomemap Validation Warnings` | Red values are unknown, have no effect, require an override, or green chunks contain mixed IDs. | Review every listed value before choosing Continue. Correct unintended values instead of accepting them. | Paint with supported IDs and keep one green zone ID per 8 x 8 chunk. |
| `Output Error`, `Generation Failed`, or `Save Failed` | The destination cannot be created, processed, or written. | Select a writable output folder with sufficient free space. | Generate outside protected and synchronized directories. |

### `Terrain Images Exceed the Maximum Limit` or `Increase Terrain Image Memory Limit?`

**Why it appears:** the ground image, vegetation image, preview, and editing
headroom exceed the configured memory limit. The maximum configurable limit is
64 GiB. Extremely large raster requests cannot be made safe by changing one
setting.

**What to do:** if the suggested value fits the computer's available RAM,
choose **Increase and Continue**. Otherwise reduce the project dimensions,
use a wider real-world scale, or split the work into smaller images.

**How to avoid it:** estimate image dimensions before generation and avoid
keeping other large map documents open while editing very large terrain
images.

### OpenStreetMap location and connection messages

| Message | Why it appears | What to do | How to avoid it |
|---|---|---|---|
| `Location Required` | No place, coordinates, or supported map link was entered. | Enter a place name, latitude and longitude, or an OpenStreetMap or supported map link. | Use **Open Map** to locate the area and paste the centered link or coordinates. |
| `Place Not Found` or `Coordinates Not Found` | The search was too broad or the shortened link did not expose coordinates. | Add a country or state, paste numeric coordinates, or paste the full centered map URL. | Prefer full links over shortened redirect links. |
| `Place Search Rate Limit` | Searches were sent less than one second apart. | Wait one second and retry. | Do not repeatedly press Find while a request is pending. |
| `Place Search Failed` or `Map Link Could Not Be Resolved` | The geocoder, redirect, or network request failed. | Retry later, paste coordinates, or select a valid HTTPS search endpoint. | Keep a copy of the coordinates for large projects. |
| `HTTPS Is Unavailable` | The portable runtime cannot initialize TLS. | Restore the OpenSSL runtime supplied with PZTools and restart WorldEd. | Keep the complete portable runtime together. |
| `Invalid Overpass Endpoint` | The endpoint is not a valid HTTPS Overpass API URL. | Reset the importer parameters or enter a valid HTTPS endpoint. | Keep the packaged endpoint list unless a known compatible service is required. |
| `OpenStreetMap Download Failed` | Every configured Overpass attempt failed. HTTP 429 means rate limiting. HTTP 502, 503, and 504 usually mean a busy or timed-out public server. | Let automatic waits and endpoint retries finish. Then retry later, reduce the margin, disable unneeded layers, or select a smaller area. | Reuse the cache and avoid repeatedly downloading the same large area. |
| `No Clear Road Grid Found` | The downloaded roads do not provide a confident dominant orientation. | Set the rotation manually using the preview. | Use a smaller urban area with a consistent street grid for automatic detection. |

### `Invalid Project Area`, `Project File Required`, `No OSM Layers`, or `OSM Import Too Large`

**Why it appears:** the requested cells extend beyond an open project, a new
project has no PZW destination, no source layer is selected, or the output
exceeds the fixed 268-million-pixel technical limit.

**What to do:** reduce or move the selected area, select at least one OSM
layer, choose the new PZW path, or use fewer cells or a wider scale.

**How to avoid it:** review the cell count and final pixel dimensions in the
preview before downloading.

### `OSM Terrain Generation Failed` or `OSM Project Data Generation Failed`

**Why it appears:** the OSM response downloaded, but its geometry could not be
rendered or the PZW, TMX, TBX, streets, zones, or InGameMap project data could
not be written.

**What to do:** read the named output in the detailed message. Choose a new,
writable project directory and retry with fewer optional outputs if necessary.
The existing project is not replaced when atomic creation fails.

**How to avoid it:** create OSM projects in an empty writable directory, keep
free disk space available, and do not open generated files in another program
while generation is running.

### `In-Game Map Generation Error`

**Why it appears:** a cell TMX, placed lot, embedded RoomDef, or feature could
not be read while generating world-map features.

**What to do:** open the cell named by the error in TileZed, resolve its map and
tileset errors, save it, then regenerate the feature type.

**How to avoid it:** validate assigned TMX files before generating InGameMap
features and keep their relative paths valid.

### `In-Game Map Image Error` mentions `MapToPNG.txt`

**Why it appears:** **Create World Image** was given terrain `Rules.txt` or an
invalid MapToPNG color-rule file.

**What to do:** select the packaged `config/MapToPNG.txt`. It is different from
the terrain Rules file used by BMP To TMX.

**How to avoid it:** keep the packaged file at its default location and do not
reuse the BMP Rules path in the World Image dialog.

### `Unable to Export In-Game Map`, `Unable to Write Worldmap`, or `Unable to Write Worldmap-Forest`

**Why it appears:** XML or binary output failed validation or could not be
written. WorldEd validates the Build 42.20 signature, version, 256-square cell
size, and dimensions before replacing existing output.

**What to do:** select a writable output directory and regenerate the complete
pair or Forest bundle. Keep the detailed validation message with a report.

**How to avoid it:** move XML and matching `.bin` files together and never mix
the XML from one generation with the binary from another.

### The game reports `invalid format (magic doesn't match)` for `worldmap-forest.xml.bin`

**Why it appears:** the binary is not a valid Build 42.20 InGameMap binary,
belongs to another format or version, or no longer matches the XML beside it.

**What to do:** remove the invalid output from the mod copy and regenerate
`worldmap-forest.xml` and its `.bin` together with **Write Worldmap-Forest**.
Do not rename an unrelated binary to `worldmap-forest.xml.bin`.

**How to avoid it:** always deploy the complete output pair produced in one
operation and do not retain an older binary after replacing its XML.

### `No In-Game Map Export`

**Why it appears:** **Overwrite** was used before this project had a recorded
export path.

**What to do:** use the normal export command once and choose the destination.

**How to avoid it:** keep the project with its saved export preference after
the first export.

### `Unsupported Annotation File`

**Why it appears:** the selected Lua file uses `symbolsAPI:add...` calls but
contains no text annotations supported by the structured editor.

**What to do:** keep the original file unchanged and edit unsupported Lua
constructs manually. Use the editor only for supported translated or literal
text symbols.

**How to avoid it:** open `worldmap-annotations.lua`, not an unrelated gameplay
Lua file, and retain a source backup when combining manual and structured
editing.

### `Unable to Load regions.lua`, `Invalid Regions`, or `Unable to Save regions.lua`

**Why it appears:** the Regions editor cannot parse the Lua table, a region has
invalid dimensions or property values, or the destination cannot be written.

**What to do:** correct the region identified by the message in a copy, then
save to a writable file. Restore a known-good `regions.lua` if the table syntax
was changed manually.

**How to avoid it:** edit region names, dimensions, and properties through the
Regions editor and keep the generated Lua structure intact.

### `Unable to Load streets.xml`, `Invalid Street Names`, or `Unable to Save streets.xml`

**Why it appears:** the XML version, a point, street width, or destination is
invalid.

**What to do:** use streets XML version 1, correct the street named by the
message, and save to a writable path.

**How to avoid it:** use the Street Names editor for geometry and preserve the
file header when editing XML manually.

### WorldGen feature, prefab, or biome errors

Titles include `Invalid feature name`, `Invalid prefab name`, `Invalid biome
name`, `Could not save feature`, `Could not save prefab`, `Prefab import
stopped`, `Could not stage prefab`, and `Could not save biome`.

**Why it appears:** a name is empty or duplicated, referenced tiles are
missing, imported data has no valid placements, a required parent is invalid,
or the game data or project override destination cannot be written.

**What to do:** use a unique valid name, resolve every listed tile, select the
correct parent biome or feature, and verify the Project Zomboid and project
override paths.

**How to avoid it:** inspect imported prefabs before saving, keep official game
data read-only, and write map-specific changes to the project override.

## TileZed

### `Error Opening Map`

**Why it appears:** the TMX is missing, malformed, references invalid external
data, or was created in an unsupported map format.

**What to do:** read the parser detail, open a backup, and correct only the
reported XML or path. If the map opens elsewhere but not in TileZed, include
the complete TMX and log in a report.

**How to avoid it:** save TMX through TileZed, keep external TSX files with the
project, and avoid editing global tile IDs by hand.

### `Unknown File Format` or `Non-unique file extension`

**Why it appears:** an export name has no recognized extension, or more than
one exporter matches the selected extension.

**What to do:** choose a specific format in the Save dialog and use its normal
extension, such as `.tmx`.

**How to avoid it:** select the file-type filter before entering the export
name.

### `BuildingEd Not Found` or `BuildingEd Launch Failed`

**Why it appears:** `BuildingEd.exe` is missing beside `TileZed.exe`, blocked,
or cannot be started.

**What to do:** restore the complete release `bin` directory and check
antivirus quarantine. Start BuildingEd directly once to reveal any runtime or
setup error.

**How to avoid it:** do not deliver TileZed without the matching standalone
BuildingEd executable and runtime files.

### `Error launching WorldEd`

**Why it appears:** TileZed cannot find or start the matching WorldEd
executable.

**What to do:** restore the complete portable release and launch
`PZWorldEd.exe` directly from `bin`.

**How to avoid it:** update the three applications as one release.

### `Reload Rules Failed`, `Import Rules Failed`, `Reload Blends Failed`, or `Import Blends Failed`

**Why it appears:** the selected terrain Rules or Blends file is missing,
malformed, or refers to an alias or tile that cannot be interpreted.

**What to do:** read the line reported by the parser and compare the file with
the packaged version. Importing replaces the Rules or Blends snapshot embedded
in the current TMX. Save the TMX to keep the replacement.

**How to avoid it:** do not press Reload immediately after Import. Import
already applies the selected snapshot, identical imports are skipped, and
Reload is only for rereading a file that changed on disk.

### Why can old Rules and Blends projects become slow?

Older TMX files can retain obsolete embedded Rules and Blends snapshots and
tileset declarations whose PNG files no longer exist. Regeneration still has
to examine applicable metadata, and the cost depends on the map, brush area,
and computer.

**What to do:** import the current Rules and Blends once, save the TMX, and use
Project Doctor in WorldEd to remove only unresolved tileset references. Do not
remove valid but currently unused tilesets blindly.

**How to avoid it:** replace embedded snapshots instead of layering repeated
Import and Reload actions, and clean unresolved tilesets when migrating an old
project.

### `Error Reading Tileset`, `Invalid Project Zomboid Tileset`, or `Tileset Too Large`

**Why it appears:** a TSX or PNG cannot be read, does not follow Project
Zomboid sheet geometry, or exceeds the supported tile count or image limits.

**What to do:** open the PNG, verify tile dimensions and spacing, and create the
tileset from a supported sheet. Correct the source image rather than editing
generated tile indexes.

**How to avoid it:** use standard PZ tileset dimensions and test a new sheet in
TileZed before referencing it across maps.

### `Missing Tilesets` or unresolved sheets in a TMX

**Why it appears:** the TMX declares sheets that are absent from the configured
Tiles tree.

**What to do:** restore the PNG, correct the Tiles path, or remove only the
unresolved reference if the project no longer needs it. Keep the ordered TMX
tileset header intact for every sheet that still exists.

**How to avoid it:** keep custom PNG files in stable subdirectories and share
the same Tiles tree with collaborators.

### `Tile Properties Error`, `Invalid Tile Definitions`, or `Tile Definitions Error`

**Why it appears:** a TileDefs file, property file, enum, object type, or tile
reference is missing or malformed.

**What to do:** select the correct `.tiles` or property file, correct the line
and value named by the message, or restore the matching official file. Use the
repair action only after reviewing its proposed changes.

**How to avoid it:** do not mix TileDefs from different game builds and keep
custom definitions separate from official source data.

### `Automatic Mapping Error` or `Automatic Mapping Warning`

**Why it appears:** an Automapper rule, alias, layer, tile name, or map state is
invalid. A warning can also indicate a recoverable skipped rule.

**What to do:** read the complete rule and line detail, correct the rule file,
and verify every referenced layer and tileset before applying it again.

**How to avoid it:** test new rules on a small TMX copy and keep layer names
consistent with the project level convention.

### `Snow Rules Error`, `Error Reading Curbs.txt`, `Error Reading Edges.txt`, or `Error Reading Fences.txt`

**Why it appears:** the selected tool configuration is missing or contains an
invalid line, tile, or rule.

**What to do:** restore the packaged file or fix the exact line reported in the
message. Confirm that every referenced tileset is loaded.

**How to avoid it:** preserve the configuration syntax and update tile names
when custom sheets are renamed.

### Depth Map errors

Titles include `Depth Map Load Error`, `Depth Map Save Error`, `Depth Map
Geometry Error`, `Depth Map Geometry Mismatch`, `Tile Geometry Load Error`, and
`Tile Geometry Save Error`.

**Why it appears:** the atlas or `tileGeometry.txt` is missing, cannot be
decoded, contains geometry for the wrong tile, or cannot be atomically
replaced.

**What to do:** verify both files belong to the same tileset and game build,
correct invalid primitives, and save to a writable directory. Keep the old
atlas until both outputs have saved successfully.

**How to avoid it:** move the atlas and geometry file together and do not edit
their tile indexes independently.

### Pack comparator and extractor errors

Titles include `Error reading .pack file`, `Pack comparison failed`,
`Extraction failed`, `Export Tilesets Failed`, and `Error creating .pack
file`.

**Why it appears:** the pack is invalid or incomplete, the output directory
cannot be created, a texture cannot be decoded, or an output PNG cannot be
written.

**What to do:** select a valid Project Zomboid pack, choose a new empty writable
output directory, and make sure sufficient disk space is available. If only
one pack fails, keep that source file with the log.

**How to avoid it:** extract directly from an intact game installation and do
not modify a pack while it is being compared or unpacked.

### `Overlay Configuration Error` or container overlay file errors

**Why it appears:** the overlay configuration is malformed, references an
unknown tile or property, or cannot be read or written.

**What to do:** correct the entry named by the message and confirm that its
tileset is loaded before importing or exporting the overlay file.

**How to avoid it:** keep overlay configuration and custom tiles together and
validate changes before replacing the deployed file.

### `Tile Rearrangement Error`

**Why it appears:** the source or target tileset is missing, the mapping is
incomplete, two outputs conflict, or a required file cannot be written.

**What to do:** select valid source and target sheets, resolve every mapping,
and write the result to a new writable location before replacing project data.

**How to avoid it:** back up the tileset, TileDefs, and affected TMX files as
one set before changing tile indexes.

### `Could not load loot definitions`, `Could not load project loot`, or `Could not save project loot`

**Why it appears:** official procedural-loot data or the project override is
missing, malformed, from an incompatible build, or read-only.

**What to do:** verify the Project Zomboid installation path, correct the Lua
error shown by the message, and save project overrides into a writable project
location.

**How to avoid it:** read official game definitions from one game build and
keep editable map overrides separate.

### `Error Saving Image`, `Error creating directory`, or `Output directory required`

**Why it appears:** image export has no destination, cannot create its folder,
or cannot encode or write the PNG.

**What to do:** select a writable output directory and reduce the requested
image size if encoding fails because of memory.

**How to avoid it:** create an export directory before batch image output and
keep sufficient RAM and disk space available.

### `Error Executing`, command start failures, or nonzero process errors

**Why it appears:** a configured external command or Lua-requested executable
is missing, blocked, cannot start, or returned an error code.

**What to do:** verify the executable path and arguments, start it directly to
expose runtime errors, and read the command output in the TileZed console.

**How to avoid it:** use absolute executable paths and move the complete tool
runtime together when relocating a portable installation.

### `Invalid Brush Mask` or `Brush Import Failed`

**Why it appears:** the custom brush PNG is larger than 128 x 128, has no dark
opaque pixels, cannot be read, or cannot be copied to the user brush folder.

**What to do:** save a smaller PNG with dark opaque mask pixels and choose a
writable brush folder.

**How to avoid it:** use a simple lossless mask, preferably 32 x 32, with
transparency or light pixels for the unpainted area.

### `Partial Chunks`

**Why it appears:** the map is not a Native256 TMX, the sidecar selection could
not be saved, or no WorldEd project is available for validation.

**What to do:** use Partial Chunks only on a Native256 project, keep the TMX
writable, and retain its `.tmx.pzchunks` sidecar beside it.

**How to avoid it:** open the TMX through its PZW project before partial LOT
generation and move the sidecar whenever the TMX is moved.

### Lua tool errors

Titles include `Lua Tool Configuration Error`, `LUA Script Invalid`, `LUA
Error`, `No World Selected`, `Backup Directory Invalid`, `Error Loading Map`,
and `Error Writing Map`.

**Why it appears:** the Lua catalog or script has invalid syntax, no required
map or world is selected, the backup destination is invalid, or the script
requested a change that could not be written.

**What to do:** open the required document, select a writable backup
directory, and correct the Lua error and line shown in the console. Do not run
the script again until the original maps and backups are intact.

**How to avoid it:** test scripts on copies, keep backups enabled, and use the
documented Lua API rather than modifying map files outside the transaction.

### `Create Folder`

**Why it appears:** the folder name is invalid or already exists, the current
Maps location is unavailable, or the parent is read-only.

**What to do:** choose a portable folder name and a writable parent in the Maps
browser.

**How to avoid it:** create folders from the browser button or right-click menu
instead of moving the project while it is open.

### Why is a TMX blank or very slow with OpenGL enabled?

The OpenGL renderer remains an optional compatibility path. Some maps, drivers,
and rule-reload workflows can display blank content or severe brush lag there.

**What to do:** disable OpenGL in Preferences and reopen the map. Use the
Raster renderer for normal editing.

**How to avoid it:** leave OpenGL disabled unless testing it for a specific
renderer issue, and mention both Raster and OpenGL results in a report.

## BuildingEd

### `Tiles Directory Required`

**Why it appears:** BuildingEd has no valid extracted Tiles directory and
cannot render a TBX safely.

**What to do:** configure the shared Tiles directory from an editor's
Preferences or Initial Setup, then restart BuildingEd.

**How to avoid it:** complete initial setup before opening TBX files directly
from Explorer or TileZed.

### `Error reading building`

**Why it appears:** the TBX or autosave is missing, malformed, incomplete, or
uses data that the reader cannot interpret.

**What to do:** open a backup or autosave. If the detail names an XML line or
property, correct that item in a copy. Verify that the file is a TBX and not a
renamed TMX or compiled format.

**How to avoid it:** save buildings through BuildingEd and keep autosave
enabled while making extensive changes.

### `Error Saving Building` or `Error saving building`

**Why it appears:** BuildingEd cannot atomically write the TBX because the
folder is read-only, the file is locked, the path is invalid, or the disk is
full.

**What to do:** use Save As to a writable local project directory, close other
programs using the TBX, and keep the current BuildingEd window open until the
save succeeds.

**How to avoid it:** edit local working copies and let synchronization or
backup software copy them only after saving.

### `Building tiles unavailable`

**Why it appears:** the building template or TBX references one or more PNG
tilesets that could not be loaded. Affected tiles use red question marks.

**What to do:** restore the listed PNG files or correct the shared Tiles path.
If the sheet is obsolete, replace its tiles in the building before removing
the reference.

**How to avoid it:** keep custom building tiles in stable paths and share them
with every mapper opening the TBX.

### Building configuration messages

Titles include `Building Configuration Error`, `Building Tiles Error`,
`Furniture Configuration Error`, `Building Templates Error`, and `Error
reading RoomNames.txt`.

**Why it appears:** the named configuration file is absent, malformed, or
contains a reference to a missing tile, template, furniture group, or room
name.

**What to do:** restore the file from the same release or correct the exact
line reported. Close all open BuildingEd documents before replacing or
reloading building tile and furniture assignments.

**How to avoid it:** keep one coherent configuration set and close documents
before importing configuration that changes their available definitions.

### `Import Templates`, `Export Templates`, or `Export Tiles and Furniture` failures

**Why it appears:** the selected definition file cannot be parsed, the current
assignments are invalid, or one of the output files cannot be written.

**What to do:** close all open BuildingEd documents, verify the input against
the packaged format, and export to a writable directory. Reopen a building only
after the new configuration has loaded successfully.

**How to avoid it:** import templates, tiles, and furniture as one controlled
configuration update and keep a backup of the previous text files.

### Basement export reports no files or invalid generated Lua

**Why it appears:** no TBX was selected, a selected building cannot be read, or
the generated `basements.lua` content fails syntax validation.

**What to do:** select valid saved TBX files, resolve their normal building
errors, and generate the Lua again. Do not deploy output that failed
validation.

**How to avoid it:** save and reopen each basement TBX successfully before
including it in a batch export.

### `Error Saving Map` during TMX export

**Why it appears:** BuildingEd could not convert the current building to TMX or
write the destination.

**What to do:** resolve missing building tiles, choose a writable `.tmx`
destination, and retry. Open the exported TMX in TileZed before using it in a
project.

**How to avoid it:** validate the building's tile references before export and
do not export over an open TMX.

### `Remove File Failed`

**Why it appears:** an autosave selected for removal is open, locked, or in a
read-only directory.

**What to do:** close other BuildingEd instances and remove it again from the
Welcome page. If necessary, change the folder permissions.

**How to avoid it:** close recovered autosaves before cleaning old recovery
files.

### `BuildingEd Lua`

**Why it appears:** a Lua script was started without an open building, or the
script reports an error in the Lua console.

**What to do:** open the target TBX first and correct the script error before
retrying. Review the single Undo operation after a successful script.

**How to avoid it:** test automation on a copy and save a known-good building
before running a new script.

### What should I do if copy or paste closes BuildingEd?

This is not a normal clipboard result and should not be worked around by
repeatedly pasting into the same file.

**What to do:** use the current release, reopen the latest autosave, and keep
the original TBX. Reproduce once in standalone BuildingEd and provide the TBX,
the selected layers and floors, whether the paste expanded the building, and
the newest BuildingEd log.

**How to avoid data loss:** enable timed autosave and save before large
multi-floor edits. Copy and paste pause timed autosave while their complete
Undo transaction is being built, so wait for placement to finish before
closing the application.

## Reporting a message that is not listed

Include the following:

- application name and release
- exact dialog title and complete message text
- shortest reproduction steps
- expected and observed result
- newest matching application log
- smallest relevant PZW, TMX, TBX, PNG, Rules, Blends, Lua, `.tiles`, or
  `.pack` file
- project grid format and dimensions when relevant
- Raster and OpenGL result for rendering issues

Do not send only a screenshot of the title. The detailed text often contains
the exact path, cell, line, tile, or operating-system reason needed to solve the
problem.
