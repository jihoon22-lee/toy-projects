#pragma once

#include <QFutureWatcher>
#include <QMainWindow>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "diskmap/scanner.hpp"

class QLabel;
class QPushButton;
class TreemapWidget;

// Owns the scan result and drives the treemap view.
//
// The scan runs on a worker thread through QtConcurrent. The core stays
// thread-agnostic: it takes a progress callback and knows nothing about Qt.
// MainWindow marshals progress back to the GUI thread and rejects stale scan
// generations before touching widgets.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    using ScanRunner = std::function<diskmap::ScanResult(
        const QString&,
        const diskmap::ScanOptions&,
        const std::shared_ptr<diskmap::ScanCancellationToken>&,
        const diskmap::ProgressFn&)>;

    explicit MainWindow(QWidget* parent = nullptr, ScanRunner scanRunner = {});
    ~MainWindow() override;

    // Starts a scan without going through the folder dialog, so
    // `diskmap-gui <path>` works and the scan path is testable headlessly.
    void scanPath(const QString& path);
    void cancelScan();
    void setScanOptions(const diskmap::ScanOptions& options);

private slots:
    void chooseFolder();
    void goUp();
    void onNodeActivated(const diskmap::FsNode* node);
    void onHoveredNodeChanged(const diskmap::FsNode* node);

private:
    TreemapWidget* treemap_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* breadcrumb_ = nullptr;
    QPushButton* upButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    std::shared_ptr<diskmap::ScanCancellationToken> activeCancellation_;
    ScanRunner scanRunner_;
    diskmap::ScanOptions scanOptions_;
    std::uint64_t activeGeneration_ = 0;

    diskmap::ScanResult result_;
    // Path from the scan root down to what is displayed; back() is current.
    std::vector<const diskmap::FsNode*> trail_;

    void startScan(const QString& path);
    void onScanProgress(std::uint64_t generation,
                        std::size_t dirs,
                        std::size_t files,
                        const QString& path);
    void onScanFinished(QFutureWatcher<diskmap::ScanResult>* watcher,
                        std::uint64_t generation);
    void showNode(const diskmap::FsNode* node);
    void updateBreadcrumb();
};
