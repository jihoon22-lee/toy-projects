#pragma once

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "diskmap/scanner.hpp"
#include "diskmap/view.hpp"

namespace diskmap {

// Cleanup planning is deliberately independent from any trash implementation.
// A plan contains value-owned scan evidence and is never permission to delete;
// every accepted target must be revalidated immediately before a recoverable
// trash backend moves it.
enum class CleanupSkipReason {
    None,
    MissingSelection,
    RootTarget,
    ProtectedRoot,
    MountBoundary,
    SymlinkDescendant,
    IncompleteScan,
    ScannerFiltered,
    StaleGeneration,
    MetadataUnknown,
    UnsupportedType,
    CoveredByParent,
    Missing,
    IdentityChanged,
    TypeChanged,
    SizeChanged,
    HardLinkChanged,
};

const char* cleanupSkipReasonName(CleanupSkipReason reason);

struct CleanupPolicy {
    // Exact roots and, when protect_subtrees is true, their descendants are
    // protected using lexical path-component comparison (never string prefix).
    std::vector<std::filesystem::path> protected_roots;
    // Mount roots can be supplied by a platform adapter. The selected scan
    // root and scanner-observed mount boundaries are always rejected.
    std::vector<std::filesystem::path> mount_roots;
    bool protect_subtrees = true;
    bool protect_home_root = true;
    bool protect_filesystem_roots = true;
    std::size_t max_selected = 100'000;
};

struct CleanupTarget {
    NodeKey key;
    std::filesystem::path path;
    FsKind kind = FsKind::Other;
    FileIdentity identity;
    std::uint64_t logical_size = 0;
    std::uint64_t allocated_size = 0;
    std::uint64_t hard_link_count = 0;
    bool allocated_size_known = false;
    bool hard_link_count_known = false;
    bool symlink = false;
    std::uint64_t scan_generation = 0;
};

struct CleanupRejectedTarget {
    NodeKey key;
    std::filesystem::path path;
    CleanupSkipReason reason = CleanupSkipReason::MissingSelection;
    std::string message;
};

struct CleanupPlan {
    std::vector<CleanupTarget> targets;
    std::vector<CleanupRejectedTarget> rejected;
    std::uint64_t reclaimable_bytes = 0;
    bool reclaimable_bytes_known = true;
    std::uint64_t scan_generation = 0;
};

// Normalizes a multi-selection in deterministic parent-first order. Directory
// selections cover descendants; symlinks never do. Unsafe or incomplete
// entries remain visible in `rejected` with a stable reason.
CleanupPlan planCleanup(const ScanResult& scan,
                        const std::vector<NodeKey>& selected,
                        const CleanupPolicy& policy = CleanupPolicy{});

struct CleanupRevalidation {
    bool accepted = false;
    CleanupSkipReason reason = CleanupSkipReason::Missing;
    std::string message;
    FsMetadata current;
};

// Reads the entry itself (`follow=false`) and requires the scan identity,
// entry kind, size, allocation evidence, and hard-link count to remain stable.
// This function is a final core check; a mutating backend still needs an
// anchored no-follow operation to close the pathname race.
CleanupRevalidation revalidateCleanupTarget(const CleanupTarget& target,
                                            const FsSource& source);

} // namespace diskmap
