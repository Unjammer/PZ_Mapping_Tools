# Discord-ready BiomeMapConfig announcement

Upload this image with the first message:

![Build 42.20 BiomeMapConfig infographic](images/biomemap-config-build-42-20-discord-v3.png)

The final built-in ImageGen prompt is archived in
`PZ-B42.20-BiomeMapConfig-Image-Prompt.md`.

Copy each block as one Discord message.

## Message 1 of 5

**WorldEd now uses the complete Project Zomboid 42.20 BiomeMapConfig**

We verified the mapping directly against Build 42.20
`BiomeMapConfig.lua` and the game-side BiomeMap reader.

A Biomemap pixel is not a normal color. It contains two independent numeric
IDs:

🔴 **Red channel** controls the map biome and optional ore selector used by
WorldGen on each map square.

The `ore` field is a second WorldGen map-biome selector. It is not another
image channel and does not name one guaranteed mineral.

🟢 **Green channel** controls the foraging zone generated from the same pixel
ID table.

🔵 **Blue channel** is not read by the Build 42.20 BiomeMap raster.

Each `biomemap_X_Y.png` is 256 x 256. One pixel corresponds to one map square.
Red is evaluated per square. Green is evaluated in 8 x 8 chunk areas, so one
green ID should remain consistent inside each chunk.

## Message 2 of 5

**What `map_forest` actually means**

`map_forest` is a WorldGen selector prefix for surface deposits. It is not a
second PNG, not an item name, and not an instruction to generate iron.

For each eligible coordinate, WorldGen uses its deterministic selector and ore
noise to resolve:

```text
Material family:
boulders | limestone | flint

Density band:
NONE | VERY_LOW | LOW | MEDIUM | HIGH | VERY_HIGH
```

The configured feature probabilities are:

```text
Density     Boulders   Limestone   Flint
NONE        none       none        none
VERY_LOW    0.1%       0.05%       0.05%
LOW         0.2%       0.1%        0.1%
MEDIUM      1%         0.5%        0.5%
HIGH        2%         1%          1%
VERY_HIGH   4%         2%          2%
```

Placement is attempted only on eligible, otherwise open natural ground.
Water tiles are excluded.

Iron and copper use a separate vein-generation system in
`WorldGen/Veins.lua`. `map_deep_forest` uses the same surface-deposit families
and density ladder under its own selector prefix.

## Message 3 of 5

**Active Vanilla Build 42.20 IDs**

```text
ID  Biome              Ore              Zone
0   none               none             Water
59  clay_shore         none             Forest
64  none               none             ForagingNav
79  clay_lake          none             Forest
96  $random            none             DeepForest
102 townhouse          none             TrailerPark
115 townhouse          none             TownZone
128 farmmix_forest     none             Farm
141 farmmix_forest     none             FarmLand
153 ph_forest          none             PHForest
179 pr_forest          map_forest       PRForest
192 farmmix_forest     map_forest       FarmMixForest
204 farm_forest        none             FarmForest
217 birch_forest       map_forest       BirchForest
230 birchmix_forest    map_forest       BirchMixForest
243 organic_forest     map_forest       OrganicForest
254 dirt               dirt             ForagingNav
255 primary_forest     map_deep_forest  DeepForest
```

IDs 34, 45, 166, and 171 are commented out in the Vanilla file. A commented
entry is not active in the game.

## Message 4 of 5

**What changed in WorldEd**

✅ The Biomemap generator now has a complete Build 42.20 reference table with
Pixel, Palette, Biome, Ore, Zone, and Availability columns.

✅ The Biomemap brush dropdown now exposes the exact biome, ore, and zone
meaning for every ID, including a direct definition of the ore selector.

✅ The brush still edits only the red channel. Existing green foraging-zone
data is preserved.

✅ Unknown green IDs stop generation because the game cannot resolve their
zone name.

✅ Unknown red IDs, red IDs with no biome or ore effect, mixed green IDs inside
an 8 x 8 chunk, and override-only IDs are all reported clearly.

✅ Legacy 300 x 300 projects still generate complete 256 x 256 Biomemap tiles.
Boundary data can still be merged over a Vanilla base Biomemap directory.

The table is embedded as a verified Build 42.20 reference. WorldEd does not
execute arbitrary Lua files.

## Message 5 of 5

**Special case, ID 171**

ID 171 is commented out in Vanilla, but it can be enabled per map to use the
verified `vegitation` biome and deterministic redbud Jumbo XXL feature.

Add the documented entry to:

`media/maps/<MapName>/WorldGenOverride.lua`

```lua
local hasJumboBiome = false
for _, entry in ipairs(biome_map_config) do
    if entry.pixel == 171 then
        hasJumboBiome = true
        break
    end
end

if not hasJumboBiome then
    table.insert(biome_map_config, {
        pixel = 171,
        biome = "vegitation",
        zone = "Vegitation",
    })
end
```

Use red 171 for the Jumbo-producing biome. Use green 171 only if that area
must also register as the `Vegitation` foraging zone.

In WorldEd, open **Generate Biome Map** and select
**Show Build 42.20 BiomeMapConfig Reference...** for the full table.
