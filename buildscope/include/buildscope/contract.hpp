#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <stdexcept>

namespace buildscope {

inline constexpr auto kSnapshotSchema = "buildscope.snapshot/v1";

struct SnapshotEntry {
    QString file;
    QString directory;
    QStringList arguments;
    QString command;
    QString output;
};

struct Snapshot {
    QString schemaVersion;
    QString producerVersion;
    QString sourcePath;
    QVector<SnapshotEntry> entries;
};

class ContractError final : public std::runtime_error {
public:
    explicit ContractError(const QString &message);
};

Snapshot loadSnapshotFile(const QString &path);
QString invocationText(const SnapshotEntry &entry);

}  // namespace buildscope
