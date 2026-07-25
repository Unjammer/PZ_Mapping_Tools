-- BuildingEd Lua API example (read-only).
-- Run from Building > Run Lua Script...

print(("Building: %d x %d, %d floors, %d rooms")
    :format(building:width(), building:height(),
            building:floorCount(), building:roomCount()))

for roomIndex = 0, building:roomCount() - 1 do
    print(("room[%d] = %s (%s)")
        :format(roomIndex,
                building:roomName(roomIndex),
                building:roomInternalName(roomIndex)))
end

for floorIndex = 0, building:floorCount() - 1 do
    local level = building:floorLevel(floorIndex)
    print(("floor[%d] level=%d objects=%d")
        :format(floorIndex, level, building:objectCount(level)))

    for objectIndex = 0, building:objectCount(level) - 1 do
        print(("  object[%d] %s at %d,%d facing %s")
            :format(objectIndex,
                    building:objectType(level, objectIndex),
                    building:objectX(level, objectIndex),
                    building:objectY(level, objectIndex),
                    building:objectDirection(level, objectIndex)))
    end

    for _, layerName in ipairs(building:userLayerNames(level)) do
        print(("  user-tile layer: %s"):format(layerName))
    end
end

for _, key in ipairs(building:propertyNames()) do
    print(("property %s = %s"):format(key, building:property(key)))
end
