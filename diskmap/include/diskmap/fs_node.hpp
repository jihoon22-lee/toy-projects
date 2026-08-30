#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "diskmap/fs_metadata.hpp"

namespace diskmap {

// Caps how many levels the recursive helpers below will descend, so a
// pathological (or maliciously deep) tree cannot overflow the call stack.
constexpr int kMaxTreeDepth = 4096;

struct FsNode {
    std::string name;
    std::string path;
    bool is_dir = false;
    std::uint64_t size = 0;
    FsMetadata metadata;
    bool has_target_metadata = false;
    FsMetadata target_metadata;
    bool followed = false;
    // Scan completeness is separate from metadata completeness: a directory
    // can have a valid stat record while listing its children fails.
    bool complete = true;
    std::string error;
    std::vector<FsNode> children;
};

// Post-order sum of subtree sizes; sets every directory's size to the sum
// of its children and returns the size of the whole tree rooted at node.
std::uint64_t aggregateSizes(FsNode& node);

// Recursively sorts every level of children by size (descending), breaking
// ties by name (ascending) so the ordering is stable and reproducible.
void sortBySizeDesc(FsNode& node);

// Finds a direct child by name, or nullptr if there is none.
const FsNode* findChild(const FsNode& node, const std::string& name);

// Counts node plus every descendant.
std::size_t countNodes(const FsNode& node);

// Returns up to n largest *files* (not directories) anywhere in the
// subtree, sorted descending by size.
std::vector<const FsNode*> topFiles(const FsNode& node, std::size_t n);

} // namespace diskmap
