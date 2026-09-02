#include "diskmap/view.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace diskmap {

namespace {

const FsMetadata* metadataForAge(const FsNode& node) {
    // A partial scan cannot establish a trustworthy row age, even when a
    // followed target happened to expose a timestamp. Keep unknown-age
    // filtering fail-closed for every incomplete node.
    if (!node.complete) {
        return nullptr;
    }
    if (node.followed) {
        if (!node.has_target_metadata || !node.target_metadata.complete) {
            return nullptr;
        }
        return &node.target_metadata;
    }
    if (!node.metadata.complete) {
        return nullptr;
    }
    return &node.metadata;
}

bool isDepthLimitError(const FsNode& node) {
    static const std::string marker = "scan depth limit reached";
    return node.error.find(marker) != std::string::npos;
}

char asciiLower(char value) {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

std::string asciiLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](char character) { return asciiLower(character); });
    return value;
}

bool containsCaseInsensitiveLiteral(const std::string& haystack,
                                    const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    return asciiLower(haystack).find(asciiLower(needle)) != std::string::npos;
}

SizeMetric filterMetric(const ViewFilter& filter) {
    if (filter.metric.has_value()) {
        return *filter.metric;
    }
    return filter.size_metric.value_or(SizeMetric::Logical);
}

template <typename T>
std::optional<T> firstBound(const std::optional<T>& canonical,
                            const std::optional<T>& firstAlias,
                            const std::optional<T>& secondAlias) {
    if (canonical.has_value()) {
        return canonical;
    }
    if (firstAlias.has_value()) {
        return firstAlias;
    }
    return secondAlias;
}

std::optional<std::uint64_t> minimumSize(const ViewFilter& filter) {
    return firstBound(filter.min_size, filter.minimum_size, filter.min_bytes);
}

std::optional<std::uint64_t> maximumSize(const ViewFilter& filter) {
    return firstBound(filter.max_size, filter.maximum_size, filter.max_bytes);
}

std::optional<std::int64_t> modifiedAfter(const ViewFilter& filter) {
    return firstBound(filter.modified_after_ns,
                      filter.min_modified_ns,
                      filter.newer_than_ns);
}

std::optional<std::int64_t> modifiedBefore(const ViewFilter& filter) {
    return firstBound(filter.modified_before_ns,
                      filter.max_modified_ns,
                      filter.older_than_ns);
}

std::string searchText(const ViewFilter& filter) {
    return filter.search.empty() ? filter.query : filter.search;
}

std::optional<FsKind> typeFilter(const ViewFilter& filter) {
    return filter.kind.has_value() ? filter.kind : filter.type;
}

bool containsKind(const std::vector<FsKind>& kinds, FsKind kind) {
    return std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
}

bool containsIssue(const std::vector<NodeIssue>& issues, NodeIssue issue) {
    return std::find(issues.begin(), issues.end(), issue) != issues.end();
}

bool logicalSubtreeKnown(const FsNode& root, bool scannerTotalsFiltered) {
    struct Frame {
        const FsNode* node;
        std::size_t nextChild = 0;
    };

    std::vector<Frame> stack;
    stack.push_back(Frame{&root});
    while (!stack.empty()) {
        Frame& frame = stack.back();
        const FsNode& node = *frame.node;
        if (!node.complete || !node.logical_size_known || node.cycle_skipped
            || node.mount_boundary_skipped
            || isDepthLimitError(node)) {
            return false;
        }
        if (node.followed && (!node.has_target_metadata || !node.target_metadata.complete)) {
            return false;
        }
        // ScanResult::totals_filtered is a global fact, not a view predicate.
        // We can keep an individual leaf exact, but cannot claim an exact
        // aggregate for any directory without a per-subtree filter map.
        if (scannerTotalsFiltered && node.is_dir) {
            return false;
        }
        if (!node.is_dir || frame.nextChild >= node.children.size()) {
            stack.pop_back();
            continue;
        }
        const FsNode* child = &node.children[frame.nextChild++];
        stack.push_back(Frame{child});
    }
    return true;
}

bool matchesSearch(const FsNode& node, const ViewFilter& filter) {
    const std::string query = searchText(filter);
    return query.empty() || containsCaseInsensitiveLiteral(node.name, query)
           || containsCaseInsensitiveLiteral(normalizedPath(node), query);
}

bool matchesType(const FsNode& node, const ViewFilter& filter) {
    const FsKind kind = nodeKind(node);
    const std::optional<FsKind> exactType = typeFilter(filter);
    if (exactType.has_value() && kind != *exactType) {
        return false;
    }
    return filter.kinds.empty() || containsKind(filter.kinds, kind);
}

bool matchesIssue(const FsNode& node, const ViewFilter& filter) {
    const NodeIssue issue = classifyNodeIssue(node, filter.scanner_totals_filtered);
    if (filter.issue.has_value() && issue != *filter.issue) {
        return false;
    }
    return filter.issues.empty() || containsIssue(filter.issues, issue);
}

bool matchesSize(const FsNode& node, const ViewFilter& filter) {
    const std::optional<std::uint64_t> lowerSize = minimumSize(filter);
    const std::optional<std::uint64_t> upperSize = maximumSize(filter);
    if (!lowerSize.has_value() && !upperSize.has_value()) {
        return true;
    }

    const MetricValue value = metricValue(node, filterMetric(filter),
                                          filter.scanner_totals_filtered);
    // Unknown values never satisfy a numeric bound, including a bound of
    // zero. This avoids turning an unknown physical total into a match.
    if (!value.known) {
        return false;
    }
    if (lowerSize.has_value() && value.bytes < *lowerSize) {
        return false;
    }
    return !upperSize.has_value() || value.bytes <= *upperSize;
}

bool matchesAge(const FsNode& node, const ViewFilter& filter) {
    const std::optional<std::int64_t> lowerTime = modifiedAfter(filter);
    const std::optional<std::int64_t> upperTime = modifiedBefore(filter);
    if (!lowerTime.has_value() && !upperTime.has_value()) {
        return true;
    }

    const FsMetadata* metadata = metadataForAge(node);
    if (metadata == nullptr || !metadata->modified_time_known) {
        return false;
    }
    if (lowerTime.has_value() && metadata->modified_ns < *lowerTime) {
        return false;
    }
    return !upperTime.has_value() || metadata->modified_ns <= *upperTime;
}

bool matchesFilter(const FsNode& node, const ViewFilter& filter) {
    return matchesSearch(node, filter) && matchesType(node, filter)
           && matchesIssue(node, filter) && matchesSize(node, filter)
           && matchesAge(node, filter);
}

int compareIdentity(const std::optional<FileIdentity>& left,
                    const std::optional<FileIdentity>& right) {
    const bool leftValid = left.has_value() && left->valid;
    const bool rightValid = right.has_value() && right->valid;
    if (leftValid != rightValid) {
        return leftValid ? 1 : -1;
    }
    if (!leftValid) {
        return 0;
    }
    if (left->device != right->device) {
        return left->device < right->device ? -1 : 1;
    }
    if (left->file != right->file) {
        return left->file < right->file ? -1 : 1;
    }
    return 0;
}

bool lessNodeKey(const NodeKey& left, const NodeKey& right) {
    if (left.normalized_path != right.normalized_path) {
        return left.normalized_path < right.normalized_path;
    }
    if (left.kind != right.kind) {
        return static_cast<int>(left.kind) < static_cast<int>(right.kind);
    }
    if (left.followed != right.followed) {
        return !left.followed;
    }
    return compareIdentity(left.identity, right.identity) < 0;
}

bool isRegularFileEntry(const FsNode& node) {
    // A symlink remains an entry of kind Symlink even when its target is
    // followed. The largest-files view describes owned file entries and must
    // not duplicate a target through an alias.
    return !node.is_dir && node.metadata.kind == FsKind::RegularFile;
}

NodeIssue structuralIssue(const FsNode& node) {
    if (node.cycle_skipped) {
        return NodeIssue::CycleSkipped;
    }
    if (node.mount_boundary_skipped) {
        return NodeIssue::MountBoundarySkipped;
    }
    return isDepthLimitError(node) ? NodeIssue::DepthLimitReached : NodeIssue::None;
}

NodeIssue metadataIssue(const FsNode& node) {
    if (node.followed && (!node.has_target_metadata || !node.target_metadata.complete)) {
        return NodeIssue::MetadataUnknown;
    }
    if (!node.metadata.complete) {
        return NodeIssue::MetadataUnknown;
    }
    if (node.followed && node.has_target_metadata && !node.target_metadata.error.empty()) {
        return NodeIssue::MetadataUnknown;
    }
    return NodeIssue::None;
}

bool sortBefore(const FsNode* left,
                const FsNode* right,
                const ViewFilter& filter,
                const SortSpec& sort) {
    if (left == nullptr || right == nullptr) {
        return left != nullptr;
    }
    const MetricValue leftValue = metricValue(*left, sort.metric,
                                              filter.scanner_totals_filtered);
    const MetricValue rightValue = metricValue(*right, sort.metric,
                                               filter.scanner_totals_filtered);
    if (leftValue.known != rightValue.known) {
        // Unknown values are always last, including ascending views.
        return leftValue.known;
    }
    if (leftValue.known && leftValue.bytes != rightValue.bytes) {
        return sort.descending ? leftValue.bytes > rightValue.bytes
                               : leftValue.bytes < rightValue.bytes;
    }

    const NodeKey leftKey = nodeKey(*left);
    const NodeKey rightKey = nodeKey(*right);
    if (leftKey != rightKey) {
        return leftKey < rightKey;
    }
    // An empty path is only possible for hand-built fixtures. Keep their
    // ordering deterministic even when two entries share every key field.
    if (left->name != right->name) {
        return left->name < right->name;
    }
    return false;
}

std::optional<std::size_t> selectedIdentityIndex(
    const std::vector<const FsNode*>& selected, const FsNode& candidate) {
    if (!candidate.metadata.identity.valid) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < selected.size(); ++index) {
        const FileIdentity& identity = selected[index]->metadata.identity;
        if (identity.valid && identity == candidate.metadata.identity) {
            return index;
        }
    }
    return std::nullopt;
}

std::size_t worstSelectedIndex(const std::vector<const FsNode*>& selected,
                               const ViewFilter& filter,
                               const SortSpec& sort) {
    std::size_t worst = 0;
    for (std::size_t index = 1; index < selected.size(); ++index) {
        if (sortBefore(selected[worst], selected[index], filter, sort)) {
            worst = index;
        }
    }
    return worst;
}

void considerLargestCandidate(std::vector<const FsNode*>& selected,
                              const FsNode& candidate,
                              std::size_t limit,
                              const ViewFilter& filter,
                              const SortSpec& sort) {
    const std::optional<std::size_t> duplicate =
        selectedIdentityIndex(selected, candidate);
    if (duplicate.has_value()) {
        if (sortBefore(&candidate, selected[*duplicate], filter, sort)) {
            selected[*duplicate] = &candidate;
        }
        return;
    }
    if (selected.size() < limit) {
        selected.push_back(&candidate);
        return;
    }
    const std::size_t worst = worstSelectedIndex(selected, filter, sort);
    if (sortBefore(&candidate, selected[worst], filter, sort)) {
        selected[worst] = &candidate;
    }
}

} // namespace

std::string normalizedPath(const FsNode& node) {
    const std::filesystem::path source =
        node.path.empty() ? std::filesystem::path(node.name) : node.path;
    const std::filesystem::path normalized = source.lexically_normal();
    std::string result = normalized.generic_string();
    const std::string root = normalized.root_path().generic_string();
    while (result.size() > root.size() && !result.empty() && result.back() == '/') {
        result.pop_back();
    }
    return result;
}

FsKind nodeKind(const FsNode& node) {
    // Keep the directory entry's own type visible. A followed symlink's
    // target type remains available in target_metadata, while `followed`
    // explicitly records that traversal used it.
    if (node.metadata.kind != FsKind::Other || node.metadata.complete) {
        return node.metadata.kind;
    }
    return node.is_dir ? FsKind::Directory : FsKind::Other;
}

MetricValue metricValue(const FsNode& node,
                        SizeMetric metric,
                        bool scannerTotalsFiltered) {
    const bool structurallyKnown = node.complete && !node.cycle_skipped
                                   && !node.mount_boundary_skipped
                                   && !isDepthLimitError(node);
    switch (metric) {
    case SizeMetric::Logical:
        return MetricValue{
            node.size,
            node.logical_size_known && logicalSubtreeKnown(node, scannerTotalsFiltered),
        };
    case SizeMetric::Allocated:
        return MetricValue{node.allocated_size,
                           structurallyKnown && node.allocated_size_known
                               && !(scannerTotalsFiltered && node.is_dir)};
    case SizeMetric::Reclaimable:
        return MetricValue{node.reclaimable_size,
                           structurallyKnown && node.reclaimable_size_known
                               && !(scannerTotalsFiltered && node.is_dir)};
    }
    return MetricValue{};
}

bool NodeKey::operator==(const NodeKey& other) const {
    return normalized_path == other.normalized_path && kind == other.kind
           && followed == other.followed && compareIdentity(identity, other.identity) == 0;
}

bool NodeKey::operator<(const NodeKey& other) const { return lessNodeKey(*this, other); }

NodeKey nodeKey(const FsNode& node) {
    NodeKey key;
    key.normalized_path = normalizedPath(node);
    key.kind = nodeKind(node);
    key.followed = node.followed;
    if (node.metadata.identity.valid) {
        key.identity = node.metadata.identity;
    }
    return key;
}

NodeIssue classifyNodeIssue(const FsNode& node, bool scannerTotalsFiltered) {
    const NodeIssue structural = structuralIssue(node);
    if (structural != NodeIssue::None) {
        return structural;
    }
    if (!node.complete) {
        return NodeIssue::Incomplete;
    }
    const NodeIssue metadata = metadataIssue(node);
    if (metadata != NodeIssue::None) {
        return metadata;
    }
    if (!node.error.empty()) {
        return NodeIssue::Error;
    }
    if (scannerTotalsFiltered && node.is_dir) {
        return NodeIssue::ScannerFiltered;
    }
    return NodeIssue::None;
}

std::vector<const FsNode*> visibleChildren(const FsNode& node,
                                           const ViewFilter& filter,
                                           const SortSpec& sort) {
    std::vector<const FsNode*> visible;
    visible.reserve(node.children.size());
    for (const FsNode& child : node.children) {
        if (matchesFilter(child, filter)) {
            visible.push_back(&child);
        }
    }
    std::sort(visible.begin(), visible.end(), [&filter, &sort](const FsNode* left,
                                                                const FsNode* right) {
        return sortBefore(left, right, filter, sort);
    });
    return visible;
}

LargestFilesResult largestFiles(const FsNode& node,
                                std::size_t limit,
                                const ViewFilter& filter,
                                const SortSpec& sort) {
    LargestFilesResult result;
    result.complete = !filter.scanner_totals_filtered;
    if (limit == 0) {
        return result;
    }

    struct TraversalFrame {
        const FsNode* node;
        std::size_t nextChild = 0;
        bool inspected = false;
    };

    std::vector<TraversalFrame> stack{{&node}};
    while (!stack.empty()) {
        TraversalFrame& frame = stack.back();
        const FsNode& current = *frame.node;
        if (!frame.inspected) {
            frame.inspected = true;
            const NodeIssue issue =
                classifyNodeIssue(current, filter.scanner_totals_filtered);
            if (issue != NodeIssue::None) {
                result.complete = false;
                if (current.is_dir && issue != NodeIssue::ScannerFiltered) {
                    stack.pop_back();
                    continue;
                }
            }
            if (!current.is_dir) {
                if (issue == NodeIssue::None && isRegularFileEntry(current)
                    && matchesFilter(current, filter)) {
                    considerLargestCandidate(result.files, current, limit, filter, sort);
                }
                stack.pop_back();
                continue;
            }
        }
        if (frame.nextChild < current.children.size()) {
            const FsNode* child = &current.children[frame.nextChild++];
            stack.push_back(TraversalFrame{child});
            continue;
        }
        stack.pop_back();
    }

    std::sort(result.files.begin(), result.files.end(), [&filter, &sort](const FsNode* left,
                                                                           const FsNode* right) {
        return sortBefore(left, right, filter, sort);
    });
    return result;
}

} // namespace diskmap
