include(../../src/libtiled/libtiled.pri)

CONFIG += qtestlib
TEMPLATE = app
DEPENDPATH += .

    LIBS += -L$$OUT_PWD/../../lib


# Input
SOURCES += test_mapreader.cpp
