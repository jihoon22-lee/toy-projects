#pragma once

#include <QMainWindow>

#include <memory>
#include <optional>

#include "loglens/filter_expr.hpp"
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
    LogModel* model_ = nullptr;
    QTableView* table_ = nullptr;
    QLineEdit* filterEdit_ = nullptr;
    QLabel* status_ = nullptr;
    TimelineWidget* timeline_ = nullptr;
    std::optional<loglens::Filter> filter_;
    // Owned rather than local: the tailer carries the read offset and the
    // restart count, which is the entire state that makes following work.
    std::unique_ptr<loglens::FileTailer> tailer_;
    QTimer* pollTimer_ = nullptr;
    QCheckBox* followBox_ = nullptr;
    bool autoScroll_ = true;

    void refreshTimeline();
    void updateStatus(const QString& extra);
};
