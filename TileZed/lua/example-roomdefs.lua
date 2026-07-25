-- Creates a level-8 RoomDefs layer when needed and adds one rectangle.
-- The example is idempotent: running it again does not duplicate the object.
local LAYER_NAME = "8_RoomDefs"
local OBJECT_NAME = "lua_documentation_example"

local roomDefs = map:objectLayer(LAYER_NAME)
if not roomDefs then
    roomDefs = map:newObjectLayer(LAYER_NAME)
    map:addLayer(roomDefs)
    print("Created object layer " .. LAYER_NAME)
end

for _, object in ipairs(roomDefs:objects()) do
    if object:name() == OBJECT_NAME then
        print("RoomDef already exists: " .. OBJECT_NAME)
        return
    end
end

local room = MapObject:new(OBJECT_NAME, "RoomDef", 20, 20, 12, 8)
room:setProperty("createdBy", "TileZed Lua documentation")
roomDefs:addObject(room)

print(("Added %s; %s now contains %d object(s)")
    :format(OBJECT_NAME, LAYER_NAME, roomDefs:objectCount()))
