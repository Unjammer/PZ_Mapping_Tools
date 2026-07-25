-- BuildingEd Lua API example (modifies the open building).
-- Change these values before running.

local level = building:currentLevel()
local roomIndex = 0
local x, y, width, height = 1, 1, 4, 3

assert(roomIndex < building:roomCount(), "The requested room does not exist")
building:fillRoom(level, x, y, width, height, roomIndex)
building:setRoomSelection(x, y, width, height)

print(("Filled %dx%d cells on level %d with room '%s'")
    :format(width, height, level, building:roomName(roomIndex)))
