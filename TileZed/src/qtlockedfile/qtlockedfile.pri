INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD
HEADERS += $$PWD/qtlockedfile.h
SOURCES += $$PWD/qtlockedfile.cpp

SOURCES += $$PWD/qtlockedfile_win.cpp

win32:contains(TEMPLATE, lib):contains(CONFIG, shared) {
    DEFINES += QT_QTLOCKEDFILE_EXPORT=__declspec(dllexport)
}
