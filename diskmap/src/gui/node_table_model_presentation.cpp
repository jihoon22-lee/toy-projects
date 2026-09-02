#include "diskmap/gui/node_table_model.hpp"

#include <QDateTime>
#include <QString>

#include <cstdint>
#include <string>

#include "explorer_text.hpp"

namespace {

constexpr const char* kPhysicalMetricExplanation =
    "Physical values are identity-aware and are not additive across sibling "
    "subtrees because hard-linked identities may overlap.";

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

QString modifiedText(std::int64_t nanoseconds) {
    return QDateTime::fromMSecsSinceEpoch(nanoseconds / 1000000)
        .toUTC()
        .toString(Qt::ISODateWithMs);
}

} // namespace

QVariant NodeTableModel::displayData(const Row& row, int column) const {
    switch (column) {
    case NameColumn:
        return diskmap_gui_text::utf8(row.node->name);
    case PathColumn:
        return diskmap_gui_text::utf8(row.key.normalized_path);
    case TypeColumn:
        return kindName(row.key.kind);
    case LogicalColumn:
        return diskmap_gui_text::metricValueText(row.logical);
    case AllocatedColumn:
        return diskmap_gui_text::metricValueText(row.allocated);
    case ReclaimableColumn:
        return diskmap_gui_text::metricValueText(row.reclaimable);
    case ModifiedColumn:
        return row.modifiedKnown ? QVariant(modifiedText(row.modifiedNs))
                                 : QVariant(QStringLiteral("Unknown"));
    case StateColumn:
        return diskmap_gui_text::issueLabel(row.issue);
    default:
        return QVariant();
    }
}

QVariant NodeTableModel::descriptionData(const Row& row) const {
    QString tooltip = QStringLiteral("%1\nType: %2\nLogical: %3\nAllocated: "
                                     "%4\nReclaimable: %5\nState: %6")
                          .arg(diskmap_gui_text::utf8(row.key.normalized_path),
                               kindName(row.key.kind),
                               diskmap_gui_text::metricValueText(row.logical),
                               diskmap_gui_text::metricValueText(row.allocated),
                               diskmap_gui_text::metricValueText(row.reclaimable),
                               diskmap_gui_text::issueLabel(row.issue));
    tooltip += row.modifiedKnown
                   ? QStringLiteral("\nModified: %1").arg(modifiedText(row.modifiedNs))
                   : QStringLiteral("\nModified: Unknown");
    if (!row.error.empty()) {
        tooltip += QStringLiteral("\nDetail: %1")
                       .arg(diskmap_gui_text::utf8(row.error));
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
        return diskmap_gui_text::utf8(row.error);
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
