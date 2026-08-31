#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "diskmap/fs_metadata.hpp"

namespace diskmap {

using CancellationCheck = std::function<bool()>;

struct DirEntry {
    std::string name;
    std::filesystem::path path;
    bool is_dir = false;
    bool is_symlink = false;
    std::uint64_t size = 0;
    FsMetadata metadata;
    bool has_target_metadata = false;
    FsMetadata target_metadata;
};

// Abstraction over "list the contents of a directory" so scanning logic can
// be unit-tested without touching a real filesystem.
class FsSource {
public:
    virtual ~FsSource();

    // Lists the direct children of path. On failure returns an empty
    // vector and sets error to a human-readable message; never throws. Long
    // real-filesystem listings cooperatively stop when cancelled returns true.
    virtual std::vector<DirEntry> list(const std::filesystem::path& path,
                                       std::string& error,
                                       const CancellationCheck& cancelled = {}) const = 0;

    // Reads metadata for the path itself or its target. The default keeps
    // scripted sources source-compatible; real filesystems override it so the
    // scanner can seed its visited set with the root's physical identity.
    virtual FsMetadata inspect(const std::filesystem::path& path, bool follow) const;
};

class RealFsSource : public FsSource {
public:
    std::vector<DirEntry> list(const std::filesystem::path& path,
                               std::string& error,
                               const CancellationCheck& cancelled = {}) const override;
    FsMetadata inspect(const std::filesystem::path& path, bool follow) const override;
};

} // namespace diskmap
