#pragma once

#include <QFutureWatcher>
#include <QMainWindow>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "diskmap/gui/node_key_metatype.hpp"
#include "diskmap/gui/node_table_model.hpp"
#include "diskmap/scanner.hpp"

class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableView;
class QTimer;
class QVBoxLayout;
class QWidget;
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
    void rescan();
    void goUp();
    void applyFilters();
    void onTableActivated(const QModelIndex& index);
    void onTableCurrentChanged(const QModelIndex& current,
                               const QModelIndex& previous);
    void onTableSortChanged(int column, Qt::SortOrder order);
    void onNodeActivated(diskmap::NodeKey key);
    void onNodeHovered(diskmap::NodeKey key);
    void clearHover();

private:
    QPushButton* chooseButton_ = nullptr;
    QPushButton* rescanButton_ = nullptr;
    TreemapWidget* treemap_ = nullptr;
    NodeTableModel* tableModel_ = nullptr;
    QTableView* table_ = nullptr;
    QLabel* status_ = nullptr;
    QWidget* breadcrumbBar_ = nullptr;
    QHBoxLayout* breadcrumbLayout_ = nullptr;
    QLabel* partialBanner_ = nullptr;
    QLabel* metricExplanation_ = nullptr;
    QLabel* projectionSummary_ = nullptr;
    QLabel* legend_ = nullptr;
    QLineEdit* searchEdit_ = nullptr;
    QLineEdit* minimumSizeEdit_ = nullptr;
    QLineEdit* maximumSizeEdit_ = nullptr;
    QComboBox* metricCombo_ = nullptr;
    QComboBox* typeCombo_ = nullptr;
    QComboBox* ageCombo_ = nullptr;
    QComboBox* issueCombo_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QTimer* filterTimer_ = nullptr;
    QPushButton* upButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    std::shared_ptr<diskmap::ScanCancellationToken> activeCancellation_;
    ScanRunner scanRunner_;
    diskmap::ScanOptions scanOptions_;
    std::uint64_t activeGeneration_ = 0;

    std::shared_ptr<const diskmap::ScanResult> document_;
    // Stable keys, never borrowed pointers, survive filter changes and rescan.
    std::vector<diskmap::NodeKey> trail_;
    std::optional<diskmap::NodeKey> selectedKey_;
    std::vector<diskmap::NodeKey> pendingTrail_;
    std::optional<diskmap::NodeKey> pendingSelectedKey_;
    QString currentScanPath_;
    QString requestedScanPath_;
    QString filterError_;
    bool pendingRestore_ = false;
    bool refreshingProjection_ = false;

    void buildNavigationBar(QWidget* central, QVBoxLayout* layout);
    void buildFilterPanel(QWidget* central, QVBoxLayout* layout);
    void buildExplorer(QWidget* central, QVBoxLayout* layout);
    void connectUi();
    void startScan(const QString& path, bool restoreNavigation = false);
    void onScanProgress(std::uint64_t generation,
                        std::size_t dirs,
                        std::size_t files,
                        const QString& path);
    void onScanFinished(QFutureWatcher<diskmap::ScanResult>* watcher,
                        std::uint64_t generation);
    void restoreNavigation(const std::vector<diskmap::NodeKey>& previousTrail);
    void navigateToKey(const diskmap::NodeKey& key);
    void navigateToBreadcrumb(std::size_t index);
    void refreshProjection();
    diskmap::ViewFilter viewFilterFromControls();
    diskmap::SortSpec sortSpecFromControls() const;
    const diskmap::FsNode* currentNode() const;
    void updateBreadcrumb();
    void updateMetricExplanation();
    void updatePartialBanner();
    void updateControlState();
    void restoreSelection();
};
