-- Edit these two values, select an area in TileZed, then run this script.
local LAYER_NAME = "0_Floor"
local TILE_NAME = "floors_interior_tilesandwood_01_0"

local layer = assert(map:tileLayer(LAYER_NAME),
    "Missing tile layer: " .. LAYER_NAME)
local tile = assert(map:tile(TILE_NAME),
    "Missing tile: " .. TILE_NAME .. " (edit TILE_NAME in the script)")
local selection = map:tileSelection()

if selection:isEmpty() then
    error("Select one or more tiles before running this script")
end

layer:fill(selection, tile)
print(("Filled %d selection rectangle(s) on %s with %s")
    :format(selection:rectCount(), LAYER_NAME, TILE_NAME))
