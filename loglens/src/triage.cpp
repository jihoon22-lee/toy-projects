#include "loglens/triage.hpp"

#include "persistence_io.hpp"
#include "storage_json.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <initializer_list>
#include <limits>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

namespace loglens {
namespace {

constexpr char kSchema[] = "loglens.triage/v1";
constexpr char kLegacySchema[] = "loglens.triage/v0";

void setError(PersistenceError& error, PersistenceErrorCode code,
              std::string message, std::size_t offset = 0) {
    if (error.ok()) {
        error.code = code;
        error.message = std::move(message);
        error.offset = offset;
    }
}

bool onlyFields(const detail::StorageJsonNode& object,
                std::initializer_list<std::string_view> allowed) {
    if (object.kind != detail::StorageJsonKind::Object) {
        return false;
    }
    std::set<std::string_view> seen;
    for (const auto& member : object.object) {
        if (std::find(allowed.begin(), allowed.end(), member.first) == allowed.end()) {
            return false;
        }
        if (!seen.insert(member.first).second) {
            return false;
        }
    }
    return true;
}

const detail::StorageJsonNode* field(const detail::StorageJsonNode& object,
                                     std::string_view name) {
    return detail::findStorageJsonField(object, name);
}

bool getString(const detail::StorageJsonNode& object, std::string_view name,
               std::string& value, PersistenceError& error) {
    const auto* node = field(object, name);
    if (node == nullptr || node->kind != detail::StorageJsonKind::String) {
        setError(error, PersistenceErrorCode::InvalidValue,
                 "triage field '" + std::string(name) + "' must be a string");
        return false;
    }
    value = node->text;
    return true;
}

bool getBoolean(const detail::StorageJsonNode& object, std::string_view name,
                bool& value, PersistenceError& error) {
    const auto* node = field(object, name);
    if (node == nullptr || node->kind != detail::StorageJsonKind::Boolean) {
        setError(error, PersistenceErrorCode::InvalidValue,
                 "triage field '" + std::string(name) + "' must be a boolean");
        return false;
    }
    value = node->text == "true";
    return true;
}

bool getInteger(const detail::StorageJsonNode& object, std::string_view name,
                std::size_t& value, PersistenceError& error) {
    const auto* node = field(object, name);
    if (node == nullptr || node->kind != detail::StorageJsonKind::Number
        || node->text.empty() || node->text.front() == '-') {
        setError(error, PersistenceErrorCode::InvalidValue,
                 "triage field '" + std::string(name) + "' must be an unsigned integer");
        return false;
    }
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(node->text.data(),
                                        node->text.data() + node->text.size(), parsed);
    if (result.ec != std::errc() || result.ptr != node->text.data() + node->text.size()
        || parsed > std::numeric_limits<std::size_t>::max()) {
        setError(error, PersistenceErrorCode::InvalidValue,
                 "triage integer is outside the supported range");
        return false;
    }
    value = static_cast<std::size_t>(parsed);
    return true;
}

bool getSignedInteger(const detail::StorageJsonNode& object, std::string_view name,
                      int& value, PersistenceError& error) {
    const auto* node = field(object, name);
    if (node == nullptr || node->kind != detail::StorageJsonKind::Number
        || node->text.empty()) {
        setError(error, PersistenceErrorCode::InvalidValue,
                 "triage field '" + std::string(name) + "' must be an integer");
        return false;
    }
    int parsed = 0;
    const auto result = std::from_chars(node->text.data(),
                                        node->text.data() + node->text.size(), parsed);
    if (result.ec != std::errc() || result.ptr != node->text.data() + node->text.size()) {
        setError(error, PersistenceErrorCode::InvalidValue,
                 "triage priority is outside the supported range");
        return false;
    }
    value = parsed;
    return true;
}

bool safeStyle(const std::string& style) {
    if (style.empty() || style.size() > 64) return false;
    if (style.front() == '#') {
        const std::size_t digits = style.size() - 1;
        return (digits == 3 || digits == 4 || digits == 6 || digits == 8)
               && std::all_of(style.begin() + 1, style.end(), [](char character) {
                      return std::isxdigit(static_cast<unsigned char>(character)) != 0;
                  });
    }
    return std::all_of(style.begin(), style.end(), [](char character) {
        const unsigned char byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) != 0 || character == '-' || character == '_';
    });
}

bool validateRule(const NamedHighlightRule& value, PersistenceError& error) {
    if (value.name.empty() || value.name.size() > kMaxPersistedNameBytes) {
        setError(error, PersistenceErrorCode::InvalidValue,
                 "highlight rule name must be 1..128 bytes");
        return false;
    }
    if ((!value.rule.whole_line && value.rule.pattern.empty())
        || value.rule.pattern.size() > kMaxHighlightPatternBytes) {
        setError(error, PersistenceErrorCode::InvalidValue,
                 "highlight pattern must be non-empty unless whole-line and at most 1024 bytes");
        return false;
    }
    if (!safeStyle(value.rule.style)) {
        setError(error, PersistenceErrorCode::InvalidValue,
                 "highlight style contains unsupported characters or length");
        return false;
    }
    return true;
}

bool validateEntry(const TriageEntry& value, PersistenceError& error) {
    if (value.source_path.empty() || value.source_path.size() > 4096 || value.line_number == 0
        || value.annotation.size() > kMaxAnnotationBytes) {
        setError(error, PersistenceErrorCode::InvalidValue,
                 "triage entry path, line, or annotation is outside the supported bounds");
        return false;
    }
    if (!value.bookmarked && value.annotation.empty()) {
        setError(error, PersistenceErrorCode::InvalidValue,
                 "empty unbookmarked triage entries are not persisted");
        return false;
    }
    return true;
}

bool parseRule(const detail::StorageJsonNode& node, NamedHighlightRule& value,
               PersistenceError& error, bool legacy, std::size_t index) {
    if (legacy) {
        if (!onlyFields(node, {"pattern", "color"})
            || !getString(node, "pattern", value.rule.pattern, error)
            || !getString(node, "color", value.rule.style, error)) {
            setError(error, PersistenceErrorCode::Malformed,
                     "legacy triage rule has an invalid shape");
            return false;
        }
        value.name = "Migrated rule " + std::to_string(index + 1);
        value.rule.priority = static_cast<int>(kMaxHighlightRules - index);
        return validateRule(value, error);
    }
    if (!onlyFields(node, {"name", "pattern", "whole_line", "priority", "style"})
        || !getString(node, "name", value.name, error)
        || !getString(node, "pattern", value.rule.pattern, error)
        || !getBoolean(node, "whole_line", value.rule.whole_line, error)
        || !getSignedInteger(node, "priority", value.rule.priority, error)
        || !getString(node, "style", value.rule.style, error)) {
        setError(error, PersistenceErrorCode::Malformed,
                 "triage highlight rule has an invalid shape");
        return false;
    }
    return validateRule(value, error);
}

bool parseEntry(const detail::StorageJsonNode& node, TriageEntry& value,
                PersistenceError& error) {
    if (!onlyFields(node, {"source_path", "line_number", "bookmarked", "annotation"})
        || !getString(node, "source_path", value.source_path, error)
        || !getInteger(node, "line_number", value.line_number, error)
        || !getBoolean(node, "bookmarked", value.bookmarked, error)
        || !getString(node, "annotation", value.annotation, error)) {
        setError(error, PersistenceErrorCode::Malformed,
                 "triage entry has an invalid shape");
        return false;
    }
    return validateEntry(value, error);
}

std::string escaped(const std::string& value) {
    std::ostringstream output;
    output << '"';
    const char* digits = "0123456789abcdef";
    for (const unsigned char character : value) {
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20U) {
                    output << "\\u00" << digits[character >> 4U] << digits[character & 0x0fU];
                } else {
                    output << static_cast<char>(character);
                }
        }
    }
    output << '"';
    return output.str();
}

std::string serialize(const TriageState& state) {
    std::ostringstream output;
    output << "{\"schema\":" << escaped(kSchema) << ",\"rules\":[";
    for (std::size_t index = 0; index < state.rules.size(); ++index) {
        if (index != 0) output << ',';
        const auto& item = state.rules[index];
        output << "{\"name\":" << escaped(item.name)
               << ",\"pattern\":" << escaped(item.rule.pattern)
               << ",\"whole_line\":" << (item.rule.whole_line ? "true" : "false")
               << ",\"priority\":" << item.rule.priority
               << ",\"style\":" << escaped(item.rule.style) << '}';
    }
    output << "],\"entries\":[";
    for (std::size_t index = 0; index < state.entries.size(); ++index) {
        if (index != 0) output << ',';
        const auto& item = state.entries[index];
        output << "{\"source_path\":" << escaped(item.source_path)
               << ",\"line_number\":" << item.line_number
               << ",\"bookmarked\":" << (item.bookmarked ? "true" : "false")
               << ",\"annotation\":" << escaped(item.annotation) << '}';
    }
    output << "]}\n";
    return output.str();
}

} // namespace

const char* triageSchemaName() { return kSchema; }

bool validateTriageState(const TriageState& state, PersistenceError& error) {
    error = PersistenceError{};
    if (state.rules.size() > kMaxHighlightRules || state.entries.size() > kMaxTriageEntries) {
        setError(error, PersistenceErrorCode::LimitExceeded,
                 "triage state exceeds the rule or entry count limit");
        return false;
    }
    std::set<std::string> names;
    for (const auto& rule : state.rules) {
        if (!validateRule(rule, error) || !names.insert(rule.name).second) {
            if (error.ok()) {
                setError(error, PersistenceErrorCode::DuplicateName,
                         "highlight rule names must be unique");
            }
            return false;
        }
    }
    std::set<std::pair<std::string, std::size_t>> identities;
    for (const auto& entry : state.entries) {
        const auto identity = std::make_pair(entry.source_path, entry.line_number);
        if (!validateEntry(entry, error) || !identities.insert(identity).second) {
            if (error.ok()) {
                setError(error, PersistenceErrorCode::DuplicateName,
                         "triage source/line identities must be unique");
            }
            return false;
        }
    }
    return true;
}

TriageLoadResult loadTriageState(const std::string& path) {
    TriageLoadResult result;
    std::string bytes;
    if (!detail::readBoundedPersistenceFile(path, bytes, result.found, result.error)
        || !result.found) {
        return result;
    }
    detail::StorageJsonNode root;
    detail::StorageJsonError parse_error;
    detail::StorageJsonLimits limits;
    limits.max_depth = 12;
    limits.max_nodes = 20 + kMaxHighlightRules * 8 + kMaxTriageEntries * 7;
    limits.max_object_members = 8;
    limits.max_array_items = kMaxTriageEntries;
    limits.max_string_bytes = kMaxAnnotationBytes + 4096;
    if (!detail::parseStorageJson(bytes, limits, root, parse_error)) {
        setError(result.error, PersistenceErrorCode::Malformed,
                 "malformed triage JSON: " + parse_error.message, parse_error.offset);
        return result;
    }
    std::string schema;
    if (!getString(root, "schema", schema, result.error)) {
        return result;
    }
    const bool legacy = schema == kLegacySchema;
    if (!legacy && schema != kSchema) {
        setError(result.error, PersistenceErrorCode::UnsupportedVersion,
                 "unsupported triage schema '" + schema + "'");
        return result;
    }
    if (!onlyFields(root, legacy ? std::initializer_list<std::string_view>{"schema", "rules"}
                                 : std::initializer_list<std::string_view>{"schema", "rules", "entries"})) {
        setError(result.error, PersistenceErrorCode::Malformed,
                 "triage document contains unknown or duplicate fields");
        return result;
    }
    const auto* rules = field(root, "rules");
    const auto* entries = legacy ? nullptr : field(root, "entries");
    if (rules == nullptr || rules->kind != detail::StorageJsonKind::Array
        || (!legacy && (entries == nullptr || entries->kind != detail::StorageJsonKind::Array))) {
        setError(result.error, PersistenceErrorCode::InvalidValue,
                 "triage rules and entries must be arrays");
        return result;
    }
    if (rules->array.size() > kMaxHighlightRules
        || (entries != nullptr && entries->array.size() > kMaxTriageEntries)) {
        setError(result.error, PersistenceErrorCode::LimitExceeded,
                 "triage document exceeds item limits");
        return result;
    }
    for (std::size_t index = 0; index < rules->array.size(); ++index) {
        NamedHighlightRule value;
        if (!parseRule(rules->array[index], value, result.error, legacy, index)) {
            return result;
        }
        result.state.rules.push_back(std::move(value));
    }
    if (entries != nullptr) {
        for (const auto& node : entries->array) {
            TriageEntry value;
            if (!parseEntry(node, value, result.error)) {
                return result;
            }
            result.state.entries.push_back(std::move(value));
        }
    }
    if (!validateTriageState(result.state, result.error)) {
        return result;
    }
    result.migrated = legacy;
    return result;
}

bool saveTriageState(const std::string& path, const TriageState& state,
                     PersistenceError& error) {
    if (!validateTriageState(state, error)) {
        return false;
    }
    try {
        return detail::atomicWritePersistenceFile(path, serialize(state), error);
    } catch (...) {
        setError(error, PersistenceErrorCode::Io,
                 "unexpected failure while saving triage state");
        return false;
    }
}

HighlightRules compileHighlightRules(const TriageState& state) {
    HighlightRules result;
    for (const auto& item : state.rules) {
        result.add(item.rule);
    }
    return result;
}

bool upsertHighlightRule(TriageState& state, NamedHighlightRule rule,
                         PersistenceError& error) {
    error = PersistenceError{};
    if (!validateRule(rule, error)) return false;
    TriageState next = state;
    auto found = std::find_if(next.rules.begin(), next.rules.end(), [&](const auto& item) {
        return item.name == rule.name;
    });
    if (found == next.rules.end()) {
        if (next.rules.size() >= kMaxHighlightRules) {
            setError(error, PersistenceErrorCode::LimitExceeded,
                     "highlight rule count exceeds 128");
            return false;
        }
        next.rules.push_back(std::move(rule));
    } else {
        *found = std::move(rule);
    }
    if (!validateTriageState(next, error)) return false;
    state = std::move(next);
    return true;
}

bool removeHighlightRule(TriageState& state, const std::string& name,
                         PersistenceError& error) {
    error = PersistenceError{};
    auto found = std::find_if(state.rules.begin(), state.rules.end(), [&](const auto& item) {
        return item.name == name;
    });
    if (found == state.rules.end()) {
        setError(error, PersistenceErrorCode::InvalidValue,
                 "highlight rule does not exist");
        return false;
    }
    state.rules.erase(found);
    return true;
}

bool moveHighlightRule(TriageState& state, std::size_t from, std::size_t to,
                       PersistenceError& error) {
    error = PersistenceError{};
    if (from >= state.rules.size() || to >= state.rules.size()) {
        setError(error, PersistenceErrorCode::InvalidValue,
                 "highlight rule move is outside the list");
        return false;
    }
    if (from == to) return true;
    NamedHighlightRule value = std::move(state.rules[from]);
    state.rules.erase(state.rules.begin() + static_cast<std::ptrdiff_t>(from));
    state.rules.insert(state.rules.begin() + static_cast<std::ptrdiff_t>(to), std::move(value));
    return true;
}

bool setTriageEntry(TriageState& state, TriageEntry entry,
                    PersistenceError& error) {
    error = PersistenceError{};
    const auto same = [&](const TriageEntry& value) {
        return value.source_path == entry.source_path && value.line_number == entry.line_number;
    };
    TriageState next = state;
    auto found = std::find_if(next.entries.begin(), next.entries.end(), same);
    if (!entry.bookmarked && entry.annotation.empty()) {
        if (found != next.entries.end()) next.entries.erase(found);
    } else {
        if (!validateEntry(entry, error)) return false;
        if (found == next.entries.end()) {
            if (next.entries.size() >= kMaxTriageEntries) {
                setError(error, PersistenceErrorCode::LimitExceeded,
                         "triage entry count exceeds 8192");
                return false;
            }
            next.entries.push_back(std::move(entry));
        } else {
            *found = std::move(entry);
        }
    }
    if (!validateTriageState(next, error)) return false;
    state = std::move(next);
    return true;
}

} // namespace loglens
