# `.tiles` comparator and Snow / Replacement Editor

TileZed provides two complementary tools for inspecting and authoring binary
or text tile definitions without modifying the source file until an explicit
save.

## Evidence and merge boundary

Binary/text TileDef structure, tileset IDs, image/grid metadata, tile records,
and property maps are read by the maintained TileDef implementation and
covered by the split/comparison regression path. The comparator distinguishes
property changes from structural changes before any merge choice is enabled.

File 2 as merge base, refusal to invent missing structural records, and the
available replacement-property presets are **tool-enforced** policies. They
avoid presenting a property-only merge as a complete tileset repair.

## Enhanced `.tiles` comparator

Open **Tools > Compare .tiles Files...** and select File 1 and File 2.

The comparison summary includes:

- the complete SHA-256 of both input files;
- tileset and tile-record counts;
- tilesets present in only one file;
- structural differences in tileset ID, image source, grid dimensions, and
  stored tile count;
- every modified property record and every tile record present on only one
  side.

The result list supports text and status filters. Selecting a result shows the
two tile images side by side and a property table in which differing keys are
highlighted. The report can be copied or exported as a tab-separated UTF-8
file.

File 2 is the base of the optional merged output. **Use File 1** and
**Use File 2** choose the property map for selected tile records that exist in
both files. Unique tilesets, changed geometry, and one-sided tile records are
reported but are not silently inserted or resized; resolve those structural
changes in the Tile Definitions editor before merging.

## Snow / Replacement Editor

Open **Tools > Snow Editor...**, then load a `.tiles` or `.tiles.txt`
definition. The left side is the target/base tileset and the right side is the
source/replacement tileset.

The property defaults to `SnowTile`. `BurntTile` and custom property names are
also supported.

Mappings can be authored in three ways:

- drag one source tile onto one target tile;
- select several target tiles and use **Assign source to selected targets**;
- select target tiles and use **Match selected by tile ID** to choose the tile
  with the same numeric ID from the selected source tileset.

Resolved replacements are previewed as blue overlays. A red outline and
tooltip identify a stored replacement name that cannot be resolved against
the current complete Tiles catalogue. The status bar reports the current
property, mapped and unresolved counts, selection size, and modified state.

Custom properties are written directly to the tile property map and are not
discarded merely because they are absent from the current property UI schema.
**Clear Property Values** removes the selected mapping keys. The editor now
prompts to save only after an actual modification and marks modified files
with `*` in the title.

Saving writes the binary `.tiles` file and its `.tiles.txt` companion.

## Regression validation

The tile-definition validator also checks unique tilesets, structural
metadata, modified properties, and one-sided tile records:

```text
TileZed.exe --validate-tiledef-split
TileZed.exe --render-tiledef-comparator <output.png>
```
