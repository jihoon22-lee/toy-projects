#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "diskmap/fs_metadata.hpp"

namespace diskmap {

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
    // vector and sets error to a human-readable message; never throws.
    virtual std::vector<DirEntry> list(const std::string& path, std::string& error) const = 0;
};

class RealFsSource : public FsSource {
public:
    std::vector<DirEntry> list(const std::string& path, std::string& error) const override;
};

} // namespace diskmap
