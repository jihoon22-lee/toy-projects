#pragma once

#include <QJsonObject>
#include <QString>

#include <optional>

namespace buildscope::native {

// _paths.py: lexical path normalization and bounded native probing. String
// paths are always returned with forward separators; foreign styles never
// touch the filesystem.
bool looksWindowsPath(const QString &raw);
QString normalizeLexical(const QString &value, const QString &base, const QString &style);
QString projectRelativeLexical(const QString &value, const QString &base,
                               const QString &projectRoot, const QString &style);
QJsonObject pathRecord(const QString &value, const QString &base, const QString &projectRoot,
                       const QString &style, const QString &expected);
bool isNativeStyle(const QString &style);
std::optional<double> nativeMtime(const QString &value, const QString &base,
                                  const QString &style);

}  // namespace buildscope::native
