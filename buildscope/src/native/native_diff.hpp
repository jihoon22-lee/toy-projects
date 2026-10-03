#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace buildscope::native {

// diff.py: exact bounded configuration diffs between two compile databases.
constexpr int kMaxLabelChars = 256;

QJsonObject compareDatabases(const QString &beforePath, const QString &afterPath,
                             const QString &beforeProjectRoot, const QString &afterProjectRoot,
                             const QString &beforeLabel, const QString &afterLabel,
                             const QStringList &suppressions);
QString dumpsDiff(const QJsonObject &report, bool pretty);

}  // namespace buildscope::native
