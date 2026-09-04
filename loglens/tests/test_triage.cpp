#include "check.hpp"

#include "loglens/triage.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

class ScopedTempDirectory {
public:
    ScopedTempDirectory() {
        const auto pattern = (std::filesystem::temp_directory_path()
                              / "loglens-triage-XXXXXX").string();
        std::vector<char> writable(pattern.begin(), pattern.end());
        writable.push_back('\0');
        char* created = ::mkdtemp(writable.data());
        if (created == nullptr) throw std::runtime_error("mkdtemp failed");
        path_ = created;
    }

    ~ScopedTempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void writeFile(const std::filesystem::path& path, const std::string& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << value;
}

void testCrudRoundTrip() {
    ScopedTempDirectory temporary;
    CHECK_EQ(std::string(loglens::triageSchemaName()), std::string("loglens.triage/v1"));
    loglens::TriageState state;
    loglens::PersistenceError error;
    CHECK(loglens::upsertHighlightRule(
        state, {"Timeout", {"timeout", false, 20, "#ff5500"}}, error));
    CHECK(loglens::upsertHighlightRule(
        state, {"Fatal row", {"", true, 100, "red"}}, error));
    CHECK(loglens::moveHighlightRule(state, 1, 0, error));
    CHECK_EQ(state.rules.front().name, std::string("Fatal row"));
    CHECK(loglens::setTriageEntry(
        state, {"/tmp/app.log", 42, true, "inspect retry path"}, error));

    const auto path = temporary.path() / "triage.json";
    CHECK(loglens::saveTriageState(path.string(), state, error));
    const auto loaded = loglens::loadTriageState(path.string());
    if (!loaded.ok()) {
        std::fprintf(stderr, "triage load error: %s\n", loaded.error.message.c_str());
    }
    CHECK(loaded.ok());
    CHECK(loaded.found);
    CHECK(!loaded.migrated);
    CHECK_EQ(loaded.state.rules.size(), static_cast<std::size_t>(2));
    CHECK_EQ(loaded.state.entries.size(), static_cast<std::size_t>(1));
    if (!loaded.state.entries.empty()) {
        CHECK_EQ(loaded.state.entries.front().annotation, std::string("inspect retry path"));
    }

    const auto rules = loglens::compileHighlightRules(loaded.state);
    CHECK(!rules.apply("timeout while retrying").empty());
    CHECK(loglens::removeHighlightRule(state, "Timeout", error));
    CHECK_EQ(state.rules.size(), static_cast<std::size_t>(1));
}

void testMigrationAndMalformedInput() {
    ScopedTempDirectory temporary;
    const auto legacy = temporary.path() / "legacy.json";
    writeFile(legacy,
              "{\"schema\":\"loglens.triage/v0\",\"rules\":["
              "{\"pattern\":\"panic\",\"color\":\"#ff0000\"}]}\n");
    const auto migrated = loglens::loadTriageState(legacy.string());
    CHECK(migrated.ok());
    CHECK(migrated.migrated);
    CHECK_EQ(migrated.state.rules.size(), static_cast<std::size_t>(1));
    CHECK_EQ(migrated.state.rules.front().name, std::string("Migrated rule 1"));

    const auto malformed = temporary.path() / "malformed.json";
    writeFile(malformed,
              "{\"schema\":\"loglens.triage/v1\",\"rules\":[],\"entries\":[],"
              "\"unknown\":true}\n");
    const auto rejected = loglens::loadTriageState(malformed.string());
    CHECK(!rejected.ok());
    CHECK(rejected.state.rules.empty());

    loglens::TriageState invalid;
    invalid.rules.push_back({"Bad", {"", false, 0, "red;url(x)"}});
    loglens::PersistenceError error;
    CHECK(!loglens::validateTriageState(invalid, error));
}

void testMissingAndWrongFieldShapes() {
    ScopedTempDirectory temporary;
    const auto absent = loglens::loadTriageState(
        (temporary.path() / "does-not-exist.json").string());
    CHECK(absent.ok());
    CHECK(!absent.found);
    CHECK(absent.state.rules.empty());
    CHECK(absent.state.entries.empty());

    const auto expectRejected = [&](const std::string& name, const std::string& json) {
        const auto path = temporary.path() / name;
        writeFile(path, json);
        const auto loaded = loglens::loadTriageState(path.string());
        CHECK(!loaded.ok());
        // A malformed document must not leak a partially parsed state to the
        // caller. This is important when the GUI replaces its live rules only
        // after a complete load succeeds.
        CHECK(loaded.state.rules.empty());
        CHECK(loaded.state.entries.empty());
    };

    expectRejected("root-array.json", "[]\n");
    expectRejected("missing-schema.json",
                   "{\"rules\":[],\"entries\":[]}\n");
    expectRejected("schema-not-string.json",
                   "{\"schema\":null,\"rules\":[],\"entries\":[]}\n");
    expectRejected("unsupported-schema.json",
                   "{\"schema\":\"loglens.triage/v99\",\"rules\":[],\"entries\":[]}\n");
    expectRejected("rules-not-array.json",
                   "{\"schema\":\"loglens.triage/v1\",\"rules\":{},\"entries\":[]}\n");
    expectRejected("entries-not-array.json",
                   "{\"schema\":\"loglens.triage/v1\",\"rules\":[],\"entries\":{}}\n");
    expectRejected("rule-not-object.json",
                   "{\"schema\":\"loglens.triage/v1\",\"rules\":[1],\"entries\":[]}\n");
    expectRejected("entry-not-object.json",
                   "{\"schema\":\"loglens.triage/v1\",\"rules\":[],\"entries\":[1]}\n");

    const std::string validRule =
        "{\"name\":\"timeout\",\"pattern\":\"x\","
        "\"whole_line\":false,\"priority\":1,\"style\":\"red\"}";
    const std::vector<std::string> wrongRuleFields = {
        "{\"name\":null,\"pattern\":\"x\",\"whole_line\":false,\"priority\":1,\"style\":\"red\"}",
        "{\"name\":\"timeout\",\"pattern\":1,\"whole_line\":false,\"priority\":1,\"style\":\"red\"}",
        "{\"name\":\"timeout\",\"pattern\":\"x\",\"whole_line\":\"false\",\"priority\":1,\"style\":\"red\"}",
        "{\"name\":\"timeout\",\"pattern\":\"x\",\"whole_line\":false,\"priority\":\"1\",\"style\":\"red\"}",
        "{\"name\":\"timeout\",\"pattern\":\"x\",\"whole_line\":false,\"priority\":1,\"style\":false}",
    };
    for (std::size_t index = 0; index < wrongRuleFields.size(); ++index) {
        expectRejected("wrong-rule-" + std::to_string(index) + ".json",
                       "{\"schema\":\"loglens.triage/v1\",\"rules\":["
                           + wrongRuleFields[index] + "],\"entries\":[]}\n");
    }
    expectRejected("rule-unknown-field.json",
                   "{\"schema\":\"loglens.triage/v1\",\"rules\":["
                   + validRule.substr(0, validRule.size() - 1)
                   + ",\"extra\":true}],\"entries\":[]}\n");
    expectRejected("partial-rule-state.json",
                   "{\"schema\":\"loglens.triage/v1\",\"rules\":["
                   + validRule
                   + ",{}],\"entries\":[]}\n");

    const std::string validEntry =
        "{\"source_path\":\"app.log\",\"line_number\":7,"
        "\"bookmarked\":true,\"annotation\":\"note\"}";
    const std::vector<std::string> wrongEntryFields = {
        "{\"source_path\":null,\"line_number\":7,\"bookmarked\":true,\"annotation\":\"note\"}",
        "{\"source_path\":\"app.log\",\"line_number\":\"7\",\"bookmarked\":true,\"annotation\":\"note\"}",
        "{\"source_path\":\"app.log\",\"line_number\":7,\"bookmarked\":1,\"annotation\":\"note\"}",
        "{\"source_path\":\"app.log\",\"line_number\":7,\"bookmarked\":true,\"annotation\":null}",
    };
    for (std::size_t index = 0; index < wrongEntryFields.size(); ++index) {
        expectRejected("wrong-entry-" + std::to_string(index) + ".json",
                       "{\"schema\":\"loglens.triage/v1\",\"rules\":[],\"entries\":["
                           + wrongEntryFields[index] + "]}\n");
    }
    expectRejected("entry-unknown-field.json",
                   "{\"schema\":\"loglens.triage/v1\",\"rules\":[],\"entries\":["
                   + validEntry.substr(0, validEntry.size() - 1)
                   + ",\"extra\":true}]}\n");
    expectRejected("partial-entry-state.json",
                   "{\"schema\":\"loglens.triage/v1\",\"rules\":[],\"entries\":["
                   + validEntry
                   + ",{}]}\n");

    // The legacy parser has a deliberately smaller shape. Exercise both its
    // successful path's validation and each typed-field failure.
    expectRejected("legacy-missing-color.json",
                   "{\"schema\":\"loglens.triage/v0\",\"rules\":["
                   "{\"pattern\":\"panic\"}]}\n");
    expectRejected("legacy-pattern-not-string.json",
                   "{\"schema\":\"loglens.triage/v0\",\"rules\":["
                   "{\"pattern\":1,\"color\":\"red\"}]}\n");
    expectRejected("legacy-color-not-string.json",
                   "{\"schema\":\"loglens.triage/v0\",\"rules\":["
                   "{\"pattern\":\"panic\",\"color\":false}]}\n");
    expectRejected("legacy-unknown-field.json",
                   "{\"schema\":\"loglens.triage/v0\",\"rules\":["
                   "{\"pattern\":\"panic\",\"color\":\"red\",\"extra\":true}]}\n");
}

void testNumericAndValueValidation() {
    ScopedTempDirectory temporary;
    const auto expectRejected = [&](const std::string& name, const std::string& json) {
        const auto path = temporary.path() / name;
        writeFile(path, json);
        CHECK(!loglens::loadTriageState(path.string()).ok());
    };
    const std::string rulePrefix =
        "{\"schema\":\"loglens.triage/v1\",\"rules\":["
        "{\"name\":\"r\",\"pattern\":\"x\",\"whole_line\":false,\"priority\":";
    const std::string ruleSuffix = ",\"style\":\"red\"}],\"entries\":[]}\n";
    expectRejected("priority-fraction.json", rulePrefix + "1.5" + ruleSuffix);
    expectRejected("priority-overflow.json", rulePrefix + "999999999999999999999" + ruleSuffix);

    const std::string entryPrefix =
        "{\"schema\":\"loglens.triage/v1\",\"rules\":[],\"entries\":["
        "{\"source_path\":\"app.log\",\"line_number\":";
    const std::string entrySuffix = ",\"bookmarked\":true,\"annotation\":\"note\"}]}\n";
    expectRejected("line-negative.json", entryPrefix + "-1" + entrySuffix);
    expectRejected("line-fraction.json", entryPrefix + "1.5" + entrySuffix);
    expectRejected("line-overflow.json", entryPrefix + "18446744073709551616" + entrySuffix);
    expectRejected("line-zero.json", entryPrefix + "0" + entrySuffix);

    loglens::PersistenceError error;
    loglens::NamedHighlightRule emptyName{"", {"x", false, 1, "red"}};
    CHECK(!loglens::validateTriageState({{emptyName}, {}}, error));
    CHECK(error.code == loglens::PersistenceErrorCode::InvalidValue);

    loglens::NamedHighlightRule hugeName{std::string(loglens::kMaxPersistedNameBytes + 1, 'n'),
                                         {"x", false, 1, "red"}};
    CHECK(!loglens::validateTriageState({{hugeName}, {}}, error));
    loglens::NamedHighlightRule hugePattern{"pattern", {
        std::string(loglens::kMaxHighlightPatternBytes + 1, 'x'), false, 1, "red"}};
    CHECK(!loglens::validateTriageState({{hugePattern}, {}}, error));
    loglens::NamedHighlightRule emptyPattern{"pattern", {"", false, 1, "red"}};
    CHECK(!loglens::validateTriageState({{emptyPattern}, {}}, error));
    loglens::NamedHighlightRule badHex{"hex", {"x", false, 1, "#ggg"}};
    CHECK(!loglens::validateTriageState({{badHex}, {}}, error));
    loglens::NamedHighlightRule shortHex{"hex", {"x", false, 1, "#12"}};
    CHECK(!loglens::validateTriageState({{shortHex}, {}}, error));
    loglens::NamedHighlightRule hugeStyle{"style", {"x", false, 1,
                                                        std::string(65, 'a')}};
    CHECK(!loglens::validateTriageState({{hugeStyle}, {}}, error));

    const std::vector<loglens::TriageEntry> invalidEntries = {
        {"", 1, true, "note"},
        {"app.log", 0, true, "note"},
        {"app.log", 1, true, std::string(loglens::kMaxAnnotationBytes + 1, 'a')},
        {"app.log", 1, false, ""},
    };
    for (const auto& entry : invalidEntries) {
        loglens::TriageState state;
        state.entries.push_back(entry);
        CHECK(!loglens::validateTriageState(state, error));
        CHECK(error.code == loglens::PersistenceErrorCode::InvalidValue);
    }

    loglens::TriageState tooManyRules;
    for (std::size_t index = 0; index <= loglens::kMaxHighlightRules; ++index) {
        tooManyRules.rules.push_back({"rule-" + std::to_string(index), {"x", false, 1, "red"}});
    }
    CHECK(!loglens::validateTriageState(tooManyRules, error));
    CHECK(error.code == loglens::PersistenceErrorCode::LimitExceeded);
    loglens::TriageState tooManyEntries;
    tooManyEntries.entries.reserve(loglens::kMaxTriageEntries + 1);
    for (std::size_t index = 0; index <= loglens::kMaxTriageEntries; ++index) {
        tooManyEntries.entries.push_back({"app-" + std::to_string(index), 1, true, "note"});
    }
    CHECK(!loglens::validateTriageState(tooManyEntries, error));
    CHECK(error.code == loglens::PersistenceErrorCode::LimitExceeded);
}

void testCrudEdgeCasesAndEscaping() {
    ScopedTempDirectory temporary;
    loglens::TriageState state;
    loglens::PersistenceError error;
    CHECK(!loglens::removeHighlightRule(state, "missing", error));
    CHECK(error.code == loglens::PersistenceErrorCode::InvalidValue);
    CHECK(!loglens::moveHighlightRule(state, 0, 0, error));
    CHECK(error.code == loglens::PersistenceErrorCode::InvalidValue);

    CHECK(loglens::upsertHighlightRule(state, {"first", {"x", false, 1, "red"}}, error));
    CHECK(loglens::upsertHighlightRule(state, {"second", {"y", false, 2, "blue"}}, error));
    CHECK(loglens::moveHighlightRule(state, 0, 0, error));
    CHECK(!loglens::moveHighlightRule(state, 0, 2, error));
    CHECK(error.code == loglens::PersistenceErrorCode::InvalidValue);
    CHECK(loglens::upsertHighlightRule(state, {"first", {"updated", false, 3, "green"}}, error));
    CHECK_EQ(state.rules.front().rule.pattern, std::string("updated"));

    // Empty, unbookmarked entries are a valid delete operation, including
    // when the identity does not exist yet.
    CHECK(loglens::setTriageEntry(state, {"app.log", 4, false, ""}, error));
    CHECK(loglens::setTriageEntry(state, {"app.log", 4, true, "keep"}, error));
    CHECK(loglens::setTriageEntry(state, {"app.log", 4, false, "updated"}, error));
    CHECK(loglens::setTriageEntry(state, {"app.log", 4, false, ""}, error));
    CHECK(state.entries.empty());
    const std::size_t ruleCount = state.rules.size();
    CHECK(!loglens::setTriageEntry(state, {"", 9, true, "bad"}, error));
    CHECK_EQ(state.rules.size(), ruleCount);

    loglens::TriageState fullRules;
    for (std::size_t index = 0; index < loglens::kMaxHighlightRules; ++index) {
        fullRules.rules.push_back({"rule-" + std::to_string(index), {"x", false, 1, "red"}});
    }
    CHECK(!loglens::upsertHighlightRule(fullRules, {"new", {"x", false, 1, "red"}}, error));
    CHECK(error.code == loglens::PersistenceErrorCode::LimitExceeded);
    CHECK_EQ(fullRules.rules.size(), loglens::kMaxHighlightRules);

    // All JSON string escape cases are round-tripped, including control bytes
    // that must be emitted as \u00XX rather than raw invalid JSON.
    const std::string escaped = "quote\"slash\\backspace\bformfeed\fline\nreturn\rtab\t";
    loglens::TriageState encoded;
    encoded.rules.push_back({escaped, {escaped, false, -3, "red"}});
    encoded.entries.push_back({"source-" + escaped, 9, true, escaped});
    const auto path = temporary.path() / "escaped.json";
    CHECK(loglens::saveTriageState(path.string(), encoded, error));
    const auto decoded = loglens::loadTriageState(path.string());
    CHECK(decoded.ok());
    CHECK_EQ(decoded.state.rules.front().name, escaped);
    CHECK_EQ(decoded.state.rules.front().rule.pattern, escaped);
    CHECK_EQ(decoded.state.entries.front().source_path, "source-" + escaped);
    CHECK_EQ(decoded.state.entries.front().annotation, escaped);
}

void testDuplicateFieldsAndIdentitiesAreRejected() {
    ScopedTempDirectory temporary;
    const auto duplicate_field = temporary.path() / "duplicate.json";
    writeFile(duplicate_field,
              "{\"schema\":\"loglens.triage/v1\",\"schema\":\"loglens.triage/v1\","
              "\"rules\":[],\"entries\":[]}\n");
    const auto loaded = loglens::loadTriageState(duplicate_field.string());
    CHECK(!loaded.ok());

    loglens::TriageState state;
    state.entries.push_back({"/tmp/app.log", 7, true, "first"});
    state.entries.push_back({"/tmp/app.log", 7, true, "second"});
    loglens::PersistenceError error;
    CHECK(!loglens::validateTriageState(state, error));
    CHECK_EQ(error.code, loglens::PersistenceErrorCode::DuplicateName);
}

void testSerializedSizeLimitIsEnforced() {
    ScopedTempDirectory temporary;
    loglens::TriageState state;
    const std::string annotation(loglens::kMaxAnnotationBytes, 'x');
    for (std::size_t index = 0; index < 1100; ++index) {
        state.entries.push_back({"/tmp/log-" + std::to_string(index), index + 1,
                                 true, annotation});
    }
    loglens::PersistenceError error;
    CHECK(!loglens::saveTriageState((temporary.path() / "too-large.json").string(),
                                    state, error));
    CHECK_EQ(error.code, loglens::PersistenceErrorCode::LimitExceeded);
}

} // namespace

int main() {
    testCrudRoundTrip();
    testMigrationAndMalformedInput();
    testMissingAndWrongFieldShapes();
    testNumericAndValueValidation();
    testCrudEdgeCasesAndEscaping();
    testDuplicateFieldsAndIdentitiesAreRejected();
    testSerializedSizeLimitIsEnforced();
    return checkSummary();
}
