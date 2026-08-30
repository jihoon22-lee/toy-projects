TEMPLATE = app
CONFIG += testcase console
CONFIG -= app_bundle qt
QT =

TARGET = test_treemap
INCLUDEPATH += $$PWD/../include $$PWD
SOURCES += test_treemap.cpp
LIBS += -L$$OUT_PWD/../src -ldiskmap_core
