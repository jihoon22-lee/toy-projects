#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace buildscope::native {

// diff_policy.py: versioned semantic policy for deterministic config diffs.
QString sourceKey(const QJsonObject &entry);
QString sourcePath(const QJsonObject &entry);
QJsonObject semanticConfiguration(const QJsonObject &entry, const QString &projectRoot,
                                  const QString &databaseParent);
QJsonObject configurationView(const QJsonObject &entry, const QString &projectRoot,
                              const QString &databaseParent);
QString roleKey(const QJsonObject &view);
QJsonArray parseSuppressions(const QStringList &values);
QString matchingSuppression(const QJsonArray &rules, const QString &category,
                            const QString &before, const QString &after, bool windows);
QJsonObject policyRecord(const QJsonArray &rules);

extern const QStringList kIgnoredFields;

}  // namespace buildscope::native
