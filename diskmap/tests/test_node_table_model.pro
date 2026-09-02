TEMPLATE = app
include($$PWD/../cxx17.pri)
CONFIG += testcase console
CONFIG -= app_bundle
QT += core gui testlib

TARGET = test_node_table_model
INCLUDEPATH += $$PWD/../include $$PWD
SOURCES += test_node_table_model.cpp
LIBS += -L$$OUT_PWD/../src/gui -ldiskmap_gui -L$$OUT_PWD/../src -ldiskmap_core
PRE_TARGETDEPS += \
    $$OUT_PWD/../src/gui/libdiskmap_gui.a \
    $$OUT_PWD/../src/libdiskmap_core.a
