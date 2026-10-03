#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace buildscope::native {

// _io.py: stable input and atomic output primitives.
QByteArray readBoundedRegular(const QString &path, qint64 limit);
void writeAtomicText(const QString &target, const QString &text,
                     const QStringList &protectedPaths);

}  // namespace buildscope::native
