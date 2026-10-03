#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QString>
#include <atomic>
#include <functional>

namespace buildscope::native {

struct AnalysisLimits {
    int maxUnits = 512;
    int totalMilliseconds = 120000;
    int unitMilliseconds = 15000;
    qint64 totalSourceBytes = 256LL * 1024 * 1024;
    qint64 unitSourceBytes = 16LL * 1024 * 1024;
    int totalFiles = 65536;
    int unitFiles = 4096;
    int totalEdges = 500000;
    int unitEdges = 100000;
    qint64 traceBytes = 16LL * 1024 * 1024;
};

// A control belongs to one analysis job. Cancellation is the only cross-thread mutation.
class AnalysisControl {
  public:
    explicit AnalysisControl(AnalysisLimits limits = {}, std::atomic_bool *cancel = nullptr);
    AnalysisLimits limits;
    std::atomic_bool *cancel = nullptr;
    std::function<void(int, int)> progress;
    // Observation seam for deterministic source-mutation tests and instrumentation.
    std::function<void(const QString &, qint64)> sourceReadObserver;
    int units = 0, files = 0, edges = 0;
    qint64 sourceBytes = 0;
    bool cancelled() const;
    void beginUnit();
    void checkpoint() const;
    void readFile();
    void readBytes(qint64 bytes);
    void addEdge();
    int remainingMilliseconds() const;
    qint64 remainingSourceBytes() const;
    QJsonObject metadata(const QString &mode) const;

  private:
    QElapsedTimer totalTimer_, unitTimer_;
    int unitFiles_ = 0, unitEdges_ = 0;
    qint64 unitSourceBytes_ = 0;
};

// Internal scoped thread-local context keeps deep scanner/replay checkpoints
// attached to their worker without global cancellation or cross-job counters.
AnalysisControl *activeAnalysisControl();
class AnalysisScope {
  public:
    explicit AnalysisScope(AnalysisControl *control);
    ~AnalysisScope();
    AnalysisScope(const AnalysisScope &) = delete;

  private:
    AnalysisControl *previous_;
};
void analysisCheckpoint();

} // namespace buildscope::native
