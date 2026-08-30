TEMPLATE = app
CONFIG += testcase console
CONFIG -= app_bundle
QT += core gui widgets testlib

TARGET = test_treemap_widget
INCLUDEPATH += $$PWD/../include $$PWD $$PWD/../src/gui
SOURCES += test_treemap_widget.cpp
LIBS += -L$$OUT_PWD/../src/gui -ldiskmap_gui -L$$OUT_PWD/../src -ldiskmap_core
