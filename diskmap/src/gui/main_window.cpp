#include "diskmap/gui/main_window.hpp"

#include <QApplication>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "diskmap/format.hpp"
#include "diskmap/fs_node.hpp"
#include "diskmap/gui/treemap_widget.hpp"

namespace {

// Runs off the GUI thread. Constructing the source here rather than passing one
// in keeps everything the worker touches local to it.
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

QString describe(const diskmap::FsNode& node) {
    return QStringLiteral("%1  %2")
        .arg(QString::fromStdString(node.name))
        .arg(QString::fromStdString(diskmap::humanBytes(node.size)));
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

    auto* bar = new QHBoxLayout();
    auto* chooseButton = new QPushButton(tr("Choose folder…"), central);
    chooseButton->setObjectName(QStringLiteral("chooseFolderButton"));
    chooseButton->setAccessibleName(tr("Choose folder"));
    upButton_ = new QPushButton(tr("Up"), central);
    upButton_->setObjectName(QStringLiteral("upButton"));
    upButton_->setAccessibleName(tr("Go to parent folder"));
    upButton_->setEnabled(false);
    cancelButton_ = new QPushButton(tr("Cancel"), central);
    cancelButton_->setObjectName(QStringLiteral("cancelScanButton"));
    cancelButton_->setAccessibleName(tr("Cancel current scan"));
    cancelButton_->setEnabled(false);
    breadcrumb_ = new QLabel(tr("(no scan yet)"), central);
    breadcrumb_->setObjectName(QStringLiteral("breadcrumb"));
    breadcrumb_->setAccessibleName(tr("Current folder"));
    bar->addWidget(chooseButton);
    bar->addWidget(upButton_);
    bar->addWidget(cancelButton_);
    bar->addWidget(breadcrumb_, 1);
    layout->addLayout(bar);

    treemap_ = new TreemapWidget(central);
    treemap_->setObjectName(QStringLiteral("treemap"));
    treemap_->setAccessibleName(tr("Disk usage treemap"));
    layout->addWidget(treemap_, 1);

    setCentralWidget(central);
    status_ = new QLabel(tr("Ready"), this);
    status_->setObjectName(QStringLiteral("status"));
    status_->setAccessibleName(tr("Scan status"));
    statusBar()->addWidget(status_);

    connect(chooseButton, &QPushButton::clicked, this, &MainWindow::chooseFolder);
    connect(upButton_, &QPushButton::clicked, this, &MainWindow::goUp);
    connect(cancelButton_, &QPushButton::clicked, this, &MainWindow::cancelScan);
    connect(treemap_, &TreemapWidget::nodeActivated, this, &MainWindow::onNodeActivated);
    connect(treemap_, &TreemapWidget::hoveredNodeChanged, this,
            &MainWindow::onHoveredNodeChanged);

    resize(960, 640);
    setWindowTitle(tr("diskmap"));
}

MainWindow::~MainWindow() {
    if (activeCancellation_) {
        activeCancellation_->cancel();
    }
}

void MainWindow::chooseFolder() {
    const QString path = QFileDialog::getExistingDirectory(this, tr("Choose a folder to scan"));
    if (path.isEmpty()) {
        return;
    }
    startScan(path);
}

void MainWindow::scanPath(const QString& path) { startScan(path); }

void MainWindow::cancelScan() {
    if (!activeCancellation_) {
        return;
    }
    activeCancellation_->cancel();
    cancelButton_->setEnabled(false);
    status_->setText(tr("Cancelling scan…"));
}

void MainWindow::setScanOptions(const diskmap::ScanOptions& options) {
    scanOptions_ = options;
}

void MainWindow::startScan(const QString& path) {
    if (path.isEmpty()) {
        status_->setText(tr("Choose a non-empty path to scan"));
        return;
    }
    if (activeCancellation_) {
        activeCancellation_->cancel();
    }

    ++activeGeneration_;
    const std::uint64_t generation = activeGeneration_;
    diskmap::ScanOptions options = scanOptions_;
    options.generation = generation;
    const auto cancellation = std::make_shared<diskmap::ScanCancellationToken>();
    activeCancellation_ = cancellation;

    auto* watcher = new QFutureWatcher<diskmap::ScanResult>(this);
    connect(watcher, &QFutureWatcher<diskmap::ScanResult>::finished, this,
            [this, watcher, generation]() { onScanFinished(watcher, generation); });

    QPointer<MainWindow> window(this);
    const diskmap::ProgressFn progress = [window, generation, path](std::size_t dirs,
                                                                   std::size_t files) {
        if (!window) {
            return;
        }
        QMetaObject::invokeMethod(
            window.data(),
            [window, generation, dirs, files, path]() {
                if (window) {
                    window->onScanProgress(generation, dirs, files, path);
                }
            },
            Qt::QueuedConnection);
    };

    status_->setText(tr("Scanning %1…").arg(path));
    cancelButton_->setEnabled(true);
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
    diskmap::ScanResult completed = watcher->result();
    watcher->deleteLater();
    if (generation != activeGeneration_) {
        return;
    }
    activeCancellation_.reset();
    cancelButton_->setEnabled(false);

    if (completed.cancelled) {
        status_->setText(tr("Scan cancelled — partial result discarded"));
        return;
    }
    if (!completed.fatal_error.empty()) {
        status_->setText(tr("Scan failed: %1")
                             .arg(QString::fromStdString(completed.fatal_error)));
        return;
    }

    result_ = std::move(completed);
    trail_.clear();
    showNode(&result_.root);

    QString message = tr("%1 dirs, %2 files, %3")
                          .arg(result_.dirs_scanned)
                          .arg(result_.files_scanned)
                          .arg(QString::fromStdString(diskmap::humanBytes(result_.root.size)));
    if (result_.error_count > 0) {
        // Surfacing the count matters: a scan that silently skipped unreadable
        // directories would report a total that is quietly too small.
        message +=
            tr("  —  %n unreadable path(s)", "", pluralCount(result_.error_count));
    }
    if (result_.totals_filtered) {
        message += tr("  —  %n filtered entry(s)", "",
                      pluralCount(result_.entries_filtered));
    }
    if (result_.mount_boundaries_skipped > 0) {
        message += tr("  —  %n mount boundary skipped", "",
                      pluralCount(result_.mount_boundaries_skipped));
    }
    status_->setText(message);
}

void MainWindow::showNode(const diskmap::FsNode* node) {
    if (node == nullptr) {
        return;
    }
    trail_.push_back(node);
    treemap_->setRoot(node);
    upButton_->setEnabled(trail_.size() > 1);
    updateBreadcrumb();
}

void MainWindow::goUp() {
    if (trail_.size() <= 1) {
        return;
    }
    trail_.pop_back();
    treemap_->setRoot(trail_.back());
    upButton_->setEnabled(trail_.size() > 1);
    updateBreadcrumb();
}

void MainWindow::updateBreadcrumb() {
    QStringList parts;
    for (const diskmap::FsNode* node : trail_) {
        parts << QString::fromStdString(node->name);
    }
    breadcrumb_->setText(parts.join(QStringLiteral(" / ")));
}

void MainWindow::onNodeActivated(const diskmap::FsNode* node) {
    if (node == nullptr || node->children.empty()) {
        return;
    }
    showNode(node);
}

void MainWindow::onHoveredNodeChanged(const diskmap::FsNode* node) {
    if (node == nullptr) {
        treemap_->setToolTip(QString());
        return;
    }
    treemap_->setToolTip(describe(*node));
}
