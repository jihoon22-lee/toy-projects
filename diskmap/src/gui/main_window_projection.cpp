#include "diskmap/gui/main_window.hpp"

#include <QByteArray>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QTableView>
#include <QToolButton>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "diskmap/fs_node.hpp"
#include "explorer_text.hpp"
#include "main_window_filter_data.hpp"
#include "diskmap/gui/treemap_widget.hpp"
#include "diskmap/view.hpp"

namespace {

constexpr std::int64_t kDayNanoseconds = 86'400'000'000'000LL;

std::string utf8Text(const QString& text) {
    const QByteArray encoded = text.toUtf8();
    return std::string(encoded.constData(), static_cast<std::size_t>(encoded.size()));
}

std::optional<std::uint64_t> unsignedValue(const QLineEdit& edit, bool& valid) {
    const QString text = edit.text().trimmed();
    if (text.isEmpty()) {
        valid = true;
        return std::nullopt;
    }
    bool converted = false;
    const qulonglong value = text.toULongLong(&converted);
    valid = converted;
    return converted ? std::optional<std::uint64_t>(value) : std::nullopt;
}

diskmap::SizeMetric metricForColumn(int column,
                                    diskmap::SizeMetric fallback) {
    if (column == NodeTableModel::LogicalColumn) {
        return diskmap::SizeMetric::Logical;
    }
    if (column == NodeTableModel::AllocatedColumn) {
        return diskmap::SizeMetric::Allocated;
    }
    if (column == NodeTableModel::ReclaimableColumn) {
        return diskmap::SizeMetric::Reclaimable;
    }
    return fallback;
}

int columnForMetric(diskmap::SizeMetric metric) {
    switch (metric) {
    case diskmap::SizeMetric::Logical:
        return NodeTableModel::LogicalColumn;
    case diskmap::SizeMetric::Allocated:
        return NodeTableModel::AllocatedColumn;
    case diskmap::SizeMetric::Reclaimable:
        return NodeTableModel::ReclaimableColumn;
    }
    return NodeTableModel::LogicalColumn;
}

void applyAgeBounds(int age, std::int64_t now, diskmap::ViewFilter& filter) {
    if (age == main_window_filter_data::LastDay) {
        filter.modified_after_ns = now - kDayNanoseconds;
    } else if (age == main_window_filter_data::LastWeek) {
        filter.modified_after_ns = now - 7 * kDayNanoseconds;
    } else if (age == main_window_filter_data::LastMonth) {
        filter.modified_after_ns = now - 30 * kDayNanoseconds;
    } else if (age == main_window_filter_data::OlderThanMonth) {
        filter.modified_before_ns = now - 30 * kDayNanoseconds;
    }
}

} // namespace

void MainWindow::restoreNavigation(
    const std::vector<diskmap::NodeKey>& previousTrail) {
    trail_.clear();
    trail_.push_back(diskmap::nodeKey(document_->root));
    const diskmap::FsNode* current = &document_->root;
    for (std::size_t index = 1; index < previousTrail.size(); ++index) {
        const auto child = std::find_if(
            current->children.begin(), current->children.end(),
            [&previousTrail, index](const diskmap::FsNode& candidate) {
                return diskmap::nodeKey(candidate) == previousTrail[index];
            });
        if (child == current->children.end() || !child->is_dir) {
            break;
        }
        current = &(*child);
        trail_.push_back(diskmap::nodeKey(*current));
    }
}

void MainWindow::navigateToKey(const diskmap::NodeKey& key) {
    if (!document_ || activeCancellation_) {
        return;
    }
    const std::vector<const diskmap::FsNode*> path =
        diskmap::nodePathByKey(document_->root, key);
    if (path.empty()) {
        return;
    }
    const diskmap::FsNode* target = path.back();
    const std::size_t trailLength =
        target->is_dir ? path.size() : std::max<std::size_t>(1, path.size() - 1);
    trail_.clear();
    trail_.reserve(trailLength);
    for (std::size_t index = 0; index < trailLength; ++index) {
        trail_.push_back(diskmap::nodeKey(*path[index]));
    }
    selectedKey_ = target->is_dir ? std::nullopt
                                  : std::optional<diskmap::NodeKey>(key);
    refreshProjection();
}

void MainWindow::navigateToBreadcrumb(std::size_t index) {
    if (activeCancellation_ || index >= trail_.size()) {
        return;
    }
    trail_.resize(index + 1);
    selectedKey_.reset();
    refreshProjection();
}

void MainWindow::goUp() {
    if (activeCancellation_ || trail_.size() <= 1) {
        return;
    }
    trail_.pop_back();
    selectedKey_.reset();
    refreshProjection();
}

void MainWindow::applyFilters() {
    updateMetricExplanation();
    if (document_ && !activeCancellation_) {
        refreshProjection();
    }
}

void MainWindow::onTableActivated(const QModelIndex& index) {
    const std::optional<diskmap::NodeKey> key = tableModel_->keyAt(index.row());
    if (key.has_value()) {
        navigateToKey(*key);
    }
}

void MainWindow::onTableCurrentChanged(const QModelIndex& current,
                                       const QModelIndex&) {
    if (!refreshingProjection_ && !modelResetInProgress_ && !activeCancellation_) {
        selectedKey_ = tableModel_->keyAt(current.row());
    }
}

void MainWindow::onTableSortChanged(int column, Qt::SortOrder) {
    if (activeCancellation_) {
        return;
    }
    const diskmap::SizeMetric selected = static_cast<diskmap::SizeMetric>(
        metricCombo_->currentData().toInt());
    const diskmap::SizeMetric metric = metricForColumn(column, selected);
    if (columnForMetric(metric) != column) {
        return;
    }
    if (metric != selected) {
        const QSignalBlocker blocker(metricCombo_);
        metricCombo_->setCurrentIndex(
            metricCombo_->findData(static_cast<int>(metric)));
    }
    applyFilters();
}

void MainWindow::onNodeActivated(diskmap::NodeKey key) { navigateToKey(key); }

void MainWindow::onNodeHovered(diskmap::NodeKey key) {
    if (!document_ || activeCancellation_) {
        return;
    }
    const diskmap::FsNode* node = diskmap::findNodeByKey(document_->root, key);
    if (node != nullptr) {
        treemap_->setToolTip(
            diskmap_gui_text::nodeDescription(*node, document_->totals_filtered));
    }
}

void MainWindow::clearHover() { treemap_->setToolTip(QString()); }

diskmap::ViewFilter MainWindow::viewFilterFromControls() {
    diskmap::ViewFilter filter;
    filter.search = utf8Text(searchEdit_->text().trimmed());
    filter.metric = static_cast<diskmap::SizeMetric>(
        metricCombo_->currentData().toInt());
    filter.scanner_totals_filtered = document_ && document_->totals_filtered;

    const int type = typeCombo_->currentData().toInt();
    if (type != main_window_filter_data::kAnyValue) {
        filter.kind = static_cast<diskmap::FsKind>(type);
    }
    const int issue = issueCombo_->currentData().toInt();
    if (issue == main_window_filter_data::kProblemIssues) {
        filter.issues = {
            diskmap::NodeIssue::Incomplete,
            diskmap::NodeIssue::CycleSkipped,
            diskmap::NodeIssue::MountBoundarySkipped,
            diskmap::NodeIssue::DepthLimitReached,
            diskmap::NodeIssue::MetadataUnknown,
            diskmap::NodeIssue::Error,
            diskmap::NodeIssue::ScannerFiltered,
        };
    } else if (issue != main_window_filter_data::kAnyIssue) {
        filter.issue = static_cast<diskmap::NodeIssue>(issue);
    }

    filterError_.clear();
    bool minimumValid = true;
    bool maximumValid = true;
    filter.min_size = unsignedValue(*minimumSizeEdit_, minimumValid);
    filter.max_size = unsignedValue(*maximumSizeEdit_, maximumValid);
    minimumSizeEdit_->setStyleSheet(minimumValid ? QString()
                                                  : QStringLiteral("border: 1px solid #b00020"));
    maximumSizeEdit_->setStyleSheet(maximumValid ? QString()
                                                  : QStringLiteral("border: 1px solid #b00020"));
    if (!minimumValid || !maximumValid) {
        filterError_ = tr("A size bound is outside the unsigned 64-bit range.");
    } else if (filter.min_size.has_value() && filter.max_size.has_value()
               && *filter.min_size > *filter.max_size) {
        filterError_ = tr("Minimum size exceeds maximum size; no item can match.");
    }

    const int age = ageCombo_->currentData().toInt();
    const std::int64_t now = QDateTime::currentMSecsSinceEpoch() * 1'000'000LL;
    applyAgeBounds(age, now, filter);
    return filter;
}

diskmap::SortSpec MainWindow::sortSpecFromControls() const {
    diskmap::SortSpec sort;
    sort.metric = static_cast<diskmap::SizeMetric>(metricCombo_->currentData().toInt());
    const int selectedColumn = table_->horizontalHeader()->sortIndicatorSection();
    sort.descending = selectedColumn != columnForMetric(sort.metric)
                      || table_->horizontalHeader()->sortIndicatorOrder()
                             == Qt::DescendingOrder;
    return sort;
}

const diskmap::FsNode* MainWindow::currentNode() const {
    if (!document_ || trail_.empty()) {
        return nullptr;
    }
    const diskmap::FsNode* current = &document_->root;
    if (diskmap::nodeKey(*current) != trail_.front()) {
        return nullptr;
    }
    for (std::size_t index = 1; index < trail_.size(); ++index) {
        const auto child = std::find_if(
            current->children.begin(), current->children.end(),
            [this, index](const diskmap::FsNode& candidate) {
                return diskmap::nodeKey(candidate) == trail_[index];
            });
        if (child == current->children.end()) {
            return nullptr;
        }
        current = &(*child);
    }
    return current;
}

void MainWindow::refreshProjection() {
    const diskmap::FsNode* root = currentNode();
    if (root == nullptr) {
        tableModel_->clear();
        treemap_->clear();
        updateBreadcrumb();
        updatePartialBanner();
        updateControlState();
        return;
    }

    const diskmap::NodeKey rootKey = diskmap::nodeKey(*root);
    const diskmap::ViewFilter filter = viewFilterFromControls();
    const int headerColumn = table_->horizontalHeader()->sortIndicatorSection();
    const int metricColumn = columnForMetric(*filter.metric);
    if (columnForMetric(metricForColumn(headerColumn, *filter.metric)) == headerColumn
        && headerColumn != metricColumn) {
        const QSignalBlocker blocker(table_->horizontalHeader());
        table_->horizontalHeader()->setSortIndicator(
            metricColumn, table_->horizontalHeader()->sortIndicatorOrder());
    }
    const diskmap::SortSpec sort = sortSpecFromControls();
    const NodeTableModel::Mode mode = static_cast<NodeTableModel::Mode>(
        modeCombo_->currentData().toInt());
    refreshingProjection_ = true;
    tableModel_->setProjection(document_, rootKey, filter, sort, mode);
    treemap_->setProjection(document_, rootKey, filter, sort);
    restoreSelection();
    refreshingProjection_ = false;
    table_->setAccessibleName(mode == NodeTableModel::ChildrenMode
                                  ? tr("Entries in current folder")
                                  : tr("Largest files in current subtree"));
    const bool projectionComplete = tableModel_->projectionComplete()
                                    && treemap_->projectionComplete();
    const diskmap::MetricValue rootMetric =
        diskmap::metricValue(*root, sort.metric, document_->totals_filtered);
    projectionSummary_->setText(
        tr("%1 item(s) · %2 · %3 subtree: %4")
            .arg(tableModel_->rowCount())
            .arg(projectionComplete ? tr("exact evidence")
                                    : tr("conservative evidence"))
            .arg(diskmap_gui_text::metricName(sort.metric),
                 diskmap_gui_text::metricValueText(rootMetric)));
    updateDocumentStatus();
    updateBreadcrumb();
    updatePartialBanner();
    updateControlState();
}

void MainWindow::restoreSelection() {
    if (!selectedKey_.has_value()) {
        table_->clearSelection();
        return;
    }
    const QModelIndex index = tableModel_->indexForKey(*selectedKey_);
    if (!index.isValid()) {
        table_->clearSelection();
        selectedKey_.reset();
        return;
    }
    table_->selectionModel()->setCurrentIndex(
        index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void MainWindow::updateBreadcrumb() {
    while (QLayoutItem* item = breadcrumbLayout_->takeAt(0)) {
        if (item->widget() != nullptr) {
            item->widget()->setObjectName(QString());
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        delete item;
    }
    if (!document_ || trail_.empty()) {
        auto* empty = new QLabel(tr("(no scan yet)"), breadcrumbBar_);
        empty->setAccessibleName(tr("No scanned path"));
        breadcrumbLayout_->addWidget(empty);
        breadcrumbLayout_->addStretch(1);
        breadcrumbBar_->setProperty("currentPath", QString());
        return;
    }

    QStringList segments;
    const diskmap::FsNode* node = &document_->root;
    for (std::size_t index = 0; index < trail_.size(); ++index) {
        if (index > 0) {
            const auto child = std::find_if(
                node->children.begin(), node->children.end(),
                [this, index](const diskmap::FsNode& candidate) {
                    return diskmap::nodeKey(candidate) == trail_[index];
                });
            if (child == node->children.end()) {
                break;
            }
            node = &(*child);
        }
        QString label = diskmap_gui_text::utf8(node->name);
        if (label.isEmpty()) {
            label = diskmap_gui_text::utf8(diskmap::normalizedPath(*node));
        }
        if (index > 0) {
            auto* separator = new QLabel(QStringLiteral("›"), breadcrumbBar_);
            separator->setAccessibleName(tr("Path separator"));
            breadcrumbLayout_->addWidget(separator);
        }
        auto* button = new QToolButton(breadcrumbBar_);
        button->setObjectName(QStringLiteral("breadcrumbSegment%1").arg(index));
        button->setText(label);
        button->setToolTip(diskmap_gui_text::utf8(diskmap::normalizedPath(*node)));
        button->setAccessibleName(tr("Open %1").arg(button->toolTip()));
        button->setAutoRaise(true);
        connect(button, &QToolButton::clicked, this,
                [this, index]() { navigateToBreadcrumb(index); });
        breadcrumbLayout_->addWidget(button);
        segments << label;
    }
    breadcrumbLayout_->addStretch(1);
    const QString path = segments.join(QStringLiteral(" / "));
    breadcrumbBar_->setProperty("currentPath", path);
    breadcrumbBar_->setAccessibleDescription(path);
}

void MainWindow::updatePartialBanner() {
    if (!document_) {
        partialBanner_->hide();
        return;
    }
    if (documentIsSnapshot_) {
        partialBanner_->setText(
            tr("This is a read-only snapshot. Rescan the live folder before staging "
               "or moving anything to recoverable Trash."));
        partialBanner_->show();
        return;
    }
    if (!filterError_.isEmpty()) {
        partialBanner_->setText(filterError_);
        partialBanner_->show();
        return;
    }
    const bool complete = tableModel_->projectionComplete()
                          && treemap_->projectionComplete();
    if (complete) {
        partialBanner_->hide();
        return;
    }
    diskmap::NodeIssue issue = tableModel_->projectionIssue();
    const diskmap::FsNode* root = currentNode();
    if (issue == diskmap::NodeIssue::None && root != nullptr) {
        issue = diskmap::classifyNodeIssue(*root, document_->totals_filtered);
    }
    const QString reason =
        issue == diskmap::NodeIssue::None
            ? tr("selected metric or filtered evidence is not known exactly")
            : diskmap_gui_text::issueDescription(issue);
    partialBanner_->setText(
        tr("This view contains conservative evidence (%1). Hatched tiles and "
           "‘At least’ values are not exact totals or exhaustive rankings.")
            .arg(reason));
    partialBanner_->show();
}
