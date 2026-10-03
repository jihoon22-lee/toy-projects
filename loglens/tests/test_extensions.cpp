#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include "loglens/evidence.hpp"
#include "loglens/file_search.hpp"
#include "loglens/filter_expr.hpp"
#include "loglens/persistence.hpp"
#include "loglens/triage.hpp"
#include "loglens/window_analysis.hpp"

namespace {
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();
}
} // namespace

class TestExtensions : public QObject {
    Q_OBJECT
private slots:
    void sha256MatchesIndependentImplementation();
    void notesRequireExactEvidenceAndPreserveLegacy();
    void sessionV2RoundTripsFullInvestigation();
    void jsonCorrelationIsStructuredAndAmbiguityIsNotGuessed();
    void wholeFileSearchCompletesMultilineAndFindsOldRecords();
    void wholeFileSearchDetectsConcurrentSameSizeRewrite();
};

void TestExtensions::sha256MatchesIndependentImplementation() {
    for (const QByteArray bytes :
         {QByteArray(), QByteArray("abc"), QByteArray(65, 'x'), QByteArray(65536, '\xff')}) {
        QCOMPARE(loglens::sha256Hex(std::string_view(bytes.data(), bytes.size())),
                 QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString());
    }
}

void TestExtensions::notesRequireExactEvidenceAndPreserveLegacy() {
    auto record = loglens::parseLine("one", loglens::Format::Raw, 1);
    loglens::TriageEntry entry{
        "/log", 1, true, "note", "1:2", 0, loglens::recordFingerprint(record)};
    QVERIFY(loglens::matchesTriageEntry(entry, "/log", "1:2", 0, record));
    QVERIFY(!loglens::matchesTriageEntry(entry, "/log", "1:3", 0, record));
    QVERIFY(!loglens::matchesTriageEntry(entry, "/log", "1:2", 1, record));
    record.raw = "two";
    QVERIFY(!loglens::matchesTriageEntry(entry, "/log", "1:2", 0, record));
    const auto legacy = loglens::parseTriageState(
        R"({"schema":"loglens.triage/v1","rules":[],"entries":[{"source_path":"/log","line_number":1,"bookmarked":true,"annotation":"old"}]})");
    QVERIFY(legacy.ok());
    QVERIFY(legacy.migrated);
    QCOMPARE(legacy.state.entries.size(), std::size_t(1));
    QVERIFY(!loglens::matchesTriageEntry(legacy.state.entries.front(), "/log", "1:2", 0, record));
}

void TestExtensions::sessionV2RoundTripsFullInvestigation() {
    QTemporaryDir directory;
    loglens::SessionState state;
    state.source_path = directory.filePath("app.log").toStdString();
    state.search = "request";
    state.filter = "trace_id==abc";
    state.follow = true;
    state.tail_mode = false;
    state.tail_records = 200;
    state.selected_window = loglens::TimeWindow{1, 2};
    state.baseline_window = loglens::TimeWindow{2, 3};
    state.comparison_window = loglens::TimeWindow{3, 4};
    state.source_identity = "1:2";
    state.source_fingerprint = loglens::sha256Hex("log");
    state.source_modified = "42";
    state.fingerprint_bytes = state.source_size = 3;
    state.plugin_fingerprint = loglens::sha256Hex("plugin");
    state.format_plugin = "/parser.json";
    state.layout = "YQ==";
    state.geometry = "Yg==";
    state.table_header = "Yw==";
    state.triage.entries.push_back(
        {state.source_path, 1, true, "note", "1:2", 0, loglens::sha256Hex("log")});
    loglens::PersistenceError error;
    const auto path = directory.filePath("session.json").toStdString();
    QVERIFY2(loglens::saveSession(path, state, error), error.message.c_str());
    const auto loaded = loglens::loadSession(path);
    QVERIFY2(loaded.ok(), loaded.error.message.c_str());
    QVERIFY(!loaded.migrated);
    QCOMPARE(loaded.state.search, state.search);
    QVERIFY(loaded.state.follow);
    QVERIFY(!loaded.state.tail_mode);
    QCOMPARE(loaded.state.selected_window->begin_ms, std::uint64_t(1));
    QCOMPARE(loaded.state.baseline_window->end_ms, std::uint64_t(3));
    QCOMPARE(loaded.state.comparison_window->end_ms, std::uint64_t(4));
    QCOMPARE(loaded.state.plugin_fingerprint, state.plugin_fingerprint);
    QCOMPARE(loaded.state.source_modified, state.source_modified);
    QCOMPARE(loaded.state.triage.entries.front().record_fingerprint,
             state.triage.entries.front().record_fingerprint);
    QCOMPARE(loaded.state.table_header, state.table_header);
}

void TestExtensions::jsonCorrelationIsStructuredAndAmbiguityIsNotGuessed() {
    const auto record = loglens::parseLine(
        R"({"ts":"2026-01-01T00:00:00Z","level":"INFO","msg":"ok","trace_id":"abc-123","request_id":"quoted\"id","n":42})",
        loglens::Format::JsonLine, 1);
    QCOMPARE(record.fields.at("trace_id"), std::string("abc-123"));
    loglens::ParseError error;
    const auto filter = loglens::Filter::parse("trace_id==abc-123 AND field.n==42", error);
    QVERIFY(filter);
    QVERIFY(filter->matches(record));
    const auto result =
        loglens::compareWindows({record}, {record.timestamp_ms - 2000, record.timestamp_ms - 1000},
                                {record.timestamp_ms, record.timestamp_ms + 1000});
    QVERIFY(
        std::any_of(result.correlations.begin(), result.correlations.end(), [](const auto &value) {
            return value.field == "trace_id" && value.value == "abc-123";
        }));
    const auto ambiguous = loglens::parseLine(R"({"trace_id":"a","trace_id":"b","msg":"x"})",
                                              loglens::Format::JsonLine, 1);
    QVERIFY(ambiguous.fields.count("trace_id") == 0);
    QVERIFY(!filter->matches(ambiguous));
}

void TestExtensions::wholeFileSearchCompletesMultilineAndFindsOldRecords() {
    QTemporaryDir directory;
    const auto path = directory.filePath("app.log");
    QByteArray bytes = "2026-01-01T00:00:00Z INFO [api] ancient needle\n  continuation evidence\n";
    for (int i = 0; i < 100; ++i)
        bytes += "2026-01-01T00:00:01Z INFO [api] ordinary\n";
    bytes += "2026-01-01T00:00:02Z ERROR [api] final needle";
    write(path, bytes);
    loglens::FileSearchOptions options;
    options.path = path.toStdString();
    options.text = "needle";
    const auto result = loglens::searchFile(options);
    QVERIFY2(result.complete, result.error.c_str());
    QCOMPARE(result.records.size(), std::size_t(2));
    QCOMPARE(result.records.front().line_number, std::size_t(1));
    QVERIFY(result.records.front().raw.find("continuation") != std::string::npos);
    QVERIFY(!result.source_identity.empty());
    options.max_results = 1;
    const auto limited = loglens::searchFile(options);
    QVERIFY(limited.limit_reached);
    QVERIFY(!limited.complete);
    QCOMPARE(limited.records.size(), std::size_t(1));
    options.max_scan_bytes = 20;
    const auto partial = loglens::searchFile(options);
    QVERIFY(partial.limit_reached);
    QVERIFY(partial.records.empty());
    const auto cancelled = loglens::searchFile(options, [] { return true; });
    QVERIFY(cancelled.cancelled);
    QVERIFY(cancelled.records.empty());
}

void TestExtensions::wholeFileSearchDetectsConcurrentSameSizeRewrite() {
    QTemporaryDir directory;
    const auto path = directory.filePath("app.log");
    write(path, "old needle\nsecond record\n");
    loglens::FileSearchOptions options;
    options.path = path.toStdString();
    options.text = "needle";
    int polls = 0;
    const auto result = loglens::searchFile(options, [&] {
        if (++polls == 2)
            write(path, "new needle\nsecond record\n");
        return false;
    });
    QVERIFY(!result.complete);
    QVERIFY(result.error.find("changed") != std::string::npos);
}
QTEST_GUILESS_MAIN(TestExtensions)
#include "test_extensions.moc"
