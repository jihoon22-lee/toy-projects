#pragma once

#include <QMainWindow>

#include <memory>
#include <optional>
#include <vector>

#include "loglens/filter_expr.hpp"
#include "loglens/log_parser.hpp"
#include "loglens/log_source.hpp"

class LogModel;
class QCheckBox;
class QLabel;
class QLineEdit;
class QTableView;
class QTimer;
class TimelineWidget;

// Loads a log file, shows it filtered, and draws a level histogram over time.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Opens a file without the dialog, so `loglens-gui <path>` works and the
    // load path can be exercised headlessly.
    void openPath(const QString& path);

private slots:
    void chooseFile();
    void applyFilter();
    void pollSource();
    void setFollowing(bool following);

private:
    enum class FollowState { Stopped, Following, WaitingRetry };

    LogModel* model_ = nullptr;
    QTableView* table_ = nullptr;
    QLineEdit* filterEdit_ = nullptr;
    QLabel* status_ = nullptr;
    TimelineWidget* timeline_ = nullptr;
    std::optional<loglens::Filter> filter_;
    // Owned rather than local: the tailer carries the read offset and the
    // restart count, which is the entire state that makes following work.
    std::unique_ptr<loglens::FileTailer> tailer_;
    // The parser owns line numbering, partial bytes and continuation state;
    // both initial open and follow polls feed this same instance.
    loglens::RecordAssembler assembler_;
    QTimer* pollTimer_ = nullptr;
    QCheckBox* followBox_ = nullptr;
    FollowState followState_ = FollowState::Stopped;
    std::size_t retryAttempts_ = 0;
    bool autoScroll_ = true;

    void refreshTimeline();
    void updateStatus(const QString& extra);
    void applyDeltas(const std::vector<loglens::RecordDelta>& deltas);
};
