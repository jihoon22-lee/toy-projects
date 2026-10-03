#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "loglens/triage_types.hpp"
#include "loglens/persistence.hpp"

namespace loglens {

struct TriageLoadResult {
    bool found = false;
    bool migrated = false;
    TriageState state;
    PersistenceError error;

    bool ok() const { return error.ok(); }
};

const char* triageSchemaName();

// Loads v2 and bounded v1/v0 legacy shapes. Unsupported note evidence remains
// unbound; migration is persisted as v2 only when the caller chooses to save.
TriageLoadResult parseTriageState(const std::string &bytes);
std::string serializeTriageState(const TriageState &state);
bool matchesTriageEntry(const TriageEntry &entry, const std::string &sourcePath,
                        const std::string &identity, std::uint64_t generation,
                        const LogRecord &record);

TriageLoadResult loadTriageState(const std::string& path);
bool saveTriageState(const std::string& path, const TriageState& state,
                     PersistenceError& error);

bool validateTriageState(const TriageState& state, PersistenceError& error);
HighlightRules compileHighlightRules(const TriageState& state);

// These helpers provide the non-Qt CRUD/reorder contract used by both tests
// and the GUI. They leave the state unchanged on failure.
bool upsertHighlightRule(TriageState& state, NamedHighlightRule rule,
                         PersistenceError& error);
bool removeHighlightRule(TriageState& state, const std::string& name,
                         PersistenceError& error);
bool moveHighlightRule(TriageState& state, std::size_t from, std::size_t to,
                       PersistenceError& error);
bool setTriageEntry(TriageState& state, TriageEntry entry,
                    PersistenceError& error);

} // namespace loglens
