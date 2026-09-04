#include "diskmap/gui/main_window.hpp"

#include <QAbstractItemView>
#include <QAbstractItemModel>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSplitter>
#include <QTableView>
#include <QTableWidget>
#include <QTimer>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <initializer_list>
#include <optional>

#include "main_window_filter_data.hpp"
#include "diskmap/gui/treemap_widget.hpp"

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
    saveSnapshotButton_ = new QPushButton(tr("Save snapshot…"), central);
    saveSnapshotButton_->setObjectName(QStringLiteral("saveSnapshotButton"));
    saveSnapshotButton_->setAccessibleName(tr("Save the current scan as a snapshot"));
    loadSnapshotButton_ = new QPushButton(tr("Load snapshot…"), central);
    loadSnapshotButton_->setObjectName(QStringLiteral("loadSnapshotButton"));
    loadSnapshotButton_->setAccessibleName(tr("Load a snapshot for read-only inspection"));
    compareSnapshotButton_ = new QPushButton(tr("Compare snapshot…"), central);
    compareSnapshotButton_->setObjectName(QStringLiteral("compareSnapshotButton"));
    compareSnapshotButton_->setAccessibleName(tr("Compare the current scan with a snapshot"));

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
    bar->addWidget(saveSnapshotButton_);
    bar->addWidget(loadSnapshotButton_);
    bar->addWidget(compareSnapshotButton_);
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
    typeCombo_->addItem(tr("All types"), main_window_filter_data::kAnyValue);
    typeCombo_->addItem(tr("Files"), static_cast<int>(diskmap::FsKind::RegularFile));
    typeCombo_->addItem(tr("Directories"), static_cast<int>(diskmap::FsKind::Directory));
    typeCombo_->addItem(tr("Symlinks"), static_cast<int>(diskmap::FsKind::Symlink));
    typeCombo_->addItem(tr("Other"), static_cast<int>(diskmap::FsKind::Other));

    ageCombo_ = new QComboBox(central);
    ageCombo_->setObjectName(QStringLiteral("ageCombo"));
    ageCombo_->setAccessibleName(tr("Modification age filter"));
    ageCombo_->addItem(tr("Any age"), main_window_filter_data::AnyAge);
    ageCombo_->addItem(tr("Modified in 24 hours"), main_window_filter_data::LastDay);
    ageCombo_->addItem(tr("Modified in 7 days"), main_window_filter_data::LastWeek);
    ageCombo_->addItem(tr("Modified in 30 days"), main_window_filter_data::LastMonth);
    ageCombo_->addItem(tr("Older than 30 days"), main_window_filter_data::OlderThanMonth);

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
    issueCombo_->addItem(tr("All states"), main_window_filter_data::kAnyIssue);
    issueCombo_->addItem(tr("Problems only"), main_window_filter_data::kProblemIssues);
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
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
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

void MainWindow::buildEvidencePanel(QWidget* central, QVBoxLayout* layout) {
    auto* heading = new QLabel(
        tr("Storage evidence — snapshots and duplicate candidates are review-only"), central);
    heading->setObjectName(QStringLiteral("evidenceHeading"));
    heading->setAccessibleName(tr("Storage evidence"));
    layout->addWidget(heading);

    auto* actions = new QHBoxLayout();
    analyzeDuplicatesButton_ = new QPushButton(tr("Analyze duplicates"), central);
    analyzeDuplicatesButton_->setObjectName(QStringLiteral("analyzeDuplicatesButton"));
    analyzeDuplicatesButton_->setAccessibleName(tr("Analyze duplicate file evidence"));
    stageDuplicatesButton_ = new QPushButton(tr("Stage safe duplicate copies"), central);
    stageDuplicatesButton_->setObjectName(QStringLiteral("stageDuplicatesButton"));
    stageDuplicatesButton_->setAccessibleName(
        tr("Stage certain reclaimable duplicate copies for cleanup review"));
    actions->addWidget(analyzeDuplicatesButton_);
    actions->addWidget(stageDuplicatesButton_);
    actions->addStretch(1);

    snapshotSummary_ = new QLabel(tr("No snapshot comparison"), central);
    snapshotSummary_->setObjectName(QStringLiteral("snapshotSummary"));
    snapshotSummary_->setAccessibleName(tr("Snapshot comparison summary"));
    snapshotSummary_->setTextFormat(Qt::PlainText);
    actions->addWidget(snapshotSummary_);
    layout->addLayout(actions);

    duplicateSummary_ = new QLabel(tr("No duplicate analysis"), central);
    duplicateSummary_->setObjectName(QStringLiteral("duplicateSummary"));
    duplicateSummary_->setAccessibleName(tr("Duplicate analysis summary"));
    duplicateSummary_->setTextFormat(Qt::PlainText);
    duplicateSummary_->setWordWrap(true);
    layout->addWidget(duplicateSummary_);

    auto* splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setObjectName(QStringLiteral("evidenceSplitter"));
    duplicateEvidenceTable_ = new QTableWidget(splitter);
    duplicateEvidenceTable_->setObjectName(QStringLiteral("duplicateEvidenceTable"));
    duplicateEvidenceTable_->setAccessibleName(tr("Duplicate evidence"));
    duplicateEvidenceTable_->setColumnCount(6);
    duplicateEvidenceTable_->setHorizontalHeaderLabels(
        {tr("Group"), tr("Confidence"), tr("Path"), tr("Size"), tr("Content hash"),
         tr("Reclamation")});
    duplicateEvidenceTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    duplicateEvidenceTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    duplicateEvidenceTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    duplicateEvidenceTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    duplicateEvidenceTable_->setSelectionMode(QAbstractItemView::SingleSelection);

    snapshotChangesTable_ = new QTableWidget(splitter);
    snapshotChangesTable_->setObjectName(QStringLiteral("snapshotChangesTable"));
    snapshotChangesTable_->setAccessibleName(tr("Snapshot comparison changes"));
    snapshotChangesTable_->setColumnCount(5);
    snapshotChangesTable_->setHorizontalHeaderLabels(
        {tr("Change"), tr("Confidence"), tr("Before"), tr("After"), tr("Reason")});
    snapshotChangesTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    snapshotChangesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    snapshotChangesTable_->setSelectionMode(QAbstractItemView::NoSelection);
    splitter->addWidget(duplicateEvidenceTable_);
    splitter->addWidget(snapshotChangesTable_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter);
}

void MainWindow::buildCleanupPanel(QWidget* central, QVBoxLayout* layout) {
    auto* heading = new QLabel(tr("Cleanup staging — review only until Move to Trash"),
                               central);
    heading->setObjectName(QStringLiteral("cleanupHeading"));
    heading->setAccessibleName(tr("Cleanup staging"));
    layout->addWidget(heading);

    auto* actions = new QHBoxLayout();
    stageCleanupButton_ = new QPushButton(tr("Stage selected"), central);
    stageCleanupButton_->setObjectName(QStringLiteral("stageCleanupButton"));
    stageCleanupButton_->setAccessibleName(tr("Stage selected entries for cleanup"));
    clearCleanupButton_ = new QPushButton(tr("Clear staging"), central);
    clearCleanupButton_->setObjectName(QStringLiteral("clearCleanupButton"));
    clearCleanupButton_->setAccessibleName(tr("Clear cleanup staging"));
    undoCleanupButton_ = new QPushButton(tr("Undo staging"), central);
    undoCleanupButton_->setObjectName(QStringLiteral("undoCleanupButton"));
    undoCleanupButton_->setAccessibleName(tr("Undo cleanup staging change"));
    redoCleanupButton_ = new QPushButton(tr("Redo staging"), central);
    redoCleanupButton_->setObjectName(QStringLiteral("redoCleanupButton"));
    redoCleanupButton_->setAccessibleName(tr("Redo cleanup staging change"));
    executeCleanupButton_ = new QPushButton(tr("Move to Trash…"), central);
    executeCleanupButton_->setObjectName(QStringLiteral("executeCleanupButton"));
    executeCleanupButton_->setAccessibleName(tr("Move reviewed entries to Trash"));
    for (QPushButton* button : {stageCleanupButton_, clearCleanupButton_,
                                undoCleanupButton_, redoCleanupButton_,
                                executeCleanupButton_}) {
        actions->addWidget(button);
    }
    actions->addStretch(1);
    restoreTokenCombo_ = new QComboBox(central);
    restoreTokenCombo_->setObjectName(QStringLiteral("restoreTokenCombo"));
    restoreTokenCombo_->setAccessibleName(tr("Recoverable Trash item"));
    restoreTrashButton_ = new QPushButton(tr("Restore"), central);
    restoreTrashButton_->setObjectName(QStringLiteral("restoreTrashButton"));
    restoreTrashButton_->setAccessibleName(tr("Restore selected Trash item"));
    actions->addWidget(restoreTokenCombo_);
    actions->addWidget(restoreTrashButton_);
    layout->addLayout(actions);

    cleanupSummary_ = new QLabel(tr("No items staged"), central);
    cleanupSummary_->setObjectName(QStringLiteral("cleanupSummary"));
    cleanupSummary_->setAccessibleName(tr("Cleanup dry-run summary"));
    cleanupSummary_->setTextFormat(Qt::PlainText);
    cleanupSummary_->setWordWrap(true);
    layout->addWidget(cleanupSummary_);

    auto* cleanupSplitter = new QSplitter(Qt::Horizontal, central);
    cleanupSplitter->setObjectName(QStringLiteral("cleanupSplitter"));
    cleanupReviewTable_ = new QTableWidget(cleanupSplitter);
    cleanupReviewTable_->setObjectName(QStringLiteral("cleanupReviewTable"));
    cleanupReviewTable_->setAccessibleName(tr("Cleanup dry-run review"));
    cleanupReviewTable_->setColumnCount(3);
    cleanupReviewTable_->setHorizontalHeaderLabels(
        {tr("Decision"), tr("Path"), tr("Evidence")});
    cleanupReviewTable_->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    cleanupReviewTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    cleanupReviewTable_->setSelectionMode(QAbstractItemView::NoSelection);

    cleanupAuditTable_ = new QTableWidget(cleanupSplitter);
    cleanupAuditTable_->setObjectName(QStringLiteral("cleanupAuditTable"));
    cleanupAuditTable_->setAccessibleName(tr("Trash operation audit"));
    cleanupAuditTable_->setColumnCount(3);
    cleanupAuditTable_->setHorizontalHeaderLabels(
        {tr("Result"), tr("Original path"), tr("Detail")});
    cleanupAuditTable_->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    cleanupAuditTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    cleanupAuditTable_->setSelectionMode(QAbstractItemView::NoSelection);
    cleanupSplitter->addWidget(cleanupReviewTable_);
    cleanupSplitter->addWidget(cleanupAuditTable_);
    cleanupSplitter->setStretchFactor(0, 1);
    cleanupSplitter->setStretchFactor(1, 1);
    layout->addWidget(cleanupSplitter);

    cleanupUndo_ = new QUndoStack(this);
}

void MainWindow::connectUi() {
    connect(chooseButton_, &QPushButton::clicked, this, &MainWindow::chooseFolder);
    connect(rescanButton_, &QPushButton::clicked, this, &MainWindow::rescan);
    connect(upButton_, &QPushButton::clicked, this, &MainWindow::goUp);
    connect(cancelButton_, &QPushButton::clicked, this, &MainWindow::cancelScan);
    connect(saveSnapshotButton_, &QPushButton::clicked, this, &MainWindow::saveSnapshot);
    connect(loadSnapshotButton_, &QPushButton::clicked, this, &MainWindow::loadSnapshot);
    connect(compareSnapshotButton_, &QPushButton::clicked, this,
            &MainWindow::compareSnapshot);
    connect(analyzeDuplicatesButton_, &QPushButton::clicked, this,
            &MainWindow::analyzeDuplicates);
    connect(stageDuplicatesButton_, &QPushButton::clicked, this,
            &MainWindow::stageDuplicateCandidates);
    connect(stageCleanupButton_, &QPushButton::clicked, this,
            &MainWindow::stageSelectedRows);
    connect(clearCleanupButton_, &QPushButton::clicked, this,
            &MainWindow::clearCleanupStaging);
    connect(undoCleanupButton_, &QPushButton::clicked, cleanupUndo_,
            &QUndoStack::undo);
    connect(redoCleanupButton_, &QPushButton::clicked, cleanupUndo_,
            &QUndoStack::redo);
    connect(executeCleanupButton_, &QPushButton::clicked, this,
            &MainWindow::executeCleanup);
    connect(restoreTrashButton_, &QPushButton::clicked, this,
            &MainWindow::restoreSelectedTrashItem);
    connect(cleanupUndo_, &QUndoStack::canUndoChanged, undoCleanupButton_,
            &QPushButton::setEnabled);
    connect(cleanupUndo_, &QUndoStack::canRedoChanged, redoCleanupButton_,
            &QPushButton::setEnabled);
    connect(restoreTokenCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateControlState(); });
    connect(table_, &QTableView::activated, this, &MainWindow::onTableActivated);
    connect(table_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &MainWindow::onTableCurrentChanged);
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this]() { updateControlState(); });
    connect(table_->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this,
            &MainWindow::onTableSortChanged);
    connect(tableModel_, &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
        modelResetInProgress_ = true;
        if (refreshingProjection_ || activeCancellation_) {
            return;
        }
        const std::optional<diskmap::NodeKey> current =
            tableModel_->keyAt(table_->currentIndex().row());
        if (current.has_value()) {
            selectedKey_ = current;
        }
    });
    connect(tableModel_, &QAbstractItemModel::modelReset, this, [this]() {
        restoreSelection();
        modelResetInProgress_ = false;
    });
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

void MainWindow::updateControlState() {
    const bool scanning = activeCancellation_ != nullptr;
    const bool analyzing = activeDuplicateCancellation_ != nullptr;
    const bool busy = scanning || analyzing;
    updateActivityControls(scanning, analyzing);
    updateNavigationControls(busy);
    updateExplorerControls(busy);
    updateCleanupControls(busy);
    updateEvidenceControls(busy);
    updateRestoreControls(busy);
}

void MainWindow::updateActivityControls(bool scanning, bool analyzing) {
    const bool scanCancellable = scanning && !activeCancellation_->isCancelled();
    const bool duplicateCancellable =
        analyzing && !activeDuplicateCancellation_->isCancelled();
    cancelButton_->setEnabled(scanCancellable || duplicateCancellable);
}

void MainWindow::updateNavigationControls(bool busy) {
    rescanButton_->setEnabled(document_ != nullptr && !busy && !documentIsSnapshot_);
    upButton_->setEnabled(trail_.size() > 1 && !busy);
    saveSnapshotButton_->setEnabled(document_ != nullptr && !busy);
    loadSnapshotButton_->setEnabled(!busy);
    compareSnapshotButton_->setEnabled(document_ != nullptr && !busy);
}

void MainWindow::updateExplorerControls(bool busy) {
    const bool enabled = document_ != nullptr && !busy;
    const std::initializer_list<QWidget*> widgets = {
        breadcrumbBar_, table_, treemap_, searchEdit_, minimumSizeEdit_,
        maximumSizeEdit_, metricCombo_, typeCombo_, ageCombo_, issueCombo_, modeCombo_};
    for (QWidget* widget : widgets) {
        widget->setEnabled(enabled);
    }
}

void MainWindow::updateCleanupControls(bool busy) {
    const bool enabled = document_ != nullptr && !busy;
    const bool hasSelection = table_->selectionModel() != nullptr
                              && table_->selectionModel()->hasSelection();
    stageCleanupButton_->setEnabled(enabled && !documentIsSnapshot_ && hasSelection);
    clearCleanupButton_->setEnabled(enabled && !stagedCleanupKeys_.empty());
    undoCleanupButton_->setEnabled(enabled && cleanupUndo_->canUndo());
    redoCleanupButton_->setEnabled(enabled && cleanupUndo_->canRedo());
    executeCleanupButton_->setEnabled(enabled && !documentIsSnapshot_
                                      && !cleanupPlan_.targets.empty());
}

void MainWindow::updateEvidenceControls(bool busy) {
    const bool enabled = document_ != nullptr && !busy;
    analyzeDuplicatesButton_->setEnabled(enabled);
    stageDuplicatesButton_->setEnabled(enabled && !documentIsSnapshot_
                                        && hasReclaimableDuplicateCandidates());
}

void MainWindow::updateRestoreControls(bool busy) {
    restoreTokenCombo_->setEnabled(!busy && restoreTokenCombo_->count() > 0);
    restoreTrashButton_->setEnabled(!busy && restoreTokenCombo_->currentIndex() >= 0);
}

bool MainWindow::hasReclaimableDuplicateCandidates() const {
    for (const diskmap::DuplicateGroup& group : duplicateAnalysis_.groups) {
        if (group.reclaimable && group.certain && group.entries.size() > 1) {
            return true;
        }
    }
    return false;
}
