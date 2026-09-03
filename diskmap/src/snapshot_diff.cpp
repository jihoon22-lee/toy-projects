#include "snapshot_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace diskmap {
namespace {

struct EntryRef {
    const FsNode* node = nullptr;
    NodeKey key;
};

void collectEntries(const FsNode& root, std::vector<EntryRef>& entries) {
    std::vector<const FsNode*> stack{&root};
    while (!stack.empty()) {
        const FsNode* node = stack.back();
        stack.pop_back();
        entries.push_back(EntryRef{node, nodeKey(*node)});
        for (auto child = node->children.rbegin(); child != node->children.rend(); ++child) {
            stack.push_back(&*child);
        }
    }
    std::stable_sort(entries.begin(), entries.end(), [](const EntryRef& left, const EntryRef& right) {
        if (left.key != right.key) {
            return left.key < right.key;
        }
        return left.node->name < right.node->name;
    });
}

using EntryIndices = std::map<std::string, std::vector<std::size_t>>;

EntryIndices indexByPath(const std::vector<EntryRef>& entries) {
    EntryIndices index;
    for (std::size_t position = 0; position < entries.size(); ++position) {
        index[entries[position].key.normalized_path].push_back(position);
    }
    return index;
}

struct IdentityKey {
    std::uint64_t device = 0;
    std::uint64_t file = 0;
    FsKind kind = FsKind::Other;
    bool followed = false;

    bool operator<(const IdentityKey& other) const {
        if (device != other.device) {
            return device < other.device;
        }
        if (file != other.file) {
            return file < other.file;
        }
        if (kind != other.kind) {
            return static_cast<int>(kind) < static_cast<int>(other.kind);
        }
        return followed < other.followed;
    }
};

std::optional<IdentityKey> identityKey(const EntryRef& entry) {
    const FileIdentity& identity = entry.node->metadata.identity;
    if (!identity.valid) {
        return std::nullopt;
    }
    return IdentityKey{identity.device, identity.file, nodeKind(*entry.node), entry.node->followed};
}

MetricValue entryMetric(const Snapshot& snapshot,
                        const FsNode& node,
                        SizeMetric metric) {
    MetricValue value = metricValue(node, metric);
    // A partial snapshot can prove a visible leaf's current bytes, but it
    // cannot prove that the inventory is complete enough for a definite
    // cross-snapshot conclusion.
    if (!snapshot.complete || snapshot.truncated) {
        value.known = false;
    }
    return value;
}

bool structurallyComplete(const FsNode& node) {
    return node.complete && node.metadata.complete && !node.cycle_skipped
           && !node.mount_boundary_skipped && node.error.empty()
           && node.metadata.error.empty()
           && (!node.followed || (node.has_target_metadata && node.target_metadata.complete
                                  && node.target_metadata.error.empty()));
}

bool exactEvidence(const Snapshot& snapshot,
                   const FsNode& node,
                   const MetricValue& metric) {
    return snapshot.complete && !snapshot.truncated && structurallyComplete(node)
           && metric.known;
}

bool completeSnapshotEvidence(const Snapshot& snapshot) {
    return snapshot.complete && !snapshot.truncated && structurallyComplete(snapshot.root);
}

void markUncertain(SnapshotDiff& diff) {
    diff.complete = false;
    diff.uncertain = true;
}

void recordChange(SnapshotDiff& diff, SnapshotChange change) {
    if (!change.certain) {
        markUncertain(diff);
    }
    diff.changes.push_back(std::move(change));
}

SnapshotChange makeChange(SnapshotChangeKind kind,
                          const EntryRef* before,
                          const EntryRef* after,
                          MetricValue beforeMetric,
                          MetricValue afterMetric,
                          bool certain,
                          std::string reason = {}) {
    SnapshotChange change;
    change.kind = kind;
    change.certain = certain;
    change.before_metric = beforeMetric;
    change.after_metric = afterMetric;
    change.reason = std::move(reason);
    if (before != nullptr) {
        change.has_before = true;
        change.before_key = before->key;
    }
    if (after != nullptr) {
        change.has_after = true;
        change.after_key = after->key;
    }
    return change;
}

void validateDiffSnapshot(const Snapshot& snapshot) {
    if (snapshot.schema_version != kSnapshotSchemaV1) {
        throw SnapshotError("unsupported diskmap snapshot schema");
    }
    SnapshotLimits limits;
    const detail::SnapshotTreeValidation validation =
        detail::validateSnapshotTree(snapshot.root, detail::checkedSnapshotLimits(limits));
    if (snapshot.complete && snapshot.truncated) {
        throw SnapshotError("truncated snapshot cannot be marked complete");
    }
    if (snapshot.complete && validation.has_incomplete_evidence) {
        throw SnapshotError("complete snapshot contains incomplete evidence");
    }
}

void compareSamePath(const Snapshot& before,
                     const Snapshot& after,
                     const EntryRef& beforeEntry,
                     const EntryRef& afterEntry,
                     const SnapshotDiffOptions& options,
                     SnapshotDiff& diff) {
    const MetricValue beforeMetric = entryMetric(before, *beforeEntry.node, options.metric);
    const MetricValue afterMetric = entryMetric(after, *afterEntry.node, options.metric);
    const bool sameKind = nodeKind(*beforeEntry.node) == nodeKind(*afterEntry.node)
                          && beforeEntry.node->followed == afterEntry.node->followed;
    const bool identitiesKnown = beforeEntry.node->metadata.identity.valid
                                 && afterEntry.node->metadata.identity.valid;
    const bool sameIdentity = identitiesKnown
                              && beforeEntry.node->metadata.identity
                                     == afterEntry.node->metadata.identity;
    const bool certain = exactEvidence(before, *beforeEntry.node, beforeMetric)
                         && exactEvidence(after, *afterEntry.node, afterMetric);
    if (!sameKind || !sameIdentity) {
        recordChange(diff, makeChange(SnapshotChangeKind::Uncertain, &beforeEntry, &afterEntry,
                                      beforeMetric, afterMetric, false,
                                      "entry kind or physical identity changed at the same path"));
        return;
    }
    if (!beforeMetric.known || !afterMetric.known) {
        recordChange(diff, makeChange(SnapshotChangeKind::Uncertain, &beforeEntry, &afterEntry,
                                      beforeMetric, afterMetric, false,
                                      "size evidence is incomplete or unknown"));
        return;
    }
    if (beforeMetric.bytes == afterMetric.bytes) {
        return;
    }
    const SnapshotChangeKind kind = afterMetric.bytes > beforeMetric.bytes
                                        ? SnapshotChangeKind::Grown
                                        : SnapshotChangeKind::Shrunk;
    recordChange(diff, makeChange(kind, &beforeEntry, &afterEntry, beforeMetric, afterMetric,
                                  certain, certain ? std::string() : "snapshot evidence is incomplete"));
}

void matchSamePaths(const Snapshot& before,
                    const Snapshot& after,
                    const SnapshotDiffOptions& options,
                    const std::vector<EntryRef>& beforeEntries,
                    const std::vector<EntryRef>& afterEntries,
                    const EntryIndices& beforePaths,
                    const EntryIndices& afterPaths,
                    std::vector<bool>& beforeUsed,
                    std::vector<bool>& afterUsed,
                    SnapshotDiff& diff) {
    std::set<std::string> allPaths;
    for (const auto& item : beforePaths) {
        allPaths.insert(item.first);
    }
    for (const auto& item : afterPaths) {
        allPaths.insert(item.first);
    }
    for (const std::string& path : allPaths) {
        const auto beforeIt = beforePaths.find(path);
        const auto afterIt = afterPaths.find(path);
        const std::vector<std::size_t> empty;
        const std::vector<std::size_t>& oldIndices =
            beforeIt == beforePaths.end() ? empty : beforeIt->second;
        const std::vector<std::size_t>& newIndices =
            afterIt == afterPaths.end() ? empty : afterIt->second;
        const std::size_t pairs = std::min(oldIndices.size(), newIndices.size());
        for (std::size_t index = 0; index < pairs; ++index) {
            beforeUsed[oldIndices[index]] = true;
            afterUsed[newIndices[index]] = true;
            ++diff.compared_nodes;
            compareSamePath(before, after, beforeEntries[oldIndices[index]],
                            afterEntries[newIndices[index]], options, diff);
        }
    }
}

using IdentityIndices = std::map<IdentityKey, std::vector<std::size_t>>;
using IdentityTotals = std::map<IdentityKey, std::size_t>;

IdentityIndices unmatchedIdentities(const std::vector<EntryRef>& entries,
                                    const std::vector<bool>& used) {
    IdentityIndices result;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (used[index]) {
            continue;
        }
        const auto key = identityKey(entries[index]);
        if (key.has_value()) {
            result[*key].push_back(index);
        }
    }
    return result;
}

IdentityTotals identityTotals(const std::vector<EntryRef>& entries) {
    IdentityTotals result;
    for (const EntryRef& entry : entries) {
        const auto key = identityKey(entry);
        if (key.has_value()) {
            ++result[*key];
        }
    }
    return result;
}

void matchMoved(const Snapshot& before,
                const Snapshot& after,
                const SnapshotDiffOptions& options,
                const std::vector<EntryRef>& beforeEntries,
                const std::vector<EntryRef>& afterEntries,
                const IdentityIndices& beforeIdentities,
                const IdentityIndices& afterIdentities,
                const IdentityTotals& beforeIdentityTotals,
                const IdentityTotals& afterIdentityTotals,
                std::vector<bool>& beforeUsed,
                std::vector<bool>& afterUsed,
                SnapshotDiff& diff) {
    for (const auto& item : beforeIdentities) {
        const auto afterIt = afterIdentities.find(item.first);
        if (afterIt == afterIdentities.end()) {
            continue;
        }
        const std::vector<std::size_t>& oldIndices = item.second;
        const std::vector<std::size_t>& newIndices = afterIt->second;
        const std::size_t pairs = std::min(oldIndices.size(), newIndices.size());
        const bool uniqueIdentity = beforeIdentityTotals.at(item.first) == 1
                                    && afterIdentityTotals.at(item.first) == 1;
        for (std::size_t index = 0; index < pairs; ++index) {
            const EntryRef& oldEntry = beforeEntries[oldIndices[index]];
            const EntryRef& newEntry = afterEntries[newIndices[index]];
            beforeUsed[oldIndices[index]] = true;
            afterUsed[newIndices[index]] = true;
            ++diff.compared_nodes;
            const MetricValue oldMetric = entryMetric(before, *oldEntry.node, options.metric);
            const MetricValue newMetric = entryMetric(after, *newEntry.node, options.metric);
            const bool certain = uniqueIdentity && exactEvidence(before, *oldEntry.node, oldMetric)
                                 && exactEvidence(after, *newEntry.node, newMetric);
            recordChange(diff, makeChange(SnapshotChangeKind::Moved, &oldEntry, &newEntry,
                                          oldMetric, newMetric, certain,
                                          certain ? std::string()
                                                  : "move is ambiguous or snapshot evidence is incomplete"));
        }
    }
}

void recordRemoved(const Snapshot& before,
                   const Snapshot& after,
                   const std::vector<EntryRef>& entries,
                   const std::vector<bool>& used,
                   const SnapshotDiffOptions& options,
                   SnapshotDiff& diff) {
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (used[index]) {
            continue;
        }
        const EntryRef& entry = entries[index];
        const MetricValue metric = entryMetric(before, *entry.node, options.metric);
        const bool certain = exactEvidence(before, *entry.node, metric)
                             && completeSnapshotEvidence(after);
        recordChange(diff, makeChange(SnapshotChangeKind::Removed, &entry, nullptr, metric, {}, certain,
                                      certain ? std::string() : "absence is not conclusive in an incomplete snapshot"));
    }
}

void recordAdded(const Snapshot& before,
                 const Snapshot& after,
                 const std::vector<EntryRef>& entries,
                 const std::vector<bool>& used,
                 const SnapshotDiffOptions& options,
                 SnapshotDiff& diff) {
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (used[index]) {
            continue;
        }
        const EntryRef& entry = entries[index];
        const MetricValue metric = entryMetric(after, *entry.node, options.metric);
        const bool certain = completeSnapshotEvidence(before)
                             && exactEvidence(after, *entry.node, metric);
        recordChange(diff, makeChange(SnapshotChangeKind::Added, nullptr, &entry, {}, metric, certain,
                                      certain ? std::string() : "presence is not conclusive in an incomplete snapshot"));
    }
}

bool pathLess(const SnapshotChange& left, const SnapshotChange& right) {
    const std::string leftPath = left.has_after ? left.after_key.normalized_path
                                                 : left.before_key.normalized_path;
    const std::string rightPath = right.has_after ? right.after_key.normalized_path
                                                   : right.before_key.normalized_path;
    if (leftPath != rightPath) {
        return leftPath < rightPath;
    }
    if (left.kind != right.kind) {
        return static_cast<int>(left.kind) < static_cast<int>(right.kind);
    }
    const std::string leftBefore = left.has_before ? left.before_key.normalized_path : std::string();
    const std::string rightBefore = right.has_before ? right.before_key.normalized_path : std::string();
    return leftBefore < rightBefore;
}

} // namespace

SnapshotDiff diffSnapshots(const Snapshot& before,
                           const Snapshot& after,
                           const SnapshotDiffOptions& options) {
    // The file APIs reject duplicate paths and inconsistent complete flags.
    // Apply the same invariant to hand-built values passed directly to diff so
    // indexByPath() can never silently pair an ambiguous entry.
    validateDiffSnapshot(before);
    validateDiffSnapshot(after);
    std::vector<EntryRef> beforeEntries;
    std::vector<EntryRef> afterEntries;
    collectEntries(before.root, beforeEntries);
    collectEntries(after.root, afterEntries);
    const EntryIndices beforePaths = indexByPath(beforeEntries);
    const EntryIndices afterPaths = indexByPath(afterEntries);
    std::vector<bool> beforeUsed(beforeEntries.size(), false);
    std::vector<bool> afterUsed(afterEntries.size(), false);
    SnapshotDiff diff;
    diff.compared_nodes = 0;
    if (!before.complete || before.truncated || !after.complete || after.truncated) {
        markUncertain(diff);
    }

    matchSamePaths(before, after, options, beforeEntries, afterEntries, beforePaths, afterPaths,
                   beforeUsed, afterUsed, diff);
    const IdentityIndices beforeIdentities = unmatchedIdentities(beforeEntries, beforeUsed);
    const IdentityIndices afterIdentities = unmatchedIdentities(afterEntries, afterUsed);
    const IdentityTotals beforeIdentityTotals = identityTotals(beforeEntries);
    const IdentityTotals afterIdentityTotals = identityTotals(afterEntries);
    matchMoved(before, after, options, beforeEntries, afterEntries, beforeIdentities, afterIdentities,
               beforeIdentityTotals, afterIdentityTotals, beforeUsed, afterUsed, diff);
    recordRemoved(before, after, beforeEntries, beforeUsed, options, diff);
    recordAdded(before, after, afterEntries, afterUsed, options, diff);

    std::stable_sort(diff.changes.begin(), diff.changes.end(), pathLess);
    return diff;
}

} // namespace diskmap
