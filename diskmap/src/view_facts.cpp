#include "diskmap/view.hpp"

#include <string>
#include <vector>

namespace diskmap {

namespace {

bool isDepthLimitError(const FsNode& node) {
    static const std::string marker = "scan depth limit reached";
    return node.error.find(marker) != std::string::npos;
}

bool metadataComplete(const FsNode& node) {
    return node.metadata.complete
           && (!node.followed || (node.has_target_metadata && node.target_metadata.complete));
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
        if (!node.complete || !metadataComplete(node) || !node.logical_size_known
            || node.cycle_skipped || node.mount_boundary_skipped
            || isDepthLimitError(node)) {
            return false;
        }
        // ScanResult::totals_filtered is a global fact, not a view predicate.
        // A leaf remains exact, but a directory has no per-subtree filter map.
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

bool physicalSubtreeKnown(const FsNode& root,
                          SizeMetric metric,
                          bool scannerTotalsFiltered) {
    std::vector<const FsNode*> stack{&root};
    while (!stack.empty()) {
        const FsNode* node = stack.back();
        stack.pop_back();
        const bool valueKnown = metric == SizeMetric::Allocated
                                    ? node->allocated_size_known
                                    : node->reclaimable_size_known;
        if (!node->complete || !metadataComplete(*node) || !valueKnown
            || node->cycle_skipped || node->mount_boundary_skipped
            || isDepthLimitError(*node)
            || (scannerTotalsFiltered && node->is_dir)) {
            return false;
        }
        for (const FsNode& child : node->children) {
            stack.push_back(&child);
        }
    }
    return true;
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
    switch (metric) {
    case SizeMetric::Logical:
        return MetricValue{
            node.size,
            node.logical_size_known && logicalSubtreeKnown(node, scannerTotalsFiltered),
            true,
        };
    case SizeMetric::Allocated:
        return MetricValue{
            node.allocated_size,
            physicalSubtreeKnown(node, metric, scannerTotalsFiltered),
            false,
        };
    case SizeMetric::Reclaimable:
        return MetricValue{
            node.reclaimable_size,
            physicalSubtreeKnown(node, metric, scannerTotalsFiltered),
            false,
        };
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

} // namespace diskmap
