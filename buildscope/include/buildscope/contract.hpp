#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>
#include <stdexcept>

namespace buildscope {

inline constexpr auto kSnapshotSchemaV1 = "buildscope.snapshot/v1";
inline constexpr auto kSnapshotSchemaV2 = "buildscope.snapshot/v2";
inline constexpr auto kSnapshotSchemaV3 = "buildscope.snapshot/v3";
inline constexpr auto kSnapshotSchema = kSnapshotSchemaV3;

struct SnapshotPath {
    QString path;
    QString scope;
    QString style;
    std::optional<bool> exists;
};

struct SnapshotCompiler {
    QString family;
    QString name;
    QString path;
    QStringList wrappers;
};

struct SnapshotDefine {
    QString action;
    QString name;
    std::optional<QString> value;
};

struct SnapshotIncludePath {
    QString path;
    QString scope;
    QString style;
    std::optional<bool> exists;
    QString kind;
    qsizetype order = -1;
};

struct SnapshotTarget {
    QString buildTarget;
    QString triple;
};

struct SnapshotNormalized {
    QStringList argv;
    QString commandStyle;
    QString invocationSource;
    SnapshotCompiler compiler;
    QString configuration;
    QVector<SnapshotDefine> defines;
    SnapshotPath directory;
    QVector<SnapshotIncludePath> includePaths;
    QString language;
    std::optional<SnapshotPath> output;
    SnapshotPath source;
    QString standard;
    std::optional<SnapshotPath> sysroot;
    SnapshotTarget target;
};

struct SnapshotState {
    bool duplicate = false;
    qsizetype entryIndex = -1;
    qsizetype sourceConfigurationCount = 0;
    QString sourceStatus;
};

struct SnapshotDiagnostic {
    QString code;
    QString message;
    QString severity;
};

struct SnapshotIncludeSearch {
    QString candidate;
    bool exists = false;
    QString kind;
    qsizetype order = -1;
    bool selected = false;
};

struct SnapshotIncludeEdge {
    QStringList alternatives;
    QString classification;
    QString delimiter;
    QString evidence;
    qsizetype line = 0;
    QString locationEvidence;
    QString parent;
    QString requested;
    std::optional<QString> resolved;
    QVector<SnapshotIncludeSearch> search;
};

struct SnapshotIncludeAnalysis {
    QStringList command;
    QVector<SnapshotDiagnostic> diagnostics;
    qsizetype durationMs = 0;
    QVector<SnapshotIncludeEdge> edges;
    QString evidence;
};

struct SnapshotEntry {
    QString file;
    QString directory;
    QStringList arguments;
    QString command;
    QString output;
    bool hasNormalized = false;
    SnapshotNormalized normalized;
    bool hasState = false;
    SnapshotState state;
    QVector<SnapshotDiagnostic> diagnostics;
    bool hasIncludeAnalysis = false;
    SnapshotIncludeAnalysis includeAnalysis;
};

struct Snapshot {
    QString schemaVersion;
    QString producerVersion;
    QString sourcePath;
    QString projectRoot;
    QVector<SnapshotEntry> entries;
};

class ContractError final : public std::runtime_error {
public:
    explicit ContractError(const QString &message);
};

Snapshot loadSnapshotFile(const QString &path);
QString invocationText(const SnapshotEntry &entry);

}  // namespace buildscope
