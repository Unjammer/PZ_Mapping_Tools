PZTOOLS_EDITOR_TARGET = BuildingEd
include(tiled.pro)

SOURCES -= main.cpp
SOURCES += buildingedmain.cpp

RC_FILE = buildinged.rc
QMAKE_POST_LINK =
