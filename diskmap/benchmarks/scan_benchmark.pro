TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt
QT =

TARGET = diskmap-scan-benchmark
INCLUDEPATH += $$PWD/../include

SOURCES += scan_benchmark.cpp

CORE_LIBRARY = $$OUT_PWD/../src/libdiskmap_core.a
PRE_TARGETDEPS += $$CORE_LIBRARY
LIBS += -L$$OUT_PWD/../src -ldiskmap_core

QMAKE_CXXFLAGS += -Wall -Wextra -Werror
