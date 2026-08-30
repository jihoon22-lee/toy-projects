TEMPLATE = app
QT += core gui widgets concurrent

TARGET = diskmap-gui
INCLUDEPATH += $$PWD/../../include $$PWD

SOURCES += gui_main.cpp

# Subprojects build into <shadow>/<their path>, so the core sits one level up
# from this one and the widget library beside it.
LIBS += -L$$OUT_PWD -ldiskmap_gui -L$$OUT_PWD/.. -ldiskmap_core
