#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "diskmap/fs_node.hpp"

namespace diskmap {

// A view can choose between logical directory-entry bytes and the
// identity-aware physical totals computed by aggregateStorage().
enum class SizeMetric { Logical, Allocated, Reclaimable };

// A byte value is still meaningful when known is false: it is a conservative
// or saturated value carried by the aggregate helpers. Callers must inspect
// known before presenting bytes as an exact total. Physical values are exact
// per subtree but are not additive across sibling subtrees that contain the
// same hard-linked identity.
struct MetricValue {
    std::uint64_t bytes = 0;
    bool known = false;
    // Logical entry bytes are additive. Identity-aware physical metrics are
    // deliberately marked non-additive because the same hard-linked identity
    // can be present below more than one sibling subtree.
    bool additive = true;

    bool operator==(const MetricValue& other) const {
        return bytes == other.bytes && known == other.known && additive == other.additive;
    }
    bool operator!=(const MetricValue& other) const { return !(*this == other); }
};

// Returns a stable, lexical representation of a path. The result uses '/'
// separators even on Windows so it can be used in deterministic keys and in
// case-insensitive path searches. An empty synthetic FsNode path falls back to
// its name; scanner-produced nodes always carry a full path.
std::string normalizedPath(const FsNode& node);

// Returns the filesystem entry kind represented by a node. A followed symlink
// remains a symlink for filtering and diagnostics; its target kind is kept in
// target_metadata and `followed` records whether traversal used that target.
FsKind nodeKind(const FsNode& node);

// Resolves a selected metric. Logical known-ness combines the explicit
// overflow bit with complete subtree/cycle/depth/mount checks.
// `scannerTotalsFiltered` is deliberately explicit:
// scanner-level min-size/exclude filtering is not the same as a view filter,
// and this API does not infer one from the other. When set, aggregate values
// for directories are conservative; leaf metadata can remain known.
MetricValue metricValue(const FsNode& node,
                        SizeMetric metric,
                        bool scannerTotalsFiltered = false);

// A stable identity for view selection and row/tile reconciliation. Identity
// is included only when the entry's own metadata has a valid physical
// identity; an unavailable identity is not fabricated as (0, 0).
struct NodeKey {
    std::string normalized_path;
    FsKind kind = FsKind::Other;
    bool followed = false;
    std::optional<FileIdentity> identity;

    bool operator==(const NodeKey& other) const;
    bool operator!=(const NodeKey& other) const { return !(*this == other); }
    bool operator<(const NodeKey& other) const;
};

NodeKey nodeKey(const FsNode& node);

// Finds an exact key in a value-owned tree without recursion. nodePathByKey()
// returns root through target so GUI navigation can rebuild every skipped
// ancestor after a key-based activation. Missing keys return nullptr/empty.
const FsNode* findNodeByKey(const FsNode& root, const NodeKey& key);
std::vector<const FsNode*> nodePathByKey(const FsNode& root, const NodeKey& key);

// The first matching issue is deterministic and intentionally preserves the
// scanner's distinctions. A cycle can be complete because it was safely
// skipped; it therefore has priority over the generic incomplete state.
enum class NodeIssue {
    None,
    Incomplete,
    CycleSkipped,
    MountBoundarySkipped,
    DepthLimitReached,
    MetadataUnknown,
    Error,
    ScannerFiltered,

    // Short aliases keep call sites readable without changing the serialized
    // classification names above.
    Cycle = CycleSkipped,
    MountBoundary = MountBoundarySkipped,
    DepthLimit = DepthLimitReached,
    UnknownMetadata = MetadataUnknown,
    Filtered = ScannerFiltered,
};

NodeIssue classifyNodeIssue(const FsNode& node, bool scannerTotalsFiltered = false);

// Filters are conjunctive: every configured dimension must match. `query` and
// `size_metric` are compatibility aliases for the canonical `search` and
// `metric` fields; if both aliases are populated, the canonical field wins.
// Search is a literal, case-insensitive substring against both basename and
// normalized full path (not a wildcard or regular expression).
struct ViewFilter {
    std::string search;
    std::string query;
    std::optional<SizeMetric> metric;
    std::optional<SizeMetric> size_metric;

    std::optional<std::uint64_t> min_size;
    std::optional<std::uint64_t> max_size;
    std::optional<std::uint64_t> minimum_size;
    std::optional<std::uint64_t> maximum_size;
    std::optional<std::uint64_t> min_bytes;
    std::optional<std::uint64_t> max_bytes;

    // Modified times use the same nanosecond epoch representation as
    // FsMetadata::modified_ns. Bounds are inclusive. The *_modified_ns and
    // newer/older aliases are accepted for callers that prefer those names.
    std::optional<std::int64_t> modified_after_ns;
    std::optional<std::int64_t> modified_before_ns;
    std::optional<std::int64_t> min_modified_ns;
    std::optional<std::int64_t> max_modified_ns;
    std::optional<std::int64_t> newer_than_ns;
    std::optional<std::int64_t> older_than_ns;

    // Type filters are optional. `kinds` is an OR allow-list within the type
    // dimension; all other configured dimensions still apply as AND filters.
    std::optional<FsKind> kind;
    std::optional<FsKind> type;
    std::vector<FsKind> kinds;

    // Issue filters are likewise an OR allow-list. They are useful to an
    // issues-first view while retaining the underlying node's exact flags.
    std::optional<NodeIssue> issue;
    std::vector<NodeIssue> issues;

    // Copy ScanResult::totals_filtered here when projecting an aggregate.
    // It is intentionally not inferred from min_size/max_size or search.
    bool scanner_totals_filtered = false;
};

struct SortSpec {
    SizeMetric metric = SizeMetric::Logical;
    bool descending = true;
};

// Returns direct children that satisfy all ViewFilter dimensions, ordered by
// SortSpec. The input tree is never mutated. Returned pointers remain valid
// only while the source tree exists and its node vectors are not mutated.
std::vector<const FsNode*> visibleChildren(const FsNode& node,
                                           const ViewFilter& filter = {},
                                           const SortSpec& sort = {});

struct LargestFilesResult {
    std::vector<const FsNode*> files;
    // False when a structural/metadata issue or scanner-side filtering means
    // the returned candidates cannot be claimed as an exhaustive ranking.
    bool complete = true;
};

// Returns at most limit regular-file nodes observed in the tree. Incomplete,
// metadata-unknown, errored, cycle, mount, and depth-pruned branches are not
// traversed. Valid physical identities are deduplicated, both traversal and
// candidate memory are bounded by tree depth and limit respectively, and
// `complete` exposes whether the list is exhaustive. It is also false when an
// observed candidate cannot be evaluated exactly against an active size/age
// predicate or the selected sort metric. A zero limit still traverses the tree
// to return truthful completeness evidence, while retaining no candidates.
// Symlink entries are not regular files, even when followed; this prevents a
// largest-files list from presenting a link alias as an additional file.
// Filtering is applied to each file, while directories are traversed even if
// they do not match search (a descendant's full path may match).
// Returned pointers follow the same lifetime rule as visibleChildren().
LargestFilesResult largestFiles(const FsNode& node,
                                std::size_t limit,
                                const ViewFilter& filter = {},
                                const SortSpec& sort = {});

// Alternate argument orders keep the projection convenient for table/list
// callers that put their filter and sort state before the result limit.
inline LargestFilesResult largestFiles(const FsNode& node,
                                       const ViewFilter& filter,
                                       const SortSpec& sort,
                                       std::size_t limit) {
    return largestFiles(node, limit, filter, sort);
}

inline LargestFilesResult largestFiles(const FsNode& node,
                                       const ViewFilter& filter,
                                       std::size_t limit) {
    return largestFiles(node, limit, filter, SortSpec{});
}

} // namespace diskmap
