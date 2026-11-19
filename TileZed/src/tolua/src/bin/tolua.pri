tolua_exe = $$shell_path($$top_builddir/tolua.exe)

tolua.name = tolua generator
tolua.input = TOLUA_PKG
tolua.depends = $$TOLUA_DEPS $${tolua_exe}
tolua.output = ${QMAKE_FILE_BASE}.tolua.cpp
tolua.commands = $$shell_quote($$tolua_exe) -n $$TOLUA_PKGNAME -o ${QMAKE_FILE_OUT} ${QMAKE_FILE_IN}
tolua.variable_out = GENERATED_SOURCES
QMAKE_EXTRA_COMPILERS += tolua
