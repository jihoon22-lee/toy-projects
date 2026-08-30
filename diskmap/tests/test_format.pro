TEMPLATE = app
CONFIG += testcase console
CONFIG -= app_bundle qt
QT =

TARGET = test_format
INCLUDEPATH += $$PWD/../include $$PWD
SOURCES += test_format.cpp
LIBS += -L$$OUT_PWD/../src -ldiskmap_core
