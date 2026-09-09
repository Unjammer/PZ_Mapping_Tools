CONFIG += c++17
QMAKE_LIBDIR += $$top_builddir/lib
QMAKE_SONAME_PREFIX = @rpath
QMAKE_RPATHDIR += @executable_path/../Frameworks \
    @executable_path/../../../../lib \
    @executable_path/../lib
QMAKE_TARGET_BUNDLE_PREFIX = io.github.Unjammer.PZTools
