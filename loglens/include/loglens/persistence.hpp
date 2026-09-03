#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "loglens/log_parser.hpp"

namespace loglens {

// Persistence is intentionally small and versioned.  These bounds apply
// before any caller-owned model is mutated, so a damaged or hostile config
// cannot turn a startup path into an unbounded allocation.
constexpr std::size_t kMaxPersistenceFileBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxPersistedItems = 128;
constexpr std::size_t kMaxPersistedNameBytes = 128;

struct SourceProfile {
    std::string name;
    Format format = Format::Auto;
    MultilinePolicy multiline = MultilinePolicy::FoldContinuations;
    std::size_t max_record_bytes = kDefaultMaxRecordBytes;
};

struct SavedQuery {
    std::string name;
    std::string expression;
};

enum class PersistenceErrorCode {
    None,
    Io,
    Malformed,
    UnsupportedVersion,
    InvalidValue,
    DuplicateName,
    InvalidQuery,
    LimitExceeded,
    UnsafePath,
    AtomicReplace,
};

struct PersistenceError {
    PersistenceErrorCode code = PersistenceErrorCode::None;
    std::size_t offset = 0;
    std::string message;

    bool ok() const { return code == PersistenceErrorCode::None; }
};

struct SourceProfileLoadResult {
    bool found = false;
    std::vector<SourceProfile> profiles;
    PersistenceError error;

    bool ok() const { return error.ok(); }
};

struct SavedQueryLoadResult {
    bool found = false;
    std::vector<SavedQuery> queries;
    PersistenceError error;

    bool ok() const { return error.ok(); }
};

const char* persistenceErrorCodeName(PersistenceErrorCode code);
const char* sourceProfileSchemaName();
const char* savedQuerySchemaName();

// Missing optional stores are successful empty loads (found == false).  Any
// present file must be a complete supported schema; unknown versions, unknown
// fields, duplicate keys/names, invalid values, and invalid Filter syntax are
// rejected without exposing partial results.
SourceProfileLoadResult loadSourceProfiles(const std::string& path);
SavedQueryLoadResult loadSavedQueries(const std::string& path);

// Saves use a same-directory temporary file followed by atomic replacement on
// POSIX filesystems.  The destination is never truncated before serialization
// and validation succeeds.  The API does not claim fsync-level durability.
bool saveSourceProfiles(const std::string& path,
                        const std::vector<SourceProfile>& profiles,
                        PersistenceError& error);
bool saveSavedQueries(const std::string& path,
                      const std::vector<SavedQuery>& queries,
                      PersistenceError& error);

} // namespace loglens
