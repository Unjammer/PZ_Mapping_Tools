PZTOOLS_SOURCE_ROOT = $$clean_path($$PWD/..)
PZTOOLS_TILEZED_SOURCE = $$PZTOOLS_SOURCE_ROOT/TileZed
PZTOOLS_WORLDED_SOURCE = $$PZTOOLS_SOURCE_ROOT/WorldEd
PZTOOLS_BUILDING_SOURCE = $$PZTOOLS_TILEZED_SOURCE/src/tiled/BuildingEditor

pztoolsConfig.path = Contents/Config
pztoolsConfig.files = \
    $$PZTOOLS_TILEZED_SOURCE/Curbs.txt \
    $$PZTOOLS_TILEZED_SOURCE/Edges.txt \
    $$PZTOOLS_TILEZED_SOURCE/Fences.txt \
    $$PZTOOLS_TILEZED_SOURCE/LuaTools.txt \
    $$PZTOOLS_TILEZED_SOURCE/Rearrange.txt \
    $$PZTOOLS_TILEZED_SOURCE/RearrangeGrid.txt \
    $$PZTOOLS_TILEZED_SOURCE/RoomNames.txt \
    $$PZTOOLS_TILEZED_SOURCE/RoomTone.txt \
    $$PZTOOLS_TILEZED_SOURCE/Textures.txt \
    $$PZTOOLS_TILEZED_SOURCE/TileProperties.txt \
    $$PZTOOLS_TILEZED_SOURCE/Tilesets.txt \
    $$PZTOOLS_TILEZED_SOURCE/TileShapes.txt \
    $$PZTOOLS_WORLDED_SOURCE/Blends.txt \
    $$PZTOOLS_WORLDED_SOURCE/MapBaseXML.txt \
    $$PZTOOLS_WORLDED_SOURCE/MapToPNG.txt \
    $$PZTOOLS_WORLDED_SOURCE/Roads.txt \
    $$PZTOOLS_WORLDED_SOURCE/Rules.txt \
    $$PZTOOLS_WORLDED_SOURCE/WorldDefaults.txt \
    $$PZTOOLS_BUILDING_SOURCE/BuildingFurniture.txt \
    $$PZTOOLS_BUILDING_SOURCE/BuildingTemplates.txt \
    $$PZTOOLS_BUILDING_SOURCE/BuildingTiles.txt \
    $$PZTOOLS_BUILDING_SOURCE/TMXConfig.txt

pztoolsDocs.path = Contents/Docs
pztoolsDocs.files = $$files($$PZTOOLS_SOURCE_ROOT/docs/*)
pztoolsBuildingDocs.path = Contents/Docs/BuildingEd
pztoolsBuildingDocs.files = $$files($$PZTOOLS_BUILDING_SOURCE/manual/*)
pztoolsLua.path = Contents/Lua
pztoolsLua.files = $$files($$PZTOOLS_TILEZED_SOURCE/lua/*)

QMAKE_BUNDLE_DATA += pztoolsConfig pztoolsDocs pztoolsBuildingDocs pztoolsLua
