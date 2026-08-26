#include "main_window.hpp"

#include <QApplication>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QtConcurrent>

#include "diskmap/format.hpp"
#include "diskmap/fs_node.hpp"
#include "treemap_widget.hpp"

namespace {

// Runs off the GUI thread. Constructing the source here rather than passing one
// in keeps everything the worker touches local to it.
diskmap::ScanResult runScan(QString path) {
    diskmap::RealFsSource source;
    const diskmap::ScanOptions options;
    diskmap::ScanResult result = diskmap::scan(source, path.toStdString(), options);
    diskmap::sortBySizeDesc(result.root);
    return result;
}

QString describe(const diskmap::FsNode& node) {
    return QStringLiteral("%1  %2")
        .arg(QString::fromStdString(node.name))
        .arg(QString::fromStdString(diskmap::humanBytes(node.size)));
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    auto* bar = new QHBoxLayout();
    auto* chooseButton = new QPushButton(tr("Choose folder…"), central);
    upButton_ = new QPushButton(tr("Up"), central);
    upButton_->setEnabled(false);
    breadcrumb_ = new QLabel(tr("(no scan yet)"), central);
    bar->addWidget(chooseButton);
    bar->addWidget(upButton_);
    bar->addWidget(breadcrumb_, 1);
    layout->addLayout(bar);

    treemap_ = new TreemapWidget(central);
    layout->addWidget(treemap_, 1);

    setCentralWidget(central);
    status_ = new QLabel(tr("Ready"), this);
    statusBar()->addWidget(status_);

    watcher_ = new QFutureWatcher<diskmap::ScanResult>(this);

    connect(chooseButton, &QPushButton::clicked, this, &MainWindow::chooseFolder);
    connect(upButton_, &QPushButton::clicked, this, &MainWindow::goUp);
    connect(watcher_, &QFutureWatcher<diskmap::ScanResult>::finished, this,
            &MainWindow::onScanFinished);
    connect(treemap_, &TreemapWidget::nodeActivated, this, &MainWindow::onNodeActivated);
    connect(treemap_, &TreemapWidget::hoveredNodeChanged, this,
            &MainWindow::onHoveredNodeChanged);

    resize(960, 640);
    setWindowTitle(tr("diskmap"));
}

void MainWindow::chooseFolder() {
    const QString path = QFileDialog::getExistingDirectory(this, tr("Choose a folder to scan"));
    if (path.isEmpty()) {
        return;
    }
    startScan(path);
}

void MainWindow::scanPath(const QString& path) { startScan(path); }

void MainWindow::startScan(const QString& path) {
    if (watcher_->isRunning()) {
        return;
    }
    rootPath_ = path;
    status_->setText(tr("Scanning %1…").arg(path));
    treemap_->setRoot(nullptr);
    watcher_->setFuture(QtConcurrent::run(runScan, path));
}

void MainWindow::onScanFinished() {
    result_ = watcher_->result();
    trail_.clear();
    showNode(&result_.root);

    QString message = tr("%1 dirs, %2 files, %3")
                          .arg(result_.dirs_scanned)
                          .arg(result_.files_scanned)
                          .arg(QString::fromStdString(diskmap::humanBytes(result_.root.size)));
    if (!result_.errors.empty()) {
        // Surfacing the count matters: a scan that silently skipped unreadable
        // directories would report a total that is quietly too small.
        message += tr("  —  %n unreadable path(s)", "", static_cast<int>(result_.errors.size()));
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
