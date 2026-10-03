#include "snapshot_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace diskmap {
namespace detail {

std::filesystem::path absoluteSnapshotFilePath(
    const std::filesystem::path& input) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(input, error);
    if (error) {
        throw SnapshotError("cannot resolve snapshot file path: " + error.message());
    }
    const std::filesystem::path path = absolute.lexically_normal();
    if (path.filename().empty() || path.filename() == "."
        || path.filename() == "..") {
        throw SnapshotError("snapshot file path must name a regular file");
    }
    return path;
}

SnapshotLimits checkedSnapshotLimits(const SnapshotLimits& input) {
    if (input.max_nodes == 0) {
        throw SnapshotError("snapshot max_nodes must be at least one");
    }
    if (input.max_serialized_bytes == 0) {
        throw SnapshotError("snapshot max_serialized_bytes must be positive");
    }
    SnapshotLimits limits = input;
    limits.max_depth = std::min(input.max_depth, static_cast<std::size_t>(kMaxTreeDepth));
    return limits;
}

void appendSnapshotError(FsNode& node, const std::string& message) {
    if (node.error.empty()) {
        node.error = message;
    } else {
        node.error += "; ";
        node.error += message;
    }
}

FsNode copyShallow(const FsNode& source) {
    // Never copy the children and clear them afterwards: doing so deep-copies
    // unbounded descendants before a snapshot node/depth budget can apply.
    FsNode result;
    result.name = source.name;
    result.path = source.path;
    result.is_dir = source.is_dir;
    result.size = source.size;
    result.logical_size_known = source.logical_size_known;
    result.allocated_size = source.allocated_size;
    result.allocated_size_known = source.allocated_size_known;
    result.reclaimable_size = source.reclaimable_size;
    result.reclaimable_size_known = source.reclaimable_size_known;
    result.metadata = source.metadata;
    result.has_target_metadata = source.has_target_metadata;
    result.target_metadata = source.target_metadata;
    result.followed = source.followed;
    result.cycle_skipped = source.cycle_skipped;
    result.mount_boundary_skipped = source.mount_boundary_skipped;
    result.complete = source.complete;
    result.error = source.error;
    // scan_generation and children deliberately remain at their defaults.
    return result;
}

void markTruncated(FsNode& node, SnapshotCopyContext& context, const char* reason) {
    context.truncated = true;
    node.complete = false;
    appendSnapshotError(node, reason);
}

void copyChildren(const FsNode& source,
                  FsNode& result,
                  std::size_t depth,
                  SnapshotCopyContext& context) {
    checkSnapshotCancellation(context.limits);
    result.children.reserve(std::min(source.children.size(),
                                     context.limits.max_nodes - context.nodes));
    for (const FsNode& child : source.children) {
        if (context.nodes >= context.limits.max_nodes) {
            markTruncated(result, context, "snapshot node limit reached");
            break;
        }
        result.children.push_back(copyBoundedSnapshotNode(child, depth + 1, context));
    }
}

FsNode copyBoundedSnapshotNode(const FsNode& source,
                               std::size_t depth,
                               SnapshotCopyContext& context) {
    checkSnapshotCancellation(context.limits);
    ++context.nodes;
    FsNode result = copyShallow(source);
    if (!source.is_dir && !source.children.empty()) {
        throw SnapshotError("snapshot contains children below a non-directory node");
    }
    if (!source.is_dir || source.children.empty()) {
        return result;
    }
    if (depth >= context.limits.max_depth) {
        markTruncated(result, context, "snapshot depth limit reached");
        return result;
    }
    copyChildren(source, result, depth, context);
    return result;
}

struct Utf8LeadBounds {
    std::size_t width;
    unsigned char secondMinimum;
    unsigned char secondMaximum;
};

std::optional<Utf8LeadBounds> utf8LeadBounds(unsigned char first) {
    Utf8LeadBounds result{0, 0x80U, 0xbfU};
    if (first >= 0xc2U && first <= 0xdfU) {
        result.width = 2;
    } else if (first == 0xe0U) {
        result.width = 3;
        result.secondMinimum = 0xa0U;
    } else if (first == 0xedU) {
        result.width = 3;
        result.secondMaximum = 0x9fU;
    } else if ((first >= 0xe1U && first <= 0xecU)
               || (first >= 0xeeU && first <= 0xefU)) {
        result.width = 3;
    } else if (first == 0xf0U) {
        result.width = 4;
        result.secondMinimum = 0x90U;
    } else if (first >= 0xf1U && first <= 0xf3U) {
        result.width = 4;
    } else if (first == 0xf4U) {
        result.width = 4;
        result.secondMaximum = 0x8fU;
    } else {
        return std::nullopt;
    }
    return result;
}

bool validUtf8Sequence(const std::string& value, std::size_t index, std::size_t& width) {
    const unsigned char first = static_cast<unsigned char>(value[index]);
    const auto bounds = utf8LeadBounds(first);
    if (!bounds) {
        return false;
    }
    width = bounds->width;
    if (width > value.size() - index) {
        return false;
    }
    const unsigned char second = static_cast<unsigned char>(value[index + 1]);
    if (second < bounds->secondMinimum || second > bounds->secondMaximum) {
        return false;
    }
    for (std::size_t offset = 2; offset < width; ++offset) {
        const unsigned char continuation = static_cast<unsigned char>(value[index + offset]);
        if (continuation < 0x80U || continuation > 0xbfU) {
            return false;
        }
    }
    return true;
}

std::string bytesHex(const std::string& value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2);
    for (unsigned char byte : value) {
        result += hex[byte >> 4]; result += hex[byte & 15];
    }
    return result;
}
std::string bytesFromHex(const std::string& value) {
    if (value.size() % 2) throw SnapshotError("invalid byte encoding");
    std::string result;
    const auto digit = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        throw SnapshotError("noncanonical byte encoding");
    };
    for (std::size_t i = 0; i < value.size(); i += 2) {
        const auto byte = (digit(value[i]) << 4) | digit(value[i + 1]);
        if (!byte) throw SnapshotError("NUL is not a filesystem path byte");
        result += static_cast<char>(byte);
    }
    return result;
}
std::string displayBytes(const std::string& value) {
    if (isValidUtf8(value)) return value;
    std::string result;
    for (unsigned char byte : value) {
        if (byte >= 32 && byte < 127 && byte != '\\') result += static_cast<char>(byte);
        else result += "\\x" + bytesHex(std::string(1, static_cast<char>(byte)));
    }
    return result;
}

bool isValidUtf8(const std::string& value) {
    for (std::size_t index = 0; index < value.size();) {
        const unsigned char first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7fU) {
            ++index;
            continue;
        }
        std::size_t width = 0;
        if (!validUtf8Sequence(value, index, width)) {
            return false;
        }
        index += width;
    }
    return true;
}

bool nodeHasIncompleteEvidence(const FsNode& node) {
    if (!node.complete || node.cycle_skipped || node.mount_boundary_skipped
        || !node.error.empty() || !node.metadata.complete || !node.metadata.error.empty()) {
        return true;
    }
    return node.followed
           && (!node.has_target_metadata || !node.target_metadata.complete
               || !node.target_metadata.error.empty());
}

struct ValidationFrame {
    const FsNode* node = nullptr;
    std::size_t depth = 0;
};

SnapshotTreeValidation validateSnapshotTree(const FsNode& root,
                                            const SnapshotLimits& limits) {
    SnapshotTreeValidation result;
    std::set<std::string> paths;
    std::vector<ValidationFrame> stack{{&root, 0}};
    while (!stack.empty()) {
        checkSnapshotCancellation(limits);
        const ValidationFrame frame = stack.back();
        if (frame.node->name.find('\0') != std::string::npos || frame.node->path.native().find('\0') != std::string::npos)
            throw SnapshotError("NUL is not a filesystem name byte");
        stack.pop_back();
        if (frame.depth > limits.max_depth) {
            throw SnapshotError("snapshot tree exceeds configured depth bound");
        }
        if (result.nodes >= limits.max_nodes) {
            throw SnapshotError("snapshot node count exceeds configured bound");
        }
        ++result.nodes;
        if (!paths.insert(normalizedPath(*frame.node)).second) {
            throw SnapshotError("snapshot contains duplicate normalized path");
        }
        result.has_incomplete_evidence =
            result.has_incomplete_evidence || nodeHasIncompleteEvidence(*frame.node);
        if (!frame.node->is_dir && !frame.node->children.empty()) {
            throw SnapshotError("snapshot contains children below a non-directory node");
        }
        if (frame.depth == limits.max_depth && !frame.node->children.empty()) {
            throw SnapshotError("snapshot tree exceeds configured depth bound");
        }
        for (const FsNode& child : frame.node->children) {
            stack.push_back(ValidationFrame{&child, frame.depth + 1});
        }
    }
    return result;
}

} // namespace detail

Snapshot snapshotFromNode(const FsNode& root, const SnapshotLimits& inputLimits) {
    const SnapshotLimits limits = detail::checkedSnapshotLimits(inputLimits);
    detail::SnapshotCopyContext context{limits};
    Snapshot snapshot;
    snapshot.root = detail::copyBoundedSnapshotNode(root, 0, context);
    snapshot.truncated = context.truncated;
    const detail::SnapshotTreeValidation validation =
        detail::validateSnapshotTree(snapshot.root, limits);
    snapshot.complete = root.complete && !snapshot.truncated
                        && !validation.has_incomplete_evidence;
    snapshot.nodes_retained = context.nodes;
    return snapshot;
}

ScanResult scanEvidenceFromSnapshot(Snapshot snapshot, std::uint64_t generation) {
    ScanResult result;
    result.generation = generation;
    const bool inventoryComplete = snapshot.complete && !snapshot.truncated;
    result.root = std::move(snapshot.root);
    if (!inventoryComplete) {
        result.root.complete = false;
        detail::appendSnapshotError(result.root,
                                    "loaded snapshot inventory is incomplete");
    }

    std::vector<FsNode*> stack{&result.root};
    while (!stack.empty()) {
        FsNode* node = stack.back();
        stack.pop_back();
        node->scan_generation = generation;
        // Inventory-wide incompleteness is inherited by every retained node.
        // Otherwise a caller selecting a complete-looking child could bypass
        // the root marker and authorize cleanup from partial historical data.
        if (!inventoryComplete) {
            node->complete = false;
        }
        if (node->is_dir) {
            ++result.dirs_scanned;
        } else if (nodeKind(*node) == FsKind::RegularFile) {
            ++result.files_scanned;
        }
        for (FsNode& child : node->children) {
            stack.push_back(&child);
        }
    }
    return result;
}

} // namespace diskmap
