# Tile definitions, property reference and comparison

TileZed provides two complementary tools for inspecting and authoring binary
or text tile definitions without modifying the source file until an explicit
save.

## Evidence and merge boundary

Binary and text TileDef structure, tileset IDs, image and grid metadata, tile
records, and property maps are read by the maintained TileDef implementation.
The comparator distinguishes property changes from structural changes before
any merge choice is enabled.

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
**Clear Property Values** removes the selected mapping keys. The editor asks
to save only after an actual modification and marks modified files with `*`
in the title.

Saving writes the binary `.tiles` file and its `.tiles.txt` companion.

## Build 42.22 property reference

Build 42.22 adds four ladder keys to the runtime property registry:
`ladderN`, `ladderS`, `ladderE` and `ladderW`. The TileDefs editor already
exposes them through **Ladder direction**. The other properties described
below existed before 42.22.

The [42.22 patch notes](https://theindiestone.com/forums/topic/102705-4222-patch-notes/)
announce usable ladders, crowbar forcing and moveable-weight corrections.
The rules below describe the property checks in the corresponding runtime
classes. To compare individual sprite records, use matching game-version
TileDefs in the comparator above.

### Ladder direction and placement

The direction is the direction the character faces while climbing. For the
topmost ladder segment at `(x, y, z)`, the adjacent upper square used to find
that ladder from above is:

| TileDef key | Facing direction | Adjacent upper square |
|---|---|---|
| `ladderN` | North | `(x, y - 1, z + 1)` |
| `ladderS` | South | `(x, y + 1, z + 1)` |
| `ladderE` | East | `(x + 1, y, z + 1)` |
| `ladderW` | West | `(x - 1, y, z + 1)` |

Set one direction on the ladder sprite. These are presence-based properties.
Writing `ladderN = false` still leaves the key present. Choose **None** in
the editor, or remove the key from text, to disable it. Capitalization matters.
The direction control does not write a property named `Ladder`.

For example, the following properties inside an existing `.tiles.txt` tile
block identify a north-facing wooden ladder:

```text
ladderN = true
Material = Wood
```

`IsoGridSquare.getLadder()` recognizes an object through any of the four
direction keys. `IsoGameCharacter.getClimbLadder()` also requires a solid
floor on the starting square. Upward climbing searches vertically at the
same X/Y coordinates and stops at the end of the ladder column or a floor
above it.

At the top, `ClimbState.calculateClimbOutcome()` checks the adjacent square
in the facing direction. An ordinary exit needs a square treated as a solid
floor, or another ladder, without `solidtrans`. A separating wall blocks
the exit unless the edge is hoppable.

For descent, `IsoGridSquare.getLadderUnder()` uses the adjacent upper-square
positions in the table. `IsoGameCharacter.getClimbLadderDown()` rejects the
entry if the square directly above that ladder segment contains a solid
floor or another ladder. The upper landing and the space over the ladder
column therefore have different roles.

These properties enable runtime climbing. WorldEd's ground-level collision
export and optional `.source` navigation rebuild do not encode 3D ladder
connections.

### Ladder sounds use Material

Both climbing states select `LadderFootstepsWood` when the ladder object's
`Material` equals `Wood`, ignoring letter case. A present ladder object with
another material, or no `Material` property, selects `LadderFootstepsMetal`.

This choice reads `Material`. Changing `MaterialType` or `FootstepMaterial`
does not change that ladder sound selection. The editor's existing
**Material** control already includes `Wood`.

Runtime references: `ClimbState.OnAnimEvent_PlayLadderFootstep()` and
`ClimbDownState.OnAnimEvent_PlayLadderFootstep()`.

### Doors and windows that can be forced

| Existing property | Relevance in 42.22 |
|---|---|
| `forceLocked` | Starts an `IsoDoor` key-locked and excludes it from crowbar forcing. This does not make the door indestructible. |
| `GarageDoor` | Selects the garage-door forcing branch as well as the existing multi-sprite door layout. |
| `EntityScript`, `EntityScriptName` | A resolved door entity script can select the forcing difficulty for recognized wooden doors, metal doors and gates. A script name must resolve to a real entity definition. |
| `DoorSound` | Supplies the door sound prefix. In the forcing fallback, an exact `Metal` substring also selects the metal-door difficulty branch. |
| `OpenTileOffset` | Its presence on the closed window sprite is required by `IsoWindow.canOpen()`, which is checked before forcing. The value still describes the relative open-sprite offset. |
| `makeWindowInvincible` | An invincible window is excluded from forcing. The existing window invincibility check also considers a matching wall object on the square. |

An already-open or barricaded door is excluded from forcing. Windows also
reject smashed or invincible states. These checks reuse existing properties.
There is no additional crowbar-enable field to configure for these paths.

The entity-name checks in `IsoDoor.getBaseForceOpenChance()` include
`WoodenDoorLvl1`, `WoodenDoorLvl2`, `MetalDoorLvl1`, `MetalDoorLvl2`,
`MetalPoleFenceGate` and `DoubleFenceGate`. The name must correspond to a
real script and its `EntityScript` setup. Garage doors are handled before
these entity checks.

Runtime references: `ILockableDoor.isForceLocked()`,
`IsoDoor.isNotValidForForcing()`, `IsoDoor.getBaseForceOpenChance()`,
`IsoWindow.canOpen()` and `IsoWindow.isNotValidForForcing()`.

### Window supports and wall cutaways

BuildingEd uses the window sprite's `WindowShape` to select the corresponding
opening from the wall material. The game reads the cutaway shape from the
supporting wall sprite. For example, `walls_exterior_house_04_34` and
`walls_exterior_house_04_35` support north-facing shapes 17 and 18.

For Build 42.22.2, the optional [window support patch](../tools/TileDefs/pz42-window-supports.patch.tiles)
adds shapes 17, 18 and 19 to selected vanilla wall sprites and supplies the
frame properties for `walls_exterior_house_04_76` through `_79`. Use the
[editable companion](../tools/TileDefs/pz42-window-supports.patch.tiles.txt)
to inspect or edit the patch in TileZed.

Copy `pz42-window-supports.patch.tiles` to the map mod's `common/media`
folder and add `tiledef=pz42-window-supports.patch N` to its active
`mod.info`. Replace `N` with a number from 100 to 8189 that is unused by
the active mods. Add the binary patch under **Additional Tile Property
Files** in TileZed Preferences to use the same properties in the editors.
List it after any mod definitions that supply those wall sprites. Regenerate
LOT files from the source building if an earlier export contains standard
wall openings in place of the shaped supports.

### Moveable weight and CustomItem

For sprites with `IsMoveAble`, `Moveable.ReadFromWorldSprite()` interprets
`PickUpWeight` in tenths of inventory weight. For example, `PickUpWeight = 250`
produces a weight of `25.0` in this method. A missing property uses `1.0`.

In 42.21 the `CustomItem` branch read the custom item script's `actualWeight`.
In 42.22 the TileDef calculation happens before that branch, which retains
the resulting weight. Audit the weight on moveable sprites that use
`CustomItem`, including custom equipment.

This is a change in how existing properties are read. It does not require
a new weight property or a new TileDefs editor control.
