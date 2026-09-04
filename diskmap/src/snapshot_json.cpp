#include "snapshot_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace diskmap {
namespace {

bool appendShortEscape(std::string& output, unsigned char character) {
    switch (character) {
    case '"': output += "\\\""; return true;
    case '\\': output += "\\\\"; return true;
    case '\b': output += "\\b"; return true;
    case '\f': output += "\\f"; return true;
    case '\n': output += "\\n"; return true;
    case '\r': output += "\\r"; return true;
    case '\t': output += "\\t"; return true;
    default: return false;
    }
}

void appendControlEscape(std::string& output,
                         unsigned char character,
                         const char* hex) {
    output += "\\u00";
    output.push_back(hex[(character >> 4U) & 0x0fU]);
    output.push_back(hex[character & 0x0fU]);
}

void appendJsonEscaped(std::string& output,
                       const std::string& value,
                       std::size_t maxStringBytes) {
    if (!detail::isValidUtf8(value)) {
        throw SnapshotError("snapshot strings must be valid UTF-8");
    }
    if (value.size() > maxStringBytes) {
        throw SnapshotError("snapshot string exceeds configured bound");
    }
    static constexpr char hex[] = "0123456789abcdef";
    output.push_back('"');
    for (unsigned char character : value) {
        if (appendShortEscape(output, character)) {
            continue;
        }
        if (character < 0x20U) {
            appendControlEscape(output, character, hex);
            continue;
        }
        output.push_back(static_cast<char>(character));
    }
    output.push_back('"');
}

class JsonWriter final {
public:
    explicit JsonWriter(const SnapshotLimits& limits) : limits_(limits) {
        output_.reserve(std::min<std::size_t>(limits.max_serialized_bytes, 4096));
    }

    void raw(const std::string& value) {
        if (value.size() > limits_.max_serialized_bytes - output_.size()) {
            throw SnapshotError("serialized snapshot exceeds configured bound");
        }
        output_ += value;
    }

    void character(char value) {
        if (output_.size() >= limits_.max_serialized_bytes) {
            throw SnapshotError("serialized snapshot exceeds configured bound");
        }
        output_.push_back(value);
    }

    void string(const std::string& value) {
        std::string encoded;
        encoded.reserve(value.size() + 2);
        appendJsonEscaped(encoded, value, limits_.max_string_bytes);
        raw(encoded);
    }

    void key(const char* value) {
        string(value);
        character(':');
    }

    void boolean(bool value) { raw(value ? "true" : "false"); }

    void unsignedInteger(std::uint64_t value) { raw(std::to_string(value)); }

    void signedInteger(std::int64_t value) { raw(std::to_string(value)); }

    const std::string& result() const { return output_; }

private:
    const SnapshotLimits& limits_;
    std::string output_;
};

const char* kindName(FsKind kind) {
    switch (kind) {
    case FsKind::RegularFile:
        return "regular_file";
    case FsKind::Directory:
        return "directory";
    case FsKind::Symlink:
        return "symlink";
    case FsKind::Other:
        return "other";
    }
    return "other";
}

void writeIdentity(JsonWriter& writer, const FileIdentity& identity) {
    writer.character('{');
    writer.key("device");
    writer.unsignedInteger(identity.device);
    writer.character(',');
    writer.key("file");
    writer.unsignedInteger(identity.file);
    writer.character(',');
    writer.key("valid");
    writer.boolean(identity.valid);
    writer.character('}');
}

void writeMetadata(JsonWriter& writer, const FsMetadata& metadata) {
    writer.character('{');
    writer.key("allocated_size");
    writer.unsignedInteger(metadata.allocated_size);
    writer.character(',');
    writer.key("allocated_size_known");
    writer.boolean(metadata.allocated_size_known);
    writer.character(',');
    writer.key("complete");
    writer.boolean(metadata.complete);
    writer.character(',');
    writer.key("error");
    writer.string(metadata.error);
    writer.character(',');
    writer.key("group");
    writer.unsignedInteger(metadata.group);
    writer.character(',');
    writer.key("hard_link_count");
    writer.unsignedInteger(metadata.hard_link_count);
    writer.character(',');
    writer.key("hard_link_count_known");
    writer.boolean(metadata.hard_link_count_known);
    writer.character(',');
    writer.key("identity");
    writeIdentity(writer, metadata.identity);
    writer.character(',');
    writer.key("kind");
    writer.string(kindName(metadata.kind));
    writer.character(',');
    writer.key("logical_size");
    writer.unsignedInteger(metadata.logical_size);
    writer.character(',');
    writer.key("modified_ns");
    writer.signedInteger(metadata.modified_ns);
    writer.character(',');
    writer.key("modified_time_known");
    writer.boolean(metadata.modified_time_known);
    writer.character(',');
    writer.key("owner");
    writer.unsignedInteger(metadata.owner);
    writer.character(',');
    writer.key("ownership_known");
    writer.boolean(metadata.ownership_known);
    writer.character(',');
    writer.key("permissions");
    writer.unsignedInteger(metadata.permissions);
    writer.character(',');
    writer.key("permissions_known");
    writer.boolean(metadata.permissions_known);
    writer.character('}');
}

bool nodeBefore(const FsNode* left, const FsNode* right) {
    const std::string leftPath = normalizedPath(*left);
    const std::string rightPath = normalizedPath(*right);
    if (leftPath != rightPath) {
        return leftPath < rightPath;
    }
    if (left->name != right->name) {
        return left->name < right->name;
    }
    const FsKind leftKind = nodeKind(*left);
    const FsKind rightKind = nodeKind(*right);
    if (leftKind != rightKind) {
        return static_cast<int>(leftKind) < static_cast<int>(rightKind);
    }
    if (left->followed != right->followed) {
        return !left->followed;
    }
    return nodeKey(*left) < nodeKey(*right);
}

void writeNode(JsonWriter& writer,
               const FsNode& node,
               std::size_t depth,
               const SnapshotLimits& limits) {
    if (depth > limits.max_depth) {
        throw SnapshotError("snapshot tree exceeds configured depth bound");
    }
    writer.character('{');
    writer.key("allocated_size");
    writer.unsignedInteger(node.allocated_size);
    writer.character(',');
    writer.key("allocated_size_known");
    writer.boolean(node.allocated_size_known);
    writer.character(',');
    writer.key("children");
    writer.character('[');
    std::vector<const FsNode*> children;
    children.reserve(node.children.size());
    for (const FsNode& child : node.children) {
        children.push_back(&child);
    }
    std::stable_sort(children.begin(), children.end(), nodeBefore);
    for (std::size_t index = 0; index < children.size(); ++index) {
        if (index != 0) {
            writer.character(',');
        }
        writeNode(writer, *children[index], depth + 1, limits);
    }
    writer.character(']');
    writer.character(',');
    writer.key("complete");
    writer.boolean(node.complete);
    writer.character(',');
    writer.key("cycle_skipped");
    writer.boolean(node.cycle_skipped);
    writer.character(',');
    writer.key("error");
    writer.string(node.error);
    writer.character(',');
    writer.key("followed");
    writer.boolean(node.followed);
    writer.character(',');
    writer.key("has_target_metadata");
    writer.boolean(node.has_target_metadata);
    writer.character(',');
    writer.key("is_dir");
    writer.boolean(node.is_dir);
    writer.character(',');
    writer.key("logical_size_known");
    writer.boolean(node.logical_size_known);
    writer.character(',');
    writer.key("metadata");
    writeMetadata(writer, node.metadata);
    writer.character(',');
    writer.key("mount_boundary_skipped");
    writer.boolean(node.mount_boundary_skipped);
    writer.character(',');
    writer.key("name");
    writer.string(node.name);
    writer.character(',');
    writer.key("path");
    writer.string(node.path.generic_string());
    writer.character(',');
    writer.key("reclaimable_size");
    writer.unsignedInteger(node.reclaimable_size);
    writer.character(',');
    writer.key("reclaimable_size_known");
    writer.boolean(node.reclaimable_size_known);
    writer.character(',');
    writer.key("size");
    writer.unsignedInteger(node.size);
    writer.character(',');
    writer.key("target_metadata");
    writeMetadata(writer, node.target_metadata);
    writer.character('}');
}

} // namespace

std::string serializeSnapshot(const Snapshot& snapshot, const SnapshotLimits& inputLimits) {
    const SnapshotLimits limits = detail::checkedSnapshotLimits(inputLimits);
    if (snapshot.schema_version != kSnapshotSchemaV1) {
        throw SnapshotError("unsupported diskmap snapshot schema");
    }
    const detail::SnapshotTreeValidation validation =
        detail::validateSnapshotTree(snapshot.root, limits);
    if (snapshot.complete && snapshot.truncated) {
        throw SnapshotError("truncated snapshot cannot be marked complete");
    }
    if (snapshot.complete && validation.has_incomplete_evidence) {
        throw SnapshotError("complete snapshot contains incomplete evidence");
    }
    const std::size_t nodeCount = validation.nodes;
    JsonWriter writer(limits);
    writer.character('{');
    writer.key("complete");
    writer.boolean(snapshot.complete);
    writer.character(',');
    writer.key("node_count");
    writer.unsignedInteger(nodeCount);
    writer.character(',');
    writer.key("root");
    writeNode(writer, snapshot.root, 0, limits);
    writer.character(',');
    writer.key("schema_version");
    writer.string(snapshot.schema_version);
    writer.character(',');
    writer.key("truncated");
    writer.boolean(snapshot.truncated);
    writer.character('}');
    return writer.result();
}

} // namespace diskmap
