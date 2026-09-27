include(../../tiled.pri)

TEMPLATE = lib
TARGET = qtlockedfile
#target.path = $${LIBDIR}
#INSTALLS += target
    DESTDIR = ../../lib
DLLDESTDIR = ../..

DEFINES += QT_NO_CAST_FROM_ASCII \
    QT_NO_CAST_TO_ASCII

HEADERS += qtlockedfile.h
SOURCES += qtlockedfile.cpp

SOURCES += qtlockedfile_win.cpp

win32:contains(TEMPLATE, lib):contains(CONFIG, shared) {
    DEFINES += QT_QTLOCKEDFILE_EXPORT=__declspec(dllexport)
}
