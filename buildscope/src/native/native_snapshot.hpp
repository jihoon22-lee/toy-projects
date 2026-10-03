#pragma once

#include <QJsonObject>
#include <QString>

namespace buildscope::native {

// snapshot.py: bounded, shell-free compilation database ingestion.
constexpr qint64 kMaxDatabaseBytes = 64 * 1024 * 1024;
constexpr qint64 kMaxSnapshotBytes = 256 * 1024 * 1024;
constexpr int kMaxEntries = 100000;
constexpr qsizetype kMaxFieldChars = 1024 * 1024;

QJsonObject loadCompilationDatabase(const QString &path, const QString &projectRoot);
QString dumpsSnapshot(const QJsonObject &snapshot, bool pretty);
QJsonObject snapshotForSchema(QJsonObject snapshot, const QString &schema);

}  // namespace buildscope::native
