#include "check.hpp"

#include "loglens/filter_expr.hpp"
#include "loglens/log_parser.hpp"
#include "loglens/persistence.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifndef _WIN32
#    include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> nextId{0};
        const auto stamp =
            std::chrono::high_resolution_clock::now().time_since_epoch().count();
        for (unsigned int attempt = 0; attempt < 128; ++attempt) {
            const fs::path candidate =
                fs::temp_directory_path()
                / ("loglens_persistence_" + std::to_string(stamp) + "_"
                   + std::to_string(nextId.fetch_add(1, std::memory_order_relaxed)) + "_"
                   + std::to_string(attempt));
            std::error_code error;
            if (fs::create_directory(candidate, error)) {
                path_ = candidate;
                return;
            }
        }
        throw std::runtime_error("cannot create persistence test directory");
    }

    ~TempDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

void writeFile(const fs::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.flush();
    CHECK(output.good());
}

std::string readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

loglens::SourceProfile profile(const std::string& name, loglens::Format format,
                               loglens::MultilinePolicy multiline,
                               std::size_t maxRecordBytes) {
    return loglens::SourceProfile{name, format, multiline, maxRecordBytes};
}

loglens::SavedQuery query(const std::string& name, const std::string& expression) {
    return loglens::SavedQuery{name, expression};
}

void testStableNames() {
    CHECK_EQ(std::string(loglens::formatName(loglens::Format::Raw)), "raw");
    CHECK(loglens::parseFormatName("raw") == loglens::Format::Raw);
    CHECK(loglens::parseFormatName("plain") == loglens::Format::PlainIso);
    CHECK(!loglens::parseFormatName("yaml").has_value());
    CHECK_EQ(std::string(loglens::multilinePolicyName(
                  loglens::MultilinePolicy::FoldContinuations)),
             "fold-continuations");
    CHECK_EQ(std::string(loglens::multilinePolicyName(
                  loglens::MultilinePolicy::SeparateLines)),
             "separate-lines");
    CHECK(loglens::parseMultilinePolicyName("separate-lines")
          == loglens::MultilinePolicy::SeparateLines);
    CHECK(!loglens::parseMultilinePolicyName("join").has_value());
    CHECK_EQ(std::string(loglens::sourceProfileSchemaName()),
             "loglens.source-profiles/v1");
    CHECK_EQ(std::string(loglens::savedQuerySchemaName()),
             "loglens.saved-queries/v1");
    CHECK_EQ(std::string(loglens::persistenceErrorCodeName(
                  loglens::PersistenceErrorCode::UnsupportedVersion)),
             "unsupported-version");

    const std::vector<loglens::ParseStatus> statuses = {
        loglens::ParseStatus::Parsed, loglens::ParseStatus::Partial,
        loglens::ParseStatus::Invalid, loglens::ParseStatus::Unstructured};
    for (const loglens::ParseStatus status : statuses) {
        CHECK(std::string(loglens::parseStatusName(status)) != "");
    }
    CHECK_EQ(std::string(loglens::parseStatusName(
                  static_cast<loglens::ParseStatus>(99))),
             "unstructured");
    const std::vector<loglens::ParseDiagnosticCode> diagnosticCodes = {
        loglens::ParseDiagnosticCode::MissingField,
        loglens::ParseDiagnosticCode::InvalidField,
        loglens::ParseDiagnosticCode::InvalidFieldType,
        loglens::ParseDiagnosticCode::InvalidTimestamp,
        loglens::ParseDiagnosticCode::InvalidTimestampOffset,
        loglens::ParseDiagnosticCode::InvalidJson,
        loglens::ParseDiagnosticCode::InvalidEscape,
        loglens::ParseDiagnosticCode::InvalidUnicode,
        loglens::ParseDiagnosticCode::DuplicateField,
        loglens::ParseDiagnosticCode::TrailingData,
        loglens::ParseDiagnosticCode::LimitExceeded};
    for (const loglens::ParseDiagnosticCode code : diagnosticCodes) {
        CHECK(std::string(loglens::parseDiagnosticCodeName(code)) != "");
    }
    CHECK_EQ(std::string(loglens::parseDiagnosticCodeName(
                  static_cast<loglens::ParseDiagnosticCode>(99))),
             "invalid-field");
}

void testProfilesRoundTripAndCanonicalOrdering() {
    TempDirectory directory;
    const fs::path path = directory.path() / "profiles.json";
    const std::vector<loglens::SourceProfile> input = {
        profile("raw", loglens::Format::Raw, loglens::MultilinePolicy::SeparateLines, 1024),
        profile("syslog", loglens::Format::Syslog,
                loglens::MultilinePolicy::FoldContinuations, 4096),
        profile("iso", loglens::Format::PlainIso,
                loglens::MultilinePolicy::FoldContinuations, 8192),
        profile("json", loglens::Format::JsonLine,
                loglens::MultilinePolicy::SeparateLines, loglens::kDefaultMaxRecordBytes),
        profile("auto", loglens::Format::Auto,
                loglens::MultilinePolicy::FoldContinuations, loglens::kMaxRecordBytes),
    };
    loglens::PersistenceError error;
    CHECK(loglens::saveSourceProfiles(path.string(), input, error));
    CHECK(error.ok());
    const std::string expected =
        "{\"schema\":\"loglens.source-profiles/v1\",\"profiles\":["
        "{\"name\":\"auto\",\"format\":\"auto\","
        "\"multiline\":\"fold-continuations\",\"max_record_bytes\":1048576},"
        "{\"name\":\"iso\",\"format\":\"iso\","
        "\"multiline\":\"fold-continuations\",\"max_record_bytes\":8192},"
        "{\"name\":\"json\",\"format\":\"jsonl\","
        "\"multiline\":\"separate-lines\",\"max_record_bytes\":65536},"
        "{\"name\":\"raw\",\"format\":\"raw\","
        "\"multiline\":\"separate-lines\",\"max_record_bytes\":1024},"
        "{\"name\":\"syslog\",\"format\":\"syslog\","
        "\"multiline\":\"fold-continuations\",\"max_record_bytes\":4096}]}\n";
    CHECK_EQ(readFile(path), expected);

    const loglens::SourceProfileLoadResult loaded =
        loglens::loadSourceProfiles(path.string());
    CHECK(loaded.ok());
    CHECK(loaded.found);
    CHECK_EQ(loaded.profiles.size(), input.size());
    if (loaded.profiles.size() == input.size()) {
        CHECK_EQ(loaded.profiles[0].name, "auto");
        CHECK(loaded.profiles[1].format == loglens::Format::PlainIso);
        CHECK(loaded.profiles[2].format == loglens::Format::JsonLine);
        CHECK(loaded.profiles[3].multiline == loglens::MultilinePolicy::SeparateLines);
        CHECK_EQ(loaded.profiles[4].max_record_bytes, static_cast<std::size_t>(4096));
    }

    const fs::path reorderedPath = directory.path() / "profiles-reordered.json";
    std::vector<loglens::SourceProfile> reversed = input;
    std::reverse(reversed.begin(), reversed.end());
    CHECK(loglens::saveSourceProfiles(reorderedPath.string(), reversed, error));
    CHECK_EQ(readFile(reorderedPath), expected);
}

void testMissingStoreIsAnEmptySuccessfulLoad() {
    TempDirectory directory;
    const loglens::SourceProfileLoadResult profiles =
        loglens::loadSourceProfiles((directory.path() / "missing-profiles.json").string());
    CHECK(profiles.ok());
    CHECK(!profiles.found);
    CHECK(profiles.profiles.empty());
    const loglens::SavedQueryLoadResult queries =
        loglens::loadSavedQueries((directory.path() / "missing-queries.json").string());
    CHECK(queries.ok());
    CHECK(!queries.found);
    CHECK(queries.queries.empty());
}

void testQueriesRoundTripAndReuseFilterSemantics() {
    TempDirectory directory;
    const fs::path path = directory.path() / "queries.json";
    const std::vector<loglens::SavedQuery> input = {
        query("errors", "level>=ERROR OR message~timeout"),
        query("api-warnings", "source==api AND level>=WARN"),
    };
    loglens::PersistenceError error;
    CHECK(loglens::saveSavedQueries(path.string(), input, error));
    CHECK(error.ok());
    CHECK_EQ(readFile(path),
             "{\"schema\":\"loglens.saved-queries/v1\",\"queries\":["
             "{\"name\":\"api-warnings\",\"expression\":\"source==api AND level>=WARN\"},"
             "{\"name\":\"errors\",\"expression\":\"level>=ERROR OR message~timeout\"}]}\n");

    const loglens::SavedQueryLoadResult loaded = loglens::loadSavedQueries(path.string());
    CHECK(loaded.ok());
    CHECK(loaded.found);
    CHECK_EQ(loaded.queries.size(), input.size());
    loglens::LogRecord record;
    record.level = loglens::Level::Warn;
    record.source = "api";
    record.message = "request timeout";
    for (const loglens::SavedQuery& saved : loaded.queries) {
        loglens::ParseError parseError;
        const auto filter = loglens::Filter::parse(saved.expression, parseError);
        CHECK(filter.has_value());
        CHECK(filter->matches(record));
    }
}

void testSavedQuerySerializationEscapesJsonControls() {
    TempDirectory directory;
    const fs::path path = directory.path() / "escaped-query.json";
    std::string expression = "message~\"";
    expression += "\\\"";
    expression += "\\\\";
    expression.push_back('\b');
    expression.push_back('\f');
    expression.push_back('\n');
    expression.push_back('\r');
    expression.push_back('\t');
    expression.push_back('\"');

    loglens::PersistenceError error;
    CHECK(loglens::saveSavedQueries(path.string(),
                                    {query("escaped", expression)}, error));
    const std::string serialized = readFile(path);
    CHECK(serialized.find("\\\\") != std::string::npos);
    CHECK(serialized.find("\\\"") != std::string::npos);
    CHECK(serialized.find("\\b") != std::string::npos);
    CHECK(serialized.find("\\f") != std::string::npos);
    CHECK(serialized.find("\\n") != std::string::npos);
    CHECK(serialized.find("\\r") != std::string::npos);
    CHECK(serialized.find("\\t") != std::string::npos);

    const loglens::SavedQueryLoadResult loaded = loglens::loadSavedQueries(path.string());
    CHECK(loaded.ok());
    CHECK_EQ(loaded.queries.size(), static_cast<std::size_t>(1));
    if (loaded.queries.size() == 1) {
        CHECK_EQ(loaded.queries[0].expression, expression);
    }
}

void testSeparateLinesPolicyUsesTheSameAssemblerContract() {
    const std::vector<std::string> lines = {"root", "  detail"};
    loglens::RecordAssembler folded(
        loglens::Format::Raw, loglens::EncodingErrorPolicy::PreserveBytes, 64,
        loglens::MultilinePolicy::FoldContinuations);
    const std::vector<loglens::RecordDelta> foldedDeltas = folded.consumeLines(lines);
    CHECK_EQ(foldedDeltas.size(), static_cast<std::size_t>(2));
    CHECK(foldedDeltas[1].kind == loglens::RecordDelta::Kind::Extend);
    CHECK_EQ(foldedDeltas[1].record.message, "root\n  detail");

    loglens::RecordAssembler separate(
        loglens::Format::Raw, loglens::EncodingErrorPolicy::PreserveBytes, 64,
        loglens::MultilinePolicy::SeparateLines);
    const std::vector<loglens::RecordDelta> separateDeltas = separate.consumeLines(lines);
    CHECK_EQ(separateDeltas.size(), static_cast<std::size_t>(2));
    CHECK(separateDeltas[1].kind == loglens::RecordDelta::Kind::Append);
    CHECK_EQ(separateDeltas[1].record.message, "  detail");
    CHECK(separate.multilinePolicy() == loglens::MultilinePolicy::SeparateLines);
}

void testInvalidSaveDoesNotTouchTheExistingStore() {
    TempDirectory directory;
    const fs::path profilePath = directory.path() / "profiles.json";
    const fs::path queryPath = directory.path() / "queries.json";
    loglens::PersistenceError error;
    CHECK(loglens::saveSourceProfiles(
        profilePath.string(),
        {profile("stable", loglens::Format::Raw,
                 loglens::MultilinePolicy::SeparateLines, 128)},
        error));
    CHECK(loglens::saveSavedQueries(queryPath.string(), {query("stable", "message~ok")}, error));
    const std::string originalProfiles = readFile(profilePath);
    const std::string originalQueries = readFile(queryPath);

    CHECK(!loglens::saveSourceProfiles(
        profilePath.string(),
        {profile("same", loglens::Format::Raw,
                 loglens::MultilinePolicy::FoldContinuations, 128),
         profile("same", loglens::Format::JsonLine,
                 loglens::MultilinePolicy::FoldContinuations, 128)},
        error));
    CHECK(error.code == loglens::PersistenceErrorCode::DuplicateName);
    CHECK_EQ(readFile(profilePath), originalProfiles);

    CHECK(!loglens::saveSavedQueries(queryPath.string(),
                                     {query("bad", "message>=oops")}, error));
    CHECK(error.code == loglens::PersistenceErrorCode::InvalidQuery);
    CHECK_EQ(readFile(queryPath), originalQueries);

    std::vector<loglens::SavedQuery> tooMany;
    for (std::size_t index = 0; index <= loglens::kMaxPersistedItems; ++index) {
        tooMany.push_back(query("q" + std::to_string(index), "message~x"));
    }
    CHECK(!loglens::saveSavedQueries(queryPath.string(), tooMany, error));
    CHECK(error.code == loglens::PersistenceErrorCode::LimitExceeded);
    CHECK_EQ(readFile(queryPath), originalQueries);
}

void expectProfileReject(const fs::path& path, const std::string& content,
                         loglens::PersistenceErrorCode code) {
    writeFile(path, content);
    const loglens::SourceProfileLoadResult result =
        loglens::loadSourceProfiles(path.string());
    CHECK(!result.ok());
    CHECK(result.error.code == code);
    CHECK(result.profiles.empty());
}

void expectQueryReject(const fs::path& path, const std::string& content,
                       loglens::PersistenceErrorCode code) {
    writeFile(path, content);
    const loglens::SavedQueryLoadResult result = loglens::loadSavedQueries(path.string());
    CHECK(!result.ok());
    CHECK(result.error.code == code);
    CHECK(result.queries.empty());
}

void testSchemaIsStrictAndMigrationFailsClosed() {
    TempDirectory directory;
    const fs::path profilePath = directory.path() / "bad-profiles.json";
    const fs::path queryPath = directory.path() / "bad-queries.json";
    expectProfileReject(
        profilePath,
        "{\"schema\":\"loglens.source-profiles/v0\",\"profiles\":[]}",
        loglens::PersistenceErrorCode::UnsupportedVersion);
    expectProfileReject(profilePath,
                        "{\"schema\":\"loglens.source-profiles/v1\","
                        "\"profiles\":[],\"future\":true}",
                        loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(profilePath,
                        "{\"schema\":\"loglens.source-profiles/v1\","
                        "\"profiles\":[{\"name\":\"one\"}]}",
                        loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(
        profilePath,
        "{\"schema\":\"loglens.source-profiles/v1\",\"profiles\":[] trailing}",
        loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(profilePath,
                        "{\"schema\":\"loglens.source-profiles/v1\","
                        "\"schema\":\"loglens.source-profiles/v1\",\"profiles\":[]}",
                        loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(profilePath,
                        "{\"schema\":\"loglens.source-profiles/v1\","
                        "\"profiles\":[{\"name\":\"one\",\"format\":\"plain\","
                        "\"multiline\":\"fold-continuations\",\"max_record_bytes\":64}]}",
                        loglens::PersistenceErrorCode::InvalidValue);
    expectProfileReject(profilePath,
                        "{\"schema\":\"loglens.source-profiles/v1\","
                        "\"profiles\":[{\"name\":\"one\",\"format\":\"iso\","
                        "\"multiline\":\"fold-continuations\",\"max_record_bytes\":1.5}]}",
                        loglens::PersistenceErrorCode::InvalidValue);
    expectQueryReject(queryPath,
                      "{\"schema\":\"loglens.saved-queries/v0\",\"queries\":[]}",
                      loglens::PersistenceErrorCode::UnsupportedVersion);
    expectQueryReject(queryPath,
                      "{\"schema\":\"loglens.saved-queries/v1\","
                      "\"queries\":[{\"name\":\"bad\",\"expression\":\"message>=x\"}]}",
                      loglens::PersistenceErrorCode::InvalidQuery);
    expectQueryReject(queryPath,
                      "{\"schema\":\"loglens.saved-queries/v1\","
                      "\"queries\":[],\"extra\":null}",
                      loglens::PersistenceErrorCode::Malformed);
    expectQueryReject(queryPath,
                      "{\"schema\":\"loglens.saved-queries/v1\","
                      "\"queries\":[{\"name\":\"same\",\"expression\":\"message~x\"},"
                      "{\"name\":\"same\",\"expression\":\"message~y\"}]}",
                      loglens::PersistenceErrorCode::DuplicateName);
}

void testValidationRejectsInvalidValuesAndAcceptsUtf8() {
    TempDirectory directory;
    const fs::path profilePath = directory.path() / "validation-profiles.json";
    const fs::path queryPath = directory.path() / "validation-queries.json";
    loglens::PersistenceError error;

    CHECK(!loglens::saveSourceProfiles(
        profilePath.string(),
        {profile("", loglens::Format::Raw,
                 loglens::MultilinePolicy::FoldContinuations, 64)},
        error));
    CHECK(error.code == loglens::PersistenceErrorCode::InvalidValue);

    std::string controlName = "bad\nname";
    CHECK(!loglens::saveSourceProfiles(
        profilePath.string(),
        {profile(controlName, loglens::Format::Raw,
                 loglens::MultilinePolicy::FoldContinuations, 64)},
        error));
    CHECK(error.code == loglens::PersistenceErrorCode::InvalidValue);

    std::string invalidContinuation = "bad\xC3(";
    CHECK(!loglens::saveSourceProfiles(
        profilePath.string(),
        {profile(invalidContinuation, loglens::Format::Raw,
                 loglens::MultilinePolicy::FoldContinuations, 64)},
        error));
    CHECK(error.code == loglens::PersistenceErrorCode::InvalidValue);

    CHECK(loglens::saveSourceProfiles(
        profilePath.string(),
        {profile("caf\xC3\xA9", loglens::Format::Raw,
                 loglens::MultilinePolicy::FoldContinuations, 64),
         profile("euro\xE2\x82\xAC", loglens::Format::Raw,
                 loglens::MultilinePolicy::FoldContinuations, 64),
         profile("smile\xF0\x9F\x98\x80", loglens::Format::Raw,
                 loglens::MultilinePolicy::FoldContinuations, 64)},
        error));

    CHECK(!loglens::saveSourceProfiles(
        profilePath.string(),
        {profile("bad-format", static_cast<loglens::Format>(99),
                 loglens::MultilinePolicy::FoldContinuations, 64)},
        error));
    CHECK(error.code == loglens::PersistenceErrorCode::InvalidValue);
    CHECK(!loglens::saveSourceProfiles(
        profilePath.string(),
        {profile("bad-policy", loglens::Format::Raw,
                 static_cast<loglens::MultilinePolicy>(99), 64)},
        error));
    CHECK(error.code == loglens::PersistenceErrorCode::InvalidValue);
    CHECK(!loglens::saveSourceProfiles(
        profilePath.string(),
        {profile("zero-limit", loglens::Format::Raw,
                 loglens::MultilinePolicy::FoldContinuations, 0)},
        error));
    CHECK(error.code == loglens::PersistenceErrorCode::LimitExceeded);
    CHECK(!loglens::saveSourceProfiles(
        profilePath.string(),
        {profile("large-limit", loglens::Format::Raw,
                 loglens::MultilinePolicy::FoldContinuations,
                 loglens::kMaxRecordBytes + 1)},
        error));
    CHECK(error.code == loglens::PersistenceErrorCode::LimitExceeded);

    CHECK(!loglens::saveSavedQueries(queryPath.string(),
                                     {query("", "message~ok")}, error));
    CHECK(error.code == loglens::PersistenceErrorCode::InvalidValue);
    CHECK(!loglens::saveSavedQueries(queryPath.string(),
                                     {query("empty", "")}, error));
    CHECK(error.code == loglens::PersistenceErrorCode::InvalidQuery);
    CHECK(!loglens::saveSavedQueries(
        queryPath.string(),
        {query("oversized", std::string(loglens::kMaxFilterQueryBytes + 1, 'x'))},
        error));
    CHECK(error.code == loglens::PersistenceErrorCode::LimitExceeded);
    std::string invalidExpression = "message~";
    invalidExpression += "\xC3(";
    CHECK(!loglens::saveSavedQueries(queryPath.string(),
                                     {query("invalid-utf8", invalidExpression)}, error));
    CHECK(error.code == loglens::PersistenceErrorCode::InvalidQuery);
    CHECK(loglens::saveSavedQueries(
        queryPath.string(),
        {query("smile\xF0\x9F\x98\x80", "message~ok")}, error));
}

void testPersistenceSyntaxAndShapeFailures() {
    TempDirectory directory;
    const fs::path path = directory.path() / "syntax-profiles.json";
    const std::string prefix = "{\"schema\":\"loglens.source-profiles/v1\",\"profiles\":";
    const std::string item =
        "{\"name\":\"one\",\"format\":\"raw\","
        "\"multiline\":\"fold-continuations\",\"max_record_bytes\":";
    expectProfileReject(path, "[]", loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(path, "{}", loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(path, prefix + "[1]}", loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(path, prefix + "[true]}", loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(path, prefix + "[false]}", loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(path, prefix + "[null]}", loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(path, prefix + "{}}", loglens::PersistenceErrorCode::InvalidValue);
    expectProfileReject(path, prefix + "[{}", loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(path, prefix + "[{} {}]}", loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(path, prefix + "[{},]}", loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(path, prefix + "[" + item + "64} " + item + "64}]}",
                        loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(path, prefix + "[" + item + "-}]}",
                        loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(path, prefix + "[" + item + "01}]}",
                        loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(path, prefix + "[" + item + "1.}]}",
                        loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(path, prefix + "[" + item + "1e}]}",
                        loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(path, prefix + "[" + item + "+1}]}",
                        loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(path,
                        "{\"schema\":\"loglens.source-profiles/v1\","
                        "\"profiles\":[{\"name\":\"one\",\"format\":\"raw\","
                        "\"multiline\":\"fold-continuations\",\"max_record_bytes\":64,]}",
                        loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(path,
                        "{\"schema\":\"loglens.source-profiles/v1\","
                        "\"profiles\":[{\"name\":\"one\" \"format\":\"raw\"}]}",
                        loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(path,
                        "{\"schema\":\"loglens.source-profiles/v1\","
                        "\"profiles\":[] \"extra\":true}",
                        loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(path,
                        "{\"schema\":\"loglens.source-profiles/v1\","
                        "\"profiles\":[{\"name\":\"bad\\x\"}]}",
                        loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(path,
                        "{\"schema\":\"loglens.source-profiles/v1\","
                        "\"profiles\":[{\"name\":\"bad\\uD800\"}]}",
                        loglens::PersistenceErrorCode::Malformed);
    expectProfileReject(path,
                        "{\"schema\":true,\"profiles\":[]}",
                        loglens::PersistenceErrorCode::InvalidValue);

    std::string tooManyMembers =
        "{\"schema\":\"loglens.source-profiles/v1\",\"profiles\":[]";
    for (unsigned index = 0; index < 7; ++index) {
        tooManyMembers += ",\"future" + std::to_string(index) + "\":false";
    }
    tooManyMembers += '}';
    expectProfileReject(path, tooManyMembers,
                        loglens::PersistenceErrorCode::LimitExceeded);

    std::string deep = prefix + '[';
    for (unsigned index = 0; index < 18; ++index) {
        deep += '[';
    }
    deep += '0';
    for (unsigned index = 0; index < 18; ++index) {
        deep += ']';
    }
    deep += "]}";
    expectProfileReject(path, deep, loglens::PersistenceErrorCode::LimitExceeded);

    expectProfileReject(path, prefix + "[" + item
                            + std::string(loglens::kMaxFilterQueryBytes + 300, '1') + "}]}",
                        loglens::PersistenceErrorCode::LimitExceeded);
    expectProfileReject(path,
                        "{\"schema\":\"loglens.source-profiles/v1\","
                        "\"profiles\":[],\"future\":tru}",
                        loglens::PersistenceErrorCode::Malformed);

    std::string hugeSchema =
        "{\"schema\":\"" + std::string(loglens::kMaxFilterQueryBytes + 300, 'x')
        + "\",\"profiles\":[]}";
    expectProfileReject(path, hugeSchema,
                        loglens::PersistenceErrorCode::LimitExceeded);
}

void testBoundsAndUnsafePaths() {
    TempDirectory directory;
    const fs::path profilePath = directory.path() / "bounds-profiles.json";
    loglens::PersistenceError error;
    CHECK(!loglens::saveSourceProfiles(
        profilePath.string(),
        {profile(std::string(loglens::kMaxPersistedNameBytes + 1, 'x'), loglens::Format::Raw,
                 loglens::MultilinePolicy::FoldContinuations, 64)},
        error));
    CHECK(error.code == loglens::PersistenceErrorCode::LimitExceeded);
    std::string invalidName = "invalid";
    invalidName.push_back(static_cast<char>(0xff));
    CHECK(!loglens::saveSourceProfiles(
        profilePath.string(),
        {profile(invalidName, loglens::Format::Raw,
                 loglens::MultilinePolicy::FoldContinuations, 64)},
        error));
    CHECK(error.code == loglens::PersistenceErrorCode::InvalidValue);

    const std::string oversizedFile(loglens::kMaxPersistenceFileBytes + 1, 'x');
    writeFile(profilePath, oversizedFile);
    const loglens::SourceProfileLoadResult oversized =
        loglens::loadSourceProfiles(profilePath.string());
    CHECK(!oversized.ok());
    CHECK(oversized.error.code == loglens::PersistenceErrorCode::LimitExceeded);

    const fs::path tooManyPath = directory.path() / "too-many-queries.json";
    std::string tooManyJson =
        "{\"schema\":\"loglens.saved-queries/v1\",\"queries\":[";
    for (std::size_t index = 0; index <= loglens::kMaxPersistedItems; ++index) {
        if (index != 0) {
            tooManyJson.push_back(',');
        }
        tooManyJson += "{\"name\":\"q" + std::to_string(index)
                       + "\",\"expression\":\"message~x\"}";
    }
    tooManyJson += "]}";
    writeFile(tooManyPath, tooManyJson);
    const loglens::SavedQueryLoadResult tooManyLoaded =
        loglens::loadSavedQueries(tooManyPath.string());
    CHECK(!tooManyLoaded.ok());
    CHECK(tooManyLoaded.error.code == loglens::PersistenceErrorCode::LimitExceeded);
    CHECK(tooManyLoaded.queries.empty());

    CHECK(!loglens::loadSourceProfiles("").ok());
    CHECK(loglens::loadSourceProfiles("").error.code
          == loglens::PersistenceErrorCode::UnsafePath);

    const fs::path target = directory.path() / "real.json";
    const fs::path link = directory.path() / "link.json";
    writeFile(target, "original");
    std::error_code symlinkError;
    fs::create_symlink(target, link, symlinkError);
    if (!symlinkError) {
        const loglens::SourceProfileLoadResult linked =
            loglens::loadSourceProfiles(link.string());
        CHECK(!linked.ok());
        CHECK(linked.error.code == loglens::PersistenceErrorCode::UnsafePath);
        CHECK(!loglens::saveSourceProfiles(
            link.string(),
            {profile("safe", loglens::Format::Raw,
                     loglens::MultilinePolicy::FoldContinuations, 64)},
            error));
        CHECK(error.code == loglens::PersistenceErrorCode::UnsafePath);
        CHECK_EQ(readFile(target), "original");
    }
}

void testPersistencePathSafetyFailures() {
    TempDirectory directory;
    const fs::path target = directory.path() / "path-safety.json";
    const loglens::SourceProfile safe =
        profile("safe", loglens::Format::Raw,
                loglens::MultilinePolicy::FoldContinuations, 64);
    loglens::PersistenceError error;

    CHECK(!loglens::saveSourceProfiles(directory.path().string(), {safe}, error));
    CHECK(error.code == loglens::PersistenceErrorCode::UnsafePath);
    CHECK(!loglens::loadSourceProfiles(directory.path().string()).ok());
    CHECK(loglens::loadSourceProfiles(directory.path().string()).error.code
          == loglens::PersistenceErrorCode::UnsafePath);

    const fs::path regularParent = directory.path() / "regular-parent";
    writeFile(regularParent, "not a directory");
    CHECK(!loglens::saveSourceProfiles(
        (regularParent / "child.json").string(), {safe}, error));
    CHECK(error.code == loglens::PersistenceErrorCode::UnsafePath);

    CHECK(!loglens::saveSourceProfiles(
        (directory.path() / "missing-parent" / "child.json").string(), {safe}, error));
    CHECK(error.code == loglens::PersistenceErrorCode::UnsafePath);

    const fs::path destinationDirectory = directory.path() / "destination-directory";
    CHECK(fs::create_directory(destinationDirectory));
    CHECK(!loglens::saveSourceProfiles(destinationDirectory.string(), {safe}, error));
    CHECK(error.code == loglens::PersistenceErrorCode::UnsafePath);

    const fs::path linkedParent = directory.path() / "linked-parent";
    std::error_code linkError;
    fs::create_directory_symlink(directory.path(), linkedParent, linkError);
    if (!linkError) {
        CHECK(!loglens::saveSourceProfiles(
            (linkedParent / "child.json").string(), {safe}, error));
        CHECK(error.code == loglens::PersistenceErrorCode::UnsafePath);
    }

}

#ifndef _WIN32
void testPredictableTemporaryPathAttackCannotOverwriteFiles() {
    TempDirectory directory;
    const fs::path target = directory.path() / "attacked.json";
    const loglens::SourceProfile safe =
        profile("safe", loglens::Format::Raw,
                loglens::MultilinePolicy::FoldContinuations, 64);
    const std::string process = std::to_string(static_cast<std::uint64_t>(::getpid()));
    std::vector<fs::path> occupied;
    for (unsigned index = 0; index < 256; ++index) {
        const fs::path candidate =
            directory.path() / ("attacked.json.tmp." + process + "."
                                + std::to_string(index));
        writeFile(candidate, "attacker-owned");
        occupied.push_back(candidate);
    }
    writeFile(target, "original");

    loglens::PersistenceError error;
    CHECK(!loglens::saveSourceProfiles(target.string(), {safe}, error));
    CHECK(error.code == loglens::PersistenceErrorCode::AtomicReplace);
    CHECK_EQ(readFile(target), "original");
    for (const fs::path& candidate : occupied) {
        CHECK_EQ(readFile(candidate), "attacker-owned");
    }
}
#endif

} // namespace

int main() {
    testStableNames();
    testProfilesRoundTripAndCanonicalOrdering();
    testMissingStoreIsAnEmptySuccessfulLoad();
    testQueriesRoundTripAndReuseFilterSemantics();
    testSavedQuerySerializationEscapesJsonControls();
    testSeparateLinesPolicyUsesTheSameAssemblerContract();
    testInvalidSaveDoesNotTouchTheExistingStore();
    testSchemaIsStrictAndMigrationFailsClosed();
    testValidationRejectsInvalidValuesAndAcceptsUtf8();
    testPersistenceSyntaxAndShapeFailures();
    testBoundsAndUnsafePaths();
    testPersistencePathSafetyFailures();
#ifndef _WIN32
    testPredictableTemporaryPathAttackCannotOverwriteFiles();
#endif
    return checkSummary();
}
