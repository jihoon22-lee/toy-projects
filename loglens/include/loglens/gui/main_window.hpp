#pragma once

#include <QMainWindow>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "loglens/filter_expr.hpp"
#include "loglens/gui/log_load_worker.hpp"
#include "loglens/log_parser.hpp"
#include "loglens/ring_buffer.hpp"

class LogModel;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableView;
class QThread;
class QTimer;
class TimelineWidget;

// Loads a log file, shows it filtered, and draws a level histogram over time.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr,
                        std::size_t recordCapacity = loglens::kDefaultRecordCapacity,
                        std::size_t sourceChunkBytes = loglens::kDefaultSourceChunkBytes);
    ~MainWindow() override;

    // Opens a file without the dialog, so `loglens-gui <path>` works and the
    // load path can be exercised headlessly.
    void openPath(const QString& path);
    void openPath(const QString& path, loglens::InitialLoadMode mode,
                  std::size_t tailRecords);

signals:
    void startLoadRequested(loglens::LoadRequest request);
    void pollRequested(quint64 jobId);
    void acknowledgeRequested(quint64 jobId, quint64 sequence);
    void loadProgress(qulonglong seen, qulonglong retained, bool initialComplete,
                      QString error);

private slots:
    void chooseFile();
    void applyFilter();
    void pollSource();
    void setFollowing(bool following);
    void handleLoadBatch(loglens::LoadBatch batch);

private:
    enum class FollowState { Stopped, Following, WaitingRetry };

    LogModel* model_ = nullptr;
    QTableView* table_ = nullptr;
    QLineEdit* filterEdit_ = nullptr;
    QLineEdit* searchEdit_ = nullptr;
    QComboBox* loadMode_ = nullptr;
    QSpinBox* tailRecords_ = nullptr;
    QLabel* status_ = nullptr;
    TimelineWidget* timeline_ = nullptr;
    std::optional<loglens::Filter> filter_;
    QThread* loaderThread_ = nullptr;
    loglens::LogLoadWorker* loader_ = nullptr;
    QTimer* pollTimer_ = nullptr;
    QTimer* timelineTimer_ = nullptr;
    QCheckBox* followBox_ = nullptr;
    FollowState followState_ = FollowState::Stopped;
    std::size_t retryAttempts_ = 0;
    bool autoScroll_ = true;
    bool backlog_pending_ = false;
    bool source_active_ = false;
    std::size_t source_chunk_bytes_ = loglens::kDefaultSourceChunkBytes;
    std::size_t record_capacity_ = loglens::kDefaultRecordCapacity;
    quint64 active_job_ = 0;
    quint64 expected_sequence_ = 0;

    void refreshTimeline();
    void scheduleTimelineRefresh();
    void updateStatus(const QString& extra);
    void applyDeltas(const std::vector<loglens::RecordDelta>& deltas);
    void handleLoadError(const loglens::LoadBatch& batch);
    loglens::InitialLoadMode selectedLoadMode() const;
};
