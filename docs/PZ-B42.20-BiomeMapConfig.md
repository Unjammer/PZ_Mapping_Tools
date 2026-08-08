# Project Zomboid 42.20 BiomeMapConfig reference

![Build 42.20 BiomeMapConfig infographic](images/biomemap-config-build-42-20-discord-v3.png)

Build 42.20 loads `biome_map_config` from
`lua/server/metazones/BiomeMapConfig.lua`. Each entry maps one unsigned pixel
value to up to three independent meanings:

- `biome` controls map-authored WorldGen replacement and vegetation behavior
  from the red channel.
- `ore` is a second WorldGen map-biome selector from the red channel. It is
  not a separate image channel and it does not name one guaranteed mineral.
- `zone` controls the generated foraging zone from the green channel.

The game reads only the red and green image bands. Blue is not part of the
BiomeMap raster data.

## What `map_forest` means

`map_forest` is a selector prefix in `worldgen.biomes_map`. It is not an ore
image, an item name, or a request for iron.

The base `map_forest` definition has `generate = false`. WorldGen uses the
prefix to select one generated child definition matching the current
procedural ore-noise band:

- `map_forest_boulder_*` places surface boulder sprites
- `map_forest_limestone_*` places surface limestone sprites
- `map_forest_flint_*` places surface flint sprites

The suffix is selected from `none`, `very_low`, `low`, `medium`, `high`, or
`very_high`. The red Biomemap value selects `map_forest`, but it does not
directly choose the material or density. WorldGen's deterministic selector and
ore noise resolve those details for each coordinate.

| Ore-noise band | Boulders probability | Limestone probability | Flint probability |
|---|---:|---:|---:|
| `NONE` | none | none | none |
| `VERY_LOW` | 0.1% | 0.05% | 0.05% |
| `LOW` | 0.2% | 0.1% | 0.1% |
| `MEDIUM` | 1% | 0.5% | 0.5% |
| `HIGH` | 2% | 1% | 1% |
| `VERY_HIGH` | 4% | 2% | 2% |

These are feature probabilities after WorldGen has selected the matching
material family and density variant. Placement is attempted only on eligible,
otherwise open natural ground. The selector allows
`blends_natural_01_*` with explicit base and edge exclusions and excludes all
`blends_natural_02_*` water tiles.

Iron and copper are not selected by `map_forest`. Build 42.20 generates those
through the separate vein definitions in `WorldGen/Veins.lua`.

`map_deep_forest` uses the same boulder, limestone, flint, and density
structure under a separate deep-forest selector prefix.

## Image geometry

The game looks for:

`media/maps/<MapName>/maps/biomemap_<cellX>_<cellY>.png`

Each file is 256 x 256 pixels. One red sample and one green sample describe
each map square in that native cell.

The red channel is evaluated per square by WorldGen. The green channel is read
in 8 x 8 groups for chunk-level foraging-zone generation. A green chunk should
therefore use one consistent zone ID.

## Verified Build 42.20 entries

| Pixel | Biome | Ore selector | Zone | Availability |
|---:|---|---|---|---|
| 0 | none | none | `Water` | Vanilla |
| 59 | `clay_shore` | none | `Forest` | Vanilla |
| 64 | none | none | `ForagingNav` | Vanilla |
| 79 | `clay_lake` | none | `Forest` | Vanilla |
| 96 | `$random` | none | `DeepForest` | Vanilla |
| 102 | `townhouse` | none | `TrailerPark` | Vanilla |
| 115 | `townhouse` | none | `TownZone` | Vanilla |
| 128 | `farmmix_forest` | none | `Farm` | Vanilla |
| 141 | `farmmix_forest` | none | `FarmLand` | Vanilla |
| 153 | `ph_forest` | none | `PHForest` | Vanilla |
| 179 | `pr_forest` | `map_forest` | `PRForest` | Vanilla |
| 192 | `farmmix_forest` | `map_forest` | `FarmMixForest` | Vanilla |
| 204 | `farm_forest` | none | `FarmForest` | Vanilla |
| 217 | `birch_forest` | `map_forest` | `BirchForest` | Vanilla |
| 230 | `birchmix_forest` | `map_forest` | `BirchMixForest` | Vanilla |
| 243 | `organic_forest` | `map_forest` | `OrganicForest` | Vanilla |
| 254 | `dirt` | `dirt` | `ForagingNav` | Vanilla |
| 255 | `primary_forest` | `map_deep_forest` | `DeepForest` | Vanilla |
| 171 | `vegitation` | none | `Vegitation` | Map override only |

Values 34, 45, 166, and 171 are commented out in the Vanilla configuration.
They do not become valid merely because they appear in comments. WorldEd keeps
only the already documented ID 171 extension because it has a verified
map-specific override workflow.

## Important special cases

- Red 0 and red 64 have no `biome` or `ore` field. They can define green-zone
  behavior, but their red value does not request a WorldGen biome.
- Red 96 uses `$random`, which delegates the square to the random WorldGen
  path instead of selecting one named map biome.
- Pixel 254 deliberately maps both `biome` and `ore` to `dirt`, while its green
  meaning remains `ForagingNav`.
- Pixel 255 combines `primary_forest`, `map_deep_forest`, and `DeepForest`.
- Pixel 171 is disabled in Vanilla. A map must add it to
  `biome_map_config` through `WorldGenOverride.lua`.

## WorldEd behavior

WorldEd now uses this complete metadata table in three places:

1. **Generate Biome Map > Show Build 42.20 BiomeMapConfig Reference...**
   displays every field and marks override-only values.
2. The Biomemap brush palette shows the exact `biome`, `ore`, and `zone`
   fields in each item tooltip. Painting changes only red and preserves green.
3. Generation validates both channels. Unknown green IDs stop generation,
   unknown red IDs and red values without an effect are reported, mixed green
   IDs inside an 8 x 8 chunk are reported, and ID 171 produces an explicit
   override warning.

The embedded table is a verified Build 42.20 reference. WorldEd does not
execute arbitrary Lua configuration files.

## Enabling ID 171 for a map

Place this idempotent block in:

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

Painting red ID 171 then selects the verified `vegitation` biome that produces
the deterministic redbud Jumbo XXL feature. Green 171 is only needed when the
same area must also become the `Vegitation` foraging zone.
