#include "scanner.hpp"

#include <utility>

namespace diskmap {

namespace {

struct WorkItem {
    std::string path;
    int depth = 0;
    FsNode* node = nullptr;
};

std::string lastPathComponent(const std::string& path) {
    std::string trimmed = path;
    while (trimmed.size() > 1 && trimmed.back() == '/') {
        trimmed.pop_back();
    }
    const std::size_t pos = trimmed.find_last_of('/');
    return pos == std::string::npos ? trimmed : trimmed.substr(pos + 1);
}

std::string joinPath(const std::string& base, const std::string& name) {
    if (base.empty()) {
        return name;
    }
    return base.back() == '/' ? base + name : base + "/" + name;
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

FsNode makeChildNode(const DirEntry& entry) {
    FsNode child;
    child.name = entry.name;
    child.is_dir = entry.is_dir;
    child.size = entry.is_dir ? 0 : entry.size;
    return child;
}

// Fills node.children from entries (skipping symlinks/undersized files per
// options) and pushes follow-up work items for any subdirectories found.
// Children are appended in a single pass before any pointer into the vector
// is taken, so the pointers handed to later work items stay valid.
void expandDirectory(WorkItem& item,
                      const std::vector<DirEntry>& entries,
                      const ScanOptions& options,
                      ScanResult& result,
                      std::vector<WorkItem>& stack) {
    item.node->children.reserve(entries.size());
    for (const DirEntry& entry : entries) {
        if (entrySkipped(entry, options)) {
            continue;
        }
        item.node->children.push_back(makeChildNode(entry));
        if (!entry.is_dir) {
            ++result.files_scanned;
        }
    }
    for (FsNode& child : item.node->children) {
        if (child.is_dir) {
            stack.push_back(WorkItem{joinPath(item.path, child.name), item.depth + 1, &child});
        }
    }
}

} // namespace

ScanResult scan(const FsSource& source,
                 const std::string& rootPath,
                 const ScanOptions& options,
                 const ProgressFn& progress) {
    ScanResult result;
    result.root.name = lastPathComponent(rootPath);
    result.root.is_dir = true;

    std::vector<WorkItem> stack;
    stack.push_back(WorkItem{rootPath, 0, &result.root});

    while (!stack.empty()) {
        WorkItem item = std::move(stack.back());
        stack.pop_back();

        if (!depthIsExpandable(item.depth, options.max_depth)) {
            continue;
        }

        std::string error;
        std::vector<DirEntry> entries = source.list(item.path, error);
        if (!error.empty()) {
            result.errors.push_back(error);
            continue;
        }
        ++result.dirs_scanned;

        expandDirectory(item, entries, options, result, stack);

        if (progress) {
            progress(result.dirs_scanned, result.files_scanned);
        }
    }

    aggregateSizes(result.root);
    return result;
}

} // namespace diskmap
