#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace buildscope::native {

// _metadata.py: compiler flag metadata extraction with ordered results.
QJsonObject diagnostic(const QString &code, const QString &message,
                       const QString &severity = QStringLiteral("warning"));

struct ExtractedMetadata {
    QJsonArray defines;
    QJsonArray diagnostics;
    QJsonArray includePaths;
    QString language;
    QString standard;
    QString sysroot;
    QString targetTriple;
};

ExtractedMetadata extractMetadata(const QStringList &argv, const QString &source);
QString outputFromArgv(const QStringList &argv);
QString cmakeTarget(const QString &output);

}  // namespace buildscope::native
