#include "loglens/gui/main_window.hpp"

#include <QCheckBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStatusBar>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include <string>
#include <vector>

#include "loglens/gui/log_model.hpp"
#include "loglens/log_parser.hpp"
#include "loglens/log_source.hpp"
#include "loglens/log_stats.hpp"
#include "loglens/gui/timeline_widget.hpp"

namespace {

constexpr std::uint64_t kBucketMs = 60000;

} // namespace

MainWindow::MainWindow(QWidget* parent, std::size_t recordCapacity, std::size_t sourceChunkBytes)
    : QMainWindow(parent), source_chunk_bytes_(sourceChunkBytes) {
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    auto* bar = new QHBoxLayout();
    auto* openButton = new QPushButton(tr("Open log…"), central);
    openButton->setObjectName(QStringLiteral("openButton"));
    openButton->setAccessibleName(tr("Open log file"));
    filterEdit_ = new QLineEdit(central);
    filterEdit_->setObjectName(QStringLiteral("filterEdit"));
    filterEdit_->setAccessibleName(tr("Log filter"));
    filterEdit_->setPlaceholderText(tr("level>=WARN AND message~timeout"));
    auto* applyButton = new QPushButton(tr("Apply"), central);
    applyButton->setObjectName(QStringLiteral("applyFilterButton"));
    applyButton->setAccessibleName(tr("Apply log filter"));
    bar->addWidget(openButton);
    bar->addWidget(filterEdit_, 1);
    bar->addWidget(applyButton);
    followBox_ = new QCheckBox(tr("Follow"), central);
    followBox_->setObjectName(QStringLiteral("followCheckBox"));
    followBox_->setAccessibleName(tr("Follow log file"));
    followBox_->setChecked(true);
    bar->addWidget(followBox_);
    layout->addLayout(bar);

    timeline_ = new TimelineWidget(central);
    timeline_->setObjectName(QStringLiteral("timelineWidget"));
    timeline_->setAccessibleName(tr("Log timeline"));
    layout->addWidget(timeline_);

    model_ = new LogModel(this, recordCapacity);
    table_ = new QTableView(central);
    table_->setObjectName(QStringLiteral("logTable"));
    table_->setAccessibleName(tr("Log records"));
    table_->setModel(model_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    // Uniform row heights let the view size itself without measuring every row,
    // which is what keeps a very large file scrollable.
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    layout->addWidget(table_, 1);

    setCentralWidget(central);
    status_ = new QLabel(tr("Ready"), this);
    status_->setObjectName(QStringLiteral("statusLabel"));
    status_->setAccessibleName(tr("Log status"));
    statusBar()->addWidget(status_);

    pollTimer_ = new QTimer(this);
    pollTimer_->setObjectName(QStringLiteral("followPollTimer"));
    pollTimer_->setInterval(500);
    connect(pollTimer_, &QTimer::timeout, this, &MainWindow::pollSource);
    connect(followBox_, &QCheckBox::toggled, this, &MainWindow::setFollowing);

    // Following the tail is only wanted while the view is already at the tail.
    // Scrolling up to read something is an explicit request to stay put, and
    // yanking the viewport back down would make the log unreadable.
    connect(table_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        autoScroll_ = value == table_->verticalScrollBar()->maximum();
    });

    connect(openButton, &QPushButton::clicked, this, &MainWindow::chooseFile);
    connect(applyButton, &QPushButton::clicked, this, &MainWindow::applyFilter);
    connect(filterEdit_, &QLineEdit::returnPressed, this, &MainWindow::applyFilter);

    resize(1100, 700);
    setWindowTitle(tr("loglens"));
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
    tailer_ = std::make_unique<loglens::FileTailer>(path.toStdString(), source_chunk_bytes_);
    loglens::SourceChunk chunk;
    std::string error;
    if (!tailer_->pollChunk(chunk, error)) {
        // Report it rather than showing an empty window with no explanation.
        status_->setText(tr("Cannot read: %1").arg(QString::fromStdString(error)));
        tailer_.reset();
        backlog_pending_ = false;
        assembler_.reset();
        model_->resetRecords();
        refreshTimeline();
        setWindowTitle(tr("loglens"));
        // A failed replacement must not leave the old source's timer spinning
        // against an empty model. This also makes a failed open recoverable:
        // the user can re-enable follow after a successful replacement open.
        followBox_->setChecked(false);
        return;
    }
    assembler_.reset(tailer_->generation());
    backlog_pending_ = chunk.more_available;
    model_->setRecords(std::vector<loglens::LogRecord>(), tailer_->generation());
    applyDeltas(assembler_.consumeBytes(chunk.bytes));
    applyFilter();
    setWindowTitle(tr("loglens — %1").arg(path));
    setFollowing(followBox_->isChecked());
    scheduleBacklogPoll();
}

void MainWindow::pollSource() {
    if (!tailer_ || (followState_ == FollowState::Stopped && !backlog_pending_)) {
        return;
    }
    const std::size_t restartsBefore = tailer_->restarts();
    loglens::SourceChunk chunk;
    std::string error;
    if (!tailer_->pollChunk(chunk, error)) {
        handleSourceError(chunk, error);
        return;
    }

    applySourceChunk(chunk, restartsBefore);
}

void MainWindow::handleSourceError(const loglens::SourceChunk& chunk,
                                   const std::string& error) {
    backlog_pending_ = false;
    if (chunk.error.retryable) {
        followState_ = FollowState::WaitingRetry;
        ++retryAttempts_;
        status_->setText(tr("Follow waiting (attempt %1): %2")
                             .arg(retryAttempts_)
                             .arg(QString::fromStdString(error)));
        return;
    }

    status_->setText(tr("Follow stopped: %1").arg(QString::fromStdString(error)));
    followBox_->setChecked(false);
}

void MainWindow::applySourceChunk(const loglens::SourceChunk& chunk,
                                  std::size_t restartsBefore) {
    followState_ = followBox_->isChecked() ? FollowState::Following : FollowState::Stopped;
    retryAttempts_ = 0;
    backlog_pending_ = chunk.more_available;
    // A truncated or replaced file makes every retained row stale, so the model
    // starts over instead of appending new lines under old ones. Comparing the
    // parser generation as well handles a replacement that was detected during
    // a retryable read failure: the old rows remain visible while waiting, but
    // are discarded before the first successful bytes from the new source.
    if (tailer_->restarts() != restartsBefore || chunk.generation_changed
        || chunk.generation != assembler_.generation()) {
        assembler_.reset(tailer_->generation());
        model_->resetRecords(tailer_->generation());
    }
    const std::vector<loglens::RecordDelta> deltas = assembler_.consumeBytes(chunk.bytes);
    if (deltas.empty()) {
        refreshTimeline();
        updateStatus(backlog_pending_ ? tr("loading…") : QString());
        scheduleBacklogPoll();
        return;
    }
    applyDeltas(deltas);
    refreshTimeline();
    updateStatus(backlog_pending_ ? tr("loading…") : QString());
    if (autoScroll_) {
        table_->scrollToBottom();
    }
    scheduleBacklogPoll();
}

void MainWindow::scheduleBacklogPoll() {
    if (backlog_pending_) {
        QTimer::singleShot(0, this, &MainWindow::pollSource);
    }
}

void MainWindow::setFollowing(bool following) {
    if (following && tailer_) {
        followState_ = FollowState::Following;
        retryAttempts_ = 0;
        pollTimer_->start();
        return;
    }
    followState_ = FollowState::Stopped;
    retryAttempts_ = 0;
    pollTimer_->stop();
}

void MainWindow::applyFilter() {
    const QString text = filterEdit_->text().trimmed();
    if (text.isEmpty()) {
        filter_.reset();
        model_->setFilter(nullptr);
        refreshTimeline();
        updateStatus(QString());
        return;
    }
    loglens::ParseError error;
    filter_ = loglens::Filter::parse(text.toStdString(), error);
    if (!filter_) {
        // Keep the previous view; a typo should not blank the table.
        updateStatus(tr("bad filter at %1: %2")
                         .arg(error.position)
                         .arg(QString::fromStdString(error.message)));
        return;
    }
    model_->setFilter(&filter_.value());
    refreshTimeline();
    updateStatus(QString());
}

void MainWindow::refreshTimeline() {
    loglens::Stats stats;
    for (int row = 0; row < model_->rowCount(); ++row) {
        const loglens::LogRecord* record = model_->recordAt(row);
        if (record != nullptr) {
            stats.add(*record);
        }
    }
    timeline_->setBuckets(stats.buckets(kBucketMs));
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
}
