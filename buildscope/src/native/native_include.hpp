#pragma once

#include <QJsonObject>
#include <QString>

namespace buildscope::native {

// include_analysis.py: bounded include explanation with explicit
// estimated/compiler-measured provenance.
QJsonObject estimateEntry(const QJsonObject &entry, const QString &projectRoot);
QJsonObject analyzeEntry(const QJsonObject &entry, const QString &projectRoot);
void annotateSnapshot(QJsonObject &snapshot, const QString &projectRoot, const QString &mode,
                      int maxUnits, int budgetSeconds);

constexpr int kMaxEdges = 100000;
constexpr qint64 kMaxSourceBytes = 4 * 1024 * 1024;
constexpr int kDefaultMaxAnalysisUnits = 512;
constexpr int kMaxAnalysisUnits = 4096;
constexpr int kDefaultAnalysisBudgetSeconds = 120;
constexpr int kMaxAnalysisBudgetSeconds = 600;

}  // namespace buildscope::native
