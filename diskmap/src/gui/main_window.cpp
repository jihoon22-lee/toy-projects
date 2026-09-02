#include "diskmap/gui/main_window.hpp"

#include <QComboBox>
#include <QFileDialog>
#include <QLabel>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent>

#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "explorer_text.hpp"

namespace {

diskmap::ScanResult runScan(
    const QString& path,
    const diskmap::ScanOptions& options,
    const std::shared_ptr<diskmap::ScanCancellationToken>& cancellation,
    const diskmap::ProgressFn& progress) {
    diskmap::RealFsSource source;
    diskmap::ScanResult result =
        diskmap::scan(source, path.toStdString(), options, progress, cancellation.get());
    if (!result.cancelled) {
        diskmap::sortBySizeDesc(result.root);
    }
    return result;
}

int pluralCount(std::size_t value) {
    const auto maximum = static_cast<std::size_t>(std::numeric_limits<int>::max());
    return value > maximum ? std::numeric_limits<int>::max() : static_cast<int>(value);
}

diskmap::ScanResult failedScanResult(const QString& path,
                                     std::uint64_t generation,
                                     std::string message) {
    diskmap::ScanResult result;
    result.generation = generation;
    result.root.path = path.toStdString();
    result.root.name = result.root.path.filename().string();
    result.root.scan_generation = generation;
    result.root.complete = false;
    result.fatal_error = std::move(message);
    result.error_count = 1;
    result.errors.push_back(result.fatal_error);
    return result;
}

} // namespace

MainWindow::MainWindow(QWidget* parent, ScanRunner scanRunner)
    : QMainWindow(parent), scanRunner_(std::move(scanRunner)) {
    if (!scanRunner_) {
        scanRunner_ = runScan;
    }
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    buildNavigationBar(central, layout);
    buildFilterPanel(central, layout);
    buildExplorer(central, layout);
    setCentralWidget(central);

    status_ = new QLabel(tr("Ready"), this);
    status_->setObjectName(QStringLiteral("status"));
    status_->setAccessibleName(tr("Scan status"));
    status_->setTextFormat(Qt::PlainText);
    statusBar()->addWidget(status_);

    progressTimer_ = new QTimer(this);
    progressTimer_->setInterval(100);
    connect(progressTimer_, &QTimer::timeout, this, [this]() {
        if (activeProgress_) {
            onScanProgress(activeGeneration_,
                           activeProgress_->dirs.load(std::memory_order_relaxed),
                           activeProgress_->files.load(std::memory_order_relaxed),
                           requestedScanPath_);
        }
    });

    connectUi();
    updateBreadcrumb();
    updateMetricExplanation();
    updatePartialBanner();
    updateControlState();
    resize(1180, 800);
    setWindowTitle(tr("diskmap explorer"));
}

MainWindow::~MainWindow() {
    if (activeCancellation_) {
        activeCancellation_->cancel();
    }
    filterTimer_->stop();
    filterTimer_->stop();
}

void MainWindow::chooseFolder() {
    const QString path = QFileDialog::getExistingDirectory(this, tr("Choose a folder to scan"));
    if (!path.isEmpty()) {
        startScan(path, false);
    }
}

void MainWindow::scanPath(const QString& path) { startScan(path, false); }

void MainWindow::rescan() {
    if (!currentScanPath_.isEmpty()) {
        startScan(currentScanPath_, true);
    }
}

void MainWindow::cancelScan() {
    if (!activeCancellation_) {
        return;
    }
    activeCancellation_->cancel();
    status_->setText(tr("Cancelling scan…"));
    updateControlState();
}

void MainWindow::setScanOptions(const diskmap::ScanOptions& options) {
    scanOptions_ = options;
}

void MainWindow::startScan(const QString& path, bool restoreNavigation) {
    if (path.isEmpty()) {
        status_->setText(tr("Choose a non-empty path to scan"));
        return;
    }
    if (activeCancellation_) {
        activeCancellation_->cancel();
    }

    pendingRestore_ = restoreNavigation && document_ != nullptr
                      && path == currentScanPath_;
    pendingTrail_ = pendingRestore_ ? trail_ : std::vector<diskmap::NodeKey>{};
    pendingSelectedKey_ = pendingRestore_ ? selectedKey_ : std::nullopt;
    requestedScanPath_ = path;

    ++activeGeneration_;
    const std::uint64_t generation = activeGeneration_;
    diskmap::ScanOptions options = scanOptions_;
    options.generation = generation;
    const auto cancellation = std::make_shared<diskmap::ScanCancellationToken>();
    const auto progressState = std::make_shared<ScanProgressState>();
    activeCancellation_ = cancellation;
    activeProgress_ = progressState;

    auto* watcher = new QFutureWatcher<diskmap::ScanResult>(this);
    connect(watcher, &QFutureWatcher<diskmap::ScanResult>::finished, this,
            [this, watcher, generation]() { onScanFinished(watcher, generation); });

    const diskmap::ProgressFn progress = [progressState](std::size_t dirs,
                                                         std::size_t files) {
        progressState->dirs.store(dirs, std::memory_order_relaxed);
        progressState->files.store(files, std::memory_order_relaxed);
    };

    status_->setText(tr("Scanning %1…").arg(path));
    progressTimer_->start();
    updateControlState();
    const ScanRunner runner = scanRunner_;
    watcher->setFuture(QtConcurrent::run(
        [runner, path, options, cancellation, progress]() {
            diskmap::ScanResult result;
            try {
                result = runner(path, options, cancellation, progress);
            } catch (const std::exception& error) {
                result = failedScanResult(
                    path, options.generation,
                    std::string("scan worker failed: ") + error.what());
            } catch (...) {
                result = failedScanResult(path, options.generation,
                                          "scan worker failed with an unknown exception");
            }
            if (cancellation->isCancelled()) {
                result.cancelled = true;
                result.root.complete = false;
            }
            return result;
        }));
}

void MainWindow::onScanProgress(std::uint64_t generation,
                                std::size_t dirs,
                                std::size_t files,
                                const QString& path) {
    if (generation != activeGeneration_ || !activeCancellation_
        || activeCancellation_->isCancelled()) {
        return;
    }
    status_->setText(tr("Scanning %1… %2 dirs, %3 files").arg(path).arg(dirs).arg(files));
}

void MainWindow::onScanFinished(QFutureWatcher<diskmap::ScanResult>* watcher,
                                std::uint64_t generation) {
    if (generation != activeGeneration_) {
        watcher->deleteLater();
        return;
    }
    diskmap::ScanResult completed = watcher->result();
    watcher->deleteLater();
    activeCancellation_.reset();
    activeProgress_.reset();
    progressTimer_->stop();

    if (completed.generation != generation
        || completed.root.scan_generation != generation) {
        discardPendingRestore();
        status_->setText(tr("Scan result discarded: generation metadata mismatch"));
        updateControlState();
        return;
    }

    if (completed.cancelled) {
        discardPendingRestore();
        status_->setText(tr("Scan cancelled — partial result discarded"));
        updateControlState();
        return;
    }
    if (!completed.fatal_error.empty()) {
        discardPendingRestore();
        status_->setText(tr("Scan failed: %1")
                             .arg(QString::fromStdString(completed.fatal_error)));
        updateControlState();
        return;
    }

    const std::vector<diskmap::NodeKey> previousTrail = pendingTrail_;
    const std::optional<diskmap::NodeKey> previousSelection = pendingSelectedKey_;
    document_ = std::make_shared<diskmap::ScanResult>(std::move(completed));
    currentScanPath_ = requestedScanPath_;
    if (pendingRestore_) {
        restoreNavigation(previousTrail);
        selectedKey_ = previousSelection;
    } else {
        trail_ = {diskmap::nodeKey(document_->root)};
        selectedKey_.reset();
    }
    discardPendingRestore();
    refreshProjection();
}

void MainWindow::updateDocumentStatus() {
    if (!document_ || activeCancellation_) {
        return;
    }
    const diskmap::SizeMetric metric = static_cast<diskmap::SizeMetric>(
        metricCombo_->currentData().toInt());
    const diskmap::MetricValue rootMetric = diskmap::metricValue(
        document_->root, metric, document_->totals_filtered);
    QString message = tr("%1 dirs, %2 files · %3: %4")
                          .arg(document_->dirs_scanned)
                          .arg(document_->files_scanned)
                          .arg(diskmap_gui_text::metricName(metric),
                               diskmap_gui_text::metricValueText(rootMetric));
    if (document_->error_count > 0) {
        message += tr("  —  %n unreadable path(s)", "",
                      pluralCount(document_->error_count));
    }
    if (document_->totals_filtered) {
        message += tr("  —  %n scanner-filtered entry(s)", "",
                      pluralCount(document_->entries_filtered));
    }
    if (document_->mount_boundaries_skipped > 0) {
        message += tr("  —  %n mount boundary skipped", "",
                      pluralCount(document_->mount_boundaries_skipped));
    }
    status_->setText(message);
}

void MainWindow::discardPendingRestore() {
    pendingRestore_ = false;
    pendingTrail_.clear();
    pendingSelectedKey_.reset();
}
