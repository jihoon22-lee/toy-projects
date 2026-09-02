#pragma once

#include <QAbstractTableModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QVariant>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "diskmap/gui/node_key_metatype.hpp"
#include "diskmap/scanner.hpp"
#include "diskmap/view.hpp"

// A non-owning row projection backed by one shared immutable scan document.
// Rows borrow FsNode pointers, while document_ guarantees those pointers stay
// valid until the next reset. Filtering and largest-file selection remain in
// the Qt-free diskmap::view layer; this class only adapts them to Qt roles.
class NodeTableModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Mode { ChildrenMode = 0, LargestFilesMode };
    Q_ENUM(Mode)

    enum Column {
        NameColumn = 0,
        PathColumn,
        TypeColumn,
        LogicalColumn,
        AllocatedColumn,
        ReclaimableColumn,
        ModifiedColumn,
        StateColumn,
        ColumnCount
    };
    Q_ENUM(Column)

    enum Role {
        NodeKeyRole = Qt::UserRole + 1,
        NodeKindRole,
        NodeIssueRole,
        NodeErrorRole,
        CompleteRole,
        FollowedRole,
        IdentityRole,
        LogicalBytesRole,
        LogicalKnownRole,
        LogicalAdditiveRole,
        AllocatedBytesRole,
        AllocatedKnownRole,
        AllocatedAdditiveRole,
        ReclaimableBytesRole,
        ReclaimableKnownRole,
        ReclaimableAdditiveRole,
        ModifiedNsRole,
        ModifiedKnownRole
    };
    Q_ENUM(Role)

    explicit NodeTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    void setProjection(std::shared_ptr<const diskmap::ScanResult> document,
                       const diskmap::NodeKey& root,
                       const diskmap::ViewFilter& filter,
                       const diskmap::SortSpec& sort,
                       Mode mode = ChildrenMode);
    void clear();
    void setLargestLimit(std::size_t limit);

    const diskmap::FsNode* nodeAt(int row) const;
    std::optional<diskmap::NodeKey> keyAt(int row) const;
    QModelIndex indexForKey(const diskmap::NodeKey& key) const;
    const diskmap::FsNode* rootNode() const;
    bool projectionComplete() const;
    diskmap::NodeIssue projectionIssue() const;
    std::size_t largestLimit() const;
    Mode mode() const;

signals:
    void projectionStatusChanged(bool complete, int issueCode);

private:
    struct Row {
        const diskmap::FsNode* node = nullptr;
        diskmap::NodeKey key;
        diskmap::MetricValue logical;
        diskmap::MetricValue allocated;
        diskmap::MetricValue reclaimable;
        diskmap::NodeIssue issue = diskmap::NodeIssue::None;
        std::int64_t modifiedNs = 0;
        bool modifiedKnown = false;
        std::string error;
    };

    std::shared_ptr<const diskmap::ScanResult> document_;
    const diskmap::FsNode* root_ = nullptr;
    diskmap::NodeKey rootKey_;
    diskmap::ViewFilter filter_;
    diskmap::SortSpec sort_;
    Mode mode_ = ChildrenMode;
    std::size_t largestLimit_ = 100;
    std::vector<Row> rows_;
    bool projectionComplete_ = true;
    diskmap::NodeIssue projectionIssue_ = diskmap::NodeIssue::None;
    int sortColumn_ = LogicalColumn;
    Qt::SortOrder sortOrder_ = Qt::DescendingOrder;

    void rebuild();
    void rebuildRows();
    void applyDisplaySort();
    QVariant displayData(const Row& row, int column) const;
    QVariant descriptionData(const Row& row) const;
    QVariant semanticRoleData(const Row& row, int role) const;
    QVariant metricRoleData(const Row& row, int role) const;
    const Row* rowForIndex(const QModelIndex& index) const;
    std::vector<const diskmap::FsNode*> projectedNodes(bool& complete) const;
    Row makeRow(const diskmap::FsNode& node) const;
    bool boundedFilterEvidenceComplete() const;
    void updateProjectionStatus(bool sourceComplete);
};
