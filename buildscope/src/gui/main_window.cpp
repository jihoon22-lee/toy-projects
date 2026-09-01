#include "buildscope/main_window.hpp"

#include "buildscope/compilation_model.hpp"
#include "buildscope/contract.hpp"
#include "buildscope/diff.hpp"
#include "buildscope/diff_model.hpp"
#include "ui_main_window.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QResource>
#include <QSortFilterProxyModel>
#include <QTableWidgetItem>
#include <QTreeWidgetItem>
#include <QUrl>

#include <utility>

static void initializeBuildScopeResources() {
    Q_INIT_RESOURCE(buildscope);
}

namespace buildscope {

class StatusFilterProxyModel final : public QSortFilterProxyModel {
public:
    explicit StatusFilterProxyModel(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) {
        setFilterCaseSensitivity(Qt::CaseInsensitive);
        setFilterKeyColumn(-1);
        setFilterRole(SearchTextRole);
        setRecursiveFilteringEnabled(true);
        setDynamicSortFilter(true);
    }

    void setStatusDecorationsEnabled(bool enabled) {
        statusDecorationsEnabled_ = enabled;
    }

    QVariant data(const QModelIndex &index, int role) const override {
        if (!statusDecorationsEnabled_ || role != Qt::DecorationRole ||
            index.column() != CompilationTreeModel::StatusColumn) {
            return QSortFilterProxyModel::data(index, role);
        }
        const auto status = QSortFilterProxyModel::data(index, SourceStatusRole).toString();
        if (status.isEmpty()) {
            return {};
        }
        if (status == QLatin1String("present")) {
            return QIcon(QStringLiteral(":/icons/status-present.svg"));
        }
        if (status == QLatin1String("stale")) {
            return QIcon(QStringLiteral(":/icons/status-stale.svg"));
        }
        if (status == QLatin1String("missing")) {
            return QIcon(QStringLiteral(":/icons/status-missing.svg"));
        }
        return QIcon(QStringLiteral(":/icons/status-unknown.svg"));
    }

private:
    bool statusDecorationsEnabled_ = true;
};

namespace {

QString visibleValue(const QString &value) {
    return value.isEmpty() ? QStringLiteral("—") : value;
}

QString existenceText(const std::optional<bool> &exists) {
    if (!exists.has_value()) {
        return QStringLiteral("unknown");
    }
    return *exists ? QStringLiteral("yes") : QStringLiteral("no");
}

QTableWidgetItem *tableItem(const QString &text) {
    auto *item = new QTableWidgetItem(text);
    item->setToolTip(text);
    return item;
}

QString includeEdgeDetails(const SnapshotIncludeEdge &edge) {
    QStringList lines = {
        QObject::tr("Evidence: %1").arg(edge.evidence),
        QObject::tr("Directive: %1:%2  #include %3")
            .arg(edge.parent)
            .arg(edge.line)
            .arg(edge.requested),
        QObject::tr("Location evidence: %1").arg(edge.locationEvidence),
        QObject::tr("Resolved: %1")
            .arg(edge.resolved.has_value() ? *edge.resolved : QObject::tr("unresolved")),
        QObject::tr("Classification: %1").arg(edge.classification),
    };
    if (!edge.alternatives.isEmpty()) {
        lines.append(QObject::tr("Same-name alternatives: %1")
                         .arg(edge.alternatives.join(QStringLiteral(", "))));
    }
    lines.append(QObject::tr("Ordered search:"));
    for (const auto &candidate : edge.search) {
        lines.append(QObject::tr("  %1. [%2] %3 — %4%5")
                         .arg(candidate.order)
                         .arg(candidate.kind, candidate.candidate,
                              candidate.exists ? QObject::tr("exists")
                                               : QObject::tr("missing"),
                              candidate.selected ? QObject::tr(" (selected)") : QString()));
    }
    return lines.join(QLatin1Char('\n'));
}

void addDiagnostic(QTreeWidget *tree, const SnapshotDiagnostic &diagnostic) {
    auto *item =
        new QTreeWidgetItem({diagnostic.severity, diagnostic.code, diagnostic.message});
    item->setToolTip(2, diagnostic.message);
    tree->addTopLevelItem(item);
}

QTreeWidgetItem *includeSearchItem(const SnapshotIncludeSearch &candidate,
                                   const SnapshotIncludeEdge &edge,
                                   const QString &details) {
    auto *item = new QTreeWidgetItem(
        {QObject::tr("%1. %2").arg(candidate.order).arg(candidate.kind), QString(),
         candidate.candidate,
         candidate.selected
             ? QObject::tr("selected")
             : (candidate.exists ? QObject::tr("candidate") : QObject::tr("missing")),
         edge.parent});
    item->setData(0, Qt::UserRole, edge.parent);
    item->setData(0, Qt::UserRole + 1, edge.line);
    item->setData(0, Qt::UserRole + 2, details);
    return item;
}

QTreeWidgetItem *includeEdgeItem(const SnapshotIncludeEdge &edge) {
    auto *item = new QTreeWidgetItem(
        {edge.evidence, edge.requested,
         edge.resolved.has_value() ? *edge.resolved : QObject::tr("unresolved"),
         edge.classification, QObject::tr("%1:%2").arg(edge.parent).arg(edge.line)});
    const auto details = includeEdgeDetails(edge);
    item->setData(0, Qt::UserRole, edge.parent);
    item->setData(0, Qt::UserRole + 1, edge.line);
    item->setData(0, Qt::UserRole + 2, details);
    item->setToolTip(2, details);
    for (const auto &candidate : edge.search) {
        item->addChild(includeSearchItem(candidate, edge, details));
    }
    return item;
}

void populateDefinitions(Ui::MainWindow &ui, const SnapshotEntry &entry) {
    ui.defineTable->setRowCount(entry.hasNormalized ? entry.normalized.defines.size() : 0);
    if (!entry.hasNormalized) {
        return;
    }
    for (qsizetype row = 0; row < entry.normalized.defines.size(); ++row) {
        const auto &define = entry.normalized.defines.at(row);
        ui.defineTable->setItem(row, 0, tableItem(define.action));
        ui.defineTable->setItem(row, 1, tableItem(define.name));
        ui.defineTable->setItem(
            row, 2, tableItem(define.value.has_value() ? *define.value : QStringLiteral("—")));
    }
}

void populateSearchPaths(Ui::MainWindow &ui, const SnapshotEntry &entry) {
    ui.includeTable->setRowCount(entry.hasNormalized ? entry.normalized.includePaths.size() : 0);
    if (!entry.hasNormalized) {
        return;
    }
    for (qsizetype row = 0; row < entry.normalized.includePaths.size(); ++row) {
        const auto &include = entry.normalized.includePaths.at(row);
        ui.includeTable->setItem(row, 0, tableItem(QString::number(include.order)));
        ui.includeTable->setItem(row, 1, tableItem(include.kind));
        ui.includeTable->setItem(row, 2, tableItem(include.scope));
        ui.includeTable->setItem(row, 3, tableItem(existenceText(include.exists)));
        ui.includeTable->setItem(row, 4, tableItem(include.path));
    }
}

void populateIncludeAnalysis(Ui::MainWindow &ui, const SnapshotEntry &entry) {
    if (!entry.hasIncludeAnalysis) {
        ui.includeEvidenceLabel->setText(
            QObject::tr("Include analysis unavailable (v1/v2 snapshot)"));
        return;
    }
    const auto &analysis = entry.includeAnalysis;
    ui.includeEvidenceLabel->setText(QObject::tr("Evidence: %1 · edges: %2 · %3 ms")
                                         .arg(analysis.evidence)
                                         .arg(analysis.edges.size())
                                         .arg(analysis.durationMs));
    ui.includeReplayEdit->setPlainText(renderArgumentVector(analysis.command));
    for (const auto &edge : analysis.edges) {
        ui.includeEdgeTree->addTopLevelItem(includeEdgeItem(edge));
    }
    for (const auto &diagnostic : analysis.diagnostics) {
        addDiagnostic(ui.diagnosticTree, diagnostic);
    }
}

}  // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui_(new Ui::MainWindow),
      model_(std::make_unique<CompilationTreeModel>()),
      diffModel_(std::make_unique<DiffTreeModel>()),
      proxy_(std::make_unique<StatusFilterProxyModel>()) {
    initializeBuildScopeResources();
    ui_->setupUi(this);
    proxy_->setSourceModel(model_.get());
    ui_->sourceTree->setModel(proxy_.get());
    ui_->sourceTree->header()->setSectionResizeMode(CompilationTreeModel::SourceColumn,
                                                    QHeaderView::Stretch);
    for (int column = CompilationTreeModel::StatusColumn;
         column < CompilationTreeModel::ColumnCount; ++column) {
        ui_->sourceTree->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    ui_->defineTable->horizontalHeader()->setStretchLastSection(true);
    ui_->includeTable->horizontalHeader()->setStretchLastSection(true);
    ui_->diffChangeTable->horizontalHeader()->setStretchLastSection(true);
    ui_->diagnosticTree->header()->setStretchLastSection(true);
    ui_->includeEdgeTree->header()->setStretchLastSection(true);
    connect(ui_->openButton, &QPushButton::clicked, this, &MainWindow::chooseSnapshot);
    connect(ui_->openDiffButton, &QPushButton::clicked, this, &MainWindow::chooseDiff);
    connect(ui_->filterEdit, &QLineEdit::textChanged, this, &MainWindow::applyFilter);
    connect(ui_->sourceTree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current, const QModelIndex &) {
                showSelection(current);
            });
    connect(ui_->includeEdgeTree, &QTreeWidget::itemClicked, this,
            &MainWindow::showIncludeEdge);
    connect(ui_->includeEdgeTree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int column) {
                showIncludeEdge(item, column);
                openIncludeLocation();
            });
    connect(ui_->openIncludeButton, &QPushButton::clicked, this,
            &MainWindow::openIncludeLocation);
    connect(ui_->showCommandButton, &QPushButton::clicked, this,
            &MainWindow::showCompilationCommand);
    clearDetails(tr("Select a source or configuration"));
    setDiffMode(false);
}

MainWindow::~MainWindow() = default;

bool MainWindow::loadSnapshot(const QString &path) {
    try {
        auto snapshot = loadSnapshotFile(path);
        const auto schemaVersion = snapshot.schemaVersion;
        const auto producerVersion = snapshot.producerVersion;
        model_->setSnapshot(std::move(snapshot));
        diffModel_->clear();
        setDiffMode(false);
        proxy_->setSourceModel(model_.get());
        proxy_->setFilterRole(SearchTextRole);
        proxy_->setStatusDecorationsEnabled(true);
        ui_->pathEdit->setText(path);
        ui_->filterEdit->clear();
        ui_->statusLabel->setText(
            tr("Sources: %1 · configurations: %2 · contract %3 · producer %4")
                .arg(model_->sourceCount())
                .arg(model_->entryCount())
                .arg(schemaVersion, producerVersion));
        clearDetails(tr("Select a source or configuration"));
        if (proxy_->rowCount() > 0) {
            const auto sourceIndex = proxy_->index(0, 0);
            ui_->sourceTree->setCurrentIndex(sourceIndex);
            if (proxy_->rowCount(sourceIndex) > 1) {
                ui_->sourceTree->expand(sourceIndex);
            }
        }
        return true;
    } catch (const ContractError &error) {
        model_->clear();
        diffModel_->clear();
        setDiffMode(false);
        proxy_->setSourceModel(model_.get());
        proxy_->setFilterRole(SearchTextRole);
        proxy_->setStatusDecorationsEnabled(true);
        ui_->pathEdit->setText(path);
        const auto message = tr("Could not load snapshot: %1").arg(error.what());
        ui_->statusLabel->setText(message);
        clearDetails(message);
        return false;
    }
}

bool MainWindow::loadDiff(const QString &path) {
    try {
        auto report = loadDiffFile(path);
        const auto schemaVersion = report.schemaVersion;
        const auto producerVersion = report.producerVersion;
        const auto summary = report.summary;
        model_->clear();
        diffModel_->setReport(std::move(report));
        setDiffMode(true);
        proxy_->setSourceModel(diffModel_.get());
        proxy_->setFilterRole(DiffSearchTextRole);
        proxy_->setStatusDecorationsEnabled(false);
        ui_->pathEdit->setText(path);
        ui_->filterEdit->clear();
        ui_->statusLabel->setText(
            tr("Changes: %1 visible · %2 suppressed · %3 unchanged · contract %4 · producer %5")
                .arg(summary.visibleUnits)
                .arg(summary.suppressedUnits)
                .arg(summary.unchanged)
                .arg(schemaVersion, producerVersion));
        clearDetails(tr("Select a changed configuration"));
        for (const auto &diagnostic : diffModel_->report().diagnostics) {
            const auto message = diagnostic.source.isEmpty()
                                     ? diagnostic.message
                                     : tr("%1: %2").arg(diagnostic.source,
                                                        diagnostic.message);
            addDiagnostic(ui_->diagnosticTree,
                          {diagnostic.code, message, diagnostic.severity});
        }
        if (proxy_->rowCount() > 0) {
            const auto unitIndex = proxy_->index(0, 0);
            ui_->sourceTree->setCurrentIndex(unitIndex);
            ui_->sourceTree->expand(unitIndex);
        }
        return true;
    } catch (const ContractError &error) {
        diffModel_->clear();
        setDiffMode(true);
        proxy_->setSourceModel(diffModel_.get());
        proxy_->setFilterRole(DiffSearchTextRole);
        proxy_->setStatusDecorationsEnabled(false);
        ui_->pathEdit->setText(path);
        const auto message = tr("Could not load diff report: %1").arg(error.what());
        ui_->statusLabel->setText(message);
        clearDetails(message);
        return false;
    }
}

int MainWindow::entryCount() const {
    return diffMode_ ? diffModel_->unitCount() : model_->entryCount();
}

QString MainWindow::statusText() const {
    return ui_->statusLabel->text();
}

void MainWindow::chooseSnapshot() {
    const auto path = QFileDialog::getOpenFileName(
        this, tr("Open BuildScope snapshot"), {}, tr("JSON files (*.json);;All files (*)"));
    if (!path.isEmpty()) {
        loadSnapshot(path);
    }
}

void MainWindow::chooseDiff() {
    const auto path = QFileDialog::getOpenFileName(
        this, tr("Open BuildScope diff report"), {}, tr("JSON files (*.json);;All files (*)"));
    if (!path.isEmpty()) {
        loadDiff(path);
    }
}

void MainWindow::showSelection(const QModelIndex &proxyIndex) {
    if (!proxyIndex.isValid()) {
        clearDetails(tr("Select a source or configuration"));
        return;
    }
    const auto sourceIndex = proxy_->mapToSource(proxyIndex.siblingAtColumn(0));
    if (diffMode_) {
        const auto unitIndex = diffModel_->unitIndex(sourceIndex);
        if (!unitIndex.has_value()) {
            clearDetails(tr("Select a changed configuration"));
            return;
        }
        showDiffUnit(diffModel_->report().units.at(*unitIndex),
                     diffModel_->changeIndex(sourceIndex));
        return;
    }
    const auto view = model_->entryView(sourceIndex);
    if (!view.isValid()) {
        const auto source = sourceIndex.data(SourcePathRole).toString();
        const auto configurations = model_->rowCount(sourceIndex);
        clearDetails(tr("%1 · %2 configurations; choose one to inspect")
                         .arg(source)
                         .arg(configurations));
        return;
    }
    showEntry(view);
}

void MainWindow::applyFilter(const QString &text) {
    proxy_->setFilterFixedString(text);
    if (!text.isEmpty()) {
        ui_->sourceTree->expandToDepth(0);
    }
}

void MainWindow::clearDetails(const QString &message) {
    ui_->selectionLabel->setText(message);
    for (auto *label : {ui_->sourceValue, ui_->directoryValue, ui_->sourceStatusValue,
                        ui_->targetValue, ui_->compilerValue, ui_->standardValue,
                        ui_->configurationValue}) {
        label->setText(QStringLiteral("—"));
    }
    ui_->invocationSourceValue->setText(tr("Invocation source: —"));
    ui_->argumentsEdit->clear();
    ui_->rawCommandEdit->clear();
    ui_->defineTable->setRowCount(0);
    ui_->includeTable->setRowCount(0);
    ui_->includeEdgeTree->clear();
    ui_->includeEvidenceLabel->setText(tr("Include analysis unavailable"));
    ui_->includeEdgeEdit->clear();
    ui_->includeReplayEdit->clear();
    ui_->openIncludeButton->setEnabled(false);
    selectedIncludePath_.clear();
    selectedIncludeLine_ = 0;
    ui_->diagnosticTree->clear();
    ui_->diffSummaryLabel->setText(message);
    ui_->diffChangeTable->setRowCount(0);
}

void MainWindow::showEntry(const CompilationEntryView &view) {
    const auto *entry = view.entry();
    if (entry == nullptr) {
        clearDetails(tr("Select a source or configuration"));
        return;
    }
    ui_->selectionLabel->setText(view.sourcePath());
    ui_->sourceValue->setText(visibleValue(view.sourcePath()));
    ui_->directoryValue->setText(visibleValue(view.directoryPath()));
    ui_->sourceStatusValue->setText(visibleValue(view.sourceStatus()));
    ui_->targetValue->setText(visibleValue(view.targetLabel()));
    ui_->compilerValue->setText(visibleValue(view.compilerLabel()));
    ui_->standardValue->setText(visibleValue(view.standard()));
    ui_->configurationValue->setText(visibleValue(view.configurationId()));
    ui_->invocationSourceValue->setText(
        tr("Invocation source: %1").arg(visibleValue(view.invocationSource())));
    ui_->argumentsEdit->setPlainText(view.structuredArguments());
    ui_->rawCommandEdit->setPlainText(
        view.rawCommand().isEmpty() ? tr("<not provided>") : view.rawCommand());
    ui_->diagnosticTree->clear();
    for (const auto &diagnostic : entry->diagnostics) {
        addDiagnostic(ui_->diagnosticTree, diagnostic);
    }
    populateDefinitions(*ui_, *entry);

    ui_->includeEdgeTree->clear();
    ui_->includeEdgeEdit->clear();
    ui_->includeReplayEdit->clear();
    selectedIncludePath_.clear();
    selectedIncludeLine_ = 0;
    ui_->openIncludeButton->setEnabled(false);
    populateIncludeAnalysis(*ui_, *entry);
    populateSearchPaths(*ui_, *entry);
}

void MainWindow::showIncludeEdge(QTreeWidgetItem *item, int) {
    if (item == nullptr) {
        return;
    }
    selectedIncludePath_ = item->data(0, Qt::UserRole).toString();
    selectedIncludeLine_ = item->data(0, Qt::UserRole + 1).toLongLong();
    ui_->includeEdgeEdit->setPlainText(item->data(0, Qt::UserRole + 2).toString());
    ui_->openIncludeButton->setEnabled(!selectedIncludePath_.isEmpty());
}

void MainWindow::openIncludeLocation() {
    if (selectedIncludePath_.isEmpty()) {
        return;
    }
    auto path = selectedIncludePath_;
    if (!QFileInfo(path).isAbsolute() && !model_->snapshot().projectRoot.isEmpty()) {
        path = QDir(model_->snapshot().projectRoot).filePath(path);
    }
    ui_->statusLabel->setText(
        tr("Opening include location %1:%2").arg(selectedIncludePath_).arg(selectedIncludeLine_));
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::showCompilationCommand() {
    ui_->detailTabs->setCurrentWidget(ui_->commandTab);
}

}  // namespace buildscope
