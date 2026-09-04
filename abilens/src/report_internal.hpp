#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "abilens/model.hpp"

namespace abilens {
namespace detail {

std::string trim(std::string value);
std::vector<std::uint64_t> version_parts(const std::string& value);
bool version_less(const std::string& left, const std::string& right);
std::string maximum_version(const ElfReport& report, const std::string& namespace_name);
bool is_absolute_path(const std::string& value);
std::size_t valid_utf8_sequence_length(const std::string& value, std::size_t offset);
std::string json_escape(const std::string& value);
std::string json_bool(bool value);
std::string json_string_array(const std::vector<std::string>& values);
std::vector<std::string> sorted_strings(const std::vector<std::string>& values);

struct JsonValue {
    enum class Kind { Null, Boolean, Number, String, Array, Object };
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue>;

    Kind kind = Kind::Null;
    bool boolean = false;
    std::string scalar;
    Array array;
    Object object;

    static JsonValue boolean_value(bool value) {
        JsonValue result;
        result.kind = Kind::Boolean;
        result.boolean = value;
        return result;
    }

    static JsonValue string_value(std::string value) {
        JsonValue result;
        result.kind = Kind::String;
        result.scalar = std::move(value);
        return result;
    }
};

JsonValue parse_json(const std::string& input);

}  // namespace detail
}  // namespace abilens
