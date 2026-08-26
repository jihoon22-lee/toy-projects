#include "fs_node.hpp"

#include <algorithm>

namespace diskmap {

namespace {

std::uint64_t aggregateSizesAt(FsNode& node, int depth) {
    if (!node.is_dir || depth >= kMaxTreeDepth) {
        return node.size;
    }
    std::uint64_t total = 0;
    for (FsNode& child : node.children) {
        total += aggregateSizesAt(child, depth + 1);
    }
    node.size = total;
    return total;
}

bool byDescendingSizeThenName(const FsNode& a, const FsNode& b) {
    if (a.size != b.size) {
        return a.size > b.size;
    }
    return a.name < b.name;
}

void sortBySizeDescAt(FsNode& node, int depth) {
    if (depth >= kMaxTreeDepth) {
        return;
    }
    std::stable_sort(node.children.begin(), node.children.end(), byDescendingSizeThenName);
    for (FsNode& child : node.children) {
        sortBySizeDescAt(child, depth + 1);
    }
}

std::size_t countNodesAt(const FsNode& node, int depth) {
    std::size_t count = 1;
    if (depth >= kMaxTreeDepth) {
        return count;
    }
    for (const FsNode& child : node.children) {
        count += countNodesAt(child, depth + 1);
    }
    return count;
}

void collectFilesAt(const FsNode& node, int depth, std::vector<const FsNode*>& out) {
    if (!node.is_dir) {
        out.push_back(&node);
        return;
    }
    if (depth >= kMaxTreeDepth) {
        return;
    }
    for (const FsNode& child : node.children) {
        collectFilesAt(child, depth + 1, out);
    }
}

bool byDescendingFileSize(const FsNode* a, const FsNode* b) {
    return a->size > b->size;
}

} // namespace

std::uint64_t aggregateSizes(FsNode& node) {
    return aggregateSizesAt(node, 0);
}

void sortBySizeDesc(FsNode& node) {
    sortBySizeDescAt(node, 0);
}

const FsNode* findChild(const FsNode& node, const std::string& name) {
    for (const FsNode& child : node.children) {
        if (child.name == name) {
            return &child;
        }
    }
    return nullptr;
}

std::size_t countNodes(const FsNode& node) {
    return countNodesAt(node, 0);
}

std::vector<const FsNode*> topFiles(const FsNode& node, std::size_t n) {
    std::vector<const FsNode*> files;
    collectFilesAt(node, 0, files);
    std::stable_sort(files.begin(), files.end(), byDescendingFileSize);
    if (files.size() > n) {
        files.resize(n);
    }
    return files;
}

} // namespace diskmap
