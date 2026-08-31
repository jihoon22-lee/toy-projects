#include "diskmap/scanner.hpp"

#include <filesystem>
#include <set>
#include <utility>

namespace diskmap {

namespace {

struct WorkItem {
    std::filesystem::path path;
    int depth = 0;
    FsNode* node = nullptr;
};

enum class RootDisposition {
    Traverse,
    Complete,
};

using IdentityKey = std::pair<std::uint64_t, std::uint64_t>;

void recordError(ScanResult& result, const ScanOptions& options, std::string message) {
    ++result.error_count;
    if (result.errors.size() < options.max_errors) {
        result.errors.push_back(std::move(message));
    } else {
        result.errors_truncated = true;
    }
}

std::string lastPathComponent(const std::filesystem::path& path) {
    const std::filesystem::path normalized = path.lexically_normal();
    if (normalized.empty()) {
        return std::string();
    }
    const std::filesystem::path filename = normalized.filename();
    if (!filename.empty()) {
        return filename.string();
    }
    const std::filesystem::path parent = normalized.parent_path();
    if (parent != normalized && !parent.filename().empty()) {
        return parent.filename().string();
    }
    return normalized.root_path().string();
}

bool depthIsExpandable(int depth, int maxDepth) {
    const int effectiveLimit =
        maxDepth < 0 || maxDepth > kMaxTreeDepth ? kMaxTreeDepth : maxDepth;
    return depth <= effectiveLimit;
}

bool wildcardMatches(const std::string& pattern, const std::string& value) {
    std::size_t patternIndex = 0;
    std::size_t valueIndex = 0;
    std::size_t starIndex = std::string::npos;
    std::size_t starValueIndex = 0;

    while (valueIndex < value.size()) {
        if (patternIndex < pattern.size()
            && (pattern[patternIndex] == '?' || pattern[patternIndex] == value[valueIndex])) {
            ++patternIndex;
            ++valueIndex;
            continue;
        }
        if (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
            starIndex = patternIndex++;
            starValueIndex = valueIndex;
            continue;
        }
        if (starIndex != std::string::npos) {
            patternIndex = starIndex + 1;
            valueIndex = ++starValueIndex;
            continue;
        }
        return false;
    }
    while (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
        ++patternIndex;
    }
    return patternIndex == pattern.size();
}

std::filesystem::path entryPath(const DirEntry& entry,
                                const std::filesystem::path& parentPath) {
    return entry.path.empty() ? parentPath / std::filesystem::path(entry.name) : entry.path;
}

bool excludedByPattern(const DirEntry& entry,
                       const std::filesystem::path& parentPath,
                       const std::filesystem::path& rootPath,
                       const ScanOptions& options) {
    const std::filesystem::path path = entryPath(entry, parentPath).lexically_normal();
    const std::string relative = path.lexically_relative(rootPath.lexically_normal()).generic_string();
    for (const std::string& pattern : options.exclude_patterns) {
        if (wildcardMatches(pattern, entry.name)
            || (!relative.empty() && wildcardMatches(pattern, relative))) {
            return true;
        }
    }
    return false;
}

bool entrySkipped(const DirEntry& entry,
                  const std::filesystem::path& parentPath,
                  const std::filesystem::path& rootPath,
                  const ScanOptions& options) {
    if (!entry.is_dir && !entry.is_symlink && entry.size < options.min_size) {
        return true;
    }
    return excludedByPattern(entry, parentPath, rootPath, options);
}

const FsMetadata& effectiveMetadata(const FsNode& node) {
    return node.followed && node.has_target_metadata ? node.target_metadata : node.metadata;
}

void inspectRootSymlink(const FsSource& source,
                        const std::filesystem::path& rootPath,
                        const ScanOptions& options,
                        ScanResult& result) {
    result.root.followed = true;
    result.root.target_metadata = source.inspect(rootPath, true);
    result.root.has_target_metadata = result.root.target_metadata.complete;
    result.root.is_dir = result.root.has_target_metadata
                         && result.root.target_metadata.kind == FsKind::Directory;
    result.root.complete = result.root.has_target_metadata;
    result.root.error = result.root.target_metadata.error;
    if (result.root.has_target_metadata) {
        return;
    }
    if (result.root.error.empty()) {
        result.root.error = "cannot inspect symlink target '" + rootPath.string() + "'";
    }
    recordError(result, options, result.root.error);
}

bool inspectRoot(const FsSource& source,
                 const std::filesystem::path& rootPath,
                 const ScanOptions& options,
                 ScanResult& result) {
    const FsMetadata metadata = source.inspect(rootPath, false);
    if (metadata.complete) {
        result.root.metadata = metadata;
        result.root.is_dir = metadata.kind == FsKind::Directory;
        if (metadata.kind == FsKind::Symlink) {
            inspectRootSymlink(source, rootPath, options, result);
        }
        return true;
    }
    if (!metadata.error.empty()) {
        result.root.complete = false;
        result.root.error = metadata.error;
        recordError(result, options, metadata.error);
    }
    return false;
}

void finalizeLeafRoot(ScanResult& result, const ProgressFn& progress) {
    const FsMetadata& metadata = effectiveMetadata(result.root);
    if (metadata.complete && metadata.kind == FsKind::RegularFile) {
        result.root.size = metadata.logical_size;
    }
    ++result.files_scanned;
    aggregateSizes(result.root);
    aggregateStorage(result.root);
    if (progress) {
        progress(result.dirs_scanned, result.files_scanned);
    }
}

FsNode makeChildNode(const DirEntry& entry,
                     const std::filesystem::path& parentPath,
                     const ScanOptions& options) {
    FsNode child;
    child.name = entry.name;
    child.path = entryPath(entry, parentPath);
    child.followed = entry.is_symlink && options.follow_symlinks;
    child.is_dir = child.followed ? entry.is_dir : entry.is_dir && !entry.is_symlink;
    child.size = child.is_dir
                     ? 0
                     : entry.is_symlink && !child.followed ? entry.metadata.logical_size
                                                           : entry.size;
    child.metadata = entry.metadata;
    child.has_target_metadata = entry.has_target_metadata;
    child.target_metadata = entry.target_metadata;
    child.scan_generation = options.generation;
    child.complete = entry.metadata.complete
                     && (!child.followed
                         || (entry.has_target_metadata && entry.target_metadata.complete));
    child.error = entry.metadata.error;
    if (child.followed && !entry.has_target_metadata) {
        child.complete = false;
        child.error = entry.target_metadata.error;
    }
    return child;
}

bool rememberDirectoryIdentity(FsNode& child,
                               std::set<IdentityKey>& visited,
                               const ScanOptions& options,
                               ScanResult& result) {
    const FileIdentity& identity = effectiveMetadata(child).identity;
    if (child.followed && !identity.valid) {
        child.complete = false;
        child.error = "cannot safely follow directory symlink '" + child.path.string()
                      + "': target identity is unavailable";
        recordError(result, options, child.error);
        return false;
    }
    if (!identity.valid) {
        return true;
    }
    const IdentityKey key{identity.device, identity.file};
    if (!visited.insert(key).second) {
        child.cycle_skipped = true;
        return false;
    }
    return true;
}

bool isDirectoryForPass(const FsNode& child, bool followedPass) {
    return child.is_dir && child.followed == followedPass;
}

bool crossesMountBoundary(const FileIdentity& identity,
                          const FileIdentity& rootIdentity,
                          const ScanOptions& options) {
    return options.one_file_system
           && (!rootIdentity.valid || !identity.valid
               || identity.device != rootIdentity.device);
}

void markMountBoundary(FsNode& child,
                       const FileIdentity& identity,
                       const FileIdentity& rootIdentity,
                       const ScanOptions& options,
                       ScanResult& result) {
    child.complete = false;
    child.mount_boundary_skipped = identity.valid && rootIdentity.valid;
    child.error = child.mount_boundary_skipped
                      ? "mount boundary excluded"
                      : "cannot verify mount boundary: directory identity is unavailable";
    if (child.mount_boundary_skipped) {
        ++result.mount_boundaries_skipped;
        return;
    }
    recordError(result, options, child.error + " ('" + child.path.string() + "')");
}

bool directoryMayBeScheduled(FsNode& child,
                             const FileIdentity& rootIdentity,
                             const ScanOptions& options,
                             ScanResult& result,
                             std::set<IdentityKey>& visited) {
    const FileIdentity& identity = effectiveMetadata(child).identity;
    if (crossesMountBoundary(identity, rootIdentity, options)) {
        markMountBoundary(child, identity, rootIdentity, options, result);
        return false;
    }
    return rememberDirectoryIdentity(child, visited, options, result);
}

void scheduleDirectoryPass(std::vector<FsNode>& children,
                           bool followedPass,
                           int childDepth,
                           const ScanOptions& options,
                           const FileIdentity& rootIdentity,
                           ScanResult& result,
                           std::vector<WorkItem>& stack,
    std::set<IdentityKey>& visited) {
    for (FsNode& child : children) {
        if (!isDirectoryForPass(child, followedPass)) {
            continue;
        }
        if (!directoryMayBeScheduled(child, rootIdentity, options, result, visited)) {
            continue;
        }
        stack.push_back(WorkItem{child.path, childDepth, &child});
    }
}

// Fills node.children from entries (skipping symlinks/undersized files per
// options) and pushes follow-up work items for any subdirectories found.
// Children are appended in a single pass before any pointer into the vector
// is taken, so the pointers handed to later work items stay valid.
void expandDirectory(WorkItem& item,
                      const std::vector<DirEntry>& entries,
                      const std::filesystem::path& rootPath,
                      const ScanOptions& options,
                      const FileIdentity& rootIdentity,
                      ScanResult& result,
                      std::vector<WorkItem>& stack,
                      std::set<IdentityKey>& visited,
                      const ScanCancellationToken* cancellation) {
    item.node->children.reserve(entries.size());
    for (const DirEntry& entry : entries) {
        if (cancellation != nullptr && cancellation->isCancelled()) {
            break;
        }
        if (entrySkipped(entry, item.path, rootPath, options)) {
            ++result.entries_filtered;
            result.totals_filtered = true;
            continue;
        }
        FsNode child = makeChildNode(entry, item.path, options);
        if (!child.complete && !child.error.empty()) {
            recordError(result, options,
                        child.error + " ('" + child.path.string() + "')");
        }
        item.node->children.push_back(std::move(child));
        if (!item.node->children.back().is_dir) {
            ++result.files_scanned;
        }
    }
    // Prefer real directory entries to symlink aliases. This makes the stable
    // lexical source order choose the canonical entry before an alias that
    // resolves to the same identity.
    scheduleDirectoryPass(item.node->children, false, item.depth + 1, options, rootIdentity,
                          result, stack, visited);
    scheduleDirectoryPass(item.node->children, true, item.depth + 1, options, rootIdentity,
                          result, stack, visited);
}

void markCancelled(ScanResult& result, std::vector<WorkItem>& stack) {
    result.cancelled = true;
    result.root.complete = false;
    if (result.root.error.empty()) {
        result.root.error = "scan cancelled before completion";
    }
    for (WorkItem& pending : stack) {
        pending.node->complete = false;
        if (pending.node->error.empty()) {
            pending.node->error = "scan cancelled before directory was visited";
        }
    }
}

bool cancellationRequested(const ScanCancellationToken* cancellation) {
    return cancellation != nullptr && cancellation->isCancelled();
}

void initializeResult(ScanResult& result,
                      const std::filesystem::path& rootPath,
                      const ScanOptions& options) {
    result.generation = options.generation;
    result.root.name = lastPathComponent(rootPath);
    result.root.path = rootPath;
    result.root.is_dir = true;
    result.root.scan_generation = options.generation;
    result.root.metadata.kind = FsKind::Directory;
    result.root.metadata.complete = true;
}

RootDisposition resolveRoot(const FsSource& source,
                            const std::filesystem::path& rootPath,
                            const ScanOptions& options,
                            const ProgressFn& progress,
                            const ScanCancellationToken* cancellation,
                            ScanResult& result) {
    const bool rootKindResolved = inspectRoot(source, rootPath, options, result);
    if (!rootKindResolved && !result.root.error.empty()) {
        result.fatal_error = result.root.error;
        return RootDisposition::Complete;
    }
    if (!rootKindResolved || result.root.is_dir) {
        return RootDisposition::Traverse;
    }
    if (!result.root.complete && !result.root.error.empty()) {
        result.fatal_error = result.root.error;
        return RootDisposition::Complete;
    }
    if (cancellationRequested(cancellation)) {
        result.cancelled = true;
        result.root.complete = false;
        result.root.error = "scan cancelled before completion";
        return RootDisposition::Complete;
    }
    finalizeLeafRoot(result, progress);
    return RootDisposition::Complete;
}

CancellationCheck cancellationCheck(const ScanCancellationToken* cancellation) {
    if (cancellation == nullptr) {
        return CancellationCheck();
    }
    return [cancellation]() { return cancellation->isCancelled(); };
}

void recordListingOutcome(WorkItem& item,
                          const std::vector<DirEntry>& entries,
                          const std::string& error,
                          const ScanOptions& options,
                          ScanResult& result) {
    if (error.empty()) {
        ++result.dirs_scanned;
        return;
    }
    item.node->complete = false;
    item.node->error = error;
    recordError(result, options, error);
    if (item.depth == 0 && entries.empty()) {
        result.fatal_error = error;
    }
}

bool processWorkItem(const FsSource& source,
                     WorkItem item,
                     const std::filesystem::path& rootPath,
                     const ScanOptions& options,
                     const FileIdentity& rootIdentity,
                     const ProgressFn& progress,
                     const ScanCancellationToken* cancellation,
                     ScanResult& result,
                     std::vector<WorkItem>& stack,
                     std::set<IdentityKey>& visited) {
    std::string error;
    const std::vector<DirEntry> entries =
        source.list(item.path, error, cancellationCheck(cancellation));
    if (cancellationRequested(cancellation)) {
        stack.push_back(std::move(item));
        markCancelled(result, stack);
        return false;
    }
    recordListingOutcome(item, entries, error, options, result);
    expandDirectory(item, entries, rootPath, options, rootIdentity, result, stack, visited,
                    cancellation);
    if (cancellationRequested(cancellation)) {
        markCancelled(result, stack);
        return false;
    }
    if (progress && (error.empty() || !entries.empty())) {
        progress(result.dirs_scanned, result.files_scanned);
    }
    return true;
}

void traverseDirectories(const FsSource& source,
                         const std::filesystem::path& rootPath,
                         const ScanOptions& options,
                         const ProgressFn& progress,
                         const ScanCancellationToken* cancellation,
                         ScanResult& result) {
    std::set<IdentityKey> visited;
    const FileIdentity& rootIdentity = effectiveMetadata(result.root).identity;
    if (rootIdentity.valid) {
        visited.insert(IdentityKey{rootIdentity.device, rootIdentity.file});
    }
    std::vector<WorkItem> stack{WorkItem{rootPath, 0, &result.root}};
    while (!stack.empty()) {
        if (cancellationRequested(cancellation)) {
            markCancelled(result, stack);
            return;
        }
        WorkItem item = std::move(stack.back());
        stack.pop_back();
        if (!depthIsExpandable(item.depth, options.max_depth)) {
            item.node->complete = false;
            item.node->error = "scan depth limit reached";
            continue;
        }
        if (!processWorkItem(source, std::move(item), rootPath, options, rootIdentity,
                             progress, cancellation, result, stack, visited)) {
            return;
        }
    }
}

void finalizeTraversal(ScanResult& result, const ProgressFn& progress) {
    if (result.cancelled) {
        if (progress) {
            progress(result.dirs_scanned, result.files_scanned);
        }
        return;
    }
    aggregateSizes(result.root);
    aggregateStorage(result.root);
}

} // namespace

ScanResult scan(const FsSource& source,
                 const std::filesystem::path& rootPath,
                 const ScanOptions& options,
                 const ProgressFn& progress,
                 const ScanCancellationToken* cancellation) {
    ScanResult result;
    initializeResult(result, rootPath, options);
    if (resolveRoot(source, rootPath, options, progress, cancellation, result)
        == RootDisposition::Complete) {
        return result;
    }
    traverseDirectories(source, rootPath, options, progress, cancellation, result);
    finalizeTraversal(result, progress);
    return result;
}

} // namespace diskmap
