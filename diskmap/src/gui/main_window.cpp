#include "diskmap/gui/main_window.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "diskmap/format.hpp"
#include "diskmap/fs_node.hpp"
#include "diskmap/gui/treemap_widget.hpp"
#include "diskmap/view.hpp"

namespace {

constexpr int kAnyValue = -1;
constexpr int kAnyIssue = -1;
constexpr int kProblemIssues = -2;
constexpr std::int64_t kDayNanoseconds = 86'400'000'000'000LL;

enum AgeChoice {
    AnyAge = 0,
    LastDay,
    LastWeek,
    LastMonth,
    OlderThanMonth,
};

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

QString issueName(diskmap::NodeIssue issue) {
    switch (issue) {
    case diskmap::NodeIssue::None:
        return QStringLiteral("complete");
    case diskmap::NodeIssue::Incomplete:
        return QStringLiteral("incomplete subtree");
    case diskmap::NodeIssue::CycleSkipped:
        return QStringLiteral("cycle skipped");
    case diskmap::NodeIssue::MountBoundarySkipped:
        return QStringLiteral("mount boundary skipped");
    case diskmap::NodeIssue::DepthLimitReached:
        return QStringLiteral("depth limit reached");
    case diskmap::NodeIssue::MetadataUnknown:
        return QStringLiteral("metadata unknown");
    case diskmap::NodeIssue::Error:
        return QStringLiteral("scan error");
    case diskmap::NodeIssue::ScannerFiltered:
        return QStringLiteral("scanner-level filtering");
    }
    return QStringLiteral("unknown state");
}

QString metricValueText(const diskmap::MetricValue& value) {
    const QString amount = QString::fromStdString(diskmap::humanBytes(value.bytes));
    if (value.known) {
        return amount;
    }
    return value.bytes == 0 ? QStringLiteral("Unknown")
                            : QStringLiteral("At least %1").arg(amount);
}

QString describe(const diskmap::FsNode& node, bool scannerTotalsFiltered) {
    const diskmap::MetricValue logical = diskmap::metricValue(
        node, diskmap::SizeMetric::Logical, scannerTotalsFiltered);
    const diskmap::MetricValue allocated = diskmap::metricValue(
        node, diskmap::SizeMetric::Allocated, scannerTotalsFiltered);
    const diskmap::MetricValue reclaimable = diskmap::metricValue(
        node, diskmap::SizeMetric::Reclaimable, scannerTotalsFiltered);
    return QStringLiteral("%1\nLogical: %2\nAllocated: %3\nReclaimable: %4\nState: %5")
        .arg(QString::fromStdString(diskmap::normalizedPath(node)),
             metricValueText(logical), metricValueText(allocated),
             metricValueText(reclaimable),
             issueName(diskmap::classifyNodeIssue(node, scannerTotalsFiltered)));
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

std::string utf8Text(const QString& text) {
    const QByteArray encoded = text.toUtf8();
    return std::string(encoded.constData(), static_cast<std::size_t>(encoded.size()));
}

std::optional<std::uint64_t> unsignedValue(const QLineEdit& edit, bool& valid) {
    const QString text = edit.text().trimmed();
    if (text.isEmpty()) {
        valid = true;
        return std::nullopt;
    }
    bool converted = false;
    const qulonglong value = text.toULongLong(&converted);
    valid = converted;
    return converted ? std::optional<std::uint64_t>(value) : std::nullopt;
}

diskmap::SizeMetric metricForColumn(int column,
                                    diskmap::SizeMetric fallback) {
    if (column == NodeTableModel::LogicalColumn) {
        return diskmap::SizeMetric::Logical;
    }
    if (column == NodeTableModel::AllocatedColumn) {
        return diskmap::SizeMetric::Allocated;
    }
    if (column == NodeTableModel::ReclaimableColumn) {
        return diskmap::SizeMetric::Reclaimable;
    }
    return fallback;
}

int columnForMetric(diskmap::SizeMetric metric) {
    switch (metric) {
    case diskmap::SizeMetric::Logical:
        return NodeTableModel::LogicalColumn;
    case diskmap::SizeMetric::Allocated:
        return NodeTableModel::AllocatedColumn;
    case diskmap::SizeMetric::Reclaimable:
        return NodeTableModel::ReclaimableColumn;
    }
    return NodeTableModel::LogicalColumn;
}

void applyAgeBounds(int age, std::int64_t now, diskmap::ViewFilter& filter) {
    if (age == LastDay) {
        filter.modified_after_ns = now - kDayNanoseconds;
    } else if (age == LastWeek) {
        filter.modified_after_ns = now - 7 * kDayNanoseconds;
    } else if (age == LastMonth) {
        filter.modified_after_ns = now - 30 * kDayNanoseconds;
    } else if (age == OlderThanMonth) {
        filter.modified_before_ns = now - 30 * kDayNanoseconds;
    }
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
}

void MainWindow::buildNavigationBar(QWidget* central, QVBoxLayout* layout) {
    auto* bar = new QHBoxLayout();
    chooseButton_ = new QPushButton(tr("Choose folder…"), central);
    chooseButton_->setObjectName(QStringLiteral("chooseFolderButton"));
    chooseButton_->setAccessibleName(tr("Choose folder"));
    rescanButton_ = new QPushButton(tr("Rescan"), central);
    rescanButton_->setObjectName(QStringLiteral("rescanButton"));
    rescanButton_->setAccessibleName(tr("Rescan the current folder"));
    upButton_ = new QPushButton(tr("Up"), central);
    upButton_->setObjectName(QStringLiteral("upButton"));
    upButton_->setAccessibleName(tr("Go to parent folder"));
    cancelButton_ = new QPushButton(tr("Cancel"), central);
    cancelButton_->setObjectName(QStringLiteral("cancelScanButton"));
    cancelButton_->setAccessibleName(tr("Cancel current scan"));

    breadcrumbBar_ = new QWidget(central);
    breadcrumbBar_->setObjectName(QStringLiteral("breadcrumb"));
    breadcrumbBar_->setAccessibleName(tr("Current folder path"));
    breadcrumbLayout_ = new QHBoxLayout(breadcrumbBar_);
    breadcrumbLayout_->setContentsMargins(4, 0, 0, 0);
    breadcrumbLayout_->setSpacing(2);

    bar->addWidget(chooseButton_);
    bar->addWidget(rescanButton_);
    bar->addWidget(upButton_);
    bar->addWidget(cancelButton_);
    bar->addWidget(breadcrumbBar_, 1);
    layout->addLayout(bar);
}

void MainWindow::buildFilterPanel(QWidget* central, QVBoxLayout* layout) {
    auto* panel = new QGridLayout();
    searchEdit_ = new QLineEdit(central);
    searchEdit_->setObjectName(QStringLiteral("searchEdit"));
    searchEdit_->setAccessibleName(tr("Search name or full path"));
    searchEdit_->setPlaceholderText(tr("Name or path contains…"));

    metricCombo_ = new QComboBox(central);
    metricCombo_->setObjectName(QStringLiteral("metricCombo"));
    metricCombo_->setAccessibleName(tr("Size metric"));
    metricCombo_->addItem(tr("Logical"), static_cast<int>(diskmap::SizeMetric::Logical));
    metricCombo_->addItem(tr("Allocated"),
                          static_cast<int>(diskmap::SizeMetric::Allocated));
    metricCombo_->addItem(tr("Reclaimable"),
                          static_cast<int>(diskmap::SizeMetric::Reclaimable));

    typeCombo_ = new QComboBox(central);
    typeCombo_->setObjectName(QStringLiteral("typeCombo"));
    typeCombo_->setAccessibleName(tr("Entry type filter"));
    typeCombo_->addItem(tr("All types"), kAnyValue);
    typeCombo_->addItem(tr("Files"), static_cast<int>(diskmap::FsKind::RegularFile));
    typeCombo_->addItem(tr("Directories"), static_cast<int>(diskmap::FsKind::Directory));
    typeCombo_->addItem(tr("Symlinks"), static_cast<int>(diskmap::FsKind::Symlink));
    typeCombo_->addItem(tr("Other"), static_cast<int>(diskmap::FsKind::Other));

    ageCombo_ = new QComboBox(central);
    ageCombo_->setObjectName(QStringLiteral("ageCombo"));
    ageCombo_->setAccessibleName(tr("Modification age filter"));
    ageCombo_->addItem(tr("Any age"), AnyAge);
    ageCombo_->addItem(tr("Modified in 24 hours"), LastDay);
    ageCombo_->addItem(tr("Modified in 7 days"), LastWeek);
    ageCombo_->addItem(tr("Modified in 30 days"), LastMonth);
    ageCombo_->addItem(tr("Older than 30 days"), OlderThanMonth);

    minimumSizeEdit_ = new QLineEdit(central);
    minimumSizeEdit_->setObjectName(QStringLiteral("minimumSizeEdit"));
    minimumSizeEdit_->setAccessibleName(tr("Minimum size in bytes"));
    minimumSizeEdit_->setPlaceholderText(tr("none"));
    maximumSizeEdit_ = new QLineEdit(central);
    maximumSizeEdit_->setObjectName(QStringLiteral("maximumSizeEdit"));
    maximumSizeEdit_->setAccessibleName(tr("Maximum size in bytes"));
    maximumSizeEdit_->setPlaceholderText(tr("none"));
    auto* byteValidator =
        new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]{0,20}")),
                                        central);
    minimumSizeEdit_->setValidator(byteValidator);
    maximumSizeEdit_->setValidator(byteValidator);

    issueCombo_ = new QComboBox(central);
    issueCombo_->setObjectName(QStringLiteral("issueCombo"));
    issueCombo_->setAccessibleName(tr("Scan state filter"));
    issueCombo_->addItem(tr("All states"), kAnyIssue);
    issueCombo_->addItem(tr("Problems only"), kProblemIssues);
    issueCombo_->addItem(tr("Complete"),
                         static_cast<int>(diskmap::NodeIssue::None));
    issueCombo_->addItem(tr("Incomplete"),
                         static_cast<int>(diskmap::NodeIssue::Incomplete));
    issueCombo_->addItem(tr("Metadata unknown"),
                         static_cast<int>(diskmap::NodeIssue::MetadataUnknown));
    issueCombo_->addItem(tr("Cycle skipped"),
                         static_cast<int>(diskmap::NodeIssue::CycleSkipped));
    issueCombo_->addItem(tr("Mount boundary"),
                         static_cast<int>(diskmap::NodeIssue::MountBoundarySkipped));
    issueCombo_->addItem(tr("Depth limit"),
                         static_cast<int>(diskmap::NodeIssue::DepthLimitReached));
    issueCombo_->addItem(tr("Scan error"),
                         static_cast<int>(diskmap::NodeIssue::Error));
    issueCombo_->addItem(tr("Scanner filtering"),
                         static_cast<int>(diskmap::NodeIssue::ScannerFiltered));

    modeCombo_ = new QComboBox(central);
    modeCombo_->setObjectName(QStringLiteral("viewModeCombo"));
    modeCombo_->setAccessibleName(tr("Table projection mode"));
    modeCombo_->addItem(tr("Current folder"), NodeTableModel::ChildrenMode);
    modeCombo_->addItem(tr("Largest files in subtree"),
                        NodeTableModel::LargestFilesMode);

    panel->addWidget(new QLabel(tr("Search"), central), 0, 0);
    panel->addWidget(searchEdit_, 0, 1, 1, 3);
    panel->addWidget(new QLabel(tr("Metric"), central), 0, 4);
    panel->addWidget(metricCombo_, 0, 5);
    panel->addWidget(new QLabel(tr("Type"), central), 0, 6);
    panel->addWidget(typeCombo_, 0, 7);
    panel->addWidget(new QLabel(tr("Age"), central), 0, 8);
    panel->addWidget(ageCombo_, 0, 9);
    panel->addWidget(new QLabel(tr("Min bytes"), central), 1, 0);
    panel->addWidget(minimumSizeEdit_, 1, 1);
    panel->addWidget(new QLabel(tr("Max bytes"), central), 1, 2);
    panel->addWidget(maximumSizeEdit_, 1, 3);
    panel->addWidget(new QLabel(tr("State"), central), 1, 4);
    panel->addWidget(issueCombo_, 1, 5);
    panel->addWidget(new QLabel(tr("Table"), central), 1, 6);
    panel->addWidget(modeCombo_, 1, 7, 1, 3);
    layout->addLayout(panel);

    metricExplanation_ = new QLabel(central);
    metricExplanation_->setObjectName(QStringLiteral("metricExplanation"));
    metricExplanation_->setAccessibleName(tr("Selected metric explanation"));
    metricExplanation_->setTextFormat(Qt::PlainText);
    metricExplanation_->setWordWrap(true);
    layout->addWidget(metricExplanation_);

    partialBanner_ = new QLabel(central);
    partialBanner_->setObjectName(QStringLiteral("partialBanner"));
    partialBanner_->setAccessibleName(tr("Projection completeness warning"));
    partialBanner_->setTextFormat(Qt::PlainText);
    partialBanner_->setWordWrap(true);
    partialBanner_->setStyleSheet(QStringLiteral(
        "QLabel { background: #fff3cd; color: #5f4500; border: 1px solid #d6a700; "
        "padding: 6px; }"));
    layout->addWidget(partialBanner_);

    filterTimer_ = new QTimer(this);
    filterTimer_->setSingleShot(true);
    filterTimer_->setInterval(150);
}

void MainWindow::buildExplorer(QWidget* central, QVBoxLayout* layout) {
    tableModel_ = new NodeTableModel(this);
    tableModel_->setLargestLimit(200);
    table_ = new QTableView(central);
    table_->setObjectName(QStringLiteral("nodeTable"));
    table_->setAccessibleName(tr("Projected filesystem entries"));
    table_->setModel(tableModel_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(true);
    table_->setWordWrap(false);
    table_->setSortingEnabled(true);
    table_->sortByColumn(NodeTableModel::LogicalColumn, Qt::DescendingOrder);
    table_->horizontalHeader()->setSectionResizeMode(NodeTableModel::PathColumn,
                                                     QHeaderView::Stretch);
    table_->setColumnWidth(NodeTableModel::NameColumn, 180);
    table_->setColumnWidth(NodeTableModel::TypeColumn, 90);
    table_->setColumnWidth(NodeTableModel::LogicalColumn, 110);
    table_->setColumnWidth(NodeTableModel::AllocatedColumn, 110);
    table_->setColumnWidth(NodeTableModel::ReclaimableColumn, 110);
    table_->setColumnWidth(NodeTableModel::ModifiedColumn, 190);
    table_->setColumnWidth(NodeTableModel::StateColumn, 130);

    treemap_ = new TreemapWidget(central);
    treemap_->setObjectName(QStringLiteral("treemap"));
    treemap_->setAccessibleName(tr("Disk usage treemap"));

    auto* splitter = new QSplitter(Qt::Vertical, central);
    splitter->setObjectName(QStringLiteral("explorerSplitter"));
    splitter->addWidget(treemap_);
    splitter->addWidget(table_);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);

    auto* footer = new QHBoxLayout();
    legend_ = new QLabel(tr("Solid = exact · hatched/dashed = unknown or incomplete"),
                         central);
    legend_->setObjectName(QStringLiteral("treemapLegend"));
    legend_->setAccessibleName(tr("Treemap uncertainty legend"));
    projectionSummary_ = new QLabel(tr("No projection"), central);
    projectionSummary_->setObjectName(QStringLiteral("projectionSummary"));
    projectionSummary_->setAccessibleName(tr("Projection item count"));
    projectionSummary_->setTextFormat(Qt::PlainText);
    footer->addWidget(legend_);
    footer->addStretch(1);
    footer->addWidget(projectionSummary_);
    layout->addLayout(footer);
}

void MainWindow::connectUi() {
    connect(chooseButton_, &QPushButton::clicked, this, &MainWindow::chooseFolder);
    connect(rescanButton_, &QPushButton::clicked, this, &MainWindow::rescan);
    connect(upButton_, &QPushButton::clicked, this, &MainWindow::goUp);
    connect(cancelButton_, &QPushButton::clicked, this, &MainWindow::cancelScan);
    connect(table_, &QTableView::activated, this, &MainWindow::onTableActivated);
    connect(table_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &MainWindow::onTableCurrentChanged);
    connect(table_->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this,
            &MainWindow::onTableSortChanged);
    connect(treemap_, &TreemapWidget::nodeActivated, this, &MainWindow::onNodeActivated);
    connect(treemap_, &TreemapWidget::nodeHovered, this, &MainWindow::onNodeHovered);
    connect(treemap_, &TreemapWidget::hoverCleared, this, &MainWindow::clearHover);
    connect(tableModel_, &NodeTableModel::projectionStatusChanged, this,
            [this](bool, int) { updatePartialBanner(); });
    connect(treemap_, &TreemapWidget::projectionStatusChanged, this,
            [this](bool) { updatePartialBanner(); });
    connect(filterTimer_, &QTimer::timeout, this, &MainWindow::applyFilters);

    const auto scheduleFilter = [this]() { filterTimer_->start(); };
    connect(searchEdit_, &QLineEdit::textChanged, this,
            [scheduleFilter](const QString&) { scheduleFilter(); });
    for (QComboBox* combo : {metricCombo_, typeCombo_, ageCombo_, issueCombo_,
                             modeCombo_}) {
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [scheduleFilter](int) { scheduleFilter(); });
    }
    connect(minimumSizeEdit_, &QLineEdit::editingFinished, this,
            &MainWindow::applyFilters);
    connect(maximumSizeEdit_, &QLineEdit::editingFinished, this,
            &MainWindow::applyFilters);
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
    diskmap::ScanResult completed = watcher->result();
    watcher->deleteLater();
    if (generation != activeGeneration_) {
        return;
    }
    activeCancellation_.reset();

    if (completed.cancelled) {
        pendingRestore_ = false;
        pendingTrail_.clear();
        pendingSelectedKey_.reset();
        status_->setText(tr("Scan cancelled — partial result discarded"));
        updateControlState();
        return;
    }
    if (!completed.fatal_error.empty()) {
        pendingRestore_ = false;
        pendingTrail_.clear();
        pendingSelectedKey_.reset();
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
    pendingRestore_ = false;
    pendingTrail_.clear();
    pendingSelectedKey_.reset();
    refreshProjection();

    QString message = tr("%1 dirs, %2 files, %3")
                          .arg(document_->dirs_scanned)
                          .arg(document_->files_scanned)
                          .arg(QString::fromStdString(
                              diskmap::humanBytes(document_->root.size)));
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
    updateControlState();
}

void MainWindow::restoreNavigation(
    const std::vector<diskmap::NodeKey>& previousTrail) {
    trail_.clear();
    trail_.push_back(diskmap::nodeKey(document_->root));
    const diskmap::FsNode* current = &document_->root;
    for (std::size_t index = 1; index < previousTrail.size(); ++index) {
        const auto child = std::find_if(
            current->children.begin(), current->children.end(),
            [&previousTrail, index](const diskmap::FsNode& candidate) {
                return diskmap::nodeKey(candidate) == previousTrail[index];
            });
        if (child == current->children.end() || !child->is_dir) {
            break;
        }
        current = &(*child);
        trail_.push_back(diskmap::nodeKey(*current));
    }
}

void MainWindow::navigateToKey(const diskmap::NodeKey& key) {
    if (!document_) {
        return;
    }
    const std::vector<const diskmap::FsNode*> path =
        diskmap::nodePathByKey(document_->root, key);
    if (path.empty()) {
        return;
    }
    const diskmap::FsNode* target = path.back();
    const std::size_t trailLength = target->is_dir ? path.size() : path.size() - 1;
    trail_.clear();
    trail_.reserve(trailLength);
    for (std::size_t index = 0; index < trailLength; ++index) {
        trail_.push_back(diskmap::nodeKey(*path[index]));
    }
    selectedKey_ = target->is_dir ? std::nullopt
                                  : std::optional<diskmap::NodeKey>(key);
    refreshProjection();
}

void MainWindow::navigateToBreadcrumb(std::size_t index) {
    if (index >= trail_.size()) {
        return;
    }
    trail_.resize(index + 1);
    selectedKey_.reset();
    refreshProjection();
}

void MainWindow::goUp() {
    if (trail_.size() <= 1) {
        return;
    }
    trail_.pop_back();
    selectedKey_.reset();
    refreshProjection();
}

void MainWindow::applyFilters() {
    updateMetricExplanation();
    if (document_) {
        refreshProjection();
    }
}

void MainWindow::onTableActivated(const QModelIndex& index) {
    const std::optional<diskmap::NodeKey> key = tableModel_->keyAt(index.row());
    if (key.has_value()) {
        navigateToKey(*key);
    }
}

void MainWindow::onTableCurrentChanged(const QModelIndex& current,
                                       const QModelIndex&) {
    if (!refreshingProjection_) {
        selectedKey_ = tableModel_->keyAt(current.row());
    }
}

void MainWindow::onTableSortChanged(int column, Qt::SortOrder) {
    const diskmap::SizeMetric selected = static_cast<diskmap::SizeMetric>(
        metricCombo_->currentData().toInt());
    const diskmap::SizeMetric metric = metricForColumn(column, selected);
    if (columnForMetric(metric) != column) {
        return;
    }
    if (metric != selected) {
        const QSignalBlocker blocker(metricCombo_);
        metricCombo_->setCurrentIndex(
            metricCombo_->findData(static_cast<int>(metric)));
    }
    applyFilters();
}

void MainWindow::onNodeActivated(diskmap::NodeKey key) { navigateToKey(key); }

void MainWindow::onNodeHovered(diskmap::NodeKey key) {
    if (!document_) {
        return;
    }
    const diskmap::FsNode* node = diskmap::findNodeByKey(document_->root, key);
    if (node != nullptr) {
        treemap_->setToolTip(describe(*node, document_->totals_filtered));
    }
}

void MainWindow::clearHover() { treemap_->setToolTip(QString()); }

diskmap::ViewFilter MainWindow::viewFilterFromControls() {
    diskmap::ViewFilter filter;
    filter.search = utf8Text(searchEdit_->text().trimmed());
    filter.metric = static_cast<diskmap::SizeMetric>(
        metricCombo_->currentData().toInt());
    filter.scanner_totals_filtered = document_ && document_->totals_filtered;

    const int type = typeCombo_->currentData().toInt();
    if (type != kAnyValue) {
        filter.kind = static_cast<diskmap::FsKind>(type);
    }
    const int issue = issueCombo_->currentData().toInt();
    if (issue == kProblemIssues) {
        filter.issues = {
            diskmap::NodeIssue::Incomplete,
            diskmap::NodeIssue::CycleSkipped,
            diskmap::NodeIssue::MountBoundarySkipped,
            diskmap::NodeIssue::DepthLimitReached,
            diskmap::NodeIssue::MetadataUnknown,
            diskmap::NodeIssue::Error,
            diskmap::NodeIssue::ScannerFiltered,
        };
    } else if (issue != kAnyIssue) {
        filter.issue = static_cast<diskmap::NodeIssue>(issue);
    }

    filterError_.clear();
    bool minimumValid = true;
    bool maximumValid = true;
    filter.min_size = unsignedValue(*minimumSizeEdit_, minimumValid);
    filter.max_size = unsignedValue(*maximumSizeEdit_, maximumValid);
    minimumSizeEdit_->setStyleSheet(minimumValid ? QString()
                                                  : QStringLiteral("border: 1px solid #b00020"));
    maximumSizeEdit_->setStyleSheet(maximumValid ? QString()
                                                  : QStringLiteral("border: 1px solid #b00020"));
    if (!minimumValid || !maximumValid) {
        filterError_ = tr("A size bound is outside the unsigned 64-bit range.");
    } else if (filter.min_size.has_value() && filter.max_size.has_value()
               && *filter.min_size > *filter.max_size) {
        filterError_ = tr("Minimum size exceeds maximum size; no item can match.");
    }

    const int age = ageCombo_->currentData().toInt();
    const std::int64_t now = QDateTime::currentMSecsSinceEpoch() * 1'000'000LL;
    applyAgeBounds(age, now, filter);
    return filter;
}

diskmap::SortSpec MainWindow::sortSpecFromControls() const {
    diskmap::SortSpec sort;
    sort.metric = static_cast<diskmap::SizeMetric>(metricCombo_->currentData().toInt());
    const int selectedColumn = table_->horizontalHeader()->sortIndicatorSection();
    sort.descending = selectedColumn != columnForMetric(sort.metric)
                      || table_->horizontalHeader()->sortIndicatorOrder()
                             == Qt::DescendingOrder;
    return sort;
}

const diskmap::FsNode* MainWindow::currentNode() const {
    if (!document_ || trail_.empty()) {
        return nullptr;
    }
    const diskmap::FsNode* current = &document_->root;
    if (diskmap::nodeKey(*current) != trail_.front()) {
        return nullptr;
    }
    for (std::size_t index = 1; index < trail_.size(); ++index) {
        const auto child = std::find_if(
            current->children.begin(), current->children.end(),
            [this, index](const diskmap::FsNode& candidate) {
                return diskmap::nodeKey(candidate) == trail_[index];
            });
        if (child == current->children.end()) {
            return nullptr;
        }
        current = &(*child);
    }
    return current;
}

void MainWindow::refreshProjection() {
    const diskmap::FsNode* root = currentNode();
    if (root == nullptr) {
        tableModel_->clear();
        treemap_->setRoot(nullptr);
        updateBreadcrumb();
        updatePartialBanner();
        updateControlState();
        return;
    }

    const diskmap::NodeKey rootKey = diskmap::nodeKey(*root);
    const diskmap::ViewFilter filter = viewFilterFromControls();
    const int headerColumn = table_->horizontalHeader()->sortIndicatorSection();
    const int metricColumn = columnForMetric(*filter.metric);
    if (columnForMetric(metricForColumn(headerColumn, *filter.metric)) == headerColumn
        && headerColumn != metricColumn) {
        const QSignalBlocker blocker(table_->horizontalHeader());
        table_->horizontalHeader()->setSortIndicator(
            metricColumn, table_->horizontalHeader()->sortIndicatorOrder());
    }
    const diskmap::SortSpec sort = sortSpecFromControls();
    const NodeTableModel::Mode mode = static_cast<NodeTableModel::Mode>(
        modeCombo_->currentData().toInt());
    refreshingProjection_ = true;
    tableModel_->setProjection(document_, rootKey, filter, sort, mode);
    treemap_->setProjection(document_, rootKey, filter, sort);
    restoreSelection();
    refreshingProjection_ = false;
    table_->setAccessibleName(mode == NodeTableModel::ChildrenMode
                                  ? tr("Entries in current folder")
                                  : tr("Largest files in current subtree"));
    const bool projectionComplete = tableModel_->projectionComplete()
                                    && treemap_->projectionComplete();
    projectionSummary_->setText(
        tr("%1 item(s) · %2")
            .arg(tableModel_->rowCount())
            .arg(projectionComplete ? tr("exact evidence")
                                    : tr("conservative evidence")));
    updateBreadcrumb();
    updatePartialBanner();
    updateControlState();
}

void MainWindow::restoreSelection() {
    if (!selectedKey_.has_value()) {
        table_->clearSelection();
        return;
    }
    const QModelIndex index = tableModel_->indexForKey(*selectedKey_);
    if (!index.isValid()) {
        table_->clearSelection();
        selectedKey_.reset();
        return;
    }
    table_->selectionModel()->setCurrentIndex(
        index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void MainWindow::updateBreadcrumb() {
    while (QLayoutItem* item = breadcrumbLayout_->takeAt(0)) {
        if (item->widget() != nullptr) {
            item->widget()->setObjectName(QString());
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        delete item;
    }
    if (!document_ || trail_.empty()) {
        auto* empty = new QLabel(tr("(no scan yet)"), breadcrumbBar_);
        empty->setAccessibleName(tr("No scanned path"));
        breadcrumbLayout_->addWidget(empty);
        breadcrumbLayout_->addStretch(1);
        breadcrumbBar_->setProperty("currentPath", QString());
        return;
    }

    QStringList segments;
    const diskmap::FsNode* node = &document_->root;
    for (std::size_t index = 0; index < trail_.size(); ++index) {
        if (index > 0) {
            const auto child = std::find_if(
                node->children.begin(), node->children.end(),
                [this, index](const diskmap::FsNode& candidate) {
                    return diskmap::nodeKey(candidate) == trail_[index];
                });
            if (child == node->children.end()) {
                break;
            }
            node = &(*child);
        }
        QString label = QString::fromStdString(node->name);
        if (label.isEmpty()) {
            label = QString::fromStdString(diskmap::normalizedPath(*node));
        }
        if (index > 0) {
            auto* separator = new QLabel(QStringLiteral("›"), breadcrumbBar_);
            separator->setAccessibleName(tr("Path separator"));
            breadcrumbLayout_->addWidget(separator);
        }
        auto* button = new QToolButton(breadcrumbBar_);
        button->setObjectName(QStringLiteral("breadcrumbSegment%1").arg(index));
        button->setText(label);
        button->setToolTip(QString::fromStdString(diskmap::normalizedPath(*node)));
        button->setAccessibleName(tr("Open %1").arg(button->toolTip()));
        button->setAutoRaise(true);
        connect(button, &QToolButton::clicked, this,
                [this, index]() { navigateToBreadcrumb(index); });
        breadcrumbLayout_->addWidget(button);
        segments << label;
    }
    breadcrumbLayout_->addStretch(1);
    const QString path = segments.join(QStringLiteral(" / "));
    breadcrumbBar_->setProperty("currentPath", path);
    breadcrumbBar_->setAccessibleDescription(path);
}

void MainWindow::updateMetricExplanation() {
    const diskmap::SizeMetric metric = static_cast<diskmap::SizeMetric>(
        metricCombo_->currentData().toInt());
    if (metric == diskmap::SizeMetric::Logical) {
        metricExplanation_->setText(tr(
            "Logical size counts every directory entry. Exact values are additive; "
            "sparse files may use less physical storage."));
        return;
    }
    if (metric == diskmap::SizeMetric::Allocated) {
        metricExplanation_->setText(tr(
            "Allocated size counts filesystem blocks once per identity inside each "
            "subtree. Sibling values are not additive when hard links overlap."));
        return;
    }
    metricExplanation_->setText(tr(
        "Reclaimable size is counted only when every known hard-link reference is "
        "inside the subtree. Unknown or partial scans cannot promise reclaimed bytes."));
}

void MainWindow::updatePartialBanner() {
    if (!document_) {
        partialBanner_->hide();
        return;
    }
    if (!filterError_.isEmpty()) {
        partialBanner_->setText(filterError_);
        partialBanner_->show();
        return;
    }
    const bool complete = tableModel_->projectionComplete()
                          && treemap_->projectionComplete();
    if (complete) {
        partialBanner_->hide();
        return;
    }
    QString reason;
    if (!tableModel_->projectionComplete()) {
        reason = issueName(tableModel_->projectionIssue());
    } else {
        const diskmap::FsNode* root = currentNode();
        const diskmap::NodeIssue issue =
            root == nullptr
                ? diskmap::NodeIssue::Error
                : diskmap::classifyNodeIssue(*root, document_->totals_filtered);
        reason = issue == diskmap::NodeIssue::None
                     ? tr("selected metric is not known exactly")
                     : issueName(issue);
    }
    partialBanner_->setText(
        tr("This view contains conservative evidence (%1). Hatched tiles and "
           "‘At least’ values are not exact totals or exhaustive rankings.")
            .arg(reason));
    partialBanner_->show();
}

void MainWindow::updateControlState() {
    const bool scanning = activeCancellation_ != nullptr;
    cancelButton_->setEnabled(scanning && !activeCancellation_->isCancelled());
    rescanButton_->setEnabled(document_ != nullptr && !scanning);
    upButton_->setEnabled(trail_.size() > 1 && !scanning);
    table_->setEnabled(document_ != nullptr);
    searchEdit_->setEnabled(document_ != nullptr);
    minimumSizeEdit_->setEnabled(document_ != nullptr);
    maximumSizeEdit_->setEnabled(document_ != nullptr);
    metricCombo_->setEnabled(document_ != nullptr);
    typeCombo_->setEnabled(document_ != nullptr);
    ageCombo_->setEnabled(document_ != nullptr);
    issueCombo_->setEnabled(document_ != nullptr);
    modeCombo_->setEnabled(document_ != nullptr);
}
