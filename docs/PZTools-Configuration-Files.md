# PZTools configuration files

This page explains the shared `config` directory used by PZWorldEd, TileZed,
and BuildingEd. It is written for mappers first: most users should not edit
these files by hand.

## The short version

- **Tiles** points to the extracted PNG tree.
- **Configuration** points to the PZTools `config` directory.
- **Settings** contains preferences and logs; it is not a configuration
  directory.
- Project files and mod overrides belong in the mapper's project, not in the
  game installation and not in PZTools `config`.

The three applications share the selected Tiles and configuration paths
through `settings/PZTools.ini`. Changing either path affects all three tools
after they are restarted.

## What each file does

| File | Used mainly by | Purpose | Normal user action |
|---|---|---|---|
| `Tilesets.txt` | All three tools | Ordered catalogue of logical sheet names, PNG locations, and logical columns/rows | Use the Tileset tools or catalogue rebuild; do not casually reorder it |
| `TileProperties.txt` | TileZed, BuildingEd | Controls and tooltips shown by the TileDef property editor | Edit only when adding a verified engine property |
| `BuildingTiles.txt` | BuildingEd | Tile categories used by walls, roofs, windows, grime, and object tools | Normally read-only |
| `BuildingFurniture.txt` | BuildingEd | Named furniture/object groups and their ordered tile definitions | Normally read-only |
| `BuildingTemplates.txt` | BuildingEd | Reusable building defaults and template categories | Normally read-only |
| `TMXConfig.txt` | TileZed, BuildingEd | Layer/object layout used when creating or exporting maps | Normally read-only |
| `RoomNames.txt` | BuildingEd | Valid room-name catalogue | Choose values in BuildingEd |
| `RoomTone.txt` | BuildingEd | Valid room-tone/building-type catalogue | Choose values in BuildingEd |
| `WorldDefaults.txt` | WorldEd | Default object types, groups, properties, templates, colors, professions, and Build 42 zone values | Project-specific values belong in the PZW project |
| `Rules.txt` | WorldEd terrain/BMP tools | Color aliases and terrain-generation rules | Copy into a project before customizing |
| `Blends.txt` | WorldEd terrain/BMP tools | Terrain edge and overlay transitions | Copy into a project before customizing |
| `MapBaseXML.txt` | WorldEd BMP-to-TMX | Base TMX layer/object structure for generated maps | Normally read-only |
| `Roads.txt` | WorldEd | Road drawing presets | Normally read-only |
| `Fences.txt` | TileZed Lua tools | Fence presets and directional tile choices | Use through Draw Fence |
| `Curbs.txt` | TileZed Lua tools | Curb presets and near/far/sunken pieces | Use through Draw Curb |
| `Edges.txt` | TileZed Lua tools | Terrain-edge presets | Use through Draw Edge |
| `LuaTools.txt` | TileZed | Menu labels, icons, and scripts for shipped Lua tools | Add user tools in a separate user catalogue |
| `Rearrange.txt` | TileZed | Ordered tile selections used by rearrangement helpers | Normally read-only |
| `RearrangeGrid.txt` | TileZed | Old/new tile-grid remapping tables | Normally read-only |

Legacy source files such as `MapToPNG.txt`, `Textures.txt`, `TileShapes.txt`,
and `thumbnails.txt` are not shipped as active runtime catalogues. Their old
presence in `config` did not make a current feature use them.

## Current Build 42.20 audit

The August 4 audit checks the maintained catalogues against the configured
`C:\pz\Tiles` tree and the local Build 42.20 reference:

| Check | Audited result |
|---|---:|
| PNG files discovered recursively | 546 |
| Unique logical PNG base names | 543 |
| `Tilesets.txt` entries | 543 |
| Missing discovered names from the catalogue | 0 |
| Catalogue names without a readable PNG | 0 |
| Room names | 588 |
| Room tones/building types | 267 |
| TileDef property controls with tooltips | 211 |
| WorldEd profession choices | `all` plus 25 Build 42.20 professions |

Three logical names exist in more than one installed location. Selection is
deterministic: 2x wins over 1x/custom, and the maintained catalogue keeps the
chosen nested pack path. File names containing spaces are valid and are not
split into a different logical name.

Every tile reference in the maintained `Rules`, `Roads`, `Fences`, `Curbs`,
`Edges`, `BuildingTiles`, `BuildingTemplates`, `BuildingFurniture`,
`Rearrange`, `RearrangeGrid`, and explicit `Blends` entries was checked against
the logical sheet dimensions. Invalid burnt-roof names, six furniture groups
using nonexistent `signs_one-off_05_512` through `_559`, and two obsolete
rearrangement groups were removed or corrected.

Some special-effect PNGs do not encode a conventional 64x128 sheet rectangle.
When their decoded rectangle cannot represent their stored tile count, the
tools preserve the valid logical columns/rows recorded by the catalogue.
This is intentional for sheets such as the Giblet, Rain, and large-blood
effects; it is not permission to hide an arbitrary size mismatch.

## Safe customization

1. Back up the file you intend to change.
2. Keep project rules and WorldGen/loot overrides inside the project.
3. Never delete or reorder a TBX `tile_entry`, `user_tiles`, or furniture list
   by hand. Its position is an ID used elsewhere in the file.
4. Keep tile IDs within `columns x rows` from `Tilesets.txt`.
5. Restart all three applications after changing a shared catalogue.
6. Read the newest log under `settings/logs` if loading stops.

If a TMX or TBX project accumulated stale paths and definitions, use
**WorldEd > Tools > Project Doctor: Tiles and Paths...**. Its first
**Check project** pass is read-only, and the normal result is a plain-language
table. Technical parser details stay hidden unless support asks for them.

## Maintainer checks

Run validators from the deployed `bin` directory, where all Qt dependencies
are present:

```powershell
.\TileZed.exe --validate-tileset-catalog
.\BuildingEd.exe --validate-building-categories
.\PZWorldEd.exe --validate-world-defaults=..\config\WorldDefaults.txt
.\PZWorldEd.exe --validate-tileset-cleanup
```

`--rebuild-tileset-catalog=<Tiles-path>` is an explicit maintenance command.
It scans recursively, resolves 2x before 1x/custom, preserves valid logical
geometry where required, creates a backup, and rewrites the catalogue. It is
not part of ordinary application startup.
