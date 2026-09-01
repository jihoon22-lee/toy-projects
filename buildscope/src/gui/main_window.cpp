#include "buildscope/main_window.hpp"

#include "buildscope/compilation_model.hpp"
#include "buildscope/contract.hpp"
#include "ui_main_window.h"

#include <QFileDialog>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QResource>
#include <QSortFilterProxyModel>
#include <QTableWidgetItem>
#include <QTreeWidgetItem>

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

    QVariant data(const QModelIndex &index, int role) const override {
        if (role != Qt::DecorationRole || index.column() != CompilationTreeModel::StatusColumn) {
            return QSortFilterProxyModel::data(index, role);
        }
        const auto status = QSortFilterProxyModel::data(index, SourceStatusRole).toString();
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

}  // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui_(new Ui::MainWindow),
      model_(std::make_unique<CompilationTreeModel>()),
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
    ui_->diagnosticTree->header()->setStretchLastSection(true);
    connect(ui_->openButton, &QPushButton::clicked, this, &MainWindow::chooseSnapshot);
    connect(ui_->filterEdit, &QLineEdit::textChanged, this, &MainWindow::applyFilter);
    connect(ui_->sourceTree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current, const QModelIndex &) {
                showSelection(current);
            });
    clearDetails(tr("Select a source or configuration"));
}

MainWindow::~MainWindow() = default;

bool MainWindow::loadSnapshot(const QString &path) {
    try {
        auto snapshot = loadSnapshotFile(path);
        const auto schemaVersion = snapshot.schemaVersion;
        const auto producerVersion = snapshot.producerVersion;
        model_->setSnapshot(std::move(snapshot));
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
        ui_->pathEdit->setText(path);
        const auto message = tr("Could not load snapshot: %1").arg(error.what());
        ui_->statusLabel->setText(message);
        clearDetails(message);
        return false;
    }
}

int MainWindow::entryCount() const {
    return model_->entryCount();
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

void MainWindow::showSelection(const QModelIndex &proxyIndex) {
    if (!proxyIndex.isValid()) {
        clearDetails(tr("Select a source or configuration"));
        return;
    }
    const auto sourceIndex = proxy_->mapToSource(proxyIndex.siblingAtColumn(0));
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
    ui_->diagnosticTree->clear();
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

    ui_->defineTable->setRowCount(entry->hasNormalized ? entry->normalized.defines.size() : 0);
    if (entry->hasNormalized) {
        for (qsizetype row = 0; row < entry->normalized.defines.size(); ++row) {
            const auto &define = entry->normalized.defines.at(row);
            ui_->defineTable->setItem(row, 0, tableItem(define.action));
            ui_->defineTable->setItem(row, 1, tableItem(define.name));
            ui_->defineTable->setItem(
                row, 2, tableItem(define.value.has_value() ? *define.value : QStringLiteral("—")));
        }
    }

    ui_->includeTable->setRowCount(
        entry->hasNormalized ? entry->normalized.includePaths.size() : 0);
    if (entry->hasNormalized) {
        for (qsizetype row = 0; row < entry->normalized.includePaths.size(); ++row) {
            const auto &include = entry->normalized.includePaths.at(row);
            ui_->includeTable->setItem(row, 0, tableItem(QString::number(include.order)));
            ui_->includeTable->setItem(row, 1, tableItem(include.kind));
            ui_->includeTable->setItem(row, 2, tableItem(include.scope));
            ui_->includeTable->setItem(row, 3, tableItem(existenceText(include.exists)));
            ui_->includeTable->setItem(row, 4, tableItem(include.path));
        }
    }

    ui_->diagnosticTree->clear();
    for (const auto &diagnostic : entry->diagnostics) {
        auto *item = new QTreeWidgetItem(
            {diagnostic.severity, diagnostic.code, diagnostic.message});
        item->setToolTip(2, diagnostic.message);
        ui_->diagnosticTree->addTopLevelItem(item);
    }
}

}  // namespace buildscope
