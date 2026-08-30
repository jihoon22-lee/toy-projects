TEMPLATE = lib
CONFIG += staticlib
QT += core gui widgets concurrent

# The widgets live in a library rather than only in the executable so tests can
# link them. A Q_OBJECT class needs moc-generated sources, which is exactly what
# ici could not provide before the build adapter existed.
TARGET = diskmap_gui
INCLUDEPATH += $$PWD/../../include $$PWD

HEADERS += main_window.hpp treemap_widget.hpp
SOURCES += main_window.cpp treemap_widget.cpp
