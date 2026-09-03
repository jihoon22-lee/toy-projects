#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace loglens::detail {

enum class StorageJsonKind { Null, Boolean, Number, String, Object, Array };

struct StorageJsonNode {
    StorageJsonKind kind = StorageJsonKind::Null;
    std::string text;
    std::vector<std::pair<std::string, StorageJsonNode>> object;
    std::vector<StorageJsonNode> array;
};

struct StorageJsonError {
    std::size_t offset = 0;
    std::string message;
};

struct StorageJsonLimits {
    std::size_t max_depth = 32;
    std::size_t max_nodes = 2048;
    std::size_t max_object_members = 256;
    std::size_t max_array_items = 256;
    std::size_t max_string_bytes = 64 * 1024;
};

bool parseStorageJson(std::string_view input, const StorageJsonLimits& limits,
                      StorageJsonNode& root, StorageJsonError& error);

const StorageJsonNode* findStorageJsonField(
    const StorageJsonNode& object, std::string_view name);

} // namespace loglens::detail
