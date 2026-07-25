layerNames = {
    '6_RoomDefs',
    '7_RoomDefs',
    '8_RoomDefs',
}

function indexOfLayer(layerName)
    for i = 1, map:layerCount() do
        if map:layerAt(i - 1):nameWithPrefix() == layerName then
            return i - 1
        end
    end
    return -1
end

layers = {}
previousExistingLayer = -1
for index, layerName in ipairs(layerNames) do
    existIndex = indexOfLayer(layerName)
    if existIndex == -1 then
        if previousExistingLayer == -1 then
            previousExistingLayer = 0
        end
        newLayer = map:newObjectLayer(layerName)
        map:insertLayer(previousExistingLayer + 1, newLayer)
        previousExistingLayer = previousExistingLayer + 1
        print('added: ' .. layerName)
    else
        previousExistingLayer = existIndex
    end
end

newobject = MapObject:new('RoomDefs_Name', 'RoomDefs_Type', 0, 0, 10, 10)
roomDefLayerNew = map:objectLayer('8_RoomDefs')

if roomDefLayerNew then
    roomDefLayerNew:addObject(newobject)
    print('added RoomDefs: ' .. newobject:name())
end
