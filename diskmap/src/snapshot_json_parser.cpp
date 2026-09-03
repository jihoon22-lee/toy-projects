#include "snapshot_internal.hpp"
#include "snapshot_json_dom.hpp"

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace diskmap {

namespace {

using detail::JsonObject;
using detail::JsonValue;

FsKind parseKind(const std::string& value) {
    if (value == "regular_file") {
        return FsKind::RegularFile;
    }
    if (value == "directory") {
        return FsKind::Directory;
    }
    if (value == "symlink") {
        return FsKind::Symlink;
    }
    if (value == "other") {
        return FsKind::Other;
    }
    throw SnapshotError("snapshot contains an unknown filesystem kind");
}

const JsonValue& member(const JsonObject& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end()) {
        throw SnapshotError(std::string("snapshot is missing key: ") + key);
    }
    return it->second;
}

void exactKeys(const JsonObject& object, std::initializer_list<const char*> keys) {
    std::set<std::string> expected;
    for (const char* key : keys) {
        expected.emplace(key);
    }
    if (object.size() != expected.size()) {
        throw SnapshotError("snapshot contains an unknown or missing key");
    }
    for (const auto& item : object) {
        if (expected.find(item.first) == expected.end()) {
            throw SnapshotError("snapshot contains an unknown key: " + item.first);
        }
    }
}

bool booleanValue(const JsonValue& value, const char* field) {
    if (value.kind != detail::JsonKind::Boolean) {
        throw SnapshotError(std::string("snapshot field is not boolean: ") + field);
    }
    return value.boolean;
}

std::uint64_t unsignedValue(const JsonValue& value, const char* field) {
    if (value.kind != detail::JsonKind::Unsigned) {
        throw SnapshotError(std::string("snapshot field is not uint64: ") + field);
    }
    return value.unsigned_value;
}

std::uint32_t unsigned32Value(const JsonValue& value, const char* field) {
    const std::uint64_t result = unsignedValue(value, field);
    if (result > std::numeric_limits<std::uint32_t>::max()) {
        throw SnapshotError(std::string("snapshot field exceeds uint32: ") + field);
    }
    return static_cast<std::uint32_t>(result);
}

std::int64_t signedValue(const JsonValue& value, const char* field) {
    if (value.kind == detail::JsonKind::Signed) {
        return value.signed_value;
    }
    if (value.kind == detail::JsonKind::Unsigned
        && value.unsigned_value <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return static_cast<std::int64_t>(value.unsigned_value);
    }
    throw SnapshotError(std::string("snapshot field is not int64: ") + field);
}

const std::string& stringValue(const JsonValue& value, const char* field) {
    if (value.kind != detail::JsonKind::String) {
        throw SnapshotError(std::string("snapshot field is not string: ") + field);
    }
    return value.string_value;
}

const JsonObject& objectValue(const JsonValue& value, const char* field) {
    if (value.kind != detail::JsonKind::Object) {
        throw SnapshotError(std::string("snapshot field is not object: ") + field);
    }
    return value.object_value;
}

const std::vector<JsonValue>& arrayValue(const JsonValue& value, const char* field) {
    if (value.kind != detail::JsonKind::Array) {
        throw SnapshotError(std::string("snapshot field is not array: ") + field);
    }
    return value.array_value;
}

FileIdentity readIdentity(const JsonValue& value) {
    const JsonObject& object = objectValue(value, "identity");
    exactKeys(object, {"device", "file", "valid"});
    FileIdentity identity;
    identity.device = unsignedValue(member(object, "device"), "identity.device");
    identity.file = unsignedValue(member(object, "file"), "identity.file");
    identity.valid = booleanValue(member(object, "valid"), "identity.valid");
    return identity;
}

FsMetadata readMetadata(const JsonValue& value) {
    const JsonObject& object = objectValue(value, "metadata");
    exactKeys(object, {"allocated_size", "allocated_size_known", "complete", "error", "group",
                       "hard_link_count", "hard_link_count_known", "identity", "kind",
                       "logical_size", "modified_ns", "modified_time_known", "owner",
                       "ownership_known", "permissions", "permissions_known"});
    FsMetadata metadata;
    metadata.allocated_size = unsignedValue(member(object, "allocated_size"), "allocated_size");
    metadata.allocated_size_known =
        booleanValue(member(object, "allocated_size_known"), "allocated_size_known");
    metadata.complete = booleanValue(member(object, "complete"), "complete");
    metadata.error = stringValue(member(object, "error"), "error");
    metadata.group = unsignedValue(member(object, "group"), "group");
    metadata.hard_link_count = unsignedValue(member(object, "hard_link_count"), "hard_link_count");
    metadata.hard_link_count_known =
        booleanValue(member(object, "hard_link_count_known"), "hard_link_count_known");
    metadata.identity = readIdentity(member(object, "identity"));
    metadata.kind = parseKind(stringValue(member(object, "kind"), "kind"));
    metadata.logical_size = unsignedValue(member(object, "logical_size"), "logical_size");
    metadata.modified_ns = signedValue(member(object, "modified_ns"), "modified_ns");
    metadata.modified_time_known =
        booleanValue(member(object, "modified_time_known"), "modified_time_known");
    metadata.owner = unsignedValue(member(object, "owner"), "owner");
    metadata.ownership_known = booleanValue(member(object, "ownership_known"), "ownership_known");
    metadata.permissions = unsigned32Value(member(object, "permissions"), "permissions");
    metadata.permissions_known =
        booleanValue(member(object, "permissions_known"), "permissions_known");
    return metadata;
}

struct ParseContext {
    SnapshotLimits limits;
    std::size_t nodes = 0;
};

FsNode readNode(const JsonValue& value, std::size_t depth, ParseContext& context) {
    if (depth > context.limits.max_depth) {
        throw SnapshotError("snapshot tree exceeds configured depth bound");
    }
    const JsonObject& object = objectValue(value, "node");
    exactKeys(object, {"allocated_size", "allocated_size_known", "children", "complete",
                       "cycle_skipped", "error", "followed", "has_target_metadata", "is_dir",
                       "logical_size_known", "metadata", "mount_boundary_skipped", "name",
                       "path", "reclaimable_size", "reclaimable_size_known", "size",
                       "target_metadata"});
    if (context.nodes >= context.limits.max_nodes) {
        throw SnapshotError("snapshot node count exceeds configured bound");
    }
    ++context.nodes;

    FsNode node;
    node.allocated_size = unsignedValue(member(object, "allocated_size"), "allocated_size");
    node.allocated_size_known =
        booleanValue(member(object, "allocated_size_known"), "allocated_size_known");
    node.complete = booleanValue(member(object, "complete"), "complete");
    node.cycle_skipped = booleanValue(member(object, "cycle_skipped"), "cycle_skipped");
    node.error = stringValue(member(object, "error"), "error");
    node.followed = booleanValue(member(object, "followed"), "followed");
    node.has_target_metadata =
        booleanValue(member(object, "has_target_metadata"), "has_target_metadata");
    node.is_dir = booleanValue(member(object, "is_dir"), "is_dir");
    node.logical_size_known =
        booleanValue(member(object, "logical_size_known"), "logical_size_known");
    node.metadata = readMetadata(member(object, "metadata"));
    node.mount_boundary_skipped =
        booleanValue(member(object, "mount_boundary_skipped"), "mount_boundary_skipped");
    node.name = stringValue(member(object, "name"), "name");
    node.path = std::filesystem::path(stringValue(member(object, "path"), "path"));
    node.reclaimable_size =
        unsignedValue(member(object, "reclaimable_size"), "reclaimable_size");
    node.reclaimable_size_known =
        booleanValue(member(object, "reclaimable_size_known"), "reclaimable_size_known");
    node.size = unsignedValue(member(object, "size"), "size");
    node.target_metadata = readMetadata(member(object, "target_metadata"));
    const std::vector<JsonValue>& children = arrayValue(member(object, "children"), "children");
    if (!node.is_dir && !children.empty()) {
        throw SnapshotError("snapshot contains children below a non-directory node");
    }
    if (depth == context.limits.max_depth && !children.empty()) {
        throw SnapshotError("snapshot tree exceeds configured depth bound");
    }
    node.children.reserve(children.size());
    for (const JsonValue& child : children) {
        node.children.push_back(readNode(child, depth + 1, context));
    }
    return node;
}

} // namespace

Snapshot parseSnapshot(std::string_view json, const SnapshotLimits& inputLimits) {
    const SnapshotLimits limits = detail::checkedSnapshotLimits(inputLimits);
    if (json.size() > limits.max_serialized_bytes) {
        throw SnapshotError("snapshot input exceeds configured byte bound");
    }
    const JsonValue rootValue = detail::parseJson(json, limits);
    const JsonObject& object = objectValue(rootValue, "snapshot");
    exactKeys(object, {"complete", "node_count", "root", "schema_version", "truncated"});
    if (stringValue(member(object, "schema_version"), "schema_version") != kSnapshotSchemaV1) {
        throw SnapshotError("unsupported diskmap snapshot schema");
    }
    const std::uint64_t expectedNodeCount = unsignedValue(member(object, "node_count"), "node_count");
    if (expectedNodeCount > limits.max_nodes) {
        throw SnapshotError("snapshot node count exceeds configured bound");
    }

    ParseContext context{limits};
    Snapshot snapshot;
    snapshot.schema_version = kSnapshotSchemaV1;
    snapshot.complete = booleanValue(member(object, "complete"), "complete");
    snapshot.truncated = booleanValue(member(object, "truncated"), "truncated");
    snapshot.root = readNode(member(object, "root"), 0, context);
    const detail::SnapshotTreeValidation validation =
        detail::validateSnapshotTree(snapshot.root, limits);
    if (context.nodes != expectedNodeCount || validation.nodes != expectedNodeCount) {
        throw SnapshotError("snapshot node_count does not match its tree");
    }
    if (snapshot.complete && validation.has_incomplete_evidence) {
        throw SnapshotError("complete snapshot contains incomplete evidence");
    }
    if (snapshot.truncated && snapshot.complete) {
        throw SnapshotError("truncated snapshot cannot be marked complete");
    }
    snapshot.nodes_retained = context.nodes;
    return snapshot;
}

} // namespace diskmap
