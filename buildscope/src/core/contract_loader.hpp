#pragma once

#include "buildscope/contract.hpp"

#include <QFile>

#include <functional>

namespace buildscope::detail {

// Internal seam used to exercise failures that can otherwise only occur during a
// filesystem race. Production callers always use loadSnapshotFile(), which
// supplies no hooks.
using SnapshotPostReadHook = std::function<void(QFile &)>;

Snapshot loadSnapshotFileWithPostReadHook(
    const QString &path, const SnapshotPostReadHook &postReadHook);

}  // namespace buildscope::detail
