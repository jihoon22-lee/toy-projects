#include "diskmap/cleanup.hpp"

#include <array>
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace diskmap {

namespace {

namespace fs = std::filesystem;

constexpr std::array<const char*, 17> kCleanupSkipMessages = {
    "",
    "selected entry is absent from the retained scan",
    "the scan root cannot be staged for cleanup",
    "the entry is inside a protected root",
    "mount roots and observed mount boundaries are protected",
    "descendants reached through a symlink cannot be staged",
    "the entry has incomplete scan or metadata evidence",
    "scanner filtering makes cleanup evidence incomplete",
    "the entry belongs to a stale scan generation",
    "the entry identity or metadata is unknown",
    "the filesystem entry type is not supported for cleanup",
    "an already staged directory covers this descendant",
    "the entry no longer exists or cannot be inspected",
    "the filesystem identity changed after scanning",
    "the filesystem entry type changed after scanning",
    "the filesystem size changed after scanning",
    "the filesystem hard-link count changed after scanning",
};

constexpr std::array<const char*, 17> kCleanupSkipReasonNames = {
    "none",
    "missing-selection",
    "root-target",
    "protected-root",
    "mount-boundary",
    "symlink-descendant",
    "incomplete-scan",
    "scanner-filtered",
    "stale-generation",
    "metadata-unknown",
    "unsupported-type",
    "covered-by-parent",
    "missing",
    "identity-changed",
    "type-changed",
    "size-changed",
    "hard-link-changed",
};

fs::path normalizedAbsolute(const fs::path& value) {
    std::error_code error;
    const fs::path absolute = fs::absolute(value, error);
    return (error ? value : absolute).lexically_normal();
}

bool pathContains(const fs::path& parent, const fs::path& child) {
    const fs::path normalizedParent = normalizedAbsolute(parent);
    const fs::path normalizedChild = normalizedAbsolute(child);
    auto parentPart = normalizedParent.begin();
    auto childPart = normalizedChild.begin();
    for (; parentPart != normalizedParent.end(); ++parentPart, ++childPart) {
        if (childPart == normalizedChild.end() || *parentPart != *childPart) {
            return false;
        }
    }
    return true;
}

bool exactPath(const fs::path& left, const fs::path& right) {
    return normalizedAbsolute(left) == normalizedAbsolute(right);
}

bool protectedPath(const fs::path& path, const CleanupPolicy& policy) {
    const fs::path normalized = normalizedAbsolute(path);
    if (policy.protect_filesystem_roots && !normalized.root_path().empty()
        && normalized == normalized.root_path()) {
        return true;
    }
    if (policy.protect_home_root) {
#ifdef _WIN32
        const char* home = std::getenv("USERPROFILE");
#else
        const char* home = std::getenv("HOME");
#endif
        if (home != nullptr && *home != '\0' && exactPath(home, normalized)) {
            return true;
        }
    }
    for (const fs::path& root : policy.protected_roots) {
        if (exactPath(root, normalized)
            || (policy.protect_subtrees && pathContains(root, normalized))) {
            return true;
        }
    }
    return false;
}

bool mountRoot(const fs::path& path, const CleanupPolicy& policy) {
    return std::any_of(policy.mount_roots.begin(), policy.mount_roots.end(),
                       [&](const fs::path& root) { return exactPath(root, path); });
}

std::size_t pathDepth(const fs::path& path) {
    const fs::path normalized = path.lexically_normal();
    return static_cast<std::size_t>(std::distance(normalized.begin(), normalized.end()));
}

bool selectionBefore(const NodeKey& left, const NodeKey& right) {
    const std::size_t leftDepth = pathDepth(left.normalized_path);
    const std::size_t rightDepth = pathDepth(right.normalized_path);
    if (leftDepth != rightDepth) {
        return leftDepth < rightDepth;
    }
    return left < right;
}

CleanupRejectedTarget rejection(const NodeKey& key,
                                const fs::path& path,
                                CleanupSkipReason reason,
                                std::string message) {
    CleanupRejectedTarget result;
    result.key = key;
    result.path = path;
    result.reason = reason;
    result.message = std::move(message);
    return result;
}

bool subtreeComplete(const FsNode& root) {
    std::vector<const FsNode*> pending{&root};
    while (!pending.empty()) {
        const FsNode* node = pending.back();
        pending.pop_back();
        if (!node->complete || node->cycle_skipped || node->mount_boundary_skipped
            || !node->metadata.complete || !node->metadata.identity.valid) {
            return false;
        }
        if (nodeKind(*node) == FsKind::Symlink) {
            continue;
        }
        for (const FsNode& child : node->children) {
            pending.push_back(&child);
        }
    }
    return true;
}

bool cleanupEvidenceComplete(const FsNode& node) {
    if (!node.metadata.complete || !node.metadata.identity.valid) {
        return false;
    }
    // A followed symlink is moved as a link entry. Its target descendants are
    // deliberately irrelevant to the cleanup evidence for that link.
    if (nodeKind(node) == FsKind::Symlink) {
        return true;
    }
    return node.complete && subtreeComplete(node);
}

bool hasSymlinkAncestor(const FsNode& root, const NodeKey& key) {
    const std::vector<const FsNode*> path = nodePathByKey(root, key);
    if (path.empty()) {
        return false;
    }
    return std::any_of(path.begin(), path.end() - 1, [](const FsNode* node) {
        return nodeKind(*node) == FsKind::Symlink;
    });
}

bool coveredByAcceptedDirectory(const fs::path& path,
                                const std::set<fs::path>& acceptedDirectories) {
    fs::path parent = normalizedAbsolute(path).parent_path();
    while (!parent.empty()) {
        if (acceptedDirectories.find(parent) != acceptedDirectories.end()) {
            return true;
        }
        const fs::path next = parent.parent_path();
        if (next == parent) {
            return false;
        }
        parent = next;
    }
    return false;
}

CleanupTarget targetFromNode(const FsNode& node, std::uint64_t generation) {
    CleanupTarget target;
    target.key = nodeKey(node);
    target.path = normalizedAbsolute(node.path);
    target.kind = nodeKind(node);
    target.identity = node.metadata.identity;
    target.logical_size = node.metadata.logical_size;
    target.allocated_size = node.metadata.allocated_size;
    target.hard_link_count = node.metadata.hard_link_count;
    target.allocated_size_known = node.metadata.allocated_size_known;
    target.hard_link_count_known = node.metadata.hard_link_count_known;
    target.symlink = target.kind == FsKind::Symlink;
    target.scan_generation = generation;
    return target;
}

struct StorageIdentity {
    std::uint64_t device = 0;
    std::uint64_t file = 0;
    FsKind kind = FsKind::Other;

    bool operator<(const StorageIdentity& other) const {
        if (device != other.device) {
            return device < other.device;
        }
        if (file != other.file) {
            return file < other.file;
        }
        return static_cast<int>(kind) < static_cast<int>(other.kind);
    }
};

struct SelectedStorage {
    std::uint64_t allocated = 0;
    std::uint64_t expected_links = 0;
    std::size_t selected_links = 0;
    bool allocated_known = false;
    bool links_known = false;
    bool consistent = true;
};

void collectStorage(const FsNode& root,
                    std::map<StorageIdentity, SelectedStorage>& identities,
                    bool& known) {
    std::vector<const FsNode*> pending{&root};
    while (!pending.empty()) {
        const FsNode* node = pending.back();
        pending.pop_back();
        if (nodeKind(*node) != FsKind::Symlink) {
            for (const FsNode& child : node->children) {
                pending.push_back(&child);
            }
        }
        if (nodeKind(*node) == FsKind::Directory) {
            continue;
        }
        const FsMetadata& metadata = node->metadata;
        if (!metadata.complete || !metadata.identity.valid
            || !metadata.allocated_size_known || !metadata.hard_link_count_known
            || metadata.hard_link_count == 0) {
            known = false;
            continue;
        }
        const StorageIdentity identity{metadata.identity.device,
                                       metadata.identity.file,
                                       nodeKind(*node)};
        SelectedStorage& selected = identities[identity];
        if (selected.selected_links != 0
            && (selected.allocated != metadata.allocated_size
                || selected.expected_links != metadata.hard_link_count)) {
            selected.consistent = false;
            known = false;
        }
        selected.allocated = metadata.allocated_size;
        selected.expected_links = metadata.hard_link_count;
        selected.allocated_known = true;
        selected.links_known = true;
        ++selected.selected_links;
    }
}

void computeReclaimable(const ScanResult& scan, CleanupPlan& plan) {
    std::map<StorageIdentity, SelectedStorage> identities;
    bool known = true;
    for (const CleanupTarget& target : plan.targets) {
        const FsNode* node = findNodeByKey(scan.root, target.key);
        if (node == nullptr) {
            known = false;
            continue;
        }
        collectStorage(*node, identities, known);
    }
    std::uint64_t total = 0;
    for (const auto& item : identities) {
        const SelectedStorage& selected = item.second;
        if (!selected.consistent || !selected.allocated_known || !selected.links_known) {
            known = false;
            continue;
        }
        if (selected.selected_links != selected.expected_links) {
            continue;
        }
        if (selected.allocated > std::numeric_limits<std::uint64_t>::max() - total) {
            total = std::numeric_limits<std::uint64_t>::max();
            known = false;
            break;
        }
        total += selected.allocated;
    }
    plan.reclaimable_bytes = total;
    plan.reclaimable_bytes_known = known;
}

CleanupSkipReason basicRejection(const ScanResult& scan,
                                 const FsNode& node,
                                 const CleanupPolicy& policy) {
    if (nodeKey(scan.root) == nodeKey(node)) {
        return CleanupSkipReason::RootTarget;
    }
    if (protectedPath(node.path, policy)) {
        return CleanupSkipReason::ProtectedRoot;
    }
    if (node.mount_boundary_skipped || mountRoot(node.path, policy)) {
        return CleanupSkipReason::MountBoundary;
    }
    if (scan.cancelled || !scan.fatal_error.empty() || !cleanupEvidenceComplete(node)) {
        return CleanupSkipReason::IncompleteScan;
    }
    if (scan.totals_filtered) {
        return CleanupSkipReason::ScannerFiltered;
    }
    if (node.scan_generation != scan.generation) {
        return CleanupSkipReason::StaleGeneration;
    }
    const FsKind kind = nodeKind(node);
    if (kind != FsKind::RegularFile && kind != FsKind::Directory
        && kind != FsKind::Symlink) {
        return CleanupSkipReason::UnsupportedType;
    }
    return CleanupSkipReason::None;
}

std::string skipMessage(CleanupSkipReason reason) {
    const auto index = static_cast<std::size_t>(reason);
    if (index < kCleanupSkipMessages.size()) {
        return kCleanupSkipMessages[index];
    }
    return "cleanup target was rejected";
}

} // namespace

const char* cleanupSkipReasonName(CleanupSkipReason reason) {
    const auto index = static_cast<std::size_t>(reason);
    if (index < kCleanupSkipReasonNames.size()) {
        return kCleanupSkipReasonNames[index];
    }
    return "unknown";
}

CleanupPlan planCleanup(const ScanResult& scan,
                        const std::vector<NodeKey>& selected,
                        const CleanupPolicy& policy) {
    if (policy.max_selected == 0 || selected.size() > policy.max_selected) {
        throw std::invalid_argument("cleanup selection exceeds the configured bound");
    }
    CleanupPlan plan;
    plan.scan_generation = scan.generation;
    std::vector<NodeKey> ordered = selected;
    std::sort(ordered.begin(), ordered.end(), selectionBefore);
    ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());

    std::set<fs::path> acceptedDirectories;
    for (const NodeKey& key : ordered) {
        const FsNode* node = findNodeByKey(scan.root, key);
        if (node == nullptr) {
            plan.rejected.push_back(rejection(
                key, key.normalized_path, CleanupSkipReason::MissingSelection,
                skipMessage(CleanupSkipReason::MissingSelection)));
            continue;
        }
        CleanupSkipReason reason = basicRejection(scan, *node, policy);
        if (reason == CleanupSkipReason::None && hasSymlinkAncestor(scan.root, key)) {
            reason = CleanupSkipReason::SymlinkDescendant;
        }
        if (reason == CleanupSkipReason::None
            && coveredByAcceptedDirectory(node->path, acceptedDirectories)) {
            reason = CleanupSkipReason::CoveredByParent;
        }
        if (reason != CleanupSkipReason::None) {
            plan.rejected.push_back(
                rejection(key, node->path, reason, skipMessage(reason)));
            continue;
        }
        CleanupTarget target = targetFromNode(*node, scan.generation);
        if (target.kind == FsKind::Directory) {
            acceptedDirectories.insert(target.path);
        }
        plan.targets.push_back(std::move(target));
    }
    computeReclaimable(scan, plan);
    return plan;
}

CleanupRevalidation revalidateCleanupTarget(const CleanupTarget& target,
                                            const FsSource& source) {
    CleanupRevalidation result;
    result.current = source.inspect(target.path, false);
    if (!result.current.complete) {
        result.reason = CleanupSkipReason::Missing;
    } else if (!result.current.identity.valid || !target.identity.valid
               || result.current.identity != target.identity) {
        result.reason = CleanupSkipReason::IdentityChanged;
    } else if (result.current.kind != target.kind) {
        result.reason = CleanupSkipReason::TypeChanged;
    } else if (result.current.logical_size != target.logical_size
               || (target.allocated_size_known
                   && (!result.current.allocated_size_known
                       || result.current.allocated_size != target.allocated_size))) {
        result.reason = CleanupSkipReason::SizeChanged;
    } else if (target.hard_link_count_known
               && (!result.current.hard_link_count_known
                   || result.current.hard_link_count != target.hard_link_count)) {
        result.reason = CleanupSkipReason::HardLinkChanged;
    } else {
        result.accepted = true;
        result.reason = CleanupSkipReason::None;
    }
    result.message = skipMessage(result.reason);
    return result;
}

} // namespace diskmap
