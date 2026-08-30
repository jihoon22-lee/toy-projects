TEMPLATE = app
CONFIG += testcase console
CONFIG -= app_bundle qt
QT =

TARGET = test_scanner
INCLUDEPATH += $$PWD/../include $$PWD
SOURCES += test_scanner.cpp
LIBS += -L$$OUT_PWD/../src -ldiskmap_core
