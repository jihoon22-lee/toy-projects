#include "check.hpp"

#include "loglens/log_parser.hpp"

using loglens::detectFormat;
using loglens::Format;
using loglens::isContinuation;
using loglens::Level;
using loglens::LogRecord;
using loglens::parseLine;

namespace {

void testDetection() {
    CHECK(detectFormat("2026-08-26T04:15:22Z INFO x") == Format::PlainIso);
    CHECK(detectFormat("Aug 26 04:15:22 host app: msg") == Format::Syslog);
    CHECK(detectFormat("  {\"level\":\"info\"}") == Format::JsonLine);
    CHECK(detectFormat("just some text") == Format::Auto);
    CHECK(detectFormat("") == Format::Auto);
    CHECK(detectFormat("12345 not a date") == Format::Auto);
}

void testPlainIso() {
    const LogRecord record =
        parseLine("2026-08-26T04:15:22.123Z WARN  [db] slow query", Format::Auto, 7);
    CHECK(record.level == Level::Warn);
    CHECK_EQ(record.source, std::string("db"));
    CHECK_EQ(record.message, std::string("slow query"));
    CHECK_EQ(record.line_number, static_cast<std::size_t>(7));
    CHECK(record.timestamp_ms > 0);
    CHECK_EQ(record.raw, std::string("2026-08-26T04:15:22.123Z WARN  [db] slow query"));

    // No bracketed component: the remainder is all message.
    const LogRecord plain = parseLine("2026-08-26T04:15:22Z INFO started up", Format::Auto, 1);
    CHECK(plain.source.empty());
    CHECK_EQ(plain.message, std::string("started up"));

    // Malformed timestamp yields 0 rather than a bogus epoch value.
    const LogRecord bad = parseLine("2026-99-99T99:99:99Z INFO x", Format::PlainIso, 1);
    CHECK(bad.level == Level::Info);
}

void testSyslog() {
    const LogRecord record =
        parseLine("Aug 26 04:15:22 host nginx: request served", Format::Auto, 2);
    CHECK_EQ(record.source, std::string("nginx"));
    CHECK_EQ(record.message, std::string("request served"));
    CHECK(record.timestamp_ms == 0);

    // No trailing colon on the component: keep the whole remainder.
    const LogRecord loose = parseLine("Aug 26 04:15:22 host bare message", Format::Syslog, 1);
    CHECK(!loose.message.empty());
}

void testJsonLine() {
    const LogRecord record = parseLine(
        R"({"ts":"2026-08-26T04:15:22.500Z","level":"error","logger":"api","msg":"boom"})",
        Format::Auto, 3);
    CHECK(record.level == Level::Error);
    CHECK_EQ(record.source, std::string("api"));
    CHECK_EQ(record.message, std::string("boom"));
    CHECK(record.timestamp_ms > 0);

    // "message" is accepted as an alias for "msg".
    const LogRecord alias =
        parseLine(R"({"level":"info","message":"alt key"})", Format::JsonLine, 1);
    CHECK_EQ(alias.message, std::string("alt key"));

    // Escapes inside the value are decoded.
    const LogRecord escaped =
        parseLine(R"({"level":"info","msg":"say \"hi\"\nthen go"})", Format::JsonLine, 1);
    CHECK(escaped.message.find('"') != std::string::npos);
    CHECK(escaped.message.find('\n') != std::string::npos);

    // Unquoted values and missing keys are tolerated.
    const LogRecord numeric = parseLine(R"({"level":"warn","msg":42})", Format::JsonLine, 1);
    CHECK_EQ(numeric.message, std::string("42"));
    const LogRecord missing = parseLine(R"({"level":"warn"})", Format::JsonLine, 1);
    CHECK(missing.level == Level::Warn);
    const LogRecord truncated = parseLine(R"({"msg":"unterminated)", Format::JsonLine, 1);
    CHECK(truncated.raw.size() > 0);
}

void testUnparseableKeepsData() {
    const std::string line = "!!! not a log line at all";
    const LogRecord record = parseLine(line, Format::Auto, 9);
    CHECK(record.level == Level::Unknown);
    CHECK_EQ(record.message, line);
    CHECK_EQ(record.raw, line);
    CHECK_EQ(record.line_number, static_cast<std::size_t>(9));

    const LogRecord empty = parseLine("", Format::Auto, 1);
    CHECK(empty.raw.empty());
}

void testContinuation() {
    CHECK(isContinuation("    at Foo.bar(Foo.java:10)"));
    CHECK(isContinuation("\tcaused by"));
    CHECK(isContinuation("at Connection.read"));
    CHECK(!isContinuation("2026-08-26T04:15:22Z INFO x"));
    CHECK(!isContinuation(""));
    // "attention" starts with "at" but not "at ", so it must not be folded.
    CHECK(!isContinuation("attention: not a continuation"));
}

} // namespace

int main() {
    testDetection();
    testPlainIso();
    testSyslog();
    testJsonLine();
    testUnparseableKeepsData();
    testContinuation();
    return checkSummary();
}
