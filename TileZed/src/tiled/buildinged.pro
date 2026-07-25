# BuildingEd uses the same editor core as TileZed, but is linked as a
# separate executable so Windows can expose its own application icon.
include(tiled.pro)

TARGET = BuildingEd
RC_FILE = buildinged.rc
QMAKE_POST_LINK =
