#pragma once

#include <QString>

#include <string>

#include "diskmap/fs_node.hpp"
#include "diskmap/view.hpp"

namespace diskmap_gui_text {

QString utf8(const std::string& value);
QString issueDescription(diskmap::NodeIssue issue);
QString issueLabel(diskmap::NodeIssue issue);
QString metricName(diskmap::SizeMetric metric);
QString metricValueText(const diskmap::MetricValue& value);
QString nodeDescription(const diskmap::FsNode& node, bool scannerTotalsFiltered);

} // namespace diskmap_gui_text
