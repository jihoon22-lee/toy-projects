#include "buildscope/impact.hpp"
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QQueue>
#include <QSet>
#include <algorithm>

namespace buildscope {
namespace {
QString key(QString path, const Snapshot &snapshot) {
    path = QDir::cleanPath(path);
    const auto root = QDir::cleanPath(snapshot.projectRoot);
    if (!root.isEmpty() && path.startsWith(root + '/'))
        path = path.mid(root.size() + 1);
    return path;
}
} // namespace
QJsonObject includeImpact(const Snapshot &snapshot, const QString &header,
                          std::atomic_bool *cancel, int maxVisitedEdges) {
    if (header.isEmpty() || maxVisitedEdges < 1)
        throw ContractError("impact requires a non-empty header and positive edge budget");
    const auto target = key(header, snapshot);
    QJsonArray units, parents;
    int visited = 0, unavailable = 0;
    bool partial = false;
    for (qsizetype index = 0; index < snapshot.entries.size(); ++index) {
        if (cancel && cancel->load()) {
            partial = true;
            break;
        }
        const auto &entry = snapshot.entries[index];
        const auto source =
            key(entry.hasNormalized ? entry.normalized.source.path : entry.file, snapshot);
        if (source == target)
            units.append(QJsonObject{{"source", source},
                                     {"entry", double(index)},
                                     {"configuration", entry.normalized.configuration},
                                     {"evidence", "translation-unit"},
                                     {"connected", true},
                                     {"chain", QJsonArray{target}}});
        if (!entry.hasIncludeAnalysis) {
            ++unavailable;
            partial = true;
            continue;
        }
        QList<const SnapshotIncludeAnalysis *> analyses{&entry.includeAnalysis};
        if (entry.includeAnalysis.fallback)
            analyses.append(entry.includeAnalysis.fallback.get());
        for (const auto *analysis : analyses) {
            if (analysis->evidence == "unavailable") {
                ++unavailable;
                partial = true;
                continue;
            }
            if (!analysis->complete)
                partial = true;
            QHash<QString, QVector<const SnapshotIncludeEdge *>> reverse;
            for (const auto &edge : analysis->edges) {
                if (++visited > maxVisitedEdges) {
                    partial = true;
                    break;
                }
                if (!edge.resolved)
                    continue;
                reverse[key(*edge.resolved, snapshot)].append(&edge);
            }
            if (visited > maxVisitedEdges)
                break;
            QQueue<QString> queue;
            queue.enqueue(target);
            QHash<QString, QString> childOf;
            QSet<QString> seen{target};
            bool found = false;
            while (!queue.isEmpty()) {
                if (cancel && cancel->load()) {
                    partial = true;
                    break;
                }
                const auto current = queue.dequeue();
                for (const auto *edge : reverse.value(current)) {
                    const auto parent = key(edge->parent, snapshot);
                    if (current == target) {
                        found = true;
                        parents.append(
                            QJsonObject{{"parent", parent},
                                        {"line", double(edge->line)},
                                        {"source", source},
                                        {"entry", double(index)},
                                        {"evidence", edge->evidence},
                                        {"location_evidence", edge->locationEvidence}});
                    }
                    if (!seen.contains(parent)) {
                        seen.insert(parent);
                        childOf.insert(parent, current);
                        queue.enqueue(parent);
                    }
                }
            }
            if (found && source != target) {
                QJsonArray chain;
                auto current = source;
                int depth = 0;
                if (seen.contains(source)) {
                    chain.append(source);
                    while (current != target && childOf.contains(current) && depth++ < 128) {
                        current = childOf.value(current);
                        chain.append(current);
                    }
                    if (current != target)
                        partial = true;
                } else
                    chain.append(target);
                units.append(QJsonObject{{"source", source},
                                         {"entry", double(index)},
                                         {"configuration", entry.normalized.configuration},
                                         {"evidence", analysis->evidence},
                                         {"connected", seen.contains(source)},
                                         {"chain", chain}});
            }
        }
        if (visited > maxVisitedEdges)
            break;
    }
    return {{"schema_version", "buildscope.impact/v1"},
            {"header", target},
            {"partial", partial},
            {"unavailable_analyses", unavailable},
            {"visited_edges", std::min(visited, maxVisitedEdges)},
            {"direct_parents", parents},
            {"translation_units", units},
            {"interpretation", "Observed reverse dependencies; estimated and measured graphs "
                               "are separate. Missing edges never prove no impact."}};
}
} // namespace buildscope
