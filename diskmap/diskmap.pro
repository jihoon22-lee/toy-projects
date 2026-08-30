TEMPLATE = subdirs
CONFIG += ordered

# ici drives this file: the build, test and sanitize engines each configure
# their own shadow tree from here and run `make check`.
#
# Subprojects are named so the dependencies can be stated rather than implied by
# ordering alone. In a shadow build each one lands at <shadow>/<its path>, which
# is what the relative LIBS paths below them rely on.
core.file  = src/src.pro
cli.file   = src/cli.pro
cli.depends  = core
gui.file   = src/gui/gui.pro
gui.depends  = core
app.file   = src/gui/app.pro
app.depends  = core gui
tests.file = tests/tests.pro
tests.depends = core gui

SUBDIRS = core cli gui app tests
