#pragma once
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace buildscope::native {
struct RootMapping {
    QString from;
    QString to;
};
QList<RootMapping> parseRootMappings(const QStringList &specifications);
QString relocatePath(const QString &path, const QList<RootMapping> &mappings);
QJsonObject relocateInvocation(const QJsonObject &raw, const QList<RootMapping> &mappings);
QJsonObject relocateSnapshot(const QJsonObject &snapshot, const QString &projectRoot,
                             const QList<RootMapping> &mappings);
} // namespace buildscope::native
