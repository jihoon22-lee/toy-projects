#pragma once

#include <QFutureWatcher>
#include <QMainWindow>

#include <vector>

#include "diskmap/scanner.hpp"

class QLabel;
class QPushButton;
class TreemapWidget;

// Owns the scan result and drives the treemap view.
//
// The scan runs on a worker thread through QtConcurrent. The core stays
// thread-agnostic: it takes a progress callback and knows nothing about Qt, so
// the callback here only touches an atomic counter and never the widgets.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Starts a scan without going through the folder dialog, so
    // `diskmap-gui <path>` works and the scan path is testable headlessly.
    void scanPath(const QString& path);

private slots:
    void chooseFolder();
    void goUp();
    void onScanFinished();
    void onNodeActivated(const diskmap::FsNode* node);
    void onHoveredNodeChanged(const diskmap::FsNode* node);

private:
    TreemapWidget* treemap_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* breadcrumb_ = nullptr;
    QPushButton* upButton_ = nullptr;
    QFutureWatcher<diskmap::ScanResult>* watcher_ = nullptr;

    diskmap::ScanResult result_;
    // Path from the scan root down to what is displayed; back() is current.
    std::vector<const diskmap::FsNode*> trail_;
    QString rootPath_;

    void startScan(const QString& path);
    void showNode(const diskmap::FsNode* node);
    void updateBreadcrumb();
};
