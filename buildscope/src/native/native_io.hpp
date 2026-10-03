#pragma once

#include <atomic>
#include <QByteArray>
#include <QString>
#include <QStringList>

namespace buildscope::native {

// _io.py: stable input and atomic output primitives.
QByteArray readBoundedRegular(const QString &path, qint64 limit, std::atomic_bool *cancel = nullptr);
void writeAtomicText(const QString &target, const QString &text,
                     const QStringList &protectedPaths);

}  // namespace buildscope::native
