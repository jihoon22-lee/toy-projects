#include "diskmap/gui/main_window.hpp"

#include <QAbstractItemView>
#include <QByteArray>
#include <QComboBox>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUndoCommand>
#include <QUndoStack>

#include <algorithm>
#include <exception>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "diskmap/format.hpp"

namespace {

QString pathText(const std::filesystem::path& path) {
    const std::string value = path.generic_string();
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

QString bytesText(std::uint64_t value) {
    return QString::fromStdString(diskmap::humanBytes(value));
}

QString targetEvidence(const diskmap::CleanupTarget& target) {
    QStringList facts;
    facts << QObject::tr("%1 bytes logical").arg(target.logical_size);
    facts << (target.allocated_size_known
                  ? QObject::tr("%1 allocated").arg(bytesText(target.allocated_size))
                  : QObject::tr("allocated size unknown"));
    facts << (target.hard_link_count_known
                  ? QObject::tr("%1 hard link(s)").arg(target.hard_link_count)
                  : QObject::tr("hard-link count unknown"));
    return facts.join(QStringLiteral(" · "));
}

void setReviewRow(QTableWidget& table,
                  int row,
                  const QString& decision,
                  const QString& path,
                  const QString& evidence) {
    table.setItem(row, 0, new QTableWidgetItem(decision));
    table.setItem(row, 1, new QTableWidgetItem(path));
    table.setItem(row, 2, new QTableWidgetItem(evidence));
}

class StagingCommand final : public QUndoCommand {
public:
    using Setter = std::function<void(std::vector<diskmap::NodeKey>)>;

    StagingCommand(Setter setter,
                   std::vector<diskmap::NodeKey> before,
                   std::vector<diskmap::NodeKey> after,
                   QString text)
        : setter_(std::move(setter)), before_(std::move(before)),
          after_(std::move(after)) {
        setText(text);
    }

    void undo() override { setter_(before_); }
    void redo() override { setter_(after_); }

private:
    Setter setter_;
    std::vector<diskmap::NodeKey> before_;
    std::vector<diskmap::NodeKey> after_;
};

} // namespace

void MainWindow::stageSelectedRows() {
    if (!document_ || activeCancellation_ || table_->selectionModel() == nullptr) {
        return;
    }
    std::vector<diskmap::NodeKey> after = stagedCleanupKeys_;
    const QModelIndexList rows = table_->selectionModel()->selectedRows();
    for (const QModelIndex& row : rows) {
        const std::optional<diskmap::NodeKey> key = tableModel_->keyAt(row.row());
        if (key.has_value()
            && std::find(after.begin(), after.end(), *key) == after.end()) {
            after.push_back(*key);
        }
    }
    if (after == stagedCleanupKeys_) {
        return;
    }
    cleanupUndo_->push(new StagingCommand(
        [this](std::vector<diskmap::NodeKey> keys) {
            setStagedCleanupKeys(std::move(keys));
        },
        stagedCleanupKeys_, std::move(after), tr("Stage cleanup selection")));
}

void MainWindow::clearCleanupStaging() {
    if (stagedCleanupKeys_.empty() || activeCancellation_) {
        return;
    }
    cleanupUndo_->push(new StagingCommand(
        [this](std::vector<diskmap::NodeKey> keys) {
            setStagedCleanupKeys(std::move(keys));
        },
        stagedCleanupKeys_, {}, tr("Clear cleanup staging")));
}

void MainWindow::setStagedCleanupKeys(std::vector<diskmap::NodeKey> keys) {
    stagedCleanupKeys_ = std::move(keys);
    refreshCleanupReview();
    updateControlState();
}

void MainWindow::refreshCleanupReview() {
    cleanupReviewTable_->setRowCount(0);
    if (!document_) {
        cleanupPlan_ = {};
        cleanupSummary_->setText(tr("No items staged"));
        return;
    }
    cleanupPlan_ = diskmap::planCleanup(*document_, stagedCleanupKeys_);
    const std::size_t rows = cleanupPlan_.targets.size()
                             + cleanupPlan_.rejected.size();
    cleanupReviewTable_->setRowCount(static_cast<int>(rows));
    int row = 0;
    for (const diskmap::CleanupTarget& target : cleanupPlan_.targets) {
        setReviewRow(*cleanupReviewTable_, row++, tr("Ready"),
                     pathText(target.path), targetEvidence(target));
    }
    for (const diskmap::CleanupRejectedTarget& rejected : cleanupPlan_.rejected) {
        setReviewRow(
            *cleanupReviewTable_, row++,
            tr("Rejected: %1")
                .arg(QString::fromLatin1(
                    diskmap::cleanupSkipReasonName(rejected.reason))),
            pathText(rejected.path), QString::fromStdString(rejected.message));
    }
    const QString reclaimable =
        cleanupPlan_.reclaimable_bytes_known
            ? bytesText(cleanupPlan_.reclaimable_bytes)
            : tr("at least %1 (incomplete hard-link evidence)")
                  .arg(bytesText(cleanupPlan_.reclaimable_bytes));
    cleanupSummary_->setText(
        tr("Dry run: %1 ready, %2 rejected · reclaimable %3 · nothing has moved")
            .arg(cleanupPlan_.targets.size())
            .arg(cleanupPlan_.rejected.size())
            .arg(reclaimable));
}

void MainWindow::appendCleanupAudit(const diskmap::TrashReceipt& receipt) {
    const int row = cleanupAuditTable_->rowCount();
    cleanupAuditTable_->insertRow(row);
    cleanupAuditTable_->setItem(
        row, 0,
        new QTableWidgetItem(
            QString::fromLatin1(diskmap::trashStatusName(receipt.status))));
    cleanupAuditTable_->setItem(
        row, 1, new QTableWidgetItem(pathText(receipt.original_path)));
    QString detail = QString::fromStdString(receipt.message);
    if (!receipt.trashed_path.empty()) {
        const QString location = pathText(receipt.trashed_path);
        detail = detail.isEmpty() ? location
                                  : tr("%1 · %2").arg(detail, location);
    }
    cleanupAuditTable_->setItem(row, 2, new QTableWidgetItem(detail));
}

void MainWindow::executeCleanup() {
    refreshCleanupReview();
    if (!document_ || activeCancellation_ || cleanupPlan_.targets.empty()
        || !cleanupServices_.confirm(cleanupPlan_)) {
        updateControlState();
        return;
    }

    std::vector<diskmap::TrashReceipt> receipts;
    try {
        receipts = cleanupServices_.move(cleanupPlan_);
    } catch (const std::exception& error) {
        diskmap::TrashReceipt receipt;
        receipt.status = diskmap::TrashStatus::IoError;
        receipt.message = std::string("trash backend failed: ") + error.what();
        receipts.push_back(std::move(receipt));
    } catch (...) {
        diskmap::TrashReceipt receipt;
        receipt.status = diskmap::TrashStatus::IoError;
        receipt.message = "trash backend failed with an unknown exception";
        receipts.push_back(std::move(receipt));
    }
    if (receipts.empty()) {
        diskmap::TrashReceipt receipt;
        receipt.status = diskmap::TrashStatus::IoError;
        receipt.message = "trash backend returned no target audit records";
        receipts.push_back(std::move(receipt));
    }

    std::size_t moved = 0;
    for (const diskmap::TrashReceipt& receipt : receipts) {
        appendCleanupAudit(receipt);
        if (receipt.status == diskmap::TrashStatus::Moved) {
            ++moved;
            if (!receipt.restore_token.empty()) {
                restoreTokenCombo_->addItem(pathText(receipt.original_path),
                                            QString::fromStdString(
                                                receipt.restore_token));
            }
        }
    }
    cleanupUndo_->clear();
    setStagedCleanupKeys({});
    status_->setText(tr("Trash audit: %1 of %2 target(s) moved")
                         .arg(moved)
                         .arg(receipts.size()));
    if (!currentScanPath_.isEmpty()) {
        startScan(currentScanPath_, true);
    }
}

void MainWindow::restoreSelectedTrashItem() {
    const int index = restoreTokenCombo_->currentIndex();
    if (index < 0 || activeCancellation_) {
        return;
    }
    const std::string token =
        restoreTokenCombo_->itemData(index).toString().toStdString();
    diskmap::TrashReceipt receipt;
    try {
        receipt = cleanupServices_.restore(token);
    } catch (const std::exception& error) {
        receipt.status = diskmap::TrashStatus::IoError;
        receipt.message = std::string("restore backend failed: ") + error.what();
    } catch (...) {
        receipt.status = diskmap::TrashStatus::IoError;
        receipt.message = "restore backend failed with an unknown exception";
    }
    appendCleanupAudit(receipt);
    if (receipt.status == diskmap::TrashStatus::Restored) {
        restoreTokenCombo_->removeItem(index);
        if (!currentScanPath_.isEmpty()) {
            startScan(currentScanPath_, true);
        }
    }
    updateControlState();
}
