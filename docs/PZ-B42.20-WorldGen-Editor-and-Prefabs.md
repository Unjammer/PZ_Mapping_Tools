# Build 42.20 WorldGen editor, biome preview, and prefabs

This document describes the two independent WorldEd windows:

- **Tools > WorldGen Biome Editor / Preview...**
- **Tools > WorldGen Prefab Editor...**

Biomes and prefabs deliberately do not share an editing window. The biome
window contains the 2x2-chunk procedural preview, biome registry, resolved
rules, and biome-feature tools. The prefab window contains only true static
prefab selection, inspection, painting/import, and staging. The two windows
still use the same read-only game path and project-owned WorldGen tree.

This document also records the Build 42.20 runtime limits verified against the
game's Lua definitions and Java WorldGen reader.

The feature is available only while a saved WorldEd project is loaded. The
selected Project Zomboid WorldGen directory is a read-only source. Everything
created by the editor is kept outside the base game.

## Evidence and limit labels

The following structures are **game-confirmed** against the local Build 42.20
Lua definitions and Java reader: the 8x8 chunk, 16x16 pending generation area,
feature-size tiers, biome registries and parent data, the four static-prefab
schematic slots, one-based tile references, z=0 application, and static-module
rectangle behavior.

The 256x256 prefab authoring cap, strict conversion failures, atomic project
writes, and refusal to write inside the game installation are
**tool-enforced** safety policies. They are not presented as smaller engine
constants.

The biome image is a **representative preview**. Sections below list the
authored-map, save-noise, road, erosion, and packaging behavior that is
deliberately **out of scope**.

## Project paths and load order

Project-owned definitions are stored below the directory containing the
project `.pzw` file:

```text
<project>/
└── media/
    └── lua/
        └── server/
            └── WorldGen/
                ├── features/
                │   ├── ground/
                │   ├── plant/
                │   ├── bush/
                │   ├── tree/
                │   └── ore/
                ├── prefabs/
                └── biomes/
                    ├── map/
                    ├── subbiomes/
                    └── worldgen/
```

The previewer loads game biome features, project biome features, game static
prefabs, project static prefabs, subbiomes, game biomes, and project biomes.
A project entry with the same registry key replaces the previewed game entry.
The UI labels the effective source as **Game** or **Project**.

This load order is an editor preview facility. The game still needs the
project packaged or mounted as a mod/map so that its Lua files are available
at runtime.

## Procedural biome versus map biome

A procedural biome is selected by WorldGen from its environmental parameters
and parent chain. Its weighted features provide ground, plant, bush, tree, and
ore candidates. The 16x16 preview is a representative forced-biome sample of
that selection.

A map biome is used where authored map/Biomemap data asks WorldGen to apply a
specific map-side biome or replacement context. It can still inherit biome
parameters and use weighted features, but its final result depends on the
authored square and replacement/protection rules. An empty synthetic preview
cannot reproduce all of that context.

Neither type is a painted TMX cell. A biome is a rule set used while the game
creates or replaces squares.

## Biome feature versus static prefab

These are two different Build 42 formats even though older PZWorldEd preview
labels called both of them “prefabs”.

| Item | Runtime registry | Purpose | Verified limit |
|---|---|---|---|
| Biome feature | `worldgen.features.<CATEGORY>` | A probabilistic tile or tile pattern selected by a biome | Pattern dimensions are at most 8x8 |
| Static prefab | `worldgen.prefabs` | A deterministic z=0 schematic applied by a `worldgen.static_modules` rectangle | No small Java dimension constant; this editor explicitly caps authoring/import at 256x256 |

The WorldGen feature placer tries target sizes 8, 4, 2, and 1. It uses a
16x16 pending-square array, which is exactly 2x2 Build 42 chunks. A feature
larger than 8x8 cannot be placed by that code.

A static prefab is read as:

```lua
local prefab = {
    dimensions = { width, height },
    zombies = 0.01,
    tiles = {
        "tileset_name_0",
        "tileset_name_1"
    },
    schematic = {
        Floor = {
            "1,1,0",
            "1,1,0"
        },
        FloorFurniture = {
            "0,0,0",
            "0,0,0"
        },
        FloorOverlay = {
            "0,2,0",
            "0,0,0"
        },
        Furniture = {
            "0,0,0",
            "0,0,0"
        }
    }
}

worldgen.prefabs["example"] = prefab
```

Tile references are one-based indexes into `tiles`; zero means empty. A zero
in `Floor` is special: the engine asks the active biome to generate its ground
there. The other three zero values add nothing.

Only these four slots are read by Build 42.20:

1. `Floor`
2. `FloorFurniture`
3. `FloorOverlay`
4. `Furniture`

The category names define application order. Sprite TileDefs still determine
whether an applied sprite behaves as a floor, wall, object, vegetation, and so
on.

## Chunk, cell, and z limits

Build 42 WorldGen chunks are 8x8 squares. Native WorldEd cells are 256x256
squares, or 32x32 chunks.

Static-prefab application is evaluated square by square. The Java code uses
the square offset modulo the prefab width and height, so a prefab can cross an
8x8 chunk boundary. A static-module rectangle can also cross a 256x256 cell
boundary when all affected cells belong to the active map.

Crossing a chunk or cell boundary is therefore not itself an error. The
staging dialog reports the first and last square, chunk, and cell so the mapper
can confirm the required map coverage.

WorldGen prefabs apply at z=0. They do not contain:

- upper or lower floors;
- rooms or room definitions;
- BuildingEd objects and object behavior;
- doors/windows as semantic building objects;
- roofs, stairs, or building metadata;
- zones, lots, properties, or object layers.

The PZWorldEd editor caps a prefab at 256x256 as an explicit authoring and
memory-safety policy. This is not presented as a smaller engine constant.

## Biome preview and editing workflow

1. Load and save a WorldEd project.
2. Open **Tools > WorldGen Biome Editor / Preview...**.
3. Select the read-only game WorldGen path.
4. Use the biome selector to inspect resolved parameters and generate a
   representative 16x16 biome preview.
5. Use **Biome feature** to create a feature or create a project variant of a
   game feature.

The biome window has no static-prefab controls. Its preview and resolved
definition tree remain focused on procedural and map biomes.

## Static-prefab editing workflow

1. Load and save a WorldEd project.
2. Open **Tools > WorldGen Prefab Editor...**.
3. Select the read-only game WorldGen path.
4. Select a game or project prefab to inspect its dimensions, zombie chance,
   sprite catalogue, and placement counts.
5. Create a prefab, copy a game prefab into the project, or import a restricted
   TMX/TBX source.
6. Paint and inspect the four runtime slots, then save below the project
   WorldGen tree.
7. Optionally use **Stage for Game / Mod...** to prepare the prefab and its
   static-module override outside the base game.

The prefab editor exposes one grid at a time for the four runtime slots. The
Tiles palette paints the selected sprite into one or more squares. The
isometric tab composites all four slots in depth order and draws visible
orange inspection lines every eight squares. Sprite images retain their real
pixel footprint, so tall and XL/XXL Tiles can overlap neighboring anchors
without being clipped to a one-square icon.

Editing a game definition never rewrites its game Lua file. Accepting an edit
creates a new project copy. Project definitions created by the editor can be
edited in place with atomic file replacement.

## TMX and TBX conversion

**Import TMX/TBX...** is intentionally strict because the prefab format is
much smaller than either source format.

The converter:

- accepts a Project Zomboid TMX whose tile layers explicitly identify
  `level="0"`, or a one-floor TBX;
- preserves z=0 tile sprite names and their layer order;
- maps one explicit `Floor` tile to the prefab `Floor` slot;
- maps up to three remaining tiles on a square to `FloorFurniture`,
  `FloorOverlay`, and `Furniture`;
- stops if a square has multiple explicit Floor tiles;
- stops if a square needs more than three non-floor slots;
- stops when a non-empty tile layer is above or below z=0;
- stops above the editor's 256x256 limit;
- reports ignored object layers and lost building metadata before opening the
  editor.

There is no silent “successful” conversion of a multi-storey building. A
complex TBX should remain a building/lot or be simplified deliberately into a
z=0 tile schematic first.

A generic Tiled TMX that omits the Project Zomboid `level` attribute is
rejected with a normal parser error instead of being guessed or crashing the
application.

## Staging for a game map or mod

**Stage for Game / Mod...** asks for:

- a project or mod root;
- the target `media/maps/<MapName>` folder;
- the prefab's global-square origin.

It writes:

```text
<target>/media/lua/server/WorldGen/prefabs/<name>.lua
<target>/media/maps/<MapName>/WorldGenOverride.lua
```

The generated override requires the prefab and adds one inclusive
`worldgen.static_modules` rectangle whose dimensions exactly match one prefab
instance. PZWorldEd wraps its block in name-specific begin/end comments.
Restaging the same prefab replaces that marked block; unrelated override
content is preserved.

PZWorldEd refuses a target inside the selected Project Zomboid installation.
Both supported game-data layouts are recognized for that protection:
`<game>/media/lua/server/WorldGen` and the extracted-reference layout
`<root>/lua/server/WorldGen`.
Staging prepares a game-ready directory layout; complete mod metadata,
workshop packaging, dependency ordering, and release export remain a separate
workflow.

Static modules are order-sensitive because the runtime uses the first module
that contains a square. Review the final `WorldGenOverride.lua` when several
modules overlap.

## Regression commands

Maintainers can exercise the same deployed-code paths without saving user
data:

```text
PZWorldEd.exe --validate-worldgen-preview=<game-or-WorldGen-path>
PZWorldEd.exe --validate-worldgen-prefab-import=<source.tmx-or-tbx>
PZWorldEd.exe --render-worldgen-prefab-window=<game-or-WorldGen-path> --worldgen-preview-output=<capture.png>
PZWorldEd.exe --render-worldgen-prefab=<game-or-WorldGen-path> --worldgen-preview-output=<capture.png>
```

The import validator reports dimensions, unique tile count, placements, and
conversion warnings. The renderer captures the isometric prefab editor after
the complete Tiles catalogue is ready.

## Preview scope

The deterministic preview is intended to answer “what do these current biome
rules and Tiles look like?” It is not a captured game save.

The preview does not claim exact parity for:

- game-save noise and surrounding-cell state;
- authored map replacement/protection context;
- every `$subbiome` marker expansion;
- roads;
- erosion.

Road WorldGen is deliberately excluded while that runtime system is still
being completed. Erosion is treated as a potential separate editor/module.
