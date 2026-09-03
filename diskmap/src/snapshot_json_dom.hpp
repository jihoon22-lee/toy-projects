#pragma once

#include "snapshot_internal.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace diskmap {
namespace detail {

enum class JsonKind { Null, Boolean, Unsigned, Signed, String, Object, Array };

struct JsonValue {
    JsonKind kind = JsonKind::Null;
    bool boolean = false;
    std::uint64_t unsigned_value = 0;
    std::int64_t signed_value = 0;
    std::string string_value;
    std::map<std::string, JsonValue> object_value;
    std::vector<JsonValue> array_value;
};

using JsonObject = std::map<std::string, JsonValue>;

JsonValue parseJson(std::string_view input, const SnapshotLimits& limits);

} // namespace detail
} // namespace diskmap
