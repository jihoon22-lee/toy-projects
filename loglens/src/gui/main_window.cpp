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

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    auto* bar = new QHBoxLayout();
    auto* openButton = new QPushButton(tr("Open log…"), central);
    filterEdit_ = new QLineEdit(central);
    filterEdit_->setPlaceholderText(tr("level>=WARN AND message~timeout"));
    auto* applyButton = new QPushButton(tr("Apply"), central);
    bar->addWidget(openButton);
    bar->addWidget(filterEdit_, 1);
    bar->addWidget(applyButton);
    followBox_ = new QCheckBox(tr("Follow"), central);
    followBox_->setChecked(true);
    bar->addWidget(followBox_);
    layout->addLayout(bar);

    timeline_ = new TimelineWidget(central);
    layout->addWidget(timeline_);

    model_ = new LogModel(this);
    table_ = new QTableView(central);
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
    statusBar()->addWidget(status_);

    pollTimer_ = new QTimer(this);
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
    for (const loglens::RecordDelta& delta : deltas) {
        if (delta.kind == loglens::RecordDelta::Kind::Append) {
            appends.push_back(delta.record);
            continue;
        }
        if (!appends.empty()) {
            model_->appendRecords(appends);
            appends.clear();
        }
        model_->updateRecord(delta.record_index, delta.record);
    }
    if (!appends.empty()) {
        model_->appendRecords(appends);
    }
}

void MainWindow::chooseFile() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Open a log file"));
    if (path.isEmpty()) {
        return;
    }
    openPath(path);
}

void MainWindow::openPath(const QString& path) {
    tailer_ = std::make_unique<loglens::FileTailer>(path.toStdString());
    loglens::SourceChunk chunk;
    std::string error;
    if (!tailer_->pollChunk(chunk, error)) {
        // Report it rather than showing an empty window with no explanation.
        status_->setText(tr("Cannot read: %1").arg(QString::fromStdString(error)));
        tailer_.reset();
        assembler_.reset();
        model_->resetRecords();
        return;
    }
    assembler_.reset(tailer_->generation());
    model_->setRecords(std::vector<loglens::LogRecord>());
    applyDeltas(assembler_.consumeBytes(chunk.bytes));
    applyFilter();
    setWindowTitle(tr("loglens — %1").arg(path));
    setFollowing(followBox_->isChecked());
}

void MainWindow::pollSource() {
    if (!tailer_) {
        return;
    }
    const std::size_t restartsBefore = tailer_->restarts();
    loglens::SourceChunk chunk;
    std::string error;
    if (!tailer_->pollChunk(chunk, error)) {
        // Stop rather than spin: whatever broke will keep breaking every tick.
        status_->setText(tr("Follow stopped: %1").arg(QString::fromStdString(error)));
        followBox_->setChecked(false);
        return;
    }

    // A truncated or replaced file makes every retained row stale, so the model
    // starts over instead of appending new lines under old ones.
    if (tailer_->restarts() != restartsBefore || chunk.generation_changed) {
        assembler_.reset(tailer_->generation());
        model_->resetRecords();
    }
    const std::vector<loglens::RecordDelta> deltas = assembler_.consumeBytes(chunk.bytes);
    if (deltas.empty()) {
        return;
    }
    applyDeltas(deltas);
    refreshTimeline();
    updateStatus(QString());
    if (autoScroll_) {
        table_->scrollToBottom();
    }
}

void MainWindow::setFollowing(bool following) {
    if (following && tailer_) {
        pollTimer_->start();
        return;
    }
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
    QString message = tr("%1 / %2 line(s)").arg(model_->rowCount()).arg(model_->totalCount());
    if (!extra.isEmpty()) {
        message += QStringLiteral("  —  ") + extra;
    }
    status_->setText(message);
}
