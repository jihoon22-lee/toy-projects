#pragma once

#include <QMainWindow>

#include <optional>

#include "loglens/filter_expr.hpp"

class LogModel;
class QLabel;
class QLineEdit;
class QTableView;
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

private:
    LogModel* model_ = nullptr;
    QTableView* table_ = nullptr;
    QLineEdit* filterEdit_ = nullptr;
    QLabel* status_ = nullptr;
    TimelineWidget* timeline_ = nullptr;
    std::optional<loglens::Filter> filter_;

    void refreshTimeline();
    void updateStatus(const QString& extra);
};
