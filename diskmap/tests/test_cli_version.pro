TEMPLATE = app
include($$PWD/../cxx17.pri)
CONFIG += testcase console
CONFIG -= app_bundle qt
QT =

TARGET = test_cli_version
INCLUDEPATH += $$PWD

# This test runs the linked CLI rather than linking its translation unit, so it
# needs the binary's path and needs it to exist before the test runs.
DEFINES += DISKMAP_BINARY=\\\"$$OUT_PWD/../src/diskmap\\\"
PRE_TARGETDEPS += $$OUT_PWD/../src/diskmap

SOURCES += test_cli_version.cpp

HEADERS += assert.hpp
