# Project Zomboid 42.20: deterministic Jumbo trees

Build 42.20 has two different mechanisms for map-authored Jumbo trees. Put the
override file at:

`media/maps/<MapName>/WorldGenOverride.lua`

## Force the vanilla redbud XXL feature from the Biomemap

The global `BiomeMapConfig.lua` leaves pixel 171 disabled, but the corresponding
`vegitation` map biome still exists. Its `TREE` feature contains only
`redbud_jumbo_xxl` with probability 1.

Add this idempotent block to `WorldGenOverride.lua`:

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

Then use WorldEd's Biomemap biome-layer brush:

1. Select **Forced Redbud Jumbo XXL (map override)**, ID 171.
2. Set the radius to 0 and paint one pixel on the square that will anchor the
   tree.
3. Keep a 5 x 5 natural-ground area around the anchor available for the
   feature's footprint.

Only the red/biome channel needs value 171 to generate the tree. The green/zone
channel may also use 171 if the square must be registered as the `Vegitation`
metazone.

This route is deterministic, but it specifically creates
`e_easternredbudJUMBOXXL_1_0`.

## Preserve a specific Jumbo tile placed in TileZed

An explicit Jumbo tile in the lot binary is a real `IsoTree`, but WorldGen may
replace it on squares covered by a valid Biomemap biome. To preserve the exact
species and size selected in TileZed, add the Jumbo wildcard to both map biomes
and their possible sub-biomes:

```lua
local function protectTilePattern(biomes, pattern)
    if not biomes then return end

    for _, biome in pairs(biomes) do
        biome.params = biome.params or {}
        biome.params.protected = biome.params.protected or {}

        local found = false
        for _, current in ipairs(biome.params.protected) do
            if current == pattern then
                found = true
                break
            end
        end

        if not found then
            table.insert(biome.params.protected, pattern)
        end
    end
end

protectTilePattern(worldgen.biomes_map, "e_*JUMBO*")
protectTilePattern(worldgen.subbiomes, "e_*JUMBO*")
```

Place the main `_0` tile at the exact anchor square, for example:

`e_yellowwoodJUMBOXXL_1_0`

The game derives the seasonal top, trunk, stump and burned sprites from the
main tree; do not place those companion sprites manually.

Without this protection, explicit placement is not a guarantee on an active
Biomemap square. Outside map-WorldGen replacement, the explicitly placed tile
already remains exact.

