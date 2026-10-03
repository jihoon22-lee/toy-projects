#pragma once

#include "diskmap/snapshot.hpp"

namespace diskmap {
namespace detail {

std::filesystem::path absoluteSnapshotFilePath(
    const std::filesystem::path& input);

SnapshotLimits checkedSnapshotLimits(const SnapshotLimits& input);

void appendSnapshotError(FsNode& node, const std::string& message);

struct SnapshotCopyContext {
    SnapshotLimits limits;
    std::size_t nodes = 0;
    bool truncated = false;
};

FsNode copyBoundedSnapshotNode(const FsNode& source,
                               std::size_t depth,
                               SnapshotCopyContext& context);

struct SnapshotTreeValidation {
    std::size_t nodes = 0;
    bool has_incomplete_evidence = false;
};

SnapshotTreeValidation validateSnapshotTree(const FsNode& root,
                                            const SnapshotLimits& limits);

inline void checkSnapshotCancellation(const SnapshotLimits& limits) {
    if (limits.cancelled && limits.cancelled()) throw SnapshotError("operation cancelled");
}
std::string bytesHex(const std::string& value);
std::string bytesFromHex(const std::string& value);
std::string displayBytes(const std::string& value);
bool isValidUtf8(const std::string& value);

} // namespace detail
} // namespace diskmap
