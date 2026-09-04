TEMPLATE = app
include($$PWD/../cxx17.pri)
CONFIG += testcase console
CONFIG -= app_bundle qt
QT =

TARGET = test_storage_cli
INCLUDEPATH += $$PWD/../include $$PWD/../src $$PWD

SOURCES += \
    test_storage_cli.cpp \
    ../src/storage_cli.cpp

LIBS += -L$$OUT_PWD/../src -ldiskmap_core
PRE_TARGETDEPS += $$OUT_PWD/../src/libdiskmap_core.a

HEADERS += \
    assert.hpp \
    ../src/storage_cli.hpp
