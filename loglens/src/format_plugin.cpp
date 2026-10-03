#include "loglens/format_plugin.hpp"

#include "persistence_io.hpp"
#include "storage_json.hpp"

#include <cstdint>

namespace loglens {

namespace {

constexpr std::size_t kMaxPluginNameBytes = 128;
constexpr std::size_t kMaxPluginPatternBytes = 8192;
constexpr std::size_t kMaxCaptureGroup = 32;

const char* kPluginKind = "loglens.format/v1";

bool parseGroupNumber(const detail::StorageJsonNode& object, const char* field,
                      std::size_t& value, std::string& error) {
    const detail::StorageJsonNode* node = detail::findStorageJsonField(object, field);
    if (node == nullptr) {
        value = 0;
        return true;
    }
    if (node->kind != detail::StorageJsonKind::Number) {
        error = std::string("fields.") + field + " must be a capture-group number";
        return false;
    }
    try {
        const unsigned long long parsed = std::stoull(node->text);
        if (parsed == 0 || parsed > kMaxCaptureGroup) {
            error = std::string("fields.") + field +
                    " must be a capture-group number between 1 and 32";
            return false;
        }
        value = static_cast<std::size_t>(parsed);
        return true;
    } catch (...) {
        error = std::string("fields.") + field + " must be a capture-group number";
        return false;
    }
}

} // namespace

const char* formatPluginErrorName(FormatPluginError code) {
    switch (code) {
        case FormatPluginError::None: return "none";
        case FormatPluginError::ReadFailed: return "read-failed";
        case FormatPluginError::MalformedDocument: return "malformed-document";
        case FormatPluginError::UnsupportedVersion: return "unsupported-version";
        case FormatPluginError::MissingField: return "missing-field";
        case FormatPluginError::InvalidField: return "invalid-field";
    }
    return "unknown";
}

FormatPluginError loadFormatPlugin(const std::string& path, FormatPlugin& plugin,
                                   std::string& error) {
    plugin = FormatPlugin{};

    std::string bytes;
    bool found = false;
    PersistenceError readError;
    if (!detail::readBoundedPersistenceFile(path, bytes, found, readError)) {
        error = readError.message;
        return FormatPluginError::ReadFailed;
    }
    if (!found) {
        error = "format plugin does not exist: " + path;
        return FormatPluginError::ReadFailed;
    }

    detail::StorageJsonLimits limits;
    detail::StorageJsonNode root;
    detail::StorageJsonError jsonError;
    if (!detail::parseStorageJson(bytes, limits, root, jsonError)) {
        error = "malformed format plugin: " + jsonError.message;
        return FormatPluginError::MalformedDocument;
    }
    if (root.kind != detail::StorageJsonKind::Object) {
        error = "format plugin must be a JSON object";
        return FormatPluginError::MalformedDocument;
    }

    const detail::StorageJsonNode* kind = detail::findStorageJsonField(root, "kind");
    if (kind == nullptr || kind->kind != detail::StorageJsonKind::String) {
        error = "format plugin is missing its kind";
        return FormatPluginError::MissingField;
    }
    if (kind->text != kPluginKind) {
        error = "unsupported format plugin kind: " + kind->text;
        return FormatPluginError::UnsupportedVersion;
    }

    const detail::StorageJsonNode* name = detail::findStorageJsonField(root, "name");
    if (name == nullptr || name->kind != detail::StorageJsonKind::String ||
        name->text.empty() || name->text.size() > kMaxPluginNameBytes) {
        error = "format plugin needs a non-empty name of at most 128 bytes";
        return FormatPluginError::InvalidField;
    }
    plugin.name = name->text;

    const detail::StorageJsonNode* pattern =
        detail::findStorageJsonField(root, "pattern");
    if (pattern == nullptr || pattern->kind != detail::StorageJsonKind::String ||
        pattern->text.empty() || pattern->text.size() > kMaxPluginPatternBytes) {
        error = "format plugin needs a non-empty pattern of at most 8192 bytes";
        return FormatPluginError::InvalidField;
    }
    plugin.pattern_text = pattern->text;
    try {
        plugin.pattern = std::regex(plugin.pattern_text, std::regex::ECMAScript);
    } catch (const std::regex_error& failure) {
        error = std::string("invalid plugin pattern: ") + failure.what();
        return FormatPluginError::InvalidField;
    }

    const detail::StorageJsonNode* fields =
        detail::findStorageJsonField(root, "fields");
    if (fields == nullptr || fields->kind != detail::StorageJsonKind::Object) {
        error = "format plugin needs a fields object mapping capture groups";
        return FormatPluginError::MissingField;
    }
    FormatFieldMap map;
    if (!parseGroupNumber(*fields, "timestamp", map.timestamp, error) ||
        !parseGroupNumber(*fields, "level", map.level, error) ||
        !parseGroupNumber(*fields, "source", map.source, error) ||
        !parseGroupNumber(*fields, "message", map.message, error)) {
        return FormatPluginError::InvalidField;
    }
    if (map.message == 0) {
        error = "format plugin must map a message capture group";
        return FormatPluginError::MissingField;
    }
    const std::size_t groups = plugin.pattern.mark_count();
    const std::size_t used[] = {map.timestamp, map.level, map.source, map.message};
    for (const std::size_t group : used) {
        if (group > groups) {
            error = "fields maps a capture group the pattern does not define";
            return FormatPluginError::InvalidField;
        }
    }
    plugin.fields = map;
    return FormatPluginError::None;
}

} // namespace loglens
