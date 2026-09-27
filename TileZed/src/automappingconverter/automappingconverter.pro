include(../../tiled.pri)
include(../libtiled/libtiled.pri)

    DESTDIR = ../..

    LIBS += -L$$OUT_PWD/../../lib

# Make sure the executable can find libtiled

greaterThan(QT_MAJOR_VERSION, 4) {
    QT += widgets
}

QT       += core gui

TARGET = automappingconverter
TEMPLATE = app


SOURCES += main.cpp \
    converterdatamodel.cpp \
    convertercontrol.cpp \
    converterwindow.cpp

HEADERS  += \
    ../portablesettings.h \
    converterdatamodel.h \
    convertercontrol.h \
    converterwindow.h

FORMS    += \
    converterwindow.ui
