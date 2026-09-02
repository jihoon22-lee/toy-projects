#include "diskmap/gui/node_table_model.hpp"

#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "diskmap/format.hpp"

namespace {

constexpr const char* kPhysicalMetricExplanation =
    "Physical values are identity-aware and are not additive across sibling "
    "subtrees because hard-linked identities may overlap.";

QString utf8(const std::string& value) {
    const std::size_t maximum = static_cast<std::size_t>(std::numeric_limits<int>::max());
    const int length = static_cast<int>(std::min(value.size(), maximum));
    return QString::fromUtf8(value.data(), length);
}

QString kindName(diskmap::FsKind kind) {
    switch (kind) {
    case diskmap::FsKind::RegularFile:
        return QStringLiteral("File");
    case diskmap::FsKind::Directory:
        return QStringLiteral("Directory");
    case diskmap::FsKind::Symlink:
        return QStringLiteral("Symlink");
    case diskmap::FsKind::Other:
        return QStringLiteral("Other");
    }
    return QStringLiteral("Other");
}

QString issueName(diskmap::NodeIssue issue) {
    switch (issue) {
    case diskmap::NodeIssue::None:
        return QStringLiteral("Complete");
    case diskmap::NodeIssue::Incomplete:
        return QStringLiteral("Incomplete");
    case diskmap::NodeIssue::CycleSkipped:
        return QStringLiteral("Cycle skipped");
    case diskmap::NodeIssue::MountBoundarySkipped:
        return QStringLiteral("Mount boundary");
    case diskmap::NodeIssue::DepthLimitReached:
        return QStringLiteral("Depth limit");
    case diskmap::NodeIssue::MetadataUnknown:
        return QStringLiteral("Metadata unknown");
    case diskmap::NodeIssue::Error:
        return QStringLiteral("Error");
    case diskmap::NodeIssue::ScannerFiltered:
        return QStringLiteral("Scanner-filtered");
    }
    return QStringLiteral("Unknown");
}

QString metricText(const diskmap::MetricValue& value) {
    const QString amount = utf8(diskmap::humanBytes(value.bytes));
    if (value.known) {
        return amount;
    }
    if (value.bytes == 0) {
        return QStringLiteral("Unknown");
    }
    return QStringLiteral("At least %1").arg(amount);
}

QString modifiedText(std::int64_t nanoseconds) {
    return QDateTime::fromMSecsSinceEpoch(nanoseconds / 1000000)
        .toUTC()
        .toString(Qt::ISODateWithMs);
}

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

QVariant NodeTableModel::displayData(const Row& row, int column) const {
    switch (column) {
    case NameColumn:
        return utf8(row.node->name);
    case PathColumn:
        return utf8(row.key.normalized_path);
    case TypeColumn:
        return kindName(row.key.kind);
    case LogicalColumn:
        return metricText(row.logical);
    case AllocatedColumn:
        return metricText(row.allocated);
    case ReclaimableColumn:
        return metricText(row.reclaimable);
    case ModifiedColumn:
        return row.modifiedKnown ? QVariant(modifiedText(row.modifiedNs))
                                 : QVariant(QStringLiteral("Unknown"));
    case StateColumn:
        return issueName(row.issue);
    default:
        return QVariant();
    }
}

QVariant NodeTableModel::descriptionData(const Row& row) const {
    QString tooltip = QStringLiteral("%1\nType: %2\nLogical: %3\nAllocated: "
                                     "%4\nReclaimable: %5\nState: %6")
                          .arg(utf8(row.key.normalized_path), kindName(row.key.kind),
                               metricText(row.logical), metricText(row.allocated),
                               metricText(row.reclaimable), issueName(row.issue));
    tooltip += row.modifiedKnown
                   ? QStringLiteral("\nModified: %1").arg(modifiedText(row.modifiedNs))
                   : QStringLiteral("\nModified: Unknown");
    if (!row.error.empty()) {
        tooltip += QStringLiteral("\nDetail: %1").arg(utf8(row.error));
    }
    tooltip += QStringLiteral("\n%1").arg(QString::fromLatin1(kPhysicalMetricExplanation));
    return tooltip;
}

QVariant NodeTableModel::semanticRoleData(const Row& row, int role) const {
    switch (role) {
    case NodeKeyRole:
        return QVariant::fromValue(row.key);
    case NodeKindRole:
        return static_cast<int>(row.key.kind);
    case NodeIssueRole:
        return static_cast<int>(row.issue);
    case NodeErrorRole:
        return utf8(row.error);
    case CompleteRole:
        return row.node->complete;
    case FollowedRole:
        return row.node->followed;
    case IdentityRole:
        if (!row.key.identity.has_value()) {
            return QVariant();
        }
        return QStringLiteral("%1:%2")
            .arg(static_cast<qulonglong>(row.key.identity->device))
            .arg(static_cast<qulonglong>(row.key.identity->file));
    default:
        return QVariant();
    }
}

QVariant NodeTableModel::metricRoleData(const Row& row, int role) const {
    switch (role) {
    case LogicalBytesRole:
        return QVariant::fromValue(static_cast<qulonglong>(row.logical.bytes));
    case LogicalKnownRole:
        return row.logical.known;
    case LogicalAdditiveRole:
        return row.logical.additive;
    case AllocatedBytesRole:
        return QVariant::fromValue(static_cast<qulonglong>(row.allocated.bytes));
    case AllocatedKnownRole:
        return row.allocated.known;
    case AllocatedAdditiveRole:
        return row.allocated.additive;
    case ReclaimableBytesRole:
        return QVariant::fromValue(static_cast<qulonglong>(row.reclaimable.bytes));
    case ReclaimableKnownRole:
        return row.reclaimable.known;
    case ReclaimableAdditiveRole:
        return row.reclaimable.additive;
    case ModifiedNsRole:
        return row.modifiedKnown
                   ? QVariant::fromValue(static_cast<qlonglong>(row.modifiedNs))
                   : QVariant();
    case ModifiedKnownRole:
        return row.modifiedKnown;
    default:
        return QVariant();
    }
}

QVariant NodeTableModel::headerData(int section,
                                    Qt::Orientation orientation,
                                    int role) const {
    if (orientation != Qt::Horizontal || section < 0 || section >= ColumnCount) {
        return QVariant();
    }
    if (role == Qt::DisplayRole) {
        static const char* const headers[] = {
            "Name", "Path", "Type", "Logical", "Allocated", "Reclaimable", "Modified",
            "State",
        };
        return QString::fromLatin1(headers[section]);
    }
    if (role == Qt::ToolTipRole) {
        if (section == LogicalColumn) {
            return QStringLiteral("Directory-entry bytes. Values are additive when exact.");
        }
        if (section == AllocatedColumn) {
            return QStringLiteral("Filesystem-allocated bytes. %1")
                .arg(QString::fromLatin1(kPhysicalMetricExplanation));
        }
        if (section == ReclaimableColumn) {
            return QStringLiteral("Bytes reclaimable when every known hard link is included. %1")
                .arg(QString::fromLatin1(kPhysicalMetricExplanation));
        }
    }
    return QVariant();
}

QHash<int, QByteArray> NodeTableModel::roleNames() const {
    QHash<int, QByteArray> roles = QAbstractTableModel::roleNames();
    roles.insert(NodeKeyRole, QByteArrayLiteral("nodeKey"));
    roles.insert(NodeKindRole, QByteArrayLiteral("nodeKind"));
    roles.insert(NodeIssueRole, QByteArrayLiteral("nodeIssue"));
    roles.insert(NodeErrorRole, QByteArrayLiteral("nodeError"));
    roles.insert(CompleteRole, QByteArrayLiteral("complete"));
    roles.insert(FollowedRole, QByteArrayLiteral("followed"));
    roles.insert(IdentityRole, QByteArrayLiteral("identity"));
    roles.insert(LogicalBytesRole, QByteArrayLiteral("logicalBytes"));
    roles.insert(LogicalKnownRole, QByteArrayLiteral("logicalKnown"));
    roles.insert(LogicalAdditiveRole, QByteArrayLiteral("logicalAdditive"));
    roles.insert(AllocatedBytesRole, QByteArrayLiteral("allocatedBytes"));
    roles.insert(AllocatedKnownRole, QByteArrayLiteral("allocatedKnown"));
    roles.insert(AllocatedAdditiveRole, QByteArrayLiteral("allocatedAdditive"));
    roles.insert(ReclaimableBytesRole, QByteArrayLiteral("reclaimableBytes"));
    roles.insert(ReclaimableKnownRole, QByteArrayLiteral("reclaimableKnown"));
    roles.insert(ReclaimableAdditiveRole, QByteArrayLiteral("reclaimableAdditive"));
    roles.insert(ModifiedNsRole, QByteArrayLiteral("modifiedNs"));
    roles.insert(ModifiedKnownRole, QByteArrayLiteral("modifiedKnown"));
    return roles;
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
        if (projectionIssue_ == diskmap::NodeIssue::None) {
            projectionIssue_ = diskmap::NodeIssue::MetadataUnknown;
        }
    }
}

void NodeTableModel::applyDisplaySort() {
    const auto compareMetric = [this](const diskmap::MetricValue& left,
                                      const diskmap::MetricValue& right) {
        if (left.known != right.known) {
            return left.known;
        }
        if (left.known && left.bytes != right.bytes) {
            return sortOrder_ == Qt::DescendingOrder ? left.bytes > right.bytes
                                                     : left.bytes < right.bytes;
        }
        return false;
    };

    std::stable_sort(rows_.begin(), rows_.end(), [this, &compareMetric](const Row& left,
                                                                        const Row& right) {
        bool different = false;
        bool before = false;
        switch (sortColumn_) {
        case NameColumn:
            different = left.node->name != right.node->name;
            before = left.node->name < right.node->name;
            break;
        case PathColumn:
            different = left.key.normalized_path != right.key.normalized_path;
            before = left.key.normalized_path < right.key.normalized_path;
            break;
        case TypeColumn:
            different = left.key.kind != right.key.kind;
            before = static_cast<int>(left.key.kind) < static_cast<int>(right.key.kind);
            break;
        case LogicalColumn:
            if (left.logical.known != right.logical.known
                || (left.logical.known && left.logical.bytes != right.logical.bytes)) {
                return compareMetric(left.logical, right.logical);
            }
            break;
        case AllocatedColumn:
            if (left.allocated.known != right.allocated.known
                || (left.allocated.known && left.allocated.bytes != right.allocated.bytes)) {
                return compareMetric(left.allocated, right.allocated);
            }
            break;
        case ReclaimableColumn:
            if (left.reclaimable.known != right.reclaimable.known
                || (left.reclaimable.known
                    && left.reclaimable.bytes != right.reclaimable.bytes)) {
                return compareMetric(left.reclaimable, right.reclaimable);
            }
            break;
        case ModifiedColumn:
            if (left.modifiedKnown != right.modifiedKnown) {
                return left.modifiedKnown;
            }
            different = left.modifiedKnown && left.modifiedNs != right.modifiedNs;
            before = left.modifiedNs < right.modifiedNs;
            break;
        case StateColumn:
            different = left.issue != right.issue;
            before = static_cast<int>(left.issue) < static_cast<int>(right.issue);
            break;
        default:
            break;
        }
        if (different) {
            return sortOrder_ == Qt::AscendingOrder ? before : !before;
        }
        return left.key < right.key;
    });
}
