TEMPLATE = app
include($$PWD/../cxx17.pri)
CONFIG += testcase console
CONFIG -= app_bundle qt
QT =

TARGET = test_duplicates
INCLUDEPATH += $$PWD/../include $$PWD
SOURCES += \
    test_duplicates.cpp

LIBS += -L$$OUT_PWD/../src -ldiskmap_core
PRE_TARGETDEPS += $$OUT_PWD/../src/libdiskmap_core.a

HEADERS += \
    assert.hpp
