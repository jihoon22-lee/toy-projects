TEMPLATE = app
CONFIG += testcase console
CONFIG += c++17
CONFIG -= app_bundle qt
QT =

TARGET = test_view
INCLUDEPATH += $$PWD/../include $$PWD
SOURCES += test_view.cpp
LIBS += -L$$OUT_PWD/../src -ldiskmap_core
PRE_TARGETDEPS += $$OUT_PWD/../src/libdiskmap_core.a
