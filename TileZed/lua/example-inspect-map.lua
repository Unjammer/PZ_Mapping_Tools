-- Read-only example for Tools > Lua Console > Run Script.
-- API collection indexes are zero-based.

print(("Map: %d x %d tiles, levels %d to %d")
    :format(map:width(), map:height(), map:minLevel(), map:maxLevel()))

local cellX, cellY = map:cellX(), map:cellY()
if cellX >= 0 and cellY >= 0 then
    print(("WorldEd cell: %d,%d"):format(cellX, cellY))
else
    print("WorldEd cell: unavailable (the TMX is not attached to a loaded world)")
end

print(("Layers: %d"):format(map:layerCount()))
for index = 0, map:layerCount() - 1 do
    local layer = map:layerAt(index)
    print(("  [%d] %s, type=%s")
        :format(index, layer:nameWithPrefix(), layer:type()))

    local objects = layer:asObjectGroup()
    if objects then
        print(("       objects=%d"):format(objects:objectCount()))
    end
end

print(("Tilesets: %d"):format(map:tilesetCount()))
for index = 0, map:tilesetCount() - 1 do
    local tileset = map:tilesetAt(index)
    print(("  [%d] %s"):format(index, tileset:name()))
end
