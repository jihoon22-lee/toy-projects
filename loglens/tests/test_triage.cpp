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
    testDuplicateFieldsAndIdentitiesAreRejected();
    testSerializedSizeLimitIsEnforced();
    return checkSummary();
}
