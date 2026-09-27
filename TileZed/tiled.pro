!win32:error("PZ Mapping Tools requires Windows.")
lessThan(QT_MAJOR_VERSION, 5):error("PZ Mapping Tools requires Qt 5 or newer.")

TEMPLATE  = subdirs
CONFIG   += ordered

SUBDIRS = initvars.pro src translations
