# Build 42.20 procedural loot viewer and editor

The same visual editor is available from:

- **TileZed > Tools > Procedural Loot Viewer / Editor...**
- **BuildingEd > Building > Procedural Loot Viewer / Editor...**

TileZed opens the editor on the selected tile's `container` property when one
is available. BuildingEd opens it on the current room's internal RoomDef name.
Both applications use the same loader, model, validation, and output format.

## Evidence and output labels

The `SuburbsDistributions` RoomDef/container registry,
`ProceduralDistributions.list`, alternating item/chance arrays, direct/junk
rolls, `procList`, weights, limits, and force selectors are
**game-confirmed** against the local Build 42.20 Lua and item-picker code.

`PZToolsLootEditor.json`, generated post-merge Lua, atomic writes, and refusal
to write below the game installation are **tool-owned** authoring and safety
mechanisms. They are not base-game file formats.

The displayed cumulative values and normalized selector shares are
**neutral previews**, not promises about a live save. Runtime rarity,
eligibility, sandbox, capacity, time, and other modifiers remain visible as
scope limitations below.

## Read-only game data and project output

The first path is the Project Zomboid installation, an extracted game-data
root, or its `lua/server/Items` directory. It is always a read-only reference.
The editor loads:

```text
Distribution_*.lua
Distributions.lua
ProceduralDistributions.lua
```

The second path is a map project or mod root outside the game installation.
Edits are saved atomically below:

```text
<project-or-mod>/
└── media/
    └── lua/
        └── server/
            └── Items/
                ├── PZToolsLootEditor.json
                └── PZToolsLootDefinitions.lua
```

`PZToolsLootEditor.json` is the editable PZTools manifest.
`PZToolsLootDefinitions.lua` is regenerated from it. The generated Lua applies
the project definitions during `Events.OnPostDistributionMerge`, before the
game parses the final registries. A project definition with the same key as a
game definition is therefore an override; a new key is an addition.

The editor refuses a project root equal to or below the selected game root.
It does not rewrite `Distributions.lua`, `ProceduralDistributions.lua`, or any
other base-game file.

## RoomDefs, containers, and procedural distributions

The **Rooms & containers** tab displays the effective
`SuburbsDistributions` registry as:

```text
RoomDef name
└── container type
```

A container mapping is one of two forms:

- **Procedural**: a `procList` selects a named entry from
  `ProceduralDistributions.list`.
- **Direct**: the container owns its `rolls`, `items`, and optional `junk`
  table directly.

The **Procedural distributions** tab lists the reusable named item tables.
Filtering matches either the distribution name or an item name. Game entries
are labelled **Game**; additions and overrides are labelled **Project**.

Editing a game entry creates a complete project override. Removing that
override reveals the read-only game definition again.

## What the displayed chances mean

An item list alternates item names and numeric chance values:

```lua
items = {
    "Base.ExampleItem", 12.5,
}
```

The editor displays that value as **Chance / roll**. With the sandbox roll
multiplier left neutral, Build 42 truncates the configured roll count to an
integer. The neutral probability of at least one success is therefore:

```text
1 - (1 - chance / 100) ^ floor(rolls)
```

That result is displayed as **Neutral cumulative**. It is a mathematical
preview, not an exact in-game guarantee. Sandbox loot rarity, item category,
zombie density, time-based rules, container capacity, and other runtime
modifiers can change the final result. Junk rolls are shown separately because
the game handles their rarity modifiers differently.

Duplicate item rows are reported as independent entries. They are not merged
silently because each entry is evaluated by the runtime.

For a procedural `procList`, `weightChance` is a relative selector weight, not
an item percentage. **Neutral share** normalizes the non-forced eligible rows
only. The real eligible set can change through:

- `min` and `max`;
- `forceForItems`;
- `forceForZones`;
- `forceForTiles`;
- `forceForRooms`.

Build 42 reads a missing `min` or `max` as zero. The viewer marks that state
explicitly instead of displaying a fabricated default; a newly added rule
starts at `min=0`, `max=99`.

The viewer resolves every referenced procedural distribution and marks missing
references explicitly.

## Editing workflow

1. Select the read-only game definitions.
2. Select a map-project or mod root outside the game.
3. Choose a RoomDef/container mapping or procedural distribution.
4. Use **Clone / edit in project...**, or create a new entry.
5. Edit rolls, items, raw chances, junk, selector weights, min/max, and forced
   selector fields.
6. Save the dialog. The manifest and generated Lua are replaced atomically.

Creating the local Lua layout does not install or enable a mod. Packaging,
`mod.info`, dependency/load ordering, export, and workshop publication remain
a separate workflow.

## Current scope

This editor covers Build 42 room/container distributions and
`ProceduralDistributions.list`. It does not currently edit:

- vehicle distributions;
- weapon upgrade distributions;
- item scripts, icons, or item definitions;
- sandbox settings;
- complete mod packaging.

## Regression command

The deployed TileZed or BuildingEd executable can validate the same loader and
merged references:

```text
TileZed.exe --validate-loot-distributions <game-root> [project-or-mod-root]
BuildingEd.exe --validate-loot-distributions <game-root> [project-or-mod-root]
TileZed.exe --render-loot-distributions <game-root> <output.png> [project-root]
```

The command reports room, container, direct/procedural mapping, distribution,
project-override, and unresolved read-only-game reference counts. Missing
references already present in the selected game data are disclosed as game
warnings. A missing reference introduced by a project mapping fails
validation.
