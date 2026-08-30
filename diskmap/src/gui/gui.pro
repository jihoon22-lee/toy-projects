TEMPLATE = lib
CONFIG += staticlib
QT += core gui widgets concurrent

# The widgets live in a library rather than only in the executable so tests can
# link them. A Q_OBJECT class needs moc-generated sources, which is exactly what
# ici could not provide before the build adapter existed.
#
# HEADERS is what qmake feeds to moc, so the headers must be named here now that
# they sit under include/ rather than beside their .cpp.
TARGET = diskmap_gui
INCLUDEPATH += $$PWD/../../include

HEADERS += \
    $$PWD/../../include/diskmap/gui/main_window.hpp \
    $$PWD/../../include/diskmap/gui/treemap_widget.hpp

SOURCES += main_window.cpp treemap_widget.cpp
