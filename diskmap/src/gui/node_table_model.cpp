#include "diskmap/gui/node_table_model.hpp"

#include <QBrush>
#include <QColor>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

const diskmap::FsMetadata* displayMetadata(const diskmap::FsNode& node) {
    if (node.followed) {
        if (!node.has_target_metadata || !node.target_metadata.complete) {
            return nullptr;
        }
        return &node.target_metadata;
    }
    return node.metadata.complete ? &node.metadata : nullptr;
}

std::string combinedError(const diskmap::FsNode& node) {
    std::string result = node.error;
    const auto append = [&result](const std::string& message) {
        if (message.empty() || result.find(message) != std::string::npos) {
            return;
        }
        if (!result.empty()) {
            result += "; ";
        }
        result += message;
    };
    append(node.metadata.error);
    if (node.has_target_metadata) {
        append(node.target_metadata.error);
    }
    return result;
}

diskmap::SizeMetric filterMetric(const diskmap::ViewFilter& filter) {
    if (filter.metric.has_value()) {
        return *filter.metric;
    }
    return filter.size_metric.value_or(diskmap::SizeMetric::Logical);
}

bool hasSizeBound(const diskmap::ViewFilter& filter) {
    return filter.min_size.has_value() || filter.max_size.has_value()
           || filter.minimum_size.has_value() || filter.maximum_size.has_value()
           || filter.min_bytes.has_value() || filter.max_bytes.has_value();
}

bool hasAgeBound(const diskmap::ViewFilter& filter) {
    return filter.modified_after_ns.has_value() || filter.modified_before_ns.has_value()
           || filter.min_modified_ns.has_value() || filter.max_modified_ns.has_value()
           || filter.newer_than_ns.has_value() || filter.older_than_ns.has_value();
}

diskmap::NodeIssue firstSubtreeIssue(const diskmap::FsNode& root,
                                    bool scannerTotalsFiltered) {
    std::vector<const diskmap::FsNode*> pending{&root};
    while (!pending.empty()) {
        const diskmap::FsNode* current = pending.back();
        pending.pop_back();
        const diskmap::NodeIssue issue =
            diskmap::classifyNodeIssue(*current, scannerTotalsFiltered);
        if (issue != diskmap::NodeIssue::None) {
            return issue;
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(&(*it));
        }
    }
    return diskmap::NodeIssue::None;
}

int metricColumn(diskmap::SizeMetric metric) {
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

std::optional<diskmap::SizeMetric> columnMetric(int column) {
    switch (column) {
    case NodeTableModel::LogicalColumn:
        return diskmap::SizeMetric::Logical;
    case NodeTableModel::AllocatedColumn:
        return diskmap::SizeMetric::Allocated;
    case NodeTableModel::ReclaimableColumn:
        return diskmap::SizeMetric::Reclaimable;
    default:
        return std::nullopt;
    }
}

} // namespace

NodeTableModel::NodeTableModel(QObject* parent) : QAbstractTableModel(parent) {
    qRegisterMetaType<diskmap::NodeKey>("diskmap::NodeKey");
}

int NodeTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int NodeTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant NodeTableModel::data(const QModelIndex& index, int role) const {
    const Row* row = rowForIndex(index);
    if (row == nullptr) {
        return QVariant();
    }
    if (role == Qt::DisplayRole) {
        return displayData(*row, index.column());
    }
    if (role == Qt::ToolTipRole || role == Qt::AccessibleTextRole) {
        return descriptionData(*row);
    }
    if (role == Qt::TextAlignmentRole
        && index.column() >= LogicalColumn && index.column() <= ReclaimableColumn) {
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }
    if (role == Qt::ForegroundRole && row->issue != diskmap::NodeIssue::None) {
        return QBrush(QColor(QStringLiteral("#9a5a00")));
    }
    if (role >= NodeKeyRole && role <= IdentityRole) {
        return semanticRoleData(*row, role);
    }
    return metricRoleData(*row, role);
}

const NodeTableModel::Row* NodeTableModel::rowForIndex(
    const QModelIndex& index) const {
    if (!index.isValid() || index.column() < 0 || index.column() >= ColumnCount
        || index.row() < 0 || index.row() >= static_cast<int>(rows_.size())) {
        return nullptr;
    }
    return &rows_[static_cast<std::size_t>(index.row())];
}

void NodeTableModel::setProjection(std::shared_ptr<const diskmap::ScanResult> document,
                                   const diskmap::NodeKey& root,
                                   const diskmap::ViewFilter& filter,
                                   const diskmap::SortSpec& sort,
                                   Mode mode) {
    const bool reuseRoot = document_ == document && root_ != nullptr
                           && diskmap::nodeKey(*root_) == root;
    beginResetModel();
    document_ = std::move(document);
    if (!reuseRoot) {
        root_ = nullptr;
    }
    rootKey_ = root;
    filter_ = filter;
    sort_ = sort;
    mode_ = mode;
    if (columnMetric(sortColumn_).has_value()) {
        sortColumn_ = metricColumn(sort_.metric);
        sortOrder_ = sort_.descending ? Qt::DescendingOrder : Qt::AscendingOrder;
    }
    rebuildRows();
    applyDisplaySort();
    endResetModel();
    emit projectionStatusChanged(projectionComplete_, static_cast<int>(projectionIssue_));
}

void NodeTableModel::clear() {
    beginResetModel();
    document_.reset();
    root_ = nullptr;
    rootKey_ = diskmap::NodeKey{};
    rebuildRows();
    endResetModel();
    emit projectionStatusChanged(projectionComplete_, static_cast<int>(projectionIssue_));
}

void NodeTableModel::setLargestLimit(std::size_t limit) {
    const std::size_t maximum = static_cast<std::size_t>(std::numeric_limits<int>::max());
    const std::size_t bounded = std::min(limit, maximum);
    if (largestLimit_ == bounded) {
        return;
    }
    largestLimit_ = bounded;
    if (mode_ == LargestFilesMode) {
        rebuild();
    }
}

void NodeTableModel::sort(int column, Qt::SortOrder order) {
    if (column < 0 || column >= ColumnCount) {
        return;
    }
    sortColumn_ = column;
    sortOrder_ = order;
    const std::optional<diskmap::SizeMetric> metric = columnMetric(column);
    if (metric.has_value()) {
        sort_.metric = *metric;
        sort_.descending = order == Qt::DescendingOrder;
        rebuild();
        return;
    }
    beginResetModel();
    applyDisplaySort();
    endResetModel();
}

const diskmap::FsNode* NodeTableModel::nodeAt(int row) const {
    if (row < 0 || row >= static_cast<int>(rows_.size())) {
        return nullptr;
    }
    return rows_[static_cast<std::size_t>(row)].node;
}

std::optional<diskmap::NodeKey> NodeTableModel::keyAt(int row) const {
    if (row < 0 || row >= static_cast<int>(rows_.size())) {
        return std::nullopt;
    }
    return rows_[static_cast<std::size_t>(row)].key;
}

QModelIndex NodeTableModel::indexForKey(const diskmap::NodeKey& key) const {
    for (std::size_t row = 0; row < rows_.size(); ++row) {
        if (rows_[row].key == key) {
            return index(static_cast<int>(row), 0);
        }
    }
    return QModelIndex();
}

const diskmap::FsNode* NodeTableModel::rootNode() const { return root_; }

bool NodeTableModel::projectionComplete() const { return projectionComplete_; }

diskmap::NodeIssue NodeTableModel::projectionIssue() const { return projectionIssue_; }

std::size_t NodeTableModel::largestLimit() const { return largestLimit_; }

NodeTableModel::Mode NodeTableModel::mode() const { return mode_; }

void NodeTableModel::rebuild() {
    beginResetModel();
    rebuildRows();
    applyDisplaySort();
    endResetModel();
    emit projectionStatusChanged(projectionComplete_, static_cast<int>(projectionIssue_));
}

void NodeTableModel::rebuildRows() {
    rows_.clear();
    projectionIssue_ = diskmap::NodeIssue::None;
    if (!document_) {
        root_ = nullptr;
        projectionComplete_ = true;
        return;
    }
    if (root_ == nullptr || diskmap::nodeKey(*root_) != rootKey_) {
        root_ = diskmap::findNodeByKey(document_->root, rootKey_);
    }
    if (root_ == nullptr) {
        projectionComplete_ = false;
        projectionIssue_ = diskmap::NodeIssue::Error;
        return;
    }

    bool sourceComplete = true;
    std::vector<const diskmap::FsNode*> projected = projectedNodes(sourceComplete);
    const std::size_t maximum = static_cast<std::size_t>(std::numeric_limits<int>::max());
    if (projected.size() > maximum) {
        projected.resize(maximum);
        sourceComplete = false;
        projectionIssue_ = diskmap::NodeIssue::Error;
    }
    rows_.reserve(projected.size());
    for (const diskmap::FsNode* node : projected) {
        rows_.push_back(makeRow(*node));
    }
    updateProjectionStatus(sourceComplete);
}

std::vector<const diskmap::FsNode*> NodeTableModel::projectedNodes(
    bool& complete) const {
    complete = true;
    if (mode_ != LargestFilesMode) {
        return diskmap::visibleChildren(*root_, filter_, sort_);
    }
    const diskmap::LargestFilesResult result =
        diskmap::largestFiles(*root_, largestLimit_, filter_, sort_);
    complete = result.complete;
    return result.files;
}

NodeTableModel::Row NodeTableModel::makeRow(const diskmap::FsNode& node) const {
    Row row;
    row.node = &node;
    row.key = diskmap::nodeKey(node);
    row.logical = diskmap::metricValue(node, diskmap::SizeMetric::Logical,
                                      filter_.scanner_totals_filtered);
    row.allocated = diskmap::metricValue(node, diskmap::SizeMetric::Allocated,
                                        filter_.scanner_totals_filtered);
    row.reclaimable = diskmap::metricValue(node, diskmap::SizeMetric::Reclaimable,
                                          filter_.scanner_totals_filtered);
    row.issue = diskmap::classifyNodeIssue(node, filter_.scanner_totals_filtered);
    const diskmap::FsMetadata* metadata = displayMetadata(node);
    if (metadata != nullptr && metadata->modified_time_known) {
        row.modifiedNs = metadata->modified_ns;
        row.modifiedKnown = true;
    }
    row.error = combinedError(node);
    return row;
}

bool NodeTableModel::boundedFilterEvidenceComplete() const {
    const bool checkSize = hasSizeBound(filter_);
    const bool checkAge = hasAgeBound(filter_);
    if (mode_ != ChildrenMode || (!checkSize && !checkAge)) {
        return true;
    }
    for (const diskmap::FsNode& child : root_->children) {
        if (checkSize
            && !diskmap::metricValue(child, filterMetric(filter_),
                                     filter_.scanner_totals_filtered)
                    .known) {
            return false;
        }
        const diskmap::FsMetadata* metadata = displayMetadata(child);
        if (checkAge && (metadata == nullptr || !metadata->modified_time_known)) {
            return false;
        }
    }
    return true;
}

void NodeTableModel::updateProjectionStatus(bool sourceComplete) {
    const diskmap::NodeIssue rootIssue =
        diskmap::classifyNodeIssue(*root_, filter_.scanner_totals_filtered);
    const bool selectedMetricKnown =
        diskmap::metricValue(*root_, sort_.metric, filter_.scanner_totals_filtered).known;
    projectionComplete_ = sourceComplete && rootIssue == diskmap::NodeIssue::None
                          && selectedMetricKnown && boundedFilterEvidenceComplete();

    if (!projectionComplete_ && projectionIssue_ == diskmap::NodeIssue::None) {
        projectionIssue_ = rootIssue;
        if (projectionIssue_ == diskmap::NodeIssue::None) {
            projectionIssue_ =
                firstSubtreeIssue(*root_, filter_.scanner_totals_filtered);
        }
    }
}

void NodeTableModel::applyDisplaySort() {
    std::stable_sort(rows_.begin(), rows_.end(), [this](const Row& left, const Row& right) {
        return rowBefore(left, right);
    });
}

bool NodeTableModel::rowBefore(const Row& left, const Row& right) const {
    if (sortColumn_ <= TypeColumn) {
        return textColumnBefore(left, right);
    }
    if (sortColumn_ <= ReclaimableColumn) {
        return metricColumnBefore(left, right);
    }
    if (sortColumn_ == ModifiedColumn) {
        return modifiedColumnBefore(left, right);
    }
    if (sortColumn_ == StateColumn) {
        return stateColumnBefore(left, right);
    }
    return tieBreakBefore(left, right);
}

bool NodeTableModel::textColumnBefore(const Row& left, const Row& right) const {
    if (sortColumn_ == NameColumn && left.node->name != right.node->name) {
        return sortDirectionBefore(left.node->name < right.node->name);
    }
    if (sortColumn_ == PathColumn
        && left.key.normalized_path != right.key.normalized_path) {
        return sortDirectionBefore(left.key.normalized_path < right.key.normalized_path);
    }
    if (sortColumn_ == TypeColumn && left.key.kind != right.key.kind) {
        return sortDirectionBefore(static_cast<int>(left.key.kind)
                                   < static_cast<int>(right.key.kind));
    }
    return tieBreakBefore(left, right);
}

bool NodeTableModel::metricColumnBefore(const Row& left, const Row& right) const {
    switch (sortColumn_) {
    case LogicalColumn:
        return metricValueBefore(left, right, left.logical, right.logical);
    case AllocatedColumn:
        return metricValueBefore(left, right, left.allocated, right.allocated);
    case ReclaimableColumn:
        return metricValueBefore(left, right, left.reclaimable, right.reclaimable);
    default:
        return tieBreakBefore(left, right);
    }
}

bool NodeTableModel::metricValueBefore(const Row& left,
                                       const Row& right,
                                       const diskmap::MetricValue& leftMetric,
                                       const diskmap::MetricValue& rightMetric) const {
    if (leftMetric.known != rightMetric.known) {
        return leftMetric.known;
    }
    if (leftMetric.known && leftMetric.bytes != rightMetric.bytes) {
        return sortDirectionBefore(leftMetric.bytes < rightMetric.bytes);
    }
    return tieBreakBefore(left, right);
}

bool NodeTableModel::modifiedColumnBefore(const Row& left, const Row& right) const {
    if (left.modifiedKnown != right.modifiedKnown) {
        return left.modifiedKnown;
    }
    if (left.modifiedKnown && left.modifiedNs != right.modifiedNs) {
        return sortDirectionBefore(left.modifiedNs < right.modifiedNs);
    }
    return tieBreakBefore(left, right);
}

bool NodeTableModel::stateColumnBefore(const Row& left, const Row& right) const {
    if (left.issue != right.issue) {
        return sortDirectionBefore(static_cast<int>(left.issue)
                                   < static_cast<int>(right.issue));
    }
    return tieBreakBefore(left, right);
}

bool NodeTableModel::sortDirectionBefore(bool ascendingBefore) const {
    return sortOrder_ == Qt::AscendingOrder ? ascendingBefore : !ascendingBefore;
}

bool NodeTableModel::tieBreakBefore(const Row& left, const Row& right) const {
    return left.key < right.key;
}
