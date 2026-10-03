#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace buildscope::native {

// normalize.py: deterministic compilation database normalization for v2.
QJsonObject normalizeEntry(const QJsonObject &entry, qsizetype index,
                           const QString &projectRoot, const QString &databaseParent);
void annotateEntrySets(QJsonArray &entries);
QString entrySourceKey(const QJsonObject &entry);

}  // namespace buildscope::native
