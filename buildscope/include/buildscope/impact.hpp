#pragma once
#include "buildscope/contract.hpp"
#include <QJsonObject>
#include <atomic>

namespace buildscope {
// Reverse traversal never combines a compiler graph with its estimated fallback.
QJsonObject includeImpact(const Snapshot &snapshot, const QString &header,
                         std::atomic_bool *cancel = nullptr, int maxVisitedEdges = 500000);
}
