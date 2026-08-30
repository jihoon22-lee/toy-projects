TEMPLATE = app
CONFIG += testcase console
CONFIG -= app_bundle qt
QT =

TARGET = test_scanner_real_safety
INCLUDEPATH += $$PWD/../include $$PWD
SOURCES += test_scanner_real_safety.cpp
LIBS += -L$$OUT_PWD/../src -ldiskmap_core
