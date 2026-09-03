#include "diskmap/gui/main_window.hpp"

#include <QComboBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QTimer>
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
                             std::size_t member) {
    if (group.reclaimable) {
        return member == 0 ? QObject::tr("Keep one copy")
                           : QObject::tr("Safe to stage");
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
    if (!document_ || activeCancellation_ || activeDuplicateCancellation_) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save DiskMap snapshot"), QString(), tr("DiskMap snapshots (*.json);;All files (*)"));
    if (!path.isEmpty()) {
        saveSnapshotPath(path);
    }
}

void MainWindow::saveSnapshotPath(const QString& path) {
    if (path.isEmpty()) {
        status_->setText(tr("Choose a non-empty snapshot path"));
        return;
    }
    if (!document_) {
        status_->setText(tr("Scan or load a snapshot before saving"));
        return;
    }
    if (activeCancellation_ || activeDuplicateCancellation_) {
        status_->setText(tr("Wait for the active operation to finish before saving"));
        return;
    }
    try {
        diskmap::writeSnapshotAtomically(currentSnapshot(), path.toStdString());
        status_->setText(tr("Snapshot saved: %1").arg(path));
    } catch (const std::exception& error) {
        status_->setText(tr("Snapshot save failed: %1")
                             .arg(QString::fromStdString(error.what())));
    } catch (...) {
        status_->setText(tr("Snapshot save failed: unknown error"));
    }
}

void MainWindow::loadSnapshot() {
    if (activeCancellation_ || activeDuplicateCancellation_) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load DiskMap snapshot"), QString(), tr("DiskMap snapshots (*.json);;All files (*)"));
    if (!path.isEmpty()) {
        loadSnapshotPath(path);
    }
}

void MainWindow::loadSnapshotPath(const QString& path) {
    if (path.isEmpty()) {
        status_->setText(tr("Choose a non-empty snapshot path"));
        return;
    }
    if (activeCancellation_ || activeDuplicateCancellation_) {
        status_->setText(tr("Wait for the active operation to finish before loading"));
        return;
    }
    try {
        installLoadedSnapshot(diskmap::readSnapshotFile(path.toStdString()), path);
    } catch (const std::exception& error) {
        status_->setText(tr("Snapshot load failed: %1")
                             .arg(QString::fromStdString(error.what())));
    } catch (...) {
        status_->setText(tr("Snapshot load failed: unknown error"));
    }
}

void MainWindow::compareSnapshot() {
    if (!document_ || activeCancellation_ || activeDuplicateCancellation_) {
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
    if (path.isEmpty()) {
        status_->setText(tr("Choose a non-empty snapshot path"));
        return;
    }
    if (!document_) {
        status_->setText(tr("Scan or load a snapshot before comparing"));
        return;
    }
    if (activeCancellation_ || activeDuplicateCancellation_) {
        status_->setText(tr("Wait for the active operation to finish before comparing"));
        return;
    }
    try {
        const diskmap::Snapshot before = diskmap::readSnapshotFile(path.toStdString());
        const diskmap::Snapshot after = currentSnapshot();
        diskmap::SnapshotDiffOptions options;
        options.metric = static_cast<diskmap::SizeMetric>(
            metricCombo_->currentData().toInt());
        const diskmap::SnapshotDiff diff = diskmap::diffSnapshots(before, after, options);
        snapshotChangesTable_->setRowCount(boundedRowCount(diff.changes.size()));
        for (std::size_t index = 0;
             index < diff.changes.size()
             && index < static_cast<std::size_t>(snapshotChangesTable_->rowCount());
             ++index) {
            const diskmap::SnapshotChange& change = diff.changes[index];
            const QString beforePath = change.has_before
                                            ? QString::fromStdString(
                                                  change.before_key.normalized_path)
                                            : tr("—");
            const QString afterPath = change.has_after
                                           ? QString::fromStdString(
                                                 change.after_key.normalized_path)
                                           : tr("—");
            setCell(*snapshotChangesTable_, static_cast<int>(index), 0,
                    changeName(change.kind));
            setCell(*snapshotChangesTable_, static_cast<int>(index), 1,
                    confidenceText(change.certain));
            setCell(*snapshotChangesTable_, static_cast<int>(index), 2,
                    change.has_before ? bytesText(change.before_metric) + " · "
                                            + beforePath
                                      : tr("—"));
            setCell(*snapshotChangesTable_, static_cast<int>(index), 3,
                    change.has_after ? bytesText(change.after_metric) + " · "
                                           + afterPath
                                     : tr("—"));
            setCell(*snapshotChangesTable_, static_cast<int>(index), 4,
                    change.reason.empty() ? tr("Evidence supports this change")
                                          : QString::fromStdString(change.reason));
        }
        snapshotSummary_->setText(
            tr("Compared with %1: %2 change(s) · %3 evidence")
                .arg(path)
                .arg(diff.changes.size())
                .arg(diff.complete ? tr("exact") : tr("conservative")));
        status_->setText(snapshotSummary_->text());
    } catch (const std::exception& error) {
        status_->setText(tr("Snapshot comparison failed: %1")
                             .arg(QString::fromStdString(error.what())));
    } catch (...) {
        status_->setText(tr("Snapshot comparison failed: unknown error"));
    }
    updateControlState();
}

void MainWindow::analyzeDuplicatesNow() { analyzeDuplicates(); }

void MainWindow::analyzeDuplicates() {
    if (!document_ || activeCancellation_ || activeDuplicateCancellation_) {
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
    std::size_t staged = 0;
    for (const diskmap::DuplicateGroup& group : duplicateAnalysis_.groups) {
        if (!group.reclaimable || !group.certain || group.entries.size() < 2) {
            continue;
        }
        // Entries are sorted by normalized path by the core. Keep the first
        // path as the deterministic representative and stage only the other
        // copies. Cleanup planning still performs its own safety checks.
        for (std::size_t index = 1; index < group.entries.size(); ++index) {
            const diskmap::NodeKey& key = group.entries[index].key;
            if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
                keys.push_back(key);
                ++staged;
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
    if (documentIsSnapshot_ && hasLoadedSnapshot_) {
        return loadedSnapshot_;
    }
    if (!document_) {
        throw diskmap::SnapshotError("no scan document is loaded");
    }
    return diskmap::snapshotFromNode(document_->root);
}

void MainWindow::installLoadedSnapshot(diskmap::Snapshot snapshot,
                                       const QString& path) {
    ++activeGeneration_;
    const std::uint64_t generation = activeGeneration_;
    diskmap::ScanResult result =
        diskmap::scanEvidenceFromSnapshot(snapshot, generation);
    document_ = std::make_shared<diskmap::ScanResult>(std::move(result));
    loadedSnapshot_ = std::move(snapshot);
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
                    duplicateReclamation(group, member));
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
    duplicateRows_.clear();
    if (duplicateEvidenceTable_ != nullptr) {
        duplicateEvidenceTable_->setRowCount(0);
    }
    if (duplicateSummary_ != nullptr) {
        duplicateSummary_->setText(tr("No duplicate analysis"));
    }
}

void MainWindow::clearSnapshotChanges() {
    if (snapshotChangesTable_ != nullptr) {
        snapshotChangesTable_->setRowCount(0);
    }
    if (snapshotSummary_ != nullptr) {
        snapshotSummary_->setText(tr("No snapshot comparison"));
    }
}
