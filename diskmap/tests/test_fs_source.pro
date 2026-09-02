TEMPLATE = app
include($$PWD/../cxx17.pri)
CONFIG += testcase console
CONFIG -= app_bundle qt
QT =

TARGET = test_fs_source
INCLUDEPATH += $$PWD/../include $$PWD
SOURCES += test_fs_source.cpp
LIBS += -L$$OUT_PWD/../src -ldiskmap_core
PRE_TARGETDEPS += $$OUT_PWD/../src/libdiskmap_core.a
