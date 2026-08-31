#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "diskmap/fs_node.hpp"
#include "diskmap/fs_source.hpp"

namespace diskmap {

struct ScanOptions {
    // Negative means unlimited. A directory pruned by a finite limit remains
    // visible but is marked incomplete, so storage totals cannot look exact.
    int max_depth = -1;
    // Controls descendants. The explicitly selected root is always
    // dereferenced, matching ordinary path argument semantics.
    bool follow_symlinks = false;
    std::uint64_t min_size = 0; // files smaller than this are skipped
    // When true, directories on another device remain visible but are not
    // traversed. The skipped subtree is explicitly incomplete.
    bool one_file_system = false;
    // Shell-free wildcard patterns matched against both the entry name and
    // its generic, root-relative path. '*' and '?' are supported.
    std::vector<std::string> exclude_patterns;
    // Bounds hostile/error-heavy trees without hiding how many errors occurred.
    std::size_t max_errors = 1024;
    std::uint64_t generation = 0;
};

struct ScanResult {
    FsNode root;
    std::vector<std::string> errors;
    std::size_t error_count = 0;
    bool errors_truncated = false;
    std::size_t dirs_scanned = 0;
    std::size_t files_scanned = 0;
    std::size_t entries_filtered = 0;
    std::size_t mount_boundaries_skipped = 0;
    bool totals_filtered = false;
    std::uint64_t generation = 0;
    bool cancelled = false;
};

using ProgressFn = std::function<void(std::size_t dirs, std::size_t files)>;

class ScanCancellationToken {
public:
    ScanCancellationToken() = default;
    ScanCancellationToken(const ScanCancellationToken&) = delete;
    ScanCancellationToken& operator=(const ScanCancellationToken&) = delete;

    void cancel() noexcept { cancelled_.store(true, std::memory_order_release); }
    bool isCancelled() const noexcept {
        return cancelled_.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> cancelled_{false};
};

// Iteratively walks a directory root via source, honoring options and
// collecting every listing error instead of stopping at the first one. A
// regular-file root is returned as a complete one-node scan. Never recurses,
// so it cannot stack-overflow on a deep tree.
ScanResult scan(const FsSource& source,
                 const std::filesystem::path& rootPath,
                 const ScanOptions& options,
                 const ProgressFn& progress = nullptr,
                 const ScanCancellationToken* cancellation = nullptr);

} // namespace diskmap
