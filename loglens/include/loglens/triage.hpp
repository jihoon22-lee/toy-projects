#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "loglens/highlight_rules.hpp"
#include "loglens/persistence.hpp"

namespace loglens {

constexpr std::size_t kMaxHighlightRules = 128;
constexpr std::size_t kMaxTriageEntries = 8192;
constexpr std::size_t kMaxHighlightPatternBytes = 1024;
constexpr std::size_t kMaxAnnotationBytes = 4096;

struct NamedHighlightRule {
    std::string name;
    Rule rule;
};

struct TriageEntry {
    std::string source_path;
    std::size_t line_number = 0;
    bool bookmarked = false;
    std::string annotation;
};

struct TriageState {
    std::vector<NamedHighlightRule> rules;
    std::vector<TriageEntry> entries;
};

struct TriageLoadResult {
    bool found = false;
    bool migrated = false;
    TriageState state;
    PersistenceError error;

    bool ok() const { return error.ok(); }
};

const char* triageSchemaName();

// Loads v1 and the bounded legacy v0 rule-only shape. Migration is explicit in
// the result and is persisted as v1 only when the caller chooses to save.
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
