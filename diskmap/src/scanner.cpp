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

using IdentityKey = std::pair<std::uint64_t, std::uint64_t>;

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
    return maxDepth < 0 || depth <= maxDepth;
}

bool entrySkipped(const DirEntry& entry, const ScanOptions& options) {
    if (entry.is_symlink && !options.follow_symlinks) {
        return true;
    }
    return !entry.is_dir && entry.size < options.min_size;
}

const FsMetadata& effectiveMetadata(const FsNode& node) {
    return node.followed && node.has_target_metadata ? node.target_metadata : node.metadata;
}

void inspectRootSymlink(const FsSource& source,
                        const std::filesystem::path& rootPath,
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
    result.errors.push_back(result.root.error);
}

bool inspectRoot(const FsSource& source,
                 const std::filesystem::path& rootPath,
                 ScanResult& result) {
    const FsMetadata metadata = source.inspect(rootPath, false);
    if (metadata.complete) {
        result.root.metadata = metadata;
        result.root.is_dir = metadata.kind == FsKind::Directory;
        if (metadata.kind == FsKind::Symlink) {
            inspectRootSymlink(source, rootPath, result);
        }
        return true;
    }
    if (!metadata.error.empty()) {
        result.root.complete = false;
        result.root.error = metadata.error;
        result.errors.push_back(metadata.error);
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

FsNode makeChildNode(const DirEntry& entry, const std::filesystem::path& parentPath) {
    FsNode child;
    child.name = entry.name;
    child.path = entry.path.empty() ? parentPath / std::filesystem::path(entry.name) : entry.path;
    child.is_dir = entry.is_dir;
    child.size = entry.is_dir ? 0 : entry.size;
    child.metadata = entry.metadata;
    child.has_target_metadata = entry.has_target_metadata;
    child.target_metadata = entry.target_metadata;
    child.followed = entry.is_symlink;
    child.complete = entry.metadata.complete
                     && (!entry.is_symlink
                         || (entry.has_target_metadata && entry.target_metadata.complete));
    child.error = entry.metadata.error;
    if (entry.is_symlink && !entry.has_target_metadata) {
        child.complete = false;
        child.error = entry.target_metadata.error;
    }
    return child;
}

bool rememberDirectoryIdentity(FsNode& child,
                               std::set<IdentityKey>& visited,
                               ScanResult& result) {
    const FileIdentity& identity = effectiveMetadata(child).identity;
    if (child.followed && !identity.valid) {
        child.complete = false;
        child.error = "cannot safely follow directory symlink '" + child.path.string()
                      + "': target identity is unavailable";
        result.errors.push_back(child.error);
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

void scheduleDirectoryPass(std::vector<FsNode>& children,
                           bool followedPass,
                           int childDepth,
                           ScanResult& result,
                           std::vector<WorkItem>& stack,
                           std::set<IdentityKey>& visited) {
    for (FsNode& child : children) {
        if (!child.is_dir || child.followed != followedPass) {
            continue;
        }
        if (rememberDirectoryIdentity(child, visited, result)) {
            stack.push_back(WorkItem{child.path, childDepth, &child});
        }
    }
}

// Fills node.children from entries (skipping symlinks/undersized files per
// options) and pushes follow-up work items for any subdirectories found.
// Children are appended in a single pass before any pointer into the vector
// is taken, so the pointers handed to later work items stay valid.
void expandDirectory(WorkItem& item,
                      const std::vector<DirEntry>& entries,
                      const ScanOptions& options,
                      ScanResult& result,
                      std::vector<WorkItem>& stack,
                      std::set<IdentityKey>& visited) {
    item.node->children.reserve(entries.size());
    for (const DirEntry& entry : entries) {
        if (entrySkipped(entry, options)) {
            continue;
        }
        item.node->children.push_back(makeChildNode(entry, item.path));
        if (!entry.is_dir) {
            ++result.files_scanned;
        }
    }
    // Prefer real directory entries to symlink aliases. This makes the stable
    // lexical source order choose the canonical entry before an alias that
    // resolves to the same identity.
    scheduleDirectoryPass(item.node->children, false, item.depth + 1, result, stack, visited);
    scheduleDirectoryPass(item.node->children, true, item.depth + 1, result, stack, visited);
}

} // namespace

ScanResult scan(const FsSource& source,
                 const std::filesystem::path& rootPath,
                 const ScanOptions& options,
                 const ProgressFn& progress) {
    ScanResult result;
    result.root.name = lastPathComponent(rootPath);
    result.root.path = rootPath;
    result.root.is_dir = true;
    result.root.metadata.kind = FsKind::Directory;
    result.root.metadata.complete = true;

    const bool rootKindResolved = inspectRoot(source, rootPath, result);
    if (rootKindResolved && !result.root.is_dir) {
        finalizeLeafRoot(result, progress);
        return result;
    }

    std::set<IdentityKey> visited;
    const FileIdentity& rootIdentity = effectiveMetadata(result.root).identity;
    if (rootIdentity.valid) {
        visited.insert(IdentityKey{rootIdentity.device, rootIdentity.file});
    }

    std::vector<WorkItem> stack;
    stack.push_back(WorkItem{rootPath, 0, &result.root});

    while (!stack.empty()) {
        WorkItem item = std::move(stack.back());
        stack.pop_back();

        if (!depthIsExpandable(item.depth, options.max_depth)) {
            item.node->complete = false;
            item.node->error = "scan depth limit reached";
            continue;
        }

        std::string error;
        std::vector<DirEntry> entries = source.list(item.path, error);
        if (!error.empty()) {
            item.node->complete = false;
            item.node->error = error;
            result.errors.push_back(error);
            continue;
        }
        ++result.dirs_scanned;

        expandDirectory(item, entries, options, result, stack, visited);

        if (progress) {
            progress(result.dirs_scanned, result.files_scanned);
        }
    }

    aggregateSizes(result.root);
    aggregateStorage(result.root);
    return result;
}

} // namespace diskmap
