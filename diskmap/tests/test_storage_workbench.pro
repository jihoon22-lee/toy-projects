TEMPLATE = app
include($$PWD/../cxx17.pri)
CONFIG += testcase console
CONFIG -= app_bundle
QT += core gui widgets concurrent testlib

TARGET = test_storage_workbench
INCLUDEPATH += $$PWD/../include $$PWD
SOURCES += test_storage_workbench.cpp
LIBS += -L$$OUT_PWD/../src/gui -ldiskmap_gui -L$$OUT_PWD/../src -ldiskmap_core
PRE_TARGETDEPS += \
    $$OUT_PWD/../src/gui/libdiskmap_gui.a \
    $$OUT_PWD/../src/libdiskmap_core.a
