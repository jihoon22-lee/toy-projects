TEMPLATE = lib
include($$PWD/../cxx17.pri)
CONFIG += staticlib
CONFIG -= qt
QT =

# The core is Qt-free so it links anywhere and unit-tests without a QApplication.
TARGET = diskmap_core
INCLUDEPATH += $$PWD/../include

HEADERS += \
    $$PWD/../include/diskmap/format.hpp \
    $$PWD/../include/diskmap/fs_metadata.hpp \
    $$PWD/../include/diskmap/fs_node.hpp \
    $$PWD/../include/diskmap/fs_source.hpp \
    $$PWD/../include/diskmap/scanner.hpp \
    $$PWD/../include/diskmap/treemap.hpp \
    $$PWD/../include/diskmap/view.hpp

# main.cpp is the CLI entry point and stays out of the library so tests can link
# it without a second main(). ici excludes entry points from coverage scope for
# the same reason.
SOURCES += \
    format.cpp \
    fs_node.cpp \
    fs_source.cpp \
    scanner.cpp \
    treemap.cpp \
    view.cpp
