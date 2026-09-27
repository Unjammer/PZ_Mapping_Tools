isEmpty(TARGET) {
    error("plugin.pri: You must provide a TARGET")
}

TEMPLATE = lib
CONFIG += plugin
contains(QT_CONFIG, reduce_exports): CONFIG += hide_symbols
INCLUDEPATH += $$PWD/../tiled
DEPENDPATH += $$PWD/../tiled
    DESTDIR = $$OUT_PWD/../../../plugins/tiled

include(../../tiled.pri)
isEmpty(INSTALL_ONLY_BUILD) {
    target.path = $${LIBDIR}/tilezed/plugins
    INSTALLS += target
}

include(../libtiled/libtiled.pri)
    LIBS += -L$$OUT_PWD/../../../lib

# Set rpath so that the plugin will resolve libtiled correctly
