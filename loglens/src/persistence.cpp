#include "loglens/persistence.hpp"

#include "loglens/filter_expr.hpp"
#include "persistence_validation.hpp"
#include "storage_json.hpp"
#include "persistence_writer.hpp"

#include <charconv>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

#include "persistence_io.hpp"

namespace loglens {

namespace {

constexpr char kSourceSchema[] = "loglens.source-profiles/v1";
constexpr char kQuerySchema[] = "loglens.saved-queries/v1";
constexpr std::size_t kStorageJsonDepth = 16;
constexpr std::size_t kStorageJsonNodes = 2048;
constexpr std::size_t kStorageJsonMembers = 8;
constexpr std::size_t kStorageJsonStrings = kMaxFilterQueryBytes + 256;

bool hasOnlyFields(const detail::StorageJsonNode& object,
                   std::initializer_list<std::string_view> allowed) {
    if (object.kind != detail::StorageJsonKind::Object) {
        return false;
    }
    for (const auto& member : object.object) {
        bool accepted = false;
        for (const std::string_view name : allowed) {
            if (member.first == name) {
                accepted = true;
                break;
            }
        }
        if (!accepted) {
            return false;
        }
    }
    return true;
}

const detail::StorageJsonNode* requiredField(const detail::StorageJsonNode& object,
                                             std::string_view name,
                                             PersistenceError& error) {
    const detail::StorageJsonNode* field = detail::findStorageJsonField(object, name);
    if (field == nullptr) {
        detail::setPersistenceError(error, PersistenceErrorCode::Malformed,
                 "persistence object is missing '" + std::string(name) + "'");
    }
    return field;
}

bool requireString(const detail::StorageJsonNode& object, std::string_view name,
                   std::string& value, PersistenceError& error) {
    const detail::StorageJsonNode* field = requiredField(object, name, error);
    if (field == nullptr) {
        return false;
    }
    if (field->kind != detail::StorageJsonKind::String) {
        detail::setPersistenceError(error, PersistenceErrorCode::InvalidValue,
                 "persistence field '" + std::string(name) + "' must be a string");
        return false;
    }
    value = field->text;
    return true;
}

bool parseUnsigned(const detail::StorageJsonNode& object, std::string_view name,
                   std::size_t& value, PersistenceError& error) {
    const detail::StorageJsonNode* field = requiredField(object, name, error);
    if (field == nullptr) {
        return false;
    }
    if (field->kind != detail::StorageJsonKind::Number || field->text.empty()
        || field->text.front() == '-') {
        detail::setPersistenceError(error, PersistenceErrorCode::InvalidValue,
                 "persistence field '" + std::string(name) + "' must be an unsigned integer");
        return false;
    }
    std::uint64_t parsed = 0;
    const char* first = field->text.data();
    const char* last = first + field->text.size();
    const std::from_chars_result result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc() || result.ptr != last
        || parsed > std::numeric_limits<std::size_t>::max()) {
        detail::setPersistenceError(error, PersistenceErrorCode::InvalidValue,
                 "persistence field '" + std::string(name) + "' is not a bounded integer");
        return false;
    }
    value = static_cast<std::size_t>(parsed);
    return true;
}

bool expectedSchema(const detail::StorageJsonNode& root, const char* expected,
                    const char* prefix, PersistenceError& error) {
    std::string schema;
    if (!requireString(root, "schema", schema, error)) {
        return false;
    }
    if (schema == expected) {
        return true;
    }
    if (schema.compare(0, std::string_view(prefix).size(), prefix) == 0) {
        detail::setPersistenceError(error, PersistenceErrorCode::UnsupportedVersion,
                 "unsupported persistence schema '" + schema + "'");
    } else {
        detail::setPersistenceError(error, PersistenceErrorCode::InvalidValue,
                 "unexpected persistence schema '" + schema + "'");
    }
    return false;
}

detail::StorageJsonLimits persistenceJsonLimits() {
    detail::StorageJsonLimits limits;
    limits.max_depth = kStorageJsonDepth;
    limits.max_nodes = kStorageJsonNodes;
    limits.max_object_members = kStorageJsonMembers;
    limits.max_array_items = kMaxPersistedItems;
    limits.max_string_bytes = kStorageJsonStrings;
    return limits;
}

PersistenceErrorCode storageErrorCode(const std::string& message) {
    if (message.find("limit exceeded") != std::string::npos
        || message.find("limit must") != std::string::npos
        || message.find("nesting depth") != std::string::npos) {
        return PersistenceErrorCode::LimitExceeded;
    }
    return PersistenceErrorCode::Malformed;
}

bool parseProfile(const detail::StorageJsonNode& object, SourceProfile& profile,
                  PersistenceError& error) {
    if (!hasOnlyFields(object, {"name", "format", "multiline", "max_record_bytes"})) {
        detail::setPersistenceError(error, PersistenceErrorCode::Malformed,
                 "source profile contains an unknown or duplicate field");
        return false;
    }
    std::string format;
    std::string multiline;
    if (!requireString(object, "name", profile.name, error)
        || !requireString(object, "format", format, error)
        || !requireString(object, "multiline", multiline, error)
        || !parseUnsigned(object, "max_record_bytes", profile.max_record_bytes, error)) {
        return false;
    }
    const std::optional<Format> parsedFormat = parseFormatName(format);
    if (!parsedFormat || formatName(*parsedFormat) != format) {
        detail::setPersistenceError(error, PersistenceErrorCode::InvalidValue,
                 "source profile format must use a canonical format name");
        return false;
    }
    profile.format = *parsedFormat;
    const std::optional<MultilinePolicy> parsedMultiline =
        parseMultilinePolicyName(multiline);
    if (!parsedMultiline) {
        detail::setPersistenceError(error, PersistenceErrorCode::InvalidValue,
                                    "source profile multiline policy is unknown");
        return false;
    }
    profile.multiline = *parsedMultiline;
    return detail::validSourceProfile(profile, error);
}

bool parseQuery(const detail::StorageJsonNode& object, SavedQuery& query,
                PersistenceError& error) {
    if (!hasOnlyFields(object, {"name", "expression"})) {
        detail::setPersistenceError(error, PersistenceErrorCode::Malformed,
                 "saved query contains an unknown or duplicate field");
        return false;
    }
    if (!requireString(object, "name", query.name, error)
        || !requireString(object, "expression", query.expression, error)) {
        return false;
    }
    return detail::validSavedQuery(query, error);
}

SourceProfileLoadResult loadProfilesImpl(const std::string& path) {
    SourceProfileLoadResult result;
    std::string bytes;
    if (!detail::readBoundedPersistenceFile(path, bytes, result.found, result.error)
        || !result.found) {
        return result;
    }
    detail::StorageJsonNode root;
    detail::StorageJsonError jsonError;
    if (!detail::parseStorageJson(bytes, persistenceJsonLimits(), root, jsonError)) {
        detail::setPersistenceError(result.error, storageErrorCode(jsonError.message),
                 "malformed source profile JSON: " + jsonError.message, jsonError.offset);
        return result;
    }
    if (!hasOnlyFields(root, {"schema", "profiles"})
        || !expectedSchema(root, kSourceSchema, "loglens.source-profiles/", result.error)) {
        if (result.error.ok()) {
            detail::setPersistenceError(result.error, PersistenceErrorCode::Malformed,
                     "source profile document contains unknown fields");
        }
        return result;
    }
    const detail::StorageJsonNode* values = requiredField(root, "profiles", result.error);
    if (values == nullptr) {
        return result;
    }
    if (values->kind != detail::StorageJsonKind::Array) {
        detail::setPersistenceError(result.error, PersistenceErrorCode::InvalidValue,
                 "source profile 'profiles' must be an array");
        return result;
    }
    std::vector<SourceProfile> parsed;
    parsed.reserve(values->array.size());
    for (const detail::StorageJsonNode& item : values->array) {
        SourceProfile profile;
        if (!parseProfile(item, profile, result.error)) {
            return result;
        }
        parsed.push_back(std::move(profile));
    }
    if (!detail::validateAndSortProfiles(parsed, result.error)) {
        return result;
    }
    result.profiles = std::move(parsed);
    return result;
}

SavedQueryLoadResult loadQueriesImpl(const std::string& path) {
    SavedQueryLoadResult result;
    std::string bytes;
    if (!detail::readBoundedPersistenceFile(path, bytes, result.found, result.error)
        || !result.found) {
        return result;
    }
    detail::StorageJsonNode root;
    detail::StorageJsonError jsonError;
    if (!detail::parseStorageJson(bytes, persistenceJsonLimits(), root, jsonError)) {
        detail::setPersistenceError(result.error, storageErrorCode(jsonError.message),
                 "malformed saved query JSON: " + jsonError.message, jsonError.offset);
        return result;
    }
    if (!hasOnlyFields(root, {"schema", "queries"})
        || !expectedSchema(root, kQuerySchema, "loglens.saved-queries/", result.error)) {
        if (result.error.ok()) {
            detail::setPersistenceError(result.error, PersistenceErrorCode::Malformed,
                     "saved query document contains unknown fields");
        }
        return result;
    }
    const detail::StorageJsonNode* values = requiredField(root, "queries", result.error);
    if (values == nullptr) {
        return result;
    }
    if (values->kind != detail::StorageJsonKind::Array) {
        detail::setPersistenceError(result.error, PersistenceErrorCode::InvalidValue,
                 "saved query 'queries' must be an array");
        return result;
    }
    std::vector<SavedQuery> parsed;
    parsed.reserve(values->array.size());
    for (const detail::StorageJsonNode& item : values->array) {
        SavedQuery query;
        if (!parseQuery(item, query, result.error)) {
            return result;
        }
        parsed.push_back(std::move(query));
    }
    if (!detail::validateAndSortQueries(parsed, result.error)) {
        return result;
    }
    result.queries = std::move(parsed);
    return result;
}

} // namespace

const char* persistenceErrorCodeName(PersistenceErrorCode code) {
    switch (code) {
        case PersistenceErrorCode::None: return "none";
        case PersistenceErrorCode::Io: return "io";
        case PersistenceErrorCode::Malformed: return "malformed";
        case PersistenceErrorCode::UnsupportedVersion: return "unsupported-version";
        case PersistenceErrorCode::InvalidValue: return "invalid-value";
        case PersistenceErrorCode::DuplicateName: return "duplicate-name";
        case PersistenceErrorCode::InvalidQuery: return "invalid-query";
        case PersistenceErrorCode::LimitExceeded: return "limit-exceeded";
        case PersistenceErrorCode::UnsafePath: return "unsafe-path";
        case PersistenceErrorCode::AtomicReplace: return "atomic-replace";
    }
    return "invalid";
}

const char* sourceProfileSchemaName() { return kSourceSchema; }

const char* savedQuerySchemaName() { return kQuerySchema; }

SourceProfileLoadResult loadSourceProfiles(const std::string& path) {
    try {
        return loadProfilesImpl(path);
    } catch (...) {
        SourceProfileLoadResult result;
        detail::setPersistenceError(result.error, PersistenceErrorCode::Io,
                 "unexpected failure while loading source profiles");
        return result;
    }
}

SavedQueryLoadResult loadSavedQueries(const std::string& path) {
    try {
        return loadQueriesImpl(path);
    } catch (...) {
        SavedQueryLoadResult result;
        detail::setPersistenceError(result.error, PersistenceErrorCode::Io,
                 "unexpected failure while loading saved queries");
        return result;
    }
}

bool saveSourceProfiles(const std::string& path,
                        const std::vector<SourceProfile>& profiles,
                        PersistenceError& error) {
    error = PersistenceError{};
    try {
        std::vector<SourceProfile> sorted = profiles;
        if (!detail::validateAndSortProfiles(sorted, error)) {
            return false;
        }
        const std::string bytes = detail::serializeSourceProfiles(sorted);
        return detail::atomicWritePersistenceFile(path, bytes, error);
    } catch (...) {
        detail::setPersistenceError(error, PersistenceErrorCode::Io,
                 "unexpected failure while saving source profiles");
        return false;
    }
}

bool saveSavedQueries(const std::string& path,
                      const std::vector<SavedQuery>& queries,
                      PersistenceError& error) {
    error = PersistenceError{};
    try {
        std::vector<SavedQuery> sorted = queries;
        if (!detail::validateAndSortQueries(sorted, error)) {
            return false;
        }
        const std::string bytes = detail::serializeSavedQueries(sorted);
        return detail::atomicWritePersistenceFile(path, bytes, error);
    } catch (...) {
        detail::setPersistenceError(error, PersistenceErrorCode::Io,
                 "unexpected failure while saving saved queries");
        return false;
    }
}

} // namespace loglens
