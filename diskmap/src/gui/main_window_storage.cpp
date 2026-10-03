#include "diskmap/gui/main_window.hpp"

#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include "diskmap/gui/treemap_widget.hpp"
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QTimer>
#include <QProgressBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUndoStack>
#include <QtConcurrent>

#include <algorithm>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "diskmap/format.hpp"
#include "diskmap/fs_node.hpp"

namespace {

QString pathText(const std::filesystem::path& path) {
    const std::string value = path.generic_string();
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

QString bytesText(const diskmap::MetricValue& value) {
    const QString amount =
        QString::fromStdString(diskmap::humanBytes(value.bytes));
    if (value.known) {
        return amount;
    }
    return value.bytes == 0 ? QObject::tr("Unknown")
                            : QObject::tr("At least %1").arg(amount);
}

QString confidenceText(bool certain) {
    return certain ? QObject::tr("Certain") : QObject::tr("Candidate / uncertain");
}

QString changeName(diskmap::SnapshotChangeKind kind) {
    switch (kind) {
    case diskmap::SnapshotChangeKind::Added:
        return QObject::tr("Added");
    case diskmap::SnapshotChangeKind::Removed:
        return QObject::tr("Removed");
    case diskmap::SnapshotChangeKind::Grown:
        return QObject::tr("Grown");
    case diskmap::SnapshotChangeKind::Shrunk:
        return QObject::tr("Shrunk");
    case diskmap::SnapshotChangeKind::Moved:
        return QObject::tr("Moved");
    case diskmap::SnapshotChangeKind::Uncertain:
        return QObject::tr("Uncertain");
    }
    return QObject::tr("Unknown");
}

void setCell(QTableWidget& table, int row, int column, const QString& value) {
    table.setItem(row, column, new QTableWidgetItem(value));
}

int boundedRowCount(std::size_t count) {
    const std::size_t maximum =
        static_cast<std::size_t>(std::numeric_limits<int>::max());
    return static_cast<int>(std::min(count, maximum));
}

QString duplicateReclamation(const diskmap::DuplicateGroup& group,
                             std::size_t) {
    if (group.reclaimable) {
        return QObject::tr("Safe to stage");
    }
    if (!group.reason.empty()) {
        return QString::fromStdString(group.reason);
    }
    return QObject::tr("Not reclaimable");
}

diskmap::DuplicateAnalysis failedDuplicateAnalysis(std::string message) {
    diskmap::DuplicateAnalysis result;
    result.complete = false;
    result.uncertain = true;
    result.issues.push_back(diskmap::DuplicateIssue{
        {}, {}, diskmap::DuplicateIssueKind::ReadError, std::move(message)});
    return result;
}

} // namespace

void MainWindow::saveSnapshot() {
    if (!document_ || activeCancellation_ || activeDuplicateCancellation_ || activeStorageCancellation_) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save DiskMap snapshot"), QString(), tr("DiskMap snapshots (*.json);;All files (*)"));
    if (!path.isEmpty()) {
        saveSnapshotPath(path);
    }
}

void MainWindow::startStorageJob(const QString& label, StorageTask task,
                                 std::function<void(StorageJobResult)> finish) {
    if (activeCancellation_ || activeDuplicateCancellation_ || activeStorageCancellation_) return;
    auto cancellation = std::make_shared<diskmap::ScanCancellationToken>();
    activeStorageCancellation_ = cancellation;
    operationProgress_->show();
    status_->setText(label + tr(" — Cancel stops before commit / between files"));
    updateControlState();
    auto* watcher = new QFutureWatcher<StorageJobResult>(this);
    storageWatcher_ = watcher;
    connect(watcher, &QFutureWatcher<StorageJobResult>::finished, this,
            [this, watcher, finish = std::move(finish)]() mutable {
        auto result = watcher->result();
        activeStorageCancellation_.reset();
        operationProgress_->hide();
        storageWatcher_ = nullptr;
        watcher->deleteLater();
        if (!result.error.isEmpty()) status_->setText(result.error);
        else finish(std::move(result));
        updateControlState();
    });
    watcher->setFuture(QtConcurrent::run([task = std::move(task), cancellation]() {
        try { return task(cancellation); }
        catch (const std::exception& error) {
            StorageJobResult result;
            result.error = QString::fromUtf8(error.what());
            return result;
        }
        catch (...) { StorageJobResult result; result.error = "Operation failed"; return result; }
    }));
}

void MainWindow::saveSnapshotPath(const QString& path) {
    if (path.isEmpty() || !document_) return;
    auto source = document_;
    const bool truncated = documentIsSnapshot_ && loadedSnapshot_.truncated;
    startStorageJob(tr("Saving snapshot"), [source, path, truncated](auto cancellation) {
        diskmap::SnapshotLimits limits;
        limits.cancelled = [cancellation]() { return cancellation->isCancelled(); };
        auto snapshot = diskmap::snapshotFromNode(source->root, limits);
        snapshot.truncated = snapshot.truncated || truncated;
        snapshot.complete = snapshot.complete && !snapshot.truncated;
        diskmap::writeSnapshotAtomically(snapshot, path.toStdString(), limits);
        return StorageJobResult{};
    }, [this, path](auto) { status_->setText(tr("Snapshot saved: %1").arg(path)); });
}

void MainWindow::loadSnapshot() {
    if (activeCancellation_ || activeDuplicateCancellation_ || activeStorageCancellation_) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load DiskMap snapshot"), QString(), tr("DiskMap snapshots (*.json);;All files (*)"));
    if (!path.isEmpty()) {
        loadSnapshotPath(path);
    }
}

void MainWindow::loadSnapshotPath(const QString& path) {
    if (path.isEmpty()) return;
    startStorageJob(tr("Loading snapshot"), [path](auto cancellation) {
        diskmap::SnapshotLimits limits;
        limits.cancelled = [cancellation]() { return cancellation->isCancelled(); };
        StorageJobResult result;
        result.snapshot = std::make_shared<diskmap::Snapshot>(diskmap::readSnapshotFile(path.toStdString(), limits));
        if (cancellation->isCancelled()) throw diskmap::SnapshotError("Snapshot load cancelled");
        return result;
    }, [this, path](auto result) { installLoadedSnapshot(std::move(*result.snapshot), path); });
}

void MainWindow::compareSnapshot() {
    if (!document_ || activeCancellation_ || activeDuplicateCancellation_ || activeStorageCancellation_) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Compare with DiskMap snapshot"), QString(),
        tr("DiskMap snapshots (*.json);;All files (*)"));
    if (!path.isEmpty()) {
        compareSnapshotPath(path);
    }
}

void MainWindow::compareSnapshotPath(const QString& path) {
    if (path.isEmpty() || !document_) return;
    auto source = document_;
    const auto metric = static_cast<diskmap::SizeMetric>(metricCombo_->currentData().toInt());
    startStorageJob(tr("Comparing snapshots"), [source, path, metric](auto cancellation) {
        diskmap::SnapshotLimits limits;
        limits.cancelled = [cancellation]() { return cancellation->isCancelled(); };
        const auto before = diskmap::readSnapshotFile(path.toStdString(), limits);
        const auto after = diskmap::snapshotFromNode(source->root, limits);
        diskmap::SnapshotDiffOptions options;
        options.metric = metric;
        options.cancelled = limits.cancelled;
        StorageJobResult result;
        result.diff = std::make_shared<diskmap::SnapshotDiff>(diskmap::diffSnapshots(before, after, options));
        return result;
    }, [this, path, metric](auto result) {
        snapshotDiffMetric_ = metric;
        snapshotDiff_ = std::move(*result.diff);
        snapshotComparePath_ = path;
        refreshSnapshotChanges();
        status_->setText(snapshotSummary_->text());
    });
}

void MainWindow::analyzeDuplicatesNow() { analyzeDuplicates(); }

void MainWindow::analyzeDuplicates() {
    if (!document_ || activeCancellation_ || activeDuplicateCancellation_ || activeStorageCancellation_) {
        return;
    }
    const std::shared_ptr<const diskmap::ScanResult> source = document_;
    const std::uint64_t generation = ++activeDuplicateGeneration_;
    const auto cancellation = std::make_shared<diskmap::ScanCancellationToken>();
    const auto progressState = std::make_shared<DuplicateProgressState>();
    activeDuplicateCancellation_ = cancellation;
    activeDuplicateProgress_ = progressState;
    clearDuplicateEvidence();
    duplicateSummary_->setText(tr("Analyzing duplicate candidates…"));
    progressTimer_->start();
    updateControlState();

    const DuplicateRunner runner = duplicateRunner_;
    const diskmap::DuplicateAnalysisOptions options;
    const diskmap::DuplicateProgressFn progress =
        [progressState](std::size_t files, std::uint64_t bytes) {
            progressState->files.store(files, std::memory_order_relaxed);
            progressState->bytes.store(bytes, std::memory_order_relaxed);
        };
    auto* watcher = new QFutureWatcher<diskmap::DuplicateAnalysis>(this);
    duplicateWatcher_ = watcher;
    connect(watcher, &QFutureWatcher<diskmap::DuplicateAnalysis>::finished, this,
            [this, watcher, generation, source]() {
                onDuplicatesFinished(watcher, generation, source);
            });
    watcher->setFuture(QtConcurrent::run(
        [runner, source, options, cancellation, progress]() {
            try {
                return runner(*source, options, cancellation, progress);
            } catch (const std::exception& error) {
                return failedDuplicateAnalysis(
                    std::string("duplicate worker failed: ") + error.what());
            } catch (...) {
                return failedDuplicateAnalysis(
                    "duplicate worker failed with an unknown exception");
            }
        }));
}

void MainWindow::stageDuplicateCandidates() {
    if (!document_ || documentIsSnapshot_ || activeCancellation_
        || activeDuplicateCancellation_) {
        return;
    }
    std::vector<diskmap::NodeKey> keys = stagedCleanupKeys_;
    const auto keepers = selectedKeepers();
    std::size_t staged = 0;
    for (const auto& group : duplicateAnalysis_.groups) {
        if (!group.reclaimable || !group.certain || group.entries.size() < 2) continue;
        const auto keeper = std::find_if(group.entries.begin(), group.entries.end(), [&](const auto& entry) {
            return std::find(keepers.begin(), keepers.end(), entry.key) != keepers.end();
        });
        if (keeper == group.entries.end()) continue;
        for (const auto& entry : group.entries) {
            if (entry.key == keeper->key) continue;
            if (std::find(keys.begin(), keys.end(), entry.key) == keys.end()) {
                keys.push_back(entry.key); ++staged;
            }
        }
    }
    if (staged == 0) {
        status_->setText(tr("No certain reclaimable duplicate copies are available to stage"));
        return;
    }
    stageCleanupKeysWithUndo(std::move(keys), tr("Stage duplicate copies"));
    status_->setText(tr("Staged %1 duplicate copy/copies for cleanup review")
                         .arg(staged));
}

void MainWindow::onDuplicatesFinished(
    QFutureWatcher<diskmap::DuplicateAnalysis>* watcher,
    std::uint64_t generation,
    std::shared_ptr<const diskmap::ScanResult> source) {
    if (generation != activeDuplicateGeneration_) {
        watcher->deleteLater();
        return;
    }
    diskmap::DuplicateAnalysis result = watcher->result();
    watcher->deleteLater();
    duplicateWatcher_ = nullptr;
    activeDuplicateCancellation_.reset();
    activeDuplicateProgress_.reset();
    if (!activeCancellation_) {
        progressTimer_->stop();
    }
    if (source != document_) {
        updateControlState();
        return;
    }
    duplicateAnalysis_ = std::move(result);
    refreshDuplicateEvidence();
    if (duplicateAnalysis_.cancelled) {
        status_->setText(tr("Duplicate analysis cancelled; retained evidence was cleared"));
    } else {
        status_->setText(duplicateSummary_->text());
    }
    updateControlState();
}

void MainWindow::cancelDuplicateAnalysis() {
    if (activeDuplicateCancellation_) {
        activeDuplicateCancellation_->cancel();
    }
}

diskmap::Snapshot MainWindow::currentSnapshot() const {
    if (!document_) {
        throw diskmap::SnapshotError("no scan document is loaded");
    }
    return diskmap::snapshotFromNode(document_->root);
}

void MainWindow::installLoadedSnapshot(diskmap::Snapshot snapshot,
                                       const QString& path) {
    ++activeGeneration_;
    const std::uint64_t generation = activeGeneration_;
    loadedSnapshot_ = {};
    loadedSnapshot_.complete = snapshot.complete;
    loadedSnapshot_.truncated = snapshot.truncated;
    loadedSnapshot_.nodes_retained = snapshot.nodes_retained;
    diskmap::ScanResult result = diskmap::scanEvidenceFromSnapshot(std::move(snapshot), generation);
    document_ = std::make_shared<diskmap::ScanResult>(std::move(result));
    hasLoadedSnapshot_ = true;
    documentIsSnapshot_ = true;
    loadedSnapshotPath_ = path;
    currentScanPath_ = pathText(document_->root.path);
    requestedScanPath_ = currentScanPath_;
    trail_ = {diskmap::nodeKey(document_->root)};
    selectedKey_.reset();
    cleanupUndo_->clear();
    stagedCleanupKeys_.clear();
    cleanupPlan_ = {};
    clearDuplicateEvidence();
    clearSnapshotChanges();
    refreshCleanupReview();
    refreshProjection();
    status_->setText(
        tr("Loaded read-only snapshot %1 · %2 node(s) · %3 evidence")
            .arg(path)
            .arg(loadedSnapshot_.nodes_retained)
            .arg(loadedSnapshot_.complete && !loadedSnapshot_.truncated
                     ? tr("complete")
                     : tr("conservative")));
    updateControlState();
}

void MainWindow::refreshDuplicateEvidence() {
    const auto keepers = selectedKeepers();
    duplicateEvidenceTable_->setRowCount(0);
    duplicateRows_.clear();
    std::size_t rowCount = 0;
    for (const diskmap::DuplicateGroup& group : duplicateAnalysis_.groups) {
        rowCount += group.entries.size();
    }
    duplicateEvidenceTable_->setRowCount(boundedRowCount(rowCount));
    std::size_t row = 0;
    std::size_t groupNumber = 1;
    for (std::size_t groupIndex = 0;
         groupIndex < duplicateAnalysis_.groups.size(); ++groupIndex, ++groupNumber) {
        const diskmap::DuplicateGroup& group = duplicateAnalysis_.groups[groupIndex];
        for (std::size_t member = 0; member < group.entries.size(); ++member) {
            if (row >= static_cast<std::size_t>(duplicateEvidenceTable_->rowCount())) {
                break;
            }
            const diskmap::DuplicateEntry& entry = group.entries[member];
            duplicateRows_.push_back({groupIndex, member});
            setCell(*duplicateEvidenceTable_, static_cast<int>(row), 0,
                    tr("#%1 · %2 files").arg(groupNumber).arg(group.entries.size()));
            setCell(*duplicateEvidenceTable_, static_cast<int>(row), 1,
                    confidenceText(group.certain && entry.certain));
            setCell(*duplicateEvidenceTable_, static_cast<int>(row), 2,
                    pathText(entry.path));
            setCell(*duplicateEvidenceTable_, static_cast<int>(row), 3,
                    QString::fromStdString(diskmap::humanBytes(entry.size)));
            setCell(*duplicateEvidenceTable_, static_cast<int>(row), 4,
                    QString::fromStdString(group.content_hash));
            setCell(*duplicateEvidenceTable_, static_cast<int>(row), 5,
                    std::find(keepers.begin(), keepers.end(), entry.key) != keepers.end() ? tr("KEEP — surviving copy") : duplicateReclamation(group, member));
            ++row;
        }
    }
    const QString inventory = duplicateAnalysis_.complete ? tr("complete inventory")
                                                          : tr("conservative inventory");
    duplicateSummary_->setText(
        tr("%1 duplicate group(s), %2 retained candidate(s) · %3 · %4 issue(s) · %5 read")
            .arg(duplicateAnalysis_.groups.size())
            .arg(duplicateAnalysis_.candidates_retained)
            .arg(inventory)
            .arg(duplicateAnalysis_.issues.size())
            .arg(QString::fromStdString(
                diskmap::humanBytes(duplicateAnalysis_.bytes_read))));
}

void MainWindow::clearDuplicateEvidence() {
    duplicateAnalysis_ = {};
    explicitKeepers_.clear();
    duplicateRows_.clear();
    if (duplicateEvidenceTable_ != nullptr) {
        duplicateEvidenceTable_->setRowCount(0);
    }
    if (duplicateSummary_ != nullptr) {
        duplicateSummary_->setText(tr("No duplicate analysis"));
    }
}

void MainWindow::clearSnapshotChanges() {
    snapshotDiff_ = {};
    snapshotComparePath_.clear();
    if (treemap_) treemap_->setChangeKinds({});
    if (folderSummary_) folderSummary_->clear();
    if (snapshotChangesTable_ != nullptr) {
        snapshotChangesTable_->setRowCount(0);
    }
    if (snapshotSummary_ != nullptr) {
        snapshotSummary_->setText(tr("No snapshot comparison"));
    }
}

std::vector<diskmap::NodeKey> MainWindow::selectedKeepers() const {
    std::vector<diskmap::NodeKey> result;
    if (!document_) return result;
    for (const auto& group : duplicateAnalysis_.groups) {
        const auto found = explicitKeepers_.find(group.content_hash);
        if (found != explicitKeepers_.end()) { result.push_back(found->second); continue; }
        const auto keeper = diskmap::chooseDuplicateKeeper(group, *document_,
            static_cast<diskmap::DuplicateKeeperPolicy>(keeperPolicyCombo_->currentIndex()),
            keeperDirectoryEdit_->text().toStdString(), stagedCleanupKeys_);
        if (keeper) result.push_back(*keeper);
    }
    return result;
}

void MainWindow::keepSelectedDuplicate() {
    const int row = duplicateEvidenceTable_->currentRow();
    if (row < 0 || static_cast<std::size_t>(row) >= duplicateRows_.size()) return;
    const auto [groupIndex, member] = duplicateRows_[static_cast<std::size_t>(row)];
    const auto& group = duplicateAnalysis_.groups[groupIndex];
    if (!group.certain || !group.reclaimable) return;
    explicitKeepers_[group.content_hash] = group.entries[member].key;
    refreshCleanupReview(); refreshDuplicateEvidence(); updateControlState();
}

void MainWindow::refreshSnapshotChanges() {
    if (!snapshotChangesTable_) return;
    std::uint64_t minimum = 0;
    if (!diffMinimumEdit_->text().trimmed().isEmpty()) {
        const auto parsed = diskmap::parseHumanBytes(diffMinimumEdit_->text().toStdString());
        if (!parsed) { snapshotSummary_->setText(tr("Enter a valid minimum change (e.g. 500 MB)")); return; }
        minimum = *parsed;
    }
    const auto delta = [](const diskmap::SnapshotChange& change) {
        const auto before = change.has_before ? change.before_metric.bytes : 0;
        const auto after = change.has_after ? change.after_metric.bytes : 0;
        return after >= before ? after - before : before - after;
    };
    std::vector<const diskmap::SnapshotChange*> rows;
    struct FolderTotal { std::uint64_t added = 0, removed = 0; bool uncertain = false, overflow = false; };
    std::map<std::string, FolderTotal> folders;
    const auto add = [](std::uint64_t& total, std::uint64_t value, bool& overflow) {
        if (value > std::numeric_limits<std::uint64_t>::max() - total) { overflow = true; total = std::numeric_limits<std::uint64_t>::max(); }
        else total += value;
    };
    std::map<std::string, int> colors;
    for (const auto& change : snapshotDiff_.changes) {
        if (change.has_after) colors[change.after_key.normalized_path] = static_cast<int>(change.kind);
        if (diffKindCombo_->currentData().toInt() >= 0 && static_cast<int>(change.kind) != diffKindCombo_->currentData().toInt()) continue;
        if (diffCertainOnly_->isChecked() && !change.certain) continue;
        const bool known = (!change.has_before || change.before_metric.known) && (!change.has_after || change.after_metric.known);
        if (minimum && (!known || delta(change) < minimum)) continue;
        const auto path = change.has_after ? change.after_key.normalized_path : change.before_key.normalized_path;
        if (!QString::fromStdString(path).contains(diffPathEdit_->text(), Qt::CaseInsensitive)) continue;
        rows.push_back(&change);
        const auto& key = change.has_after ? change.after_key : change.before_key;
        if (key.kind != diskmap::FsKind::Directory) {
            if (change.has_before) {
                auto& summary = folders[std::filesystem::path(change.before_key.normalized_path).parent_path().generic_string()];
                add(summary.removed, change.before_metric.bytes, summary.overflow);
                summary.uncertain |= !known || !change.certain;
            }
            if (change.has_after) {
                auto& summary = folders[std::filesystem::path(change.after_key.normalized_path).parent_path().generic_string()];
                add(summary.added, change.after_metric.bytes, summary.overflow);
                summary.uncertain |= !known || !change.certain;
            }
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [&](const auto* a, const auto* b) { return delta(*a) > delta(*b); });
    snapshotChangesTable_->setRowCount(static_cast<int>(std::min<std::size_t>(rows.size(), 5000)));
    for (int row = 0; row < snapshotChangesTable_->rowCount(); ++row) {
        const auto& change = *rows[static_cast<std::size_t>(row)];
        setCell(*snapshotChangesTable_, row, 0, changeName(change.kind));
        setCell(*snapshotChangesTable_, row, 1, confidenceText(change.certain));
        setCell(*snapshotChangesTable_, row, 2, change.has_before ? bytesText(change.before_metric) + " · " + QString::fromStdString(change.before_key.normalized_path) : tr("—"));
        setCell(*snapshotChangesTable_, row, 3, change.has_after ? bytesText(change.after_metric) + " · " + QString::fromStdString(change.after_key.normalized_path) : tr("—"));
        setCell(*snapshotChangesTable_, row, 4, QString::fromStdString(change.reason));
        const auto before = change.has_before ? change.before_metric.bytes : 0;
        const auto after = change.has_after ? change.after_metric.bytes : 0;
        const bool known = (!change.has_before || change.before_metric.known) && (!change.has_after || change.after_metric.known);
        setCell(*snapshotChangesTable_, row, 5, known ? (after >= before ? "+" : "-") + QString::fromStdString(diskmap::humanBytes(delta(change))) : tr("Unknown"));
    }
    QStringList summary;
    std::vector<std::pair<std::string, FolderTotal>> orderedFolders(folders.begin(), folders.end());
    const auto magnitude = [](const FolderTotal& value) { return value.added >= value.removed ? value.added - value.removed : value.removed - value.added; };
    std::stable_sort(orderedFolders.begin(), orderedFolders.end(), [&](const auto& a, const auto& b) { return magnitude(a.second) > magnitude(b.second); });
    for (const auto& [path, value] : orderedFolders) {
        if (summary.size() >= 8) break;
        const QString amount = value.overflow ? tr("unknown (overflow)") : (value.added >= value.removed ? "+" : "-") + QString::number(static_cast<qulonglong>(magnitude(value))) + tr(" bytes");
        summary << QString::fromStdString(path) + ": " + amount + (value.uncertain ? tr(" (uncertain)") : QString());
    }
    const auto metric = snapshotDiffMetric_ == diskmap::SizeMetric::Logical ? tr("Logical") : snapshotDiffMetric_ == diskmap::SizeMetric::Allocated ? tr("Allocated") : tr("Reclaimable");
    snapshotChangesTable_->horizontalHeaderItem(2)->setText(tr("Before (%1)").arg(metric));
    snapshotChangesTable_->horizontalHeaderItem(3)->setText(tr("After (%1)").arg(metric));
    folderSummary_->setText(tr("Folder entry sums (top 8; physical metrics may overlap hard links): %1").arg(summary.join(" · ")));
    snapshotSummary_->setText(tr("Compared with %1 (%2): showing %3 of %4 matching changes (%5 total) · sorted by absolute change").arg(snapshotComparePath_, metric).arg(snapshotChangesTable_->rowCount()).arg(rows.size()).arg(snapshotDiff_.changes.size()));
    treemap_->setChangeKinds(std::move(colors));
}
