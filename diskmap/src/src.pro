TEMPLATE = lib
include($$PWD/../cxx17.pri)
CONFIG += staticlib
CONFIG -= qt
QT =

# The core is Qt-free so it links anywhere and unit-tests without a QApplication.
TARGET = diskmap_core
INCLUDEPATH += $$PWD/../include

HEADERS += \
    $$PWD/../include/diskmap/cleanup.hpp \
    $$PWD/../include/diskmap/duplicates.hpp \
    $$PWD/../include/diskmap/format.hpp \
    $$PWD/../include/diskmap/fs_metadata.hpp \
    $$PWD/../include/diskmap/fs_node.hpp \
    $$PWD/../include/diskmap/fs_source.hpp \
    $$PWD/../include/diskmap/scanner.hpp \
    $$PWD/../include/diskmap/snapshot.hpp \
    $$PWD/../include/diskmap/trash.hpp \
    $$PWD/../include/diskmap/treemap.hpp \
    $$PWD/../include/diskmap/view.hpp \
    snapshot_json_dom.hpp

# main.cpp is the CLI entry point and stays out of the library so tests can link
# it without a second main(). ici excludes entry points from coverage scope for
# the same reason.
SOURCES += \
    cleanup.cpp \
    duplicates.cpp \
    duplicates_access.cpp \
    duplicates_evidence.cpp \
    duplicates_hash.cpp \
    format.cpp \
    fs_node.cpp \
    fs_source.cpp \
    scanner.cpp \
    snapshot.cpp \
    snapshot_diff.cpp \
    snapshot_io.cpp \
    snapshot_write.cpp \
    snapshot_json_dom.cpp \
    snapshot_json.cpp \
    snapshot_json_parser.cpp \
    trash.cpp \
    trash_info.cpp \
    trash_metadata.cpp \
    trash_linux_fs.cpp \
    trash_linux_ops.cpp \
    trash_linux_restore.cpp \
    treemap.cpp \
    view.cpp \
    view_facts.cpp
