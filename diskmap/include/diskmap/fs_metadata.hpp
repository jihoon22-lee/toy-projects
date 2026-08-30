#pragma once

#include <cstdint>
#include <string>

namespace diskmap {

struct FileIdentity {
    std::uint64_t device = 0;
    std::uint64_t file = 0;
    bool valid = false;

    bool operator==(const FileIdentity& other) const {
        if (!valid || !other.valid) {
            return valid == other.valid;
        }
        return device == other.device && file == other.file;
    }

    bool operator!=(const FileIdentity& other) const { return !(*this == other); }
};

enum class FsKind { RegularFile, Directory, Symlink, Other };

struct FsMetadata {
    FsKind kind = FsKind::Other;
    FileIdentity identity;
    std::uint64_t logical_size = 0;
    std::uint64_t allocated_size = 0;
    bool allocated_size_known = false;
    std::uint64_t hard_link_count = 0;
    bool hard_link_count_known = false;
    std::uint32_t permissions = 0;
    bool permissions_known = false;
    std::uint64_t owner = 0;
    std::uint64_t group = 0;
    bool ownership_known = false;
    std::int64_t modified_ns = 0;
    bool modified_time_known = false;
    // Complete means the metadata read itself succeeded. Individual fields
    // that are not portable still use their corresponding *_known flag.
    bool complete = false;
    std::string error;
};

} // namespace diskmap
