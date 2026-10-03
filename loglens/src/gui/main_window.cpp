#include "loglens/gui/main_window.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QGridLayout>
#include <QSettings>
#include <QCoreApplication>
#include <QDockWidget>
#include <QTreeWidget>
#include <QPlainTextEdit>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QScrollBar>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableView>
#include <QTimer>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

#include "loglens/gui/log_model.hpp"
#include "loglens/evidence.hpp"
#include "loglens/gui/highlight_delegate.hpp"
#include "loglens/gui/log_load_worker.hpp"
#include "loglens/log_stats.hpp"
#include "loglens/gui/timeline_widget.hpp"

namespace {

constexpr std::uint64_t kBucketMs = 60000;

QString filterErrorRange(const loglens::ParseError& error) {
    const qulonglong begin = static_cast<qulonglong>(error.position);
    const qulonglong end = static_cast<qulonglong>(error.end);
    return QStringLiteral("[%1,%2)").arg(begin).arg(end);
}

} // namespace

MainWindow::MainWindow(QWidget* parent, MainWindowOptions options)
    : QMainWindow(parent), source_chunk_bytes_(options.sourceChunkBytes),
      record_capacity_(std::min(loglens::kMaxRecordCapacity,
                                std::max<std::size_t>(1, options.recordCapacity))) {
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    auto* bar = new QHBoxLayout();
    auto *filterBar = new QHBoxLayout();
    auto* openButton = new QPushButton(tr("Open log…"), central);
    openButton->setObjectName(QStringLiteral("openButton"));
    openButton->setAccessibleName(tr("Open log file"));
    filterEdit_ = new QLineEdit(central);
    filterEdit_->setObjectName(QStringLiteral("filterEdit"));
    filterEdit_->setAccessibleName(tr("Log filter"));
    filterEdit_->setMaxLength(static_cast<int>(loglens::kMaxFilterQueryBytes));
    filterEdit_->setPlaceholderText(tr("level>=WARN AND message~timeout"));
    auto* applyButton = new QPushButton(tr("Apply"), central);
    applyButton->setObjectName(QStringLiteral("applyFilterButton"));
    applyButton->setAccessibleName(tr("Apply log filter"));
    bar->addWidget(openButton);
    loadMode_ = new QComboBox(central);
    loadMode_->setObjectName(QStringLiteral("loadModeComboBox"));
    loadMode_->setAccessibleName(tr("Initial load mode"));
    loadMode_->addItem(tr("Latest records"));
    loadMode_->addItem(tr("From start"));
    bar->addWidget(loadMode_);
    tailRecords_ = new QSpinBox(central);
    tailRecords_->setObjectName(QStringLiteral("tailRecordsSpinBox"));
    tailRecords_->setAccessibleName(tr("Latest record count"));
    tailRecords_->setRange(1, static_cast<int>(record_capacity_));
    tailRecords_->setValue(static_cast<int>(record_capacity_));
    bar->addWidget(tailRecords_);
    filterBar->addWidget(filterEdit_, 1);
    filterBar->addWidget(applyButton);
    searchEdit_ = new QLineEdit(central);
    searchEdit_->setObjectName(QStringLiteral("searchEdit"));
    searchEdit_->setAccessibleName(tr("Search loaded log text"));
    searchEdit_->setPlaceholderText(tr("Search text"));
    filterBar->addWidget(searchEdit_, 1);
    searchEdit_->setMaxLength(static_cast<int>(loglens::kMaxFilterQueryBytes));
    searchEdit_->setPlaceholderText(tr("Search retained records"));
    followBox_ = new QCheckBox(tr("Follow"), central);
    followBox_->setObjectName(QStringLiteral("followCheckBox"));
    followBox_->setAccessibleName(tr("Follow log file"));
    followBox_->setChecked(true);
    bar->addWidget(followBox_);
    layout->addLayout(bar);
    layout->addLayout(filterBar);

    auto* profileBar = new QHBoxLayout();
    auto* profileLabel = new QLabel(tr("Source profile"), central);
    sourceProfile_ = new QComboBox(central);
    sourceProfile_->setObjectName(QStringLiteral("sourceProfileComboBox"));
    sourceProfile_->setAccessibleName(tr("Source profile"));
    sourceProfile_->setEditable(true);
    sourceProfile_->setInsertPolicy(QComboBox::NoInsert);
    sourceProfile_->setMaxCount(static_cast<int>(loglens::kMaxPersistedItems));
    sourceProfile_->lineEdit()->setMaxLength(
        static_cast<int>(loglens::kMaxPersistedNameBytes));
    sourceProfile_->lineEdit()->setPlaceholderText(tr("New profile name"));
    profileLabel->setBuddy(sourceProfile_);
    profileBar->addWidget(profileLabel);
    profileBar->addWidget(sourceProfile_);

    sourceFormat_ = new QComboBox(central);
    sourceFormat_->setObjectName(QStringLiteral("sourceFormatComboBox"));
    sourceFormat_->setAccessibleName(tr("Source format"));
    sourceFormat_->addItem(tr("Auto-detect"), QStringLiteral("auto"));
    sourceFormat_->addItem(tr("ISO timestamp"), QStringLiteral("iso"));
    sourceFormat_->addItem(tr("Syslog"), QStringLiteral("syslog"));
    sourceFormat_->addItem(tr("JSON Lines"), QStringLiteral("jsonl"));
    sourceFormat_->addItem(tr("Raw lines"), QStringLiteral("raw"));
    auto* formatLabel = new QLabel(tr("Format"), central);
    formatLabel->setBuddy(sourceFormat_);
    profileBar->addWidget(formatLabel);
    profileBar->addWidget(sourceFormat_);

    multilinePolicy_ = new QComboBox(central);
    multilinePolicy_->setObjectName(QStringLiteral("multilinePolicyComboBox"));
    multilinePolicy_->setAccessibleName(tr("Multiline policy"));
    multilinePolicy_->addItem(tr("Fold continuations"),
                              QStringLiteral("fold-continuations"));
    multilinePolicy_->addItem(tr("Keep separate lines"), QStringLiteral("separate-lines"));
    auto* multilineLabel = new QLabel(tr("Multiline"), central);
    multilineLabel->setBuddy(multilinePolicy_);
    profileBar->addWidget(multilineLabel);
    profileBar->addWidget(multilinePolicy_);

    maxRecordBytes_ = new QSpinBox(central);
    maxRecordBytes_->setObjectName(QStringLiteral("maxRecordBytesSpinBox"));
    maxRecordBytes_->setAccessibleName(tr("Maximum record bytes"));
    maxRecordBytes_->setRange(1, static_cast<int>(loglens::kMaxRecordBytes));
    maxRecordBytes_->setSingleStep(1024);
    maxRecordBytes_->setValue(static_cast<int>(loglens::kDefaultMaxRecordBytes));
    maxRecordBytes_->setSuffix(tr(" bytes"));
    auto* maxRecordLabel = new QLabel(tr("Max record"), central);
    maxRecordLabel->setBuddy(maxRecordBytes_);
    profileBar->addWidget(maxRecordLabel);
    profileBar->addWidget(maxRecordBytes_);

    auto* applyProfileButton = new QPushButton(tr("Apply profile"), central);
    applyProfileButton->setObjectName(QStringLiteral("applySourceProfileButton"));
    applyProfileButton->setAccessibleName(tr("Apply source profile"));
    profileBar->addWidget(applyProfileButton);
    auto* saveProfileButton = new QPushButton(tr("Save profile"), central);
    saveProfileButton->setObjectName(QStringLiteral("saveSourceProfileButton"));
    saveProfileButton->setAccessibleName(tr("Save source profile"));
    profileBar->addWidget(saveProfileButton);
    auto* openSessionButton = new QPushButton(tr("Open session…"), central);
    openSessionButton->setObjectName(QStringLiteral("openSessionButton"));
    openSessionButton->setAccessibleName(tr("Open investigation session"));
    profileBar->addWidget(openSessionButton);
    auto* saveSessionButton = new QPushButton(tr("Save session"), central);
    saveSessionButton->setObjectName(QStringLiteral("saveSessionButton"));
    saveSessionButton->setAccessibleName(tr("Save investigation session"));
    profileBar->addWidget(saveSessionButton);
    auto *settingsPanel = new QWidget(central);
    settingsPanel->setObjectName(QStringLiteral("sourceSettingsPanel"));
    auto *settingsLayout = new QVBoxLayout(settingsPanel);
    auto *profileGrid = new QGridLayout();
    int profileCell = 0;
    while (auto *item = profileBar->takeAt(0)) {
        if (item->widget())
            profileGrid->addWidget(item->widget(), profileCell / 4, profileCell % 4);
        ++profileCell;
        delete item;
    }
    delete profileBar;
    settingsLayout->addLayout(profileGrid);
    auto *settingsButton = new QPushButton(tr("Source / session settings"), central);
    settingsButton->setObjectName(QStringLiteral("sourceSettingsButton"));
    settingsButton->setCheckable(true);
    bar->addWidget(settingsButton);
    bar->addStretch();
    connect(settingsButton, &QPushButton::toggled, settingsPanel, &QWidget::setVisible);
    settingsPanel->setVisible(false);
    layout->addWidget(settingsPanel);
    auto *pluginBar = new QHBoxLayout();
    pluginLabel_ = new QLabel(tr("Built-in parser"), settingsPanel);
    pluginLabel_->setObjectName(QStringLiteral("formatPluginLabel"));
    pluginLabel_->setWordWrap(true);
    auto *pluginButton = new QPushButton(tr("Choose parser plugin…"), settingsPanel);
    pluginButton->setObjectName(QStringLiteral("chooseFormatPluginButton"));
    auto *clearPlugin = new QPushButton(tr("Use built-in"), settingsPanel);
    clearPlugin->setObjectName(QStringLiteral("clearFormatPluginButton"));
    pluginBar->addWidget(pluginLabel_, 1);
    pluginBar->addWidget(pluginButton);
    pluginBar->addWidget(clearPlugin);
    settingsLayout->addLayout(pluginBar);
    connect(pluginButton, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Open declarative parser plugin"),
                                                       QString(), tr("JSON files (*.json)"));
        if (!path.isEmpty() && setFormatPluginPath(path) && !currentPath_.isEmpty())
            applySourceProfile();
    });
    connect(clearPlugin, &QPushButton::clicked, this, [this] {
        setFormatPluginPath(QString());
        if (!currentPath_.isEmpty())
            applySourceProfile();
    });

    auto* queryBar = new QHBoxLayout();
    auto* queryLabel = new QLabel(tr("Saved query"), central);
    savedQuery_ = new QComboBox(central);
    savedQuery_->setObjectName(QStringLiteral("savedQueryComboBox"));
    savedQuery_->setAccessibleName(tr("Saved query"));
    savedQuery_->setEditable(true);
    savedQuery_->setInsertPolicy(QComboBox::NoInsert);
    savedQuery_->setMaxCount(static_cast<int>(loglens::kMaxPersistedItems));
    savedQuery_->lineEdit()->setMaxLength(static_cast<int>(loglens::kMaxPersistedNameBytes));
    savedQuery_->lineEdit()->setPlaceholderText(tr("Saved query name"));
    queryLabel->setBuddy(savedQuery_);
    queryBar->addWidget(queryLabel);
    queryBar->addWidget(savedQuery_, 1);
    auto* applyQueryButton = new QPushButton(tr("Apply query"), central);
    applyQueryButton->setObjectName(QStringLiteral("applySavedQueryButton"));
    applyQueryButton->setAccessibleName(tr("Apply saved query"));
    queryBar->addWidget(applyQueryButton);
    auto* saveQueryButton = new QPushButton(tr("Save query"), central);
    saveQueryButton->setObjectName(QStringLiteral("saveSavedQueryButton"));
    saveQueryButton->setAccessibleName(tr("Save saved query"));
    queryBar->addWidget(saveQueryButton);
    settingsLayout->addLayout(queryBar);

    timeline_ = new TimelineWidget(central);
    timeline_->setObjectName(QStringLiteral("timelineWidget"));
    timeline_->setAccessibleName(tr("Log timeline"));
    layout->addWidget(timeline_);

    model_ = new LogModel(this, record_capacity_);
    table_ = new QTableView(central);
    table_->setObjectName(QStringLiteral("logTable"));
    table_->setAccessibleName(tr("Log records"));
    table_->setModel(model_);
    table_->setItemDelegateForColumn(LogModel::ColumnMessage,
                                     new HighlightDelegate(table_));
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    // Uniform row heights let the view size itself without measuring every row,
    // which is what keeps a very large file scrollable.
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    layout->addWidget(table_, 1);

    setCentralWidget(central);
    setupInvestigationDock();
    setupWholeFileSearch();
    status_ = new QLabel(tr("Ready"), this);
    status_->setObjectName(QStringLiteral("statusLabel"));
    status_->setAccessibleName(tr("Log status"));
    status_->setWordWrap(false);
    status_->setMaximumHeight(28);
    statusBar()->setMaximumHeight(32);
    status_->setMinimumWidth(0);
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    statusBar()->addWidget(status_, 1);

    pollTimer_ = new QTimer(this);
    pollTimer_->setObjectName(QStringLiteral("followPollTimer"));
    pollTimer_->setInterval(500);
    connect(pollTimer_, &QTimer::timeout, this, &MainWindow::pollSource);
    connect(followBox_, &QCheckBox::toggled, this, &MainWindow::setFollowing);
    connect(sourceProfile_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MainWindow::selectSourceProfile);
    connect(loadMode_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) { tailRecords_->setEnabled(index == 0); });
    timelineTimer_ = new QTimer(this);
    timelineTimer_->setObjectName(QStringLiteral("timelineRefreshTimer"));
    timelineTimer_->setSingleShot(true);
    timelineTimer_->setInterval(50);
    connect(timelineTimer_, &QTimer::timeout, this, &MainWindow::refreshTimeline);
    connect(searchEdit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        timeline_->clearSelection();
        model_->setSearch(text);
        scheduleTimelineRefresh();
        updateStatus(QString());
    });

    qRegisterMetaType<loglens::LoadRequest>("loglens::LoadRequest");
    qRegisterMetaType<loglens::LoadBatch>("loglens::LoadBatch");
    loaderThread_ = new QThread(this);
    loaderThread_->setObjectName(QStringLiteral("logLoadThread"));
    loader_ = new loglens::LogLoadWorker();
    loader_->moveToThread(loaderThread_);
    connect(loaderThread_, &QThread::finished, loader_, &QObject::deleteLater);
    connect(this, &MainWindow::startLoadRequested, loader_, &loglens::LogLoadWorker::startLoad,
            Qt::QueuedConnection);
    connect(this, &MainWindow::pollRequested, loader_, &loglens::LogLoadWorker::poll,
            Qt::QueuedConnection);
    connect(this, &MainWindow::acknowledgeRequested, loader_,
            &loglens::LogLoadWorker::acknowledge, Qt::QueuedConnection);
    connect(loader_, &loglens::LogLoadWorker::batchReady, this, &MainWindow::handleLoadBatch,
            Qt::QueuedConnection);
    loaderThread_->start();

    // Following the tail is only wanted while the view is already at the tail.
    // Scrolling up to read something is an explicit request to stay put, and
    // yanking the viewport back down would make the log unreadable.
    connect(table_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        autoScroll_ = value == table_->verticalScrollBar()->maximum();
    });

    connect(openButton, &QPushButton::clicked, this, &MainWindow::chooseFile);
    connect(applyButton, &QPushButton::clicked, this, &MainWindow::applyFilter);
    connect(filterEdit_, &QLineEdit::returnPressed, this, &MainWindow::applyFilter);
    connect(applyProfileButton, &QPushButton::clicked, this, &MainWindow::applySourceProfile);
    connect(saveProfileButton, &QPushButton::clicked, this, &MainWindow::saveSourceProfile);
    connect(openSessionButton, &QPushButton::clicked, this, &MainWindow::openSessionFile);
    connect(saveSessionButton, &QPushButton::clicked, this, &MainWindow::saveSessionToFile);
    connect(applyQueryButton, &QPushButton::clicked, this, &MainWindow::applySavedQuery);
    connect(saveQueryButton, &QPushButton::clicked, this, &MainWindow::saveSavedQuery);
    connect(timeline_, &TimelineWidget::rangeSelected, this,
            &MainWindow::selectTimelineRange);
    connect(timeline_, &TimelineWidget::rangeCleared, this,
            &MainWindow::clearTimelineRange);
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex&, const QModelIndex&) { refreshRecordDetail(); });

    resize(1100, 700);
    setWindowTitle(tr("loglens"));

    sourceProfilesPath_ = options.sourceProfilesPath;
    savedQueriesPath_ = options.savedQueriesPath;
    triagePath_ = options.triagePath;
    if (sourceProfilesPath_.isEmpty()) {
        const QString configDirectory = QStandardPaths::writableLocation(
            QStandardPaths::AppConfigLocation);
        sourceProfilesPath_ = (configDirectory.isEmpty() ? QDir::currentPath() : configDirectory)
                              + QStringLiteral("/source-profiles.json");
        sourceProfilesPathIsDefault_ = true;
    }
    if (savedQueriesPath_.isEmpty()) {
        const QString configDirectory = QStandardPaths::writableLocation(
            QStandardPaths::AppConfigLocation);
        savedQueriesPath_ = (configDirectory.isEmpty() ? QDir::currentPath() : configDirectory)
                            + QStringLiteral("/saved-queries.json");
        savedQueriesPathIsDefault_ = true;
    }
    if (triagePath_.isEmpty()) {
        const QString configDirectory = QStandardPaths::writableLocation(
            QStandardPaths::AppConfigLocation);
        triagePath_ = (configDirectory.isEmpty() ? QDir::currentPath() : configDirectory)
                      + QStringLiteral("/triage.json");
        triagePathIsDefault_ = true;
    }
    sourceProfile_->setEditText(QStringLiteral("Default"));
    loadPersistenceState();
    loadTriageWorkflow();
    persistLayout_ = QCoreApplication::applicationName() == QStringLiteral("loglens") &&
                     options.sourceProfilesPath.isEmpty() && options.savedQueriesPath.isEmpty() &&
                     options.triagePath.isEmpty();
    if (persistLayout_) {
        QSettings settings(QDir(QFileInfo(sourceProfilesPath_).absolutePath()).filePath(
                               QStringLiteral("window-layout.ini")), QSettings::IniFormat);
        restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
        restoreState(settings.value(QStringLiteral("window/layout")).toByteArray(), 2);
        table_->horizontalHeader()->restoreState(
            settings.value(QStringLiteral("window/tableHeader")).toByteArray());
    }
}

MainWindow::~MainWindow() {
    cancelWholeFileSearch();
    if (searchThread_)
        searchThread_->wait();
    if (persistLayout_) {
        QSettings settings(QDir(QFileInfo(sourceProfilesPath_).absolutePath()).filePath(
                               QStringLiteral("window-layout.ini")), QSettings::IniFormat);
        settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
        settings.setValue(QStringLiteral("window/layout"), saveState(2));
        settings.setValue(QStringLiteral("window/tableHeader"),
                          table_->horizontalHeader()->saveState());
    }
    ++active_job_;
    loader_->selectJob(active_job_);
    loaderThread_->quit();
    loaderThread_->wait();
}

void MainWindow::applyDeltas(const std::vector<loglens::RecordDelta>& deltas) {
    std::vector<loglens::LogRecord> appends;
    std::size_t firstAppendIndex = 0;
    std::uint64_t appendGeneration = 0;
    const auto flushAppends = [this, &appends, &firstAppendIndex, &appendGeneration]() {
        if (!appends.empty()) {
            model_->appendRecords(appends, firstAppendIndex, appendGeneration);
            appends.clear();
        }
    };
    for (const loglens::RecordDelta& delta : deltas) {
        if (delta.kind == loglens::RecordDelta::Kind::Append) {
            const bool contiguous = appends.empty()
                                    || (delta.generation == appendGeneration
                                        && delta.record_index
                                               == firstAppendIndex + appends.size());
            if (!contiguous) {
                flushAppends();
            }
            if (appends.empty()) {
                firstAppendIndex = delta.record_index;
                appendGeneration = delta.generation;
            }
            appends.push_back(delta.record);
            continue;
        }
        flushAppends();
        model_->updateRecord(delta.record_index, delta.record, delta.generation);
    }
    flushAppends();
}

void MainWindow::chooseFile() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Open a log file"));
    if (path.isEmpty()) {
        return;
    }
    openPath(path);
}

void MainWindow::openPath(const QString& path) {
    openPath(path, selectedLoadMode(), static_cast<std::size_t>(tailRecords_->value()));
}

void MainWindow::openPath(const QString& path, loglens::InitialLoadMode mode,
                          std::size_t tailRecords) {
    cancelWholeFileSearch();
    pendingSession_.reset();
    sessionEvidenceNotice_.clear();
    baselineWindow_.reset();
    comparisonWindow_.reset();
    baselineWindowLabel_->setText(tr("Baseline: not set"));
    comparisonWindowLabel_->setText(tr("Comparison: not set"));
    searchResults_->clear();
    searchEvidence_->clear();
    searchResult_ = {};
    searchStatus_->setText(tr("Source changed. Start a new whole-file search."));
    currentPath_ = QFileInfo(path).absoluteFilePath();
    model_->setSourceIdentity({});
    ++active_job_;
    loader_->selectJob(active_job_);
    expected_sequence_ = 0;
    retryAttempts_ = 0;
    backlog_pending_ = true;
    source_active_ = true;
    followBox_->setEnabled(true);
    model_->resetRecords();
    model_->setTriageState(triageState_, currentPath_);
    timeline_->clearSelection();
    refreshTimeline();
    updateStatus(tr("loading…"));
    setWindowTitle(tr("loglens — %1").arg(currentPath_));
    setFollowing(followBox_->isChecked());

    loglens::LoadRequest request;
    request.job_id = active_job_;
    request.path = currentPath_;
    request.mode = mode;
    request.tail_records = std::max<std::size_t>(1, std::min(tailRecords, record_capacity_));
    request.source_chunk_bytes = source_chunk_bytes_;
    const loglens::SourceProfile profile = profileFromControls();
    request.format = profile.format;
    request.multiline = profile.multiline;
    request.max_record_bytes = profile.max_record_bytes;
    request.format_plugin = formatPlugin_;
    emit startLoadRequested(std::move(request));
}

void MainWindow::pollSource() {
    if (active_job_ == 0 || followState_ == FollowState::Stopped) {
        return;
    }
    emit pollRequested(active_job_);
}

void MainWindow::handleLoadError(const loglens::LoadBatch& batch) {
    backlog_pending_ = false;
    if (batch.initial_phase) {
        source_active_ = false;
        status_->setText(tr("Cannot read: %1").arg(batch.error));
        setWindowTitle(tr("loglens"));
        followBox_->setChecked(false);
        followBox_->setEnabled(false);
        return;
    }
    if (batch.retryable) {
        followState_ = FollowState::WaitingRetry;
        ++retryAttempts_;
        status_->setText(tr("Follow waiting (attempt %1): %2")
                             .arg(retryAttempts_)
                             .arg(batch.error));
        return;
    }

    status_->setText(tr("Follow stopped: %1").arg(batch.error));
    followBox_->setChecked(false);
}

void MainWindow::handleLoadBatch(const loglens::LoadBatch& batch) {
    if (batch.job_id != active_job_) {
        return;
    }
    if (batch.sequence != expected_sequence_) {
        ++active_job_;
        loader_->selectJob(active_job_);
        source_active_ = false;
        backlog_pending_ = false;
        followBox_->setChecked(false);
        status_->setText(tr("Load stopped: background batch sequence mismatch"));
        return;
    }
    ++expected_sequence_;
    followState_ = followBox_->isChecked() ? FollowState::Following : FollowState::Stopped;
    backlog_pending_ = batch.backlog_pending;
    if (!batch.error.isEmpty()) {
        handleLoadError(batch);
        emit loadProgress(static_cast<qulonglong>(model_->totalSeen()),
                          static_cast<qulonglong>(model_->totalCount()), false,
                          batch.error);
        emit acknowledgeRequested(batch.job_id, batch.sequence);
        return;
    }
    retryAttempts_ = 0;
    model_->setSourceIdentity(loglens::sourceIdentity(batch.identity));
    if (batch.reset_model) {
        timeline_->clearSelection();
        model_->resetRecords(batch.generation);
    }
    applyDeltas(batch.deltas);
    if (batch.initial_complete && pendingSession_) {
        const auto state = *pendingSession_;
        pendingSession_.reset();
        baselineWindow_ = state.baseline_window;
        comparisonWindow_ = state.comparison_window;
        baselineWindowLabel_->setText(state.baseline_window
                                          ? tr("Baseline: %1–%2 ms")
                                                .arg(state.baseline_window->begin_ms)
                                                .arg(state.baseline_window->end_ms)
                                          : tr("Baseline: not set"));
        comparisonWindowLabel_->setText(state.comparison_window
                                            ? tr("Comparison: %1–%2 ms")
                                                  .arg(state.comparison_window->begin_ms)
                                                  .arg(state.comparison_window->end_ms)
                                            : tr("Comparison: not set"));
        if (state.selected_window)
            timeline_->setSelection(state.selected_window->begin_ms, state.selected_window->end_ms);
    }
    if (batch.initial_complete || batch.reset_model)
        refreshArchivedTriage();
    scheduleTimelineRefresh();
    updateStatus(backlog_pending_ ? tr("loading…") : sessionEvidenceNotice_);
    if (autoScroll_ && !batch.deltas.empty()) {
        table_->scrollToBottom();
    }
    emit loadProgress(static_cast<qulonglong>(model_->totalSeen()),
                      static_cast<qulonglong>(model_->totalCount()),
                      batch.initial_complete, QString());
    emit acknowledgeRequested(batch.job_id, batch.sequence);
}

void MainWindow::setFollowing(bool following) {
    loader_->setFollowing(active_job_, following && source_active_);
    if (following && source_active_ && active_job_ != 0) {
        followState_ = FollowState::Following;
        retryAttempts_ = 0;
        pollTimer_->start();
        return;
    }
    followState_ = FollowState::Stopped;
    retryAttempts_ = 0;
    pollTimer_->stop();
}

loglens::InitialLoadMode MainWindow::selectedLoadMode() const {
    return loadMode_->currentIndex() == 0 ? loglens::InitialLoadMode::TailRecords
                                          : loglens::InitialLoadMode::FromStart;
}

void MainWindow::applyFilter() {
    applyFilterText(filterEdit_->text());
}

bool MainWindow::applyFilterText(const QString& text, const QString& successMessage) {
    if (text.trimmed().isEmpty()) {
        filter_.reset();
        timeline_->clearSelection();
        model_->setFilter(nullptr);
        refreshTimeline();
        updateStatus(successMessage);
        return true;
    }
    loglens::ParseError error;
    const std::optional<loglens::Filter> candidate =
        loglens::Filter::parse(text.toStdString(), error);
    if (!candidate) {
        // Keep the previous view; a typo should not blank the table.
        updateStatus(tr("bad filter at bytes %1: %2")
                         .arg(filterErrorRange(error))
                         .arg(QString::fromStdString(error.message)));
        return false;
    }
    filter_ = candidate;
    timeline_->clearSelection();
    model_->setFilter(&filter_.value());
    refreshTimeline();
    updateStatus(successMessage);
    return true;
}

void MainWindow::refreshTimeline() {
    loglens::Stats stats;
    for (const loglens::LogRecord& record : model_->visibleRecords(false)) {
        stats.add(record);
    }
    timeline_->setBuckets(stats.buckets(kBucketMs));
}

void MainWindow::scheduleTimelineRefresh() {
    timelineTimer_->start();
}

void MainWindow::updateStatus(const QString& extra) {
    QString lineRange = tr("none");
    const std::optional<std::size_t> oldest = model_->oldestLine();
    const std::optional<std::size_t> newest = model_->newestLine();
    if (oldest && newest) {
        lineRange = QStringLiteral("%1–%2")
                        .arg(static_cast<qulonglong>(*oldest))
                        .arg(static_cast<qulonglong>(*newest));
    }
    QString message =
        tr("%1 visible / %2 retained · %3 seen · %4 dropped · lines %5 · cap %6")
            .arg(model_->rowCount())
            .arg(model_->totalCount())
            .arg(static_cast<qulonglong>(model_->totalSeen()))
            .arg(static_cast<qulonglong>(model_->droppedCount()))
            .arg(lineRange)
            .arg(static_cast<qulonglong>(model_->capacity()));
    if (!extra.isEmpty()) {
        message += QStringLiteral("  —  ") + extra;
    }
    status_->setText(message);
    status_->setToolTip(message);
}
