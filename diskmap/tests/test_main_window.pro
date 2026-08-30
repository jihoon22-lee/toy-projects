TEMPLATE = app
CONFIG += testcase console
CONFIG -= app_bundle
QT += core gui widgets concurrent testlib

TARGET = test_main_window
INCLUDEPATH += $$PWD/../include $$PWD
SOURCES += test_main_window.cpp
LIBS += -L$$OUT_PWD/../src/gui -ldiskmap_gui -L$$OUT_PWD/../src -ldiskmap_core
