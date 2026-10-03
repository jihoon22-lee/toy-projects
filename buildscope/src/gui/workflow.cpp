#include "workflow.hpp"
#include "../core/contract_json_guard.hpp"
#include "buildscope/compilation_model.hpp"
#include "buildscope/contract.hpp"
#include "buildscope/diff.hpp"
#include "buildscope/impact.hpp"
#include "native_include.hpp"
#include "native_io.hpp"
#include "native_snapshot.hpp"
#include "ui_main_window.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSizePolicy>
#include <QSpinBox>
#include <QtConcurrent>

namespace buildscope {
namespace {
struct Loaded {
    Snapshot snapshot;
    DiffReport diff;
    QJsonObject raw;
    QString error;
    bool isDiff = false;
};
QJsonDocument readDocument(const QString &path, std::atomic_bool *cancel) {
    const auto bytes = native::readBoundedRegular(path, native::kMaxSnapshotBytes, cancel);
    detail::rejectDuplicateJsonKeys(bytes);
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError)
        throw ContractError(error.errorString());
    return doc;
}
QString databaseRoot(const QString &path) {
    auto dir = QFileInfo(path).absoluteDir();
    if (dir.dirName() == "build" || dir.dirName().startsWith("cmake-build-"))
        dir.cdUp();
    return dir.absolutePath();
}
} // namespace
void MainWindow::setupWorkflow() {
    workflow_ = std::make_shared<Workflow>();
    auto *import = new QPushButton(tr("Import compile DB…"), this);
    import->setObjectName("importDatabaseButton");
    ui_->pathLayout->addWidget(import);
    workflow_->save = new QPushButton(tr("Save…"), this);
    workflow_->save->setObjectName("saveSnapshotButton");
    ui_->pathLayout->addWidget(workflow_->save);
    workflow_->cancel = new QPushButton(tr("Cancel"), this);
    workflow_->cancel->setObjectName("cancelAnalysisButton");
    ui_->pathLayout->addWidget(workflow_->cancel);
    auto *bar = new QHBoxLayout;
    workflow_->mode = new QComboBox(this);
    workflow_->mode->setObjectName("analysisMode");
    workflow_->mode->addItem(tr("Estimate includes (no compiler)"), "estimate");
    workflow_->mode->addItem(tr("Compiler trace selected source"), "delayed");
    workflow_->mode->addItem(tr("Compiler trace all sources"), "compiler");
    bar->addWidget(workflow_->mode);
    workflow_->analyze = new QPushButton(tr("Analyze"), this);
    workflow_->analyze->setObjectName("analyzeButton");
    bar->addWidget(workflow_->analyze);
    auto *budgets = new QPushButton(tr("Budgets…"), this);
    budgets->setObjectName("analysisBudgetsButton");
    bar->addWidget(budgets);
    auto *relocate = new QPushButton(tr("Relocate root…"), this);
    relocate->setObjectName("relocateRootButton");
    bar->addWidget(relocate);
    auto *editor = new QPushButton(tr("Editor argv…"), this);
    bar->addWidget(editor);
    workflow_->progress = new QProgressBar(this);
    workflow_->progress->setMaximumWidth(150);
    workflow_->progress->setRange(0, 100);
    bar->addWidget(workflow_->progress);
    bar->addStretch();
    ui_->verticalLayout->insertLayout(2, bar);
    workflow_->filterTimer = new QTimer(this);
    workflow_->filterTimer->setSingleShot(true);
    workflow_->filterTimer->setInterval(150);
    connect(workflow_->filterTimer, &QTimer::timeout, this,
            [this] { applyFilter(ui_->filterEdit->text()); });
    connect(import, &QPushButton::clicked, this, [this] {
        const auto file = QFileDialog::getOpenFileName(this, tr("Import compilation database"),
                                                       {}, tr("JSON (*.json)"));
        if (!file.isEmpty())
            openInputAsync(file, true);
    });
    connect(workflow_->cancel, &QPushButton::clicked, this, &MainWindow::cancelWork);
    connect(workflow_->analyze, &QPushButton::clicked, this, [this] {
        const auto mode = workflow_->mode->currentData().toString();
        QStringList units;
        if (mode == "delayed") {
            const auto source = ui_->sourceTree->currentIndex().data(SourcePathRole).toString();
            if (source.isEmpty()) {
                ui_->statusLabel->setText(tr("Select a source before compiler replay."));
                return;
            }
            units.append(QStringLiteral("exact:") + source);
        }
        analyzeAsync(mode, units);
    });
    connect(budgets, &QPushButton::clicked, this, &MainWindow::chooseBudgets);
    connect(relocate, &QPushButton::clicked, this, &MainWindow::chooseRootMapping);
    connect(editor, &QPushButton::clicked, this, &MainWindow::chooseEditor);
    connect(workflow_->save, &QPushButton::clicked, this, [this] {
        if (workflow_->raw.isEmpty())
            return;
        const auto file = QFileDialog::getSaveFileName(this, tr("Save BuildScope snapshot"), {},
                                                       tr("JSON (*.json)"));
        if (file.isEmpty())
            return;
        try {
            QStringList protectedPaths{workflow_->inputPath};
            auto database = workflow_->raw.value("source").toObject().value("path").toString();
            if (QFileInfo::exists(database))
                protectedPaths.append(database);
            native::writeAtomicText(file, native::dumpsSnapshot(workflow_->raw, true),
                                    protectedPaths);
        } catch (const std::exception &error) {
            QMessageBox::warning(this, tr("Cannot save snapshot"), error.what());
        }
    });
    auto *impactTab = new QWidget;
    auto *impactLayout = new QVBoxLayout(impactTab);
    auto *query = new QHBoxLayout;
    workflow_->impactPath = new QLineEdit;
    workflow_->impactPath->setObjectName("impactHeaderEdit");
    workflow_->impactPath->setPlaceholderText(tr("Observed header path, e.g. include/api.hpp"));
    query->addWidget(workflow_->impactPath);
    auto *run = new QPushButton(tr("Reverse includes / impact"));
    run->setObjectName("impactButton");
    query->addWidget(run);
    impactLayout->addLayout(query);
    workflow_->impactText = new QPlainTextEdit;
    workflow_->impactText->setObjectName("impactEvidenceEdit");
    workflow_->impactText->setReadOnly(true);
    impactLayout->addWidget(workflow_->impactText);
    ui_->detailTabs->addTab(impactTab, tr("Header impact"));
    connect(run, &QPushButton::clicked, this, &MainWindow::showImpactAsync);
    connect(workflow_->impactPath, &QLineEdit::returnPressed, this,
            &MainWindow::showImpactAsync);
    auto *poll = new QTimer(this);
    poll->setInterval(100);
    connect(poll, &QTimer::timeout, this, [this] {
        if (!workflow_->busy || !workflow_->job)
            return;
        auto job = workflow_->job;
        if (job->total > 0) {
            workflow_->progress->setRange(0, job->total);
            workflow_->progress->setValue(job->completed);
        }
    });
    poll->start();
    new QShortcut(QKeySequence::Open, this, [this] { chooseSnapshot(); });
    new QShortcut(QKeySequence(Qt::Key_Escape), this, [this] { cancelWork(); });
    new QShortcut(QKeySequence("Ctrl+L"), this, [this] { ui_->filterEdit->setFocus(); });
    for (auto *label : {ui_->sourceValue, ui_->directoryValue, ui_->targetValue,
                        ui_->compilerValue, ui_->configurationValue, ui_->selectionLabel}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setMinimumWidth(0);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
    ui_->statusLabel->setTextFormat(Qt::PlainText);
    ui_->statusLabel->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);
    ui_->verticalLayout->setStretchFactor(ui_->explorerSplitter,1);
    ui_->detailTabs->setTabText(ui_->detailTabs->indexOf(ui_->includesTab),tr("Paths"));
    ui_->detailTabs->setTabText(ui_->detailTabs->indexOf(ui_->includeExplanationTab),tr("Includes"));
    ui_->detailTabs->setTabText(ui_->detailTabs->indexOf(ui_->diffTab),tr("Diff"));
    ui_->includeEvidenceLabel->setTextFormat(Qt::PlainText);
    ui_->explorerSplitter->setSizes({480, 760});
    auto saved = QSettings().value("editor/argv").toStringList();
    if (!saved.isEmpty()) {
        try {
            setEditorArguments(saved);
        } catch (const ContractError &) {
        }
    }
    updateBusy(false);
}
void MainWindow::scheduleFilter(const QString &) { workflow_->filterTimer->start(); }
void MainWindow::updateBusy(bool busyValue) {
    workflow_->busy = busyValue;
    workflow_->cancel->setEnabled(busyValue);
    workflow_->analyze->setEnabled(!busyValue && !workflow_->raw.isEmpty() && !diffMode_ &&
                                   !model_->snapshot().projectRoot.isEmpty());
    workflow_->save->setEnabled(!busyValue && !workflow_->raw.isEmpty() && !diffMode_);
    workflow_->progress->setRange(0, busyValue ? 0 : 100);
    if (!busyValue)
        workflow_->progress->setValue(100);
}
bool MainWindow::busy() const { return workflow_->busy; }
void MainWindow::cancelWork() {
    if (workflow_ && workflow_->job)
        workflow_->job->cancel = true;
}
void MainWindow::openInputAsync(const QString &path, bool database) {
    cancelWork();
    auto job = std::make_shared<Workflow::Job>();
    workflow_->job = job;
    const auto token = ++workflow_->generation;
    updateBusy(true);
    ui_->statusLabel->setText(tr("Loading and validating %1…").arg(path));
    auto mappings = workflow_->mappings;
    auto limits = workflow_->limits;
    auto *watcher = new QFutureWatcher<Loaded>(this);
    connect(
        watcher, &QFutureWatcher<Loaded>::finished, this, [this, watcher, token, path, job] {
            auto result = watcher->result();
            watcher->deleteLater();
            if (token != workflow_->generation)
                return;
            if (!result.error.isEmpty())
                ui_->statusLabel->setText(result.error);
            else if (job->cancel &&
                     !result.raw.value("analysis_run").toObject().value("cancelled").toBool())
                ui_->statusLabel->setText(tr("Loading cancelled"));
            else {
                workflow_->raw = result.raw;
                workflow_->inputPath = path;
                if (result.isDiff)
                    applyDiffReport(std::move(result.diff), path);
                else
                    applySnapshot(std::move(result.snapshot), path);
            }
            updateBusy(false);
            emit workFinished(result.error.isEmpty());
        });
    watcher->setFuture(QtConcurrent::run([path, database, mappings, limits, job] {
        Loaded result;
        try {
            auto doc = readDocument(path, &job->cancel);
            if (doc.isObject() &&
                doc.object().value("schema_version") == "buildscope.diff/v1") {
                result.diff = loadDiffFile(path, &job->cancel);
                result.isDiff = true;
                return result;
            }
            if (database || doc.isArray()) {
                auto root = databaseRoot(path);
                if (!mappings.isEmpty())
                    root = mappings.first().to;
                result.raw =
                    native::loadCompilationDatabase(path, root, mappings, &job->cancel);
                native::AnalysisControl control(limits, &job->cancel);
                control.progress = [job](int done, int total) {
                    job->completed = done;
                    job->total = total;
                };
                native::annotateSnapshotControlled(result.raw, root, "estimate", control);
            } else
                result.raw = doc.object();
            result.snapshot = parseSnapshot(
                QJsonDocument(result.raw),
                result.raw.value("analysis_run").toObject().value("cancelled").toBool()
                    ? nullptr
                    : &job->cancel);
        } catch (const std::exception &error) {
            result.error =
                QStringLiteral("Could not load input: ") + QString::fromUtf8(error.what());
        }
        return result;
    }));
}
void MainWindow::openDiffAsync(const QString &path) {
    cancelWork();
    auto job = std::make_shared<Workflow::Job>();
    workflow_->job = job;
    const auto token = ++workflow_->generation;
    updateBusy(true);
    ui_->statusLabel->setText(tr("Loading diff…"));
    auto *watcher = new QFutureWatcher<Loaded>(this);
    connect(watcher, &QFutureWatcher<Loaded>::finished, this,
            [this, watcher, job, token, path] {
                auto result = watcher->result();
                watcher->deleteLater();
                if (token != workflow_->generation)
                    return;
                if (job->cancel)
                    ui_->statusLabel->setText(tr("Diff loading cancelled"));
                else if (!result.error.isEmpty())
                    ui_->statusLabel->setText(result.error);
                else {
                    workflow_->raw = {};
                    workflow_->inputPath = path;
                    applyDiffReport(std::move(result.diff), path);
                }
                updateBusy(false);
                emit workFinished(result.error.isEmpty() && !job->cancel);
            });
    watcher->setFuture(QtConcurrent::run([path, job] {
        Loaded result;
        try {
            if (!job->cancel)
                result.diff = loadDiffFile(path, &job->cancel);
        } catch (const std::exception &e) {
            result.error = e.what();
        }
        return result;
    }));
}
void MainWindow::analyzeAsync(const QString &mode, const QStringList &units) {
    if (workflow_->raw.isEmpty())
        return;
    cancelWork();
    auto job = std::make_shared<Workflow::Job>();
    workflow_->job = job;
    const auto token = ++workflow_->generation;
    auto raw = workflow_->raw;
    auto mappings = workflow_->mappings;
    auto limits = workflow_->limits;
    const auto path = workflow_->inputPath;
    updateBusy(true);
    ui_->statusLabel->setText(tr("Analyzing include evidence…"));
    auto *watcher = new QFutureWatcher<Loaded>(this);
    connect(watcher, &QFutureWatcher<Loaded>::finished, this, [this, watcher, token, path] {
        auto result = watcher->result();
        watcher->deleteLater();
        if (token != workflow_->generation)
            return;
        if (!result.error.isEmpty())
            ui_->statusLabel->setText(result.error);
        else {
            workflow_->raw = result.raw;
            applySnapshot(std::move(result.snapshot), path);
            if (workflow_->raw.value("analysis_run").toObject().value("cancelled").toBool())
                ui_->statusLabel->setText(tr("Analysis cancelled — partial evidence retained"));
        }
        updateBusy(false);
        emit workFinished(result.error.isEmpty());
    });
    watcher->setFuture(QtConcurrent::run([raw, mappings, limits, mode, units, job]() mutable {
        Loaded result;
        try {
            auto root = raw.value("source").toObject().value("project_root").toString();
            if (!mappings.isEmpty()) {
                root = native::relocatePath(root, mappings);
                raw = native::relocateSnapshot(raw, root, mappings);
            }
            native::AnalysisControl control(limits, &job->cancel);
            control.progress = [job](int done, int total) {
                job->completed = done;
                job->total = total;
            };
            native::annotateSnapshotControlled(raw, root, mode, control, units);
            result.raw = raw;
            result.snapshot = parseSnapshot(QJsonDocument(raw));
        } catch (const std::exception &e) {
            result.error = QStringLiteral("Could not analyze: ") + e.what();
        }
        return result;
    }));
}
void MainWindow::showImpactAsync() {
    if (diffMode_ || workflow_->impactPath->text().isEmpty())
        return;
    cancelWork();
    auto job = std::make_shared<Workflow::Job>();
    workflow_->job = job;
    const auto token = ++workflow_->generation;
    const auto snapshot = model_->snapshot();
    const auto header = workflow_->impactPath->text();
    updateBusy(true);
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, token] {
        auto result = watcher->result();
        watcher->deleteLater();
        if (token != workflow_->generation)
            return;
        workflow_->impactText->setPlainText(result);
        updateBusy(false);
        emit workFinished(true);
    });
    watcher->setFuture(QtConcurrent::run([snapshot, header, job] {
        try {
            return QString::fromUtf8(
                QJsonDocument(includeImpact(snapshot, header, &job->cancel)).toJson());
        } catch (const std::exception &e) {
            return QString::fromUtf8(e.what());
        }
    }));
}
void MainWindow::setEditorArguments(const QStringList &arguments) {
    if (arguments.size() > 64)
        throw ContractError("editor argv exceeds 64 tokens");
    bool file = false;
    for (const auto &arg : arguments) {
        if (arg.isEmpty() || arg.size() > 4096 || arg.contains(QChar::Null))
            throw ContractError("invalid editor argument");
        if (arg.contains("{file}"))
            file = true;
    }
    if (!arguments.isEmpty() && (!file || arguments.first().contains('{')))
        throw ContractError("editor argv needs a literal program and {file} placeholder");
    workflow_->editorArgv = arguments;
}
QStringList MainWindow::editorArgumentsFor(const QString &path, qsizetype line) const {
    auto argv = workflow_->editorArgv;
    for (auto &arg : argv) {
        arg.replace("{file}", native::relocatePath(path, workflow_->mappings));
        arg.replace("{line}", QString::number(std::max(qsizetype(1), line)));
    }
    return argv;
}
void MainWindow::setRootMappings(const QStringList &specifications) {
    workflow_->mappings = native::parseRootMappings(specifications);
}
void MainWindow::chooseRootMapping() {
    if (model_->snapshot().projectRoot.isEmpty())
        return;
    auto root = QFileDialog::getExistingDirectory(this, tr("Choose relocated project root"));
    if (root.isEmpty())
        return;
    try {
        setRootMappings({model_->snapshot().projectRoot + '=' + root});
        ui_->statusLabel->setText(tr("Explicit root mapping configured; Analyze refreshes "
                                     "evidence at the new location."));
    } catch (const std::exception &e) {
        QMessageBox::warning(this, tr("Invalid mapping"), e.what());
    }
}
void MainWindow::chooseEditor() {
    bool ok;
    const auto initial =
        workflow_->editorArgv.isEmpty()
            ? QStringLiteral("[\"code\",\"--goto\",\"{file}:{line}\"]")
            : QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(workflow_->editorArgv))
                                    .toJson(QJsonDocument::Compact));
    const auto input = QInputDialog::getMultiLineText(
        this, tr("Configure editor argv"),
        tr("JSON array; {file} and {line} are substituted into individual tokens. An empty "
           "array restores the desktop opener."),
        initial, &ok);
    if (!ok)
        return;
    try {
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(input.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isArray())
            throw ContractError("editor configuration must be a JSON array");
        QStringList argv;
        for (const auto &value : doc.array()) {
            if (!value.isString())
                throw ContractError("editor argv items must be strings");
            argv.append(value.toString());
        }
        setEditorArguments(argv);
        QSettings().setValue("editor/argv", argv);
    } catch (const std::exception &e) {
        QMessageBox::warning(this, tr("Invalid editor argv"), e.what());
    }
}
void MainWindow::chooseBudgets() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Analysis budgets"));
    QFormLayout form(&dialog);
    auto number = [&](const QString &name, int current, int maximum) {
        auto *spin = new QSpinBox(&dialog);
        spin->setRange(1, maximum);
        spin->setValue(current);
        form.addRow(name, spin);
        return spin;
    };
    auto *units = number(tr("Translation units"), workflow_->limits.maxUnits, 4096);
    auto *total = number(tr("Global time (ms)"), workflow_->limits.totalMilliseconds, 600000);
    auto *unit = number(tr("Per-unit time (ms)"), workflow_->limits.unitMilliseconds, 600000);
    auto *bytes = number(tr("Global source budget (MiB)"),
                         int(workflow_->limits.totalSourceBytes / 1048576), 4096);
    auto *files = number(tr("Global source files"), workflow_->limits.totalFiles, 1000000);
    auto *edges = number(tr("Global evidence edges"), workflow_->limits.totalEdges, 1000000);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    workflow_->limits.maxUnits = units->value();
    workflow_->limits.totalMilliseconds = total->value();
    workflow_->limits.unitMilliseconds = unit->value();
    workflow_->limits.totalSourceBytes = qint64(bytes->value()) * 1048576;
    workflow_->limits.totalFiles = files->value();
    workflow_->limits.totalEdges = edges->value();
}
} // namespace buildscope
