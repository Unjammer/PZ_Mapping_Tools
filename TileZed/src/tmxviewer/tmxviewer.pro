include(../../tiled.pri)
include(../libtiled/libtiled.pri)

TEMPLATE = app
TARGET = tmxviewer
isEmpty(INSTALL_ONLY_BUILD) {
    target.path = $${PREFIX}/bin
    INSTALLS += target
}
TEMPLATE = app

greaterThan(QT_MAJOR_VERSION, 4) {
    QT += widgets
}

    DESTDIR = ../..

    LIBS += -L$$OUT_PWD/../../lib

# Make sure the executable can find libtiled

SOURCES += main.cpp \
         tmxviewer.cpp

HEADERS += tmxviewer.h \
           ../portablesettings.h
