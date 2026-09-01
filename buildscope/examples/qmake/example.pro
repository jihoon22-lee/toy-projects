TEMPLATE = app
TARGET = buildscope-qmake-example
CONFIG += console
CONFIG -= app_bundle
QT =

SOURCES += \
    src/main.cpp \
    src/message.cpp

HEADERS += \
    include/buildscope-example/message.hpp

INCLUDEPATH += include
DEFINES += BUILDSCOPE_QMAKE_EXAMPLE=1
QMAKE_CXXFLAGS += -std=c++20
