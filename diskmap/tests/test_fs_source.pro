TEMPLATE = app
CONFIG += testcase console
CONFIG -= app_bundle qt
QT =

TARGET = test_fs_source
INCLUDEPATH += $$PWD/../include $$PWD
SOURCES += test_fs_source.cpp
LIBS += -L$$OUT_PWD/../src -ldiskmap_core
