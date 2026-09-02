#pragma once

#include <QMetaType>

#include "diskmap/view.hpp"

// Keep Qt registration at the GUI boundary so the core NodeKey type remains
// usable without Qt. The value type is safe in queued signals and QVariant on
// both supported Qt majors.
Q_DECLARE_METATYPE(diskmap::NodeKey)
