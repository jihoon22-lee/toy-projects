TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
QT -= gui

TARGET = test_cleanup

INCLUDEPATH += ../include

SOURCES += \
    test_cleanup.cpp \
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
    ../include/diskmap/scanner.hpp \
    ../include/diskmap/view.hpp
