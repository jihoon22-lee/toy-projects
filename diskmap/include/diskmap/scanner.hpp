#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "diskmap/fs_node.hpp"
#include "diskmap/fs_source.hpp"

namespace diskmap {

struct ScanOptions {
    int max_depth = -1; // negative means unlimited
    bool follow_symlinks = false;
    std::uint64_t min_size = 0; // files smaller than this are skipped
};

struct ScanResult {
    FsNode root;
    std::vector<std::string> errors;
    std::size_t dirs_scanned = 0;
    std::size_t files_scanned = 0;
};

using ProgressFn = std::function<void(std::size_t dirs, std::size_t files)>;

// Iteratively walks rootPath via source, honoring options, collecting every
// listing error instead of stopping at the first one. Never recurses, so it
// cannot stack-overflow on a deep tree.
ScanResult scan(const FsSource& source,
                 const std::string& rootPath,
                 const ScanOptions& options,
                 const ProgressFn& progress = nullptr);

} // namespace diskmap
