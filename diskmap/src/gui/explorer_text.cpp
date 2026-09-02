#include "explorer_text.hpp"

#include <algorithm>
#include <limits>

#include "diskmap/format.hpp"

namespace diskmap_gui_text {

QString utf8(const std::string& value) {
    const std::size_t maximum = static_cast<std::size_t>(std::numeric_limits<int>::max());
    const int length = static_cast<int>(std::min(value.size(), maximum));
    return QString::fromUtf8(value.data(), length);
}

QString issueDescription(diskmap::NodeIssue issue) {
    switch (issue) {
    case diskmap::NodeIssue::None:
        return QStringLiteral("complete");
    case diskmap::NodeIssue::Incomplete:
        return QStringLiteral("incomplete subtree");
    case diskmap::NodeIssue::CycleSkipped:
        return QStringLiteral("cycle skipped");
    case diskmap::NodeIssue::MountBoundarySkipped:
        return QStringLiteral("mount boundary skipped");
    case diskmap::NodeIssue::DepthLimitReached:
        return QStringLiteral("depth limit reached");
    case diskmap::NodeIssue::MetadataUnknown:
        return QStringLiteral("metadata unknown");
    case diskmap::NodeIssue::Error:
        return QStringLiteral("scan error");
    case diskmap::NodeIssue::ScannerFiltered:
        return QStringLiteral("scanner-level filtering");
    }
    return QStringLiteral("unknown state");
}

QString issueLabel(diskmap::NodeIssue issue) {
    QString label = issueDescription(issue);
    if (!label.isEmpty()) {
        label[0] = label.at(0).toUpper();
    }
    return label;
}

QString metricName(diskmap::SizeMetric metric) {
    switch (metric) {
    case diskmap::SizeMetric::Logical:
        return QStringLiteral("Logical");
    case diskmap::SizeMetric::Allocated:
        return QStringLiteral("Allocated");
    case diskmap::SizeMetric::Reclaimable:
        return QStringLiteral("Reclaimable");
    }
    return QStringLiteral("Size");
}

QString metricValueText(const diskmap::MetricValue& value) {
    const QString amount = utf8(diskmap::humanBytes(value.bytes));
    if (value.known) {
        return amount;
    }
    return value.bytes == 0 ? QStringLiteral("Unknown")
                            : QStringLiteral("At least %1").arg(amount);
}

QString nodeDescription(const diskmap::FsNode& node, bool scannerTotalsFiltered) {
    const diskmap::MetricValue logical = diskmap::metricValue(
        node, diskmap::SizeMetric::Logical, scannerTotalsFiltered);
    const diskmap::MetricValue allocated = diskmap::metricValue(
        node, diskmap::SizeMetric::Allocated, scannerTotalsFiltered);
    const diskmap::MetricValue reclaimable = diskmap::metricValue(
        node, diskmap::SizeMetric::Reclaimable, scannerTotalsFiltered);
    return QStringLiteral("%1\nLogical: %2\nAllocated: %3\nReclaimable: %4\nState: %5")
        .arg(utf8(diskmap::normalizedPath(node)), metricValueText(logical),
             metricValueText(allocated), metricValueText(reclaimable),
             issueDescription(
                 diskmap::classifyNodeIssue(node, scannerTotalsFiltered)));
}

} // namespace diskmap_gui_text
