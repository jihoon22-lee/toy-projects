#include "native_analysis.hpp"
#include "native_error.hpp"
#include <algorithm>

namespace buildscope::native {
namespace {
thread_local AnalysisControl *active = nullptr;
}
AnalysisControl::AnalysisControl(AnalysisLimits configured, std::atomic_bool *flag)
    : limits(configured), cancel(flag) {
    if (limits.maxUnits < 1 || limits.maxUnits > 4096 || limits.totalMilliseconds < 1 ||
        limits.totalMilliseconds > 600000 || limits.unitMilliseconds < 1 ||
        limits.unitMilliseconds > 600000 || limits.totalSourceBytes < 1 ||
        limits.unitSourceBytes < 1 || limits.totalFiles < 1 || limits.unitFiles < 1 ||
        limits.totalEdges < 1 || limits.unitEdges < 1 || limits.unitEdges > 100000 ||
        limits.traceBytes < 1 || limits.traceBytes > 16LL * 1024 * 1024)
        throw IncludeAnalysisError(QStringLiteral("invalid analysis budget"));
    totalTimer_.start();
}
bool AnalysisControl::cancelled() const { return cancel && cancel->load(); }
void AnalysisControl::checkpoint() const {
    if (cancelled())
        throw IncludeAnalysisError(QStringLiteral("analysis cancelled"));
    if (totalTimer_.elapsed() >= limits.totalMilliseconds)
        throw IncludeAnalysisError(QStringLiteral("global analysis time budget exhausted"));
    if (unitTimer_.isValid() && unitTimer_.elapsed() >= limits.unitMilliseconds)
        throw IncludeAnalysisError(QStringLiteral("per-unit analysis time budget exhausted"));
}
void AnalysisControl::beginUnit() {
    unitTimer_.invalidate();
    checkpoint();
    if (units >= limits.maxUnits)
        throw IncludeAnalysisError(QStringLiteral("analysis unit count budget exhausted"));
    ++units;
    unitFiles_ = unitEdges_ = 0;
    unitSourceBytes_ = 0;
    unitTimer_.start();
}
void AnalysisControl::readFile() {
    checkpoint();
    if (files >= limits.totalFiles || unitFiles_ >= limits.unitFiles)
        throw IncludeAnalysisError(QStringLiteral("analysis source file budget exhausted"));
    ++files;
    ++unitFiles_;
}
void AnalysisControl::readBytes(qint64 bytes) {
    checkpoint();
    if (bytes < 0 || bytes > limits.totalSourceBytes - sourceBytes ||
        bytes > limits.unitSourceBytes - unitSourceBytes_)
        throw IncludeAnalysisError(QStringLiteral("analysis source byte budget exhausted"));
    sourceBytes += bytes;
    unitSourceBytes_ += bytes;
}
void AnalysisControl::addEdge() {
    if (edges >= limits.totalEdges || unitEdges_ >= limits.unitEdges)
        throw IncludeAnalysisError(QStringLiteral("analysis edge budget exhausted"));
    ++edges;
    ++unitEdges_;
}
qint64 AnalysisControl::remainingSourceBytes() const {
    return std::min(limits.totalSourceBytes - sourceBytes,
                    limits.unitSourceBytes - unitSourceBytes_);
}
int AnalysisControl::remainingMilliseconds() const {
    qint64 remaining = limits.totalMilliseconds - totalTimer_.elapsed();
    if (unitTimer_.isValid())
        remaining = std::min(remaining, limits.unitMilliseconds - unitTimer_.elapsed());
    return int(std::max(qint64(0), remaining));
}
QJsonObject AnalysisControl::metadata(const QString &mode) const {
    return {
        {"mode", mode},
        {"cancelled", cancelled()},
        {"units_attempted", units},
        {"files_read", files},
        {"source_bytes", QString::number(sourceBytes)},
        {"edges_recorded", edges},
        {"limits", QJsonObject{{"units", limits.maxUnits},
                               {"total_ms", limits.totalMilliseconds},
                               {"unit_ms", limits.unitMilliseconds},
                               {"total_source_bytes", QString::number(limits.totalSourceBytes)},
                               {"unit_source_bytes", QString::number(limits.unitSourceBytes)},
                               {"total_files", limits.totalFiles},
                               {"unit_files", limits.unitFiles},
                               {"total_edges", limits.totalEdges},
                               {"unit_edges", limits.unitEdges},
                               {"trace_bytes", QString::number(limits.traceBytes)}}}};
}
AnalysisControl *activeAnalysisControl() { return active; }
AnalysisScope::AnalysisScope(AnalysisControl *control) : previous_(active) { active = control; }
AnalysisScope::~AnalysisScope() { active = previous_; }
void analysisCheckpoint() {
    if (active)
        active->checkpoint();
}
} // namespace buildscope::native
