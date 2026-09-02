TEMPLATE = app
include($$PWD/../cxx17.pri)
CONFIG += console
CONFIG -= app_bundle qt
QT =

# The Qt-free CLI. Kept out of src.pro because a library cannot hold a main(),
# and ici excludes entry points from coverage scope for the same reason.
TARGET = diskmap
INCLUDEPATH += $$PWD/../include

SOURCES += main.cpp
LIBS += -L$$OUT_PWD -ldiskmap_core
PRE_TARGETDEPS += $$OUT_PWD/libdiskmap_core.a
