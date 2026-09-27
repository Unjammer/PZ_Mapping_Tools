include($$top_srcdir/PZWorldEd.pri)

TEMPLATE = lib
TARGET = zlib1
#CONFIG += static
CONFIG -= app_bundle
CONFIG -= qt

DEFINES += ZLIB_DLL

#target.path = $${LIBDIR}
#INSTALLS += target

DESTDIR = $$top_builddir/lib

DLLDESTDIR = $$top_builddir

SOURCES += \
    adler32.c \
    compress.c \
    crc32.c \
    deflate.c \
    gzclose.c \
    gzlib.c \
    gzread.c \
    gzwrite.c \
    infback.c \
    inffast.c \
    inflate.c \
    inftrees.c \
    trees.c \
    uncompr.c \
    zutil.c

HEADERS += \
    crc32.h \
    deflate.h \
    gzguts.h \
    inffast.h \
    inffixed.h \
    inflate.h \
    inftrees.h \
    trees.h \
    zconf.h \
    zlib.h \
    zutil.h

OTHER_FILES += \
    win32/zlib.def \
    win32/zlib1.rc

