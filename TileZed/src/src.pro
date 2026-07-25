TEMPLATE  = subdirs
CONFIG   += ordered

buildinged.file = tiled/buildinged.pro
buildinged.makefile = Makefile.BuildingEd
buildinged.depends = tiled

SUBDIRS = zlib lua tolua config libtiled worlded tiled buildinged plugins \
    tmxviewer \
    automappingconverter
