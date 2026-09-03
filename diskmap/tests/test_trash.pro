TEMPLATE = app
include($$PWD/../cxx17.pri)
CONFIG += testcase console
CONFIG -= app_bundle qt
QT =

TARGET = test_trash
INCLUDEPATH += $$PWD/../include $$PWD

SOURCES += \
    test_trash.cpp \
    ../src/trash.cpp \
    ../src/cleanup.cpp \
    ../src/fs_source.cpp \
    ../src/view.cpp \
    ../src/view_facts.cpp \
    ../src/fs_node.cpp

HEADERS += \
    assert.hpp \
    ../include/diskmap/cleanup.hpp \
    ../include/diskmap/fs_metadata.hpp \
    ../include/diskmap/fs_node.hpp \
    ../include/diskmap/fs_source.hpp \
    ../include/diskmap/trash.hpp \
    ../include/diskmap/view.hpp
