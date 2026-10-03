#pragma once

#include <QJsonObject>
#include <QString>

namespace buildscope::native {

// include_analysis.py: bounded include explanation with explicit
// estimated/compiler-measured provenance.
QJsonObject estimateEntry(const QJsonObject &entry, const QString &projectRoot);
QJsonObject analyzeEntry(const QJsonObject &entry, const QString &projectRoot);
// "estimate" and "compiler" analyze every unit within the unit/time budget.
// "delayed" estimates every unit, then replays only units whose normalized
// file path matches one of unitGlobs; replayed units count against the same
// limits and a failed or over-budget replay keeps the estimate plus a
// diagnostic instead of reporting unavailable.
void annotateSnapshot(QJsonObject &snapshot, const QString &projectRoot, const QString &mode,
                      int maxUnits, int budgetSeconds,
                      const QStringList &unitGlobs = {});

constexpr int kMaxEdges = 100000;
constexpr qint64 kMaxSourceBytes = 4 * 1024 * 1024;
constexpr int kDefaultMaxAnalysisUnits = 512;
constexpr int kMaxAnalysisUnits = 4096;
constexpr int kDefaultAnalysisBudgetSeconds = 120;
constexpr int kMaxAnalysisBudgetSeconds = 600;

}  // namespace buildscope::native
