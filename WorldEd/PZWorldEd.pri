# See the README file for instructions about setting the install prefix.
isEmpty(PREFIX):PREFIX = /usr/local
isEmpty(LIBDIR):LIBDIR = $${PREFIX}/lib

macx {
    include(../shared/macos.pri)
}

DEFINES += ZOMBOID WORLDED
