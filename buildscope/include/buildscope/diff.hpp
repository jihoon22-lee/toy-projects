#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace buildscope {

inline constexpr auto kDiffSchemaV1 = "buildscope.diff/v1";
inline constexpr auto kDiffPolicyV1 = "buildscope.diff-policy/v1";

struct DiffInput {
    qsizetype configurationCount = 0;
    QString label;
    QString semanticDigest;
    qsizetype sourceCount = 0;
};

struct DiffSuppressionRule {
    QString category;
    QString path;
};

struct DiffPolicy {
    QStringList ignoredFields;
    QVector<DiffSuppressionRule> suppressionRules;
    QString version;
};

struct DiffConfiguration {
    qsizetype entryIndex = 0;
    QJsonObject semantic;
    QString semanticDigest;
};

struct DiffSource {
    std::optional<QString> after;
    std::optional<QString> before;
    QString style;
};

struct DiffChange {
    QJsonValue after;
    QJsonValue before;
    QString category;
    bool suppressed = false;
    std::optional<QString> suppression;
};

struct DiffUnit {
    std::optional<DiffConfiguration> after;
    std::optional<DiffConfiguration> before;
    QVector<DiffChange> changes;
    QString kind;
    DiffSource source;
    bool suppressed = false;
};

struct DiffDiagnostic {
    QString code;
    QString message;
    QString severity;
    QString source;
};

struct DiffSummary {
    qsizetype added = 0;
    qsizetype changed = 0;
    qsizetype changeCount = 0;
    qsizetype moved = 0;
    qsizetype removed = 0;
    qsizetype suppressedChanges = 0;
    qsizetype suppressedUnits = 0;
    qsizetype unchanged = 0;
    qsizetype visibleChanges = 0;
    qsizetype visibleUnits = 0;
};

struct DiffReport {
    QVector<DiffDiagnostic> diagnostics;
    DiffInput afterInput;
    DiffInput beforeInput;
    DiffPolicy policy;
    QString producerVersion;
    QString schemaVersion;
    DiffSummary summary;
    QVector<DiffUnit> units;
};

DiffReport loadDiffFile(const QString &path);
QString renderDiffValue(const QJsonValue &value);

}  // namespace buildscope
