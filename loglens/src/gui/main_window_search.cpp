#include "loglens/gui/main_window.hpp"
#include "loglens/gui/log_model.hpp"
#include "loglens/evidence.hpp"

#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTabWidget>
#include <QThread>
#include <QTreeWidget>
#include <QHeaderView>
#include <unordered_map>

void MainWindow::setupWholeFileSearch() {
    auto *tabs = findChild<QTabWidget *>(QStringLiteral("investigationTabs"));
    auto *page = new QWidget(tabs);
    auto *layout = new QVBoxLayout(page);
    auto *help =
        new QLabel(tr("Search the complete source snapshot, independently of retained rows. Uses "
                      "the active parser and structured filter. ASCII case-insensitive text."),
                   page);
    help->setWordWrap(true);
    layout->addWidget(help);
    wholeSearchEdit_ = new QLineEdit(page);
    wholeSearchEdit_->setObjectName(QStringLiteral("wholeFileSearchEdit"));
    wholeSearchEdit_->setMaxLength(static_cast<int>(loglens::kMaxFilterQueryBytes));
    wholeSearchEdit_->setPlaceholderText(tr("Text in the complete file"));
    layout->addWidget(wholeSearchEdit_);
    auto *buttons = new QHBoxLayout();
    auto *search = new QPushButton(tr("Search file"), page);
    search->setObjectName(QStringLiteral("wholeFileSearchButton"));
    auto *cancel = new QPushButton(tr("Cancel"), page);
    cancel->setObjectName(QStringLiteral("cancelWholeFileSearchButton"));
    buttons->addWidget(search);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);
    searchStatus_ =
        new QLabel(tr("Up to 1 GiB, 30 seconds, 1,000 matches or 8 MiB of results"), page);
    searchStatus_->setObjectName(QStringLiteral("wholeFileSearchStatus"));
    searchStatus_->setWordWrap(true);
    layout->addWidget(searchStatus_);
    searchResults_ = new QTreeWidget(page);
    searchResults_->setObjectName(QStringLiteral("wholeFileSearchResults"));
    searchResults_->setColumnCount(3);
    searchResults_->setHeaderLabels({tr("Line"), tr("Level"), tr("Evidence")});
    searchResults_->setRootIsDecorated(false);
    searchResults_->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(searchResults_, 1);
    searchEvidence_ = new QPlainTextEdit(page);
    searchEvidence_->setObjectName(QStringLiteral("wholeFileSearchEvidence"));
    searchEvidence_->setReadOnly(true);
    layout->addWidget(searchEvidence_, 1);
    tabs->addTab(page, tr("Search file"));
    connect(search, &QPushButton::clicked, this, &MainWindow::startWholeFileSearch);
    connect(wholeSearchEdit_, &QLineEdit::returnPressed, this, &MainWindow::startWholeFileSearch);
    connect(cancel, &QPushButton::clicked, this, &MainWindow::cancelWholeFileSearch);
    connect(searchResults_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (!item)
            return;
        const auto index = item->data(0, Qt::UserRole).toULongLong();
        if (index >= searchResult_.records.size())
            return;
        const auto &record = searchResult_.records[static_cast<std::size_t>(index)];
        searchEvidence_->setPlainText(
            tr("Source identity: %1\nLine: %2\nRecord SHA-256: %3\nSnapshot: %4 bytes\n\n%5")
                .arg(QString::fromStdString(searchResult_.source_identity))
                .arg(static_cast<qulonglong>(record.line_number))
                .arg(record.omitted_bytes == 0
                         ? QString::fromStdString(loglens::recordFingerprint(record))
                         : tr("unavailable: %1 bytes omitted").arg(record.omitted_bytes))
                .arg(searchResult_.snapshot_end)
                .arg(QString::fromUtf8(record.raw.data(), static_cast<int>(record.raw.size()))));
    });
    archivedTriage_ = new QTreeWidget(tabs);
    archivedTriage_->setObjectName(QStringLiteral("archivedTriageTree"));
    archivedTriage_->setColumnCount(3);
    archivedTriage_->setHeaderLabels({tr("Note"), tr("Source line"), tr("Binding")});
    archivedTriage_->setRootIsDecorated(false);
    tabs->addTab(archivedTriage_, tr("All notes"));
}

void MainWindow::startWholeFileSearch() {
    if (searchThread_ && searchThread_->isRunning()) {
        searchStatus_->setText(tr("A search is running. Cancel it before starting another."));
        return;
    }
    if (currentPath_.isEmpty()) {
        searchStatus_->setText(tr("Open a source first"));
        return;
    }
    loglens::FileSearchOptions options;
    options.path = currentPath_.toStdString();
    options.text = wholeSearchEdit_->text().toStdString();
    options.filter = activeFilterText_.toStdString();
    const auto profile = profileFromControls();
    options.format = profile.format;
    options.multiline = profile.multiline;
    options.max_record_bytes = profile.max_record_bytes;
    options.plugin = formatPlugin_;
    searchResults_->clear();
    searchEvidence_->clear();
    searchStatus_->setText(tr("Searching the fixed source snapshot…"));
    searchCancelled_ = std::make_shared<std::atomic<bool>>(false);
    const auto gate = searchCancelled_;
    const auto sourceJob = active_job_;
    const auto searchJob = ++search_job_;
    auto *thread = QThread::create([this, options, gate, sourceJob, searchJob] {
        auto result =
            loglens::searchFile(options, [gate] { return gate->load(std::memory_order_acquire); });
        QMetaObject::invokeMethod(
            this,
            [this, result = std::move(result), sourceJob, searchJob]() mutable {
                if (sourceJob != active_job_ || searchJob != search_job_)
                    return;
                searchResult_ = std::move(result);
                for (std::size_t i = 0; i < searchResult_.records.size(); ++i) {
                    const auto &record = searchResult_.records[i];
                    auto *item = new QTreeWidgetItem(searchResults_);
                    item->setText(0, QString::number(record.line_number));
                    item->setText(1, QString::fromLatin1(loglens::levelName(record.level)));
                    item->setText(2, QString::fromUtf8(record.message.data(),
                                                       static_cast<int>(record.message.size()))
                                         .section('\n', 0, 0));
                    item->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<qulonglong>(i)));
                }
                const QString outcome =
                    !searchResult_.error.empty()  ? QString::fromStdString(searchResult_.error)
                    : searchResult_.cancelled     ? tr("cancelled; partial results")
                    : searchResult_.limit_reached ? tr("budget reached; partial results")
                                                  : tr("snapshot complete");
                searchStatus_->setText(tr("%1 matches · %2/%3 bytes · %4")
                                           .arg(searchResult_.records.size())
                                           .arg(searchResult_.scanned_bytes)
                                           .arg(searchResult_.snapshot_end)
                                           .arg(outcome));
            },
            Qt::QueuedConnection);
    });
    thread->setObjectName(QStringLiteral("wholeFileSearchThread"));
    searchThread_ = thread;
    thread->setParent(this);
    connect(thread, &QThread::finished, this, [this, thread] {
        if (searchThread_ == thread)
            searchThread_ = nullptr;
        thread->deleteLater();
    });
    thread->start();
}

void MainWindow::cancelWholeFileSearch() {
    if (searchCancelled_)
        searchCancelled_->store(true, std::memory_order_release);
}

void MainWindow::refreshArchivedTriage() {
    if (!archivedTriage_)
        return;
    archivedTriage_->clear();
    std::unordered_map<std::size_t, const loglens::LogRecord *> records;
    if (!triageState_.entries.empty()) {
        for (int row = 0; row < model_->rowCount(); ++row) {
            const auto *record = model_->recordAt(row);
            if (record) records.emplace(record->line_number, record);
        }
    }
    for (const auto &entry : triageState_.entries) {
        const auto found = records.find(entry.line_number);
        const auto *record = found == records.end() ? nullptr : found->second;
        const bool bound = record && loglens::matchesTriageEntry(entry, currentPath_.toStdString(),
                                                                 model_->sourceIdentity(),
                                                                 model_->generation(), *record);
        auto *item = new QTreeWidgetItem(archivedTriage_);
        item->setText(
            0, QString::fromStdString(entry.annotation.empty() ? "Bookmark" : entry.annotation));
        item->setText(1, QString::fromStdString(entry.source_path) + ":" +
                             QString::number(entry.line_number));
        item->setText(2, bound ? tr("Verified in view")
                               : tr("Archived / not verified in retained view"));
        item->setToolTip(2,
                         tr("Notes require the same opened-file identity, generation and complete "
                            "record digest. Older or unmatched notes are preserved here."));
    }
}
