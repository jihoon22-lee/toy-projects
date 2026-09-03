#include "check.hpp"

#include "loglens/log_parser.hpp"

#include <stdexcept>
#include <utility>

using loglens::detectFormat;
using loglens::EncodingErrorPolicy;
using loglens::Format;
using loglens::isContinuation;
using loglens::Level;
using loglens::LogRecord;
using loglens::ParseDiagnosticCode;
using loglens::ParseStatus;
using loglens::kMaxParseDiagnostics;
using loglens::parseLine;
using loglens::parseDiagnosticCodeName;
using loglens::parseStatusName;
using loglens::RecordAssembler;
using loglens::RecordDelta;

namespace {

bool hasDiagnostic(const LogRecord& record, ParseDiagnosticCode code) {
    for (const loglens::ParseDiagnostic& diagnostic : record.diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

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
    CHECK(record.parse_status == ParseStatus::Parsed);
    CHECK(record.diagnostics.empty());
    CHECK_EQ(record.source, std::string("nginx"));
    CHECK_EQ(record.message, std::string("request served"));
    // RFC3164 has no year or timezone, so a valid syslog timestamp is kept as
    // an unknown epoch value rather than being guessed from the local clock.
    CHECK(record.timestamp_ms == 0);
    CHECK_EQ(record.raw, std::string("Aug 26 04:15:22 host nginx: request served"));

    const LogRecord paddedDay =
        parseLine("Aug  6 04:15:22 host nginx: request served", Format::Syslog, 1);
    CHECK(paddedDay.parse_status == ParseStatus::Parsed);
    CHECK(paddedDay.diagnostics.empty());

    // No trailing colon on the component: keep the whole remainder but expose
    // the malformed component instead of reporting a false Parsed result.
    const LogRecord loose = parseLine("Aug 26 04:15:22 host bare message", Format::Syslog, 1);
    CHECK(loose.parse_status == ParseStatus::Partial);
    CHECK(hasDiagnostic(loose, ParseDiagnosticCode::InvalidField));
    CHECK_EQ(loose.message, std::string("bare message"));
    CHECK_EQ(loose.raw, std::string("Aug 26 04:15:22 host bare message"));
}

void testSyslogValidation() {
    const std::vector<std::pair<std::string, ParseDiagnosticCode>> invalidFields = {
        {"Foo 26 04:15:22 host app: message", ParseDiagnosticCode::InvalidField},
        {"Aug 00 04:15:22 host app: message", ParseDiagnosticCode::InvalidField},
        {"Aug 32 04:15:22 host app: message", ParseDiagnosticCode::InvalidField},
        {"Aug xx 04:15:22 host app: message", ParseDiagnosticCode::InvalidField},
        {"Aug 26 04:15:22 host app message", ParseDiagnosticCode::InvalidField},
        {"Aug 26 04:15:22 host : message", ParseDiagnosticCode::InvalidField},
        {"Aug 26 04:15:22 host app:: message", ParseDiagnosticCode::InvalidField},
    };
    for (const auto& [line, code] : invalidFields) {
        const LogRecord record = parseLine(line, Format::Syslog, 1);
        CHECK(record.parse_status == ParseStatus::Partial);
        CHECK(hasDiagnostic(record, code));
        CHECK_EQ(record.raw, line);
    }

    const std::string longHost(256, 'h');
    const LogRecord oversizedHost = parseLine(
        "Aug 26 04:15:22 " + longHost + " app: message", Format::Syslog, 1);
    CHECK(oversizedHost.parse_status == ParseStatus::Partial);
    CHECK(hasDiagnostic(oversizedHost, ParseDiagnosticCode::InvalidField));
    CHECK_EQ(oversizedHost.raw, "Aug 26 04:15:22 " + longHost + " app: message");

    std::string controlHost = "host";
    controlHost.push_back('\x01');
    const LogRecord invalidHost = parseLine(
        "Aug 26 04:15:22 " + controlHost + " app: message", Format::Syslog, 1);
    CHECK(invalidHost.parse_status == ParseStatus::Partial);
    CHECK(hasDiagnostic(invalidHost, ParseDiagnosticCode::InvalidField));
    CHECK_EQ(invalidHost.raw, "Aug 26 04:15:22 " + controlHost + " app: message");

    const std::vector<std::string> invalidTimes = {
        "Aug 26 24:00:00 host app: message",
        "Aug 26 04:60:00 host app: message",
        "Aug 26 04:15:60 host app: message",
        "Aug 26 4:15:22 host app: message",
        "Aug 26 04:15 host app: message",
    };
    for (const std::string& line : invalidTimes) {
        const LogRecord record = parseLine(line, Format::Syslog, 1);
        CHECK(record.parse_status == ParseStatus::Partial);
        CHECK(hasDiagnostic(record, ParseDiagnosticCode::InvalidTimestamp));
        CHECK_EQ(record.raw, line);
    }

    const std::string longComponent(33, 'a');
    const LogRecord oversizedComponent = parseLine(
        "Aug 26 04:15:22 host " + longComponent + ": message", Format::Syslog, 1);
    CHECK(oversizedComponent.parse_status == ParseStatus::Partial);
    CHECK(hasDiagnostic(oversizedComponent, ParseDiagnosticCode::InvalidField));
    CHECK_EQ(oversizedComponent.raw,
             "Aug 26 04:15:22 host " + longComponent + ": message");

    const std::vector<std::string> missingFields = {
        "", "Aug", "Aug 26", "Aug 26 04:15:22", "Aug 26 04:15:22 host"};
    for (const std::string& line : missingFields) {
        const LogRecord record = parseLine(line, Format::Syslog, 1);
        CHECK(record.parse_status == ParseStatus::Partial);
        CHECK(hasDiagnostic(record, ParseDiagnosticCode::MissingField));
        CHECK_EQ(record.raw, line);
    }
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

void testJsonValidationAndUnicode() {
    const LogRecord unicode = parseLine(
        R"({"ts":"2026-08-26T04:15:22Z","level":"info","logger":"api","msg":"smile \u263A \uD834\uDD1E"})",
        Format::JsonLine, 4);
    CHECK(unicode.parse_status == ParseStatus::Parsed);
    CHECK_EQ(unicode.message,
             std::string("smile ") + "\xE2\x98\xBA" + " " + "\xF0\x9D\x84\x9E");
    CHECK(unicode.diagnostics.empty());

    const LogRecord malformedEscape =
        parseLine(R"({"level":"info","msg":"bad \x"})", Format::JsonLine, 1);
    CHECK(malformedEscape.parse_status == ParseStatus::Invalid);
    CHECK(malformedEscape.level == Level::Unknown);
    CHECK_EQ(malformedEscape.message, malformedEscape.raw);
    CHECK(hasDiagnostic(malformedEscape, ParseDiagnosticCode::InvalidEscape));

    const LogRecord malformedUnicode =
        parseLine(R"({"level":"info","msg":"bad \uD800"})", Format::JsonLine, 1);
    CHECK(malformedUnicode.parse_status == ParseStatus::Invalid);
    CHECK(hasDiagnostic(malformedUnicode, ParseDiagnosticCode::InvalidUnicode));

    const LogRecord trailing =
        parseLine(R"({"level":"info","msg":"ok"} trailing)", Format::JsonLine, 1);
    CHECK(trailing.parse_status == ParseStatus::Invalid);
    CHECK(hasDiagnostic(trailing, ParseDiagnosticCode::TrailingData));

    const LogRecord duplicate = parseLine(
        R"({"level":"info","msg":"first","msg":"second"})", Format::JsonLine, 1);
    CHECK(duplicate.parse_status == ParseStatus::Partial);
    CHECK_EQ(duplicate.message, std::string("first"));
    CHECK(hasDiagnostic(duplicate, ParseDiagnosticCode::DuplicateField));

    const LogRecord nested = parseLine(
        R"({"ts":"2026-08-26T04:15:22Z","level":"warn","logger":"api","msg":"ok","extra":{"items":[true,null,-2.5e+3]}})",
        Format::JsonLine, 1);
    CHECK(nested.parse_status == ParseStatus::Parsed);
    CHECK_EQ(nested.message, std::string("ok"));

    std::string utf8Line =
        R"({"ts":"2026-08-26T04:15:22Z","level":"info","logger":"api","msg":"caf)";
    utf8Line += "\xC3\xA9\"}";
    const LogRecord utf8 = parseLine(utf8Line, Format::JsonLine, 1);
    CHECK(utf8.parse_status == ParseStatus::Parsed);
    CHECK_EQ(utf8.message, std::string("caf") + "\xC3\xA9");

    std::string invalidUtf8 =
        R"({"ts":"2026-08-26T04:15:22Z","level":"info","msg":"bad )";
    invalidUtf8.push_back(static_cast<char>(0xC3));
    invalidUtf8 += "(\"}";
    const LogRecord invalid = parseLine(invalidUtf8, Format::JsonLine, 1);
    CHECK(invalid.parse_status == ParseStatus::Invalid);
    CHECK_EQ(invalid.message, invalid.raw);
    CHECK(hasDiagnostic(invalid, ParseDiagnosticCode::InvalidUnicode));

    const LogRecord badStructure =
        parseLine(R"([{"level":"info","msg":"not an object"}])", Format::JsonLine, 1);
    CHECK(badStructure.parse_status == ParseStatus::Invalid);
    CHECK_EQ(badStructure.message, badStructure.raw);
    CHECK(hasDiagnostic(badStructure, ParseDiagnosticCode::InvalidJson));
}

void testTimestampOffsetsAreValidated() {
    const LogRecord utc =
        parseLine("2026-08-26T01:45:22.123Z INFO [api] event", Format::PlainIso, 1);
    const LogRecord offset =
        parseLine("2026-08-26T04:15:22.123+02:30 INFO [api] event", Format::PlainIso, 1);
    CHECK(utc.parse_status == ParseStatus::Parsed);
    CHECK(offset.parse_status == ParseStatus::Parsed);
    CHECK_EQ(offset.timestamp_ms, utc.timestamp_ms);

    const LogRecord compact =
        parseLine("2026-08-26T04:15:22.123+0230 INFO [api] event", Format::PlainIso, 1);
    CHECK(compact.parse_status == ParseStatus::Parsed);
    CHECK_EQ(compact.timestamp_ms, utc.timestamp_ms);

    const LogRecord badOffset =
        parseLine("2026-08-26T04:15:22+25:00 INFO [api] event", Format::PlainIso, 1);
    CHECK(badOffset.parse_status == ParseStatus::Partial);
    CHECK(badOffset.timestamp_ms == 0);
    CHECK(hasDiagnostic(badOffset, ParseDiagnosticCode::InvalidTimestampOffset));

    const LogRecord badDate =
        parseLine("2026-02-29T04:15:22Z INFO [api] event", Format::PlainIso, 1);
    CHECK(badDate.parse_status == ParseStatus::Partial);
    CHECK(hasDiagnostic(badDate, ParseDiagnosticCode::InvalidTimestamp));

    const LogRecord epoch =
        parseLine("1970-01-01T00:00:00Z INFO [api] epoch", Format::PlainIso, 1);
    CHECK(epoch.parse_status == ParseStatus::Parsed);
    CHECK_EQ(epoch.timestamp_ms, static_cast<std::uint64_t>(0));

    const LogRecord beforeEpoch =
        parseLine("1969-12-31T23:59:59Z INFO [api] before", Format::PlainIso, 1);
    CHECK(beforeEpoch.parse_status == ParseStatus::Partial);
    CHECK(hasDiagnostic(beforeEpoch, ParseDiagnosticCode::InvalidTimestamp));

    const LogRecord upperBound =
        parseLine("9999-12-31T23:59:59.999Z INFO [api] upper", Format::PlainIso, 1);
    CHECK(upperBound.parse_status == ParseStatus::Parsed);
    CHECK(upperBound.timestamp_ms > 0);

    const LogRecord leapDay =
        parseLine("2000-02-29T04:15:22Z INFO [api] leap", Format::PlainIso, 1);
    CHECK(leapDay.parse_status == ParseStatus::Parsed);
    const LogRecord centuryDay =
        parseLine("1900-02-29T04:15:22Z INFO [api] invalid", Format::PlainIso, 1);
    CHECK(centuryDay.parse_status == ParseStatus::Partial);
    CHECK(hasDiagnostic(centuryDay, ParseDiagnosticCode::InvalidTimestamp));

    const LogRecord highPrecision = parseLine(
        "2026-08-26T04:15:22.123456789z INFO [api] precise", Format::PlainIso, 1);
    CHECK(highPrecision.parse_status == ParseStatus::Parsed);
    CHECK(highPrecision.timestamp_ms > utc.timestamp_ms);
    const LogRecord negativeOffset =
        parseLine("2026-08-26T01:45:22-00:30 INFO [api] event", Format::PlainIso, 1);
    CHECK(negativeOffset.parse_status == ParseStatus::Parsed);
    CHECK(negativeOffset.timestamp_ms > utc.timestamp_ms);

    const std::vector<std::string> malformedOffsets = {
        "2026-08-26T04:15:22+02 INFO [api] event",
        "2026-08-26T04:15:22+02: INFO [api] event",
        "2026-08-26T04:15:22+02:60 INFO [api] event",
        "2026-08-26T04:15:22+24:00 INFO [api] event",
        "2026-08-26T04:15:22+00:00junk INFO [api] event",
    };
    for (const std::string& line : malformedOffsets) {
        const LogRecord malformed = parseLine(line, Format::PlainIso, 1);
        CHECK(malformed.parse_status == ParseStatus::Partial);
        CHECK(hasDiagnostic(malformed, ParseDiagnosticCode::InvalidTimestampOffset));
    }
    const LogRecord missingTimezone =
        parseLine("2026-08-26T04:15:22 INFO [api] event", Format::PlainIso, 1);
    CHECK(missingTimezone.parse_status == ParseStatus::Partial);
    CHECK(hasDiagnostic(missingTimezone, ParseDiagnosticCode::InvalidTimestamp));
    const LogRecord emptyFraction =
        parseLine("2026-08-26T04:15:22.Z INFO [api] event", Format::PlainIso, 1);
    CHECK(emptyFraction.parse_status == ParseStatus::Partial);
    CHECK(hasDiagnostic(emptyFraction, ParseDiagnosticCode::InvalidTimestamp));
    const LogRecord longFraction =
        parseLine("2026-08-26T04:15:22.1234567890Z INFO [api] event", Format::PlainIso, 1);
    CHECK(longFraction.parse_status == ParseStatus::Partial);
    CHECK(hasDiagnostic(longFraction, ParseDiagnosticCode::InvalidTimestamp));
}

void testMalformedJsonCorpusNeverThrows() {
    const std::vector<std::string> malformed = {
        "{", "}", "{\"msg\":}", "{\"msg\":01}", "{\"msg\":true,}",
        "{\"msg\":\"line\nfeed\"}", "{\"msg\":\"\\u12\"}",
        "{\"msg\":\"\\uDC00\"}", "{\"msg\":\"x\"} garbage",
        "{\"msg\": [1, 2,]}", "{\"msg\": {\"nested\": 1,}}",
    };
    for (const std::string& line : malformed) {
        try {
            const LogRecord record = parseLine(line, Format::JsonLine, 1);
            CHECK(record.parse_status == ParseStatus::Invalid);
            CHECK_EQ(record.raw, line);
            CHECK_EQ(record.message, line);
        } catch (...) {
            CHECK(false);
        }
    }
}

void testJsonStringBoundIsReported() {
    const std::string payload(loglens::kMaxRecordBytes / 4 + 32, 'x');
    const std::string line = "{\"level\":\"info\",\"msg\":\"" + payload + "\"}";
    const LogRecord record = parseLine(line, Format::JsonLine, 1);
    CHECK(record.parse_status == ParseStatus::Partial);
    CHECK(record.message.size() < payload.size());
    CHECK(hasDiagnostic(record, ParseDiagnosticCode::LimitExceeded));
    CHECK_EQ(record.raw, line);
}

void testJsonDiagnosticsRemainBounded() {
    std::string line = "{";
    for (std::size_t index = 0; index < 400; ++index) {
        if (index != 0) {
            line += ',';
        }
        line += "\"extra" + std::to_string(index) + "\":0";
    }
    line += '}';
    const LogRecord record = parseLine(line, Format::JsonLine, 1);
    CHECK(record.parse_status == ParseStatus::Partial);
    CHECK(record.diagnostics.size() <= 64);
    CHECK(hasDiagnostic(record, ParseDiagnosticCode::LimitExceeded));
    CHECK_EQ(record.raw, line);
}

void testJsonFieldDiagnosticsRemainBoundedAfterReaderDiagnostics() {
    std::string line = "{\"extra\":0";
    for (std::size_t index = 0; index < 128; ++index) {
        line += ",\"extra\":0";
    }
    line += '}';
    const LogRecord record = parseLine(line, Format::JsonLine, 1);
    CHECK(record.parse_status == ParseStatus::Partial);
    CHECK(record.diagnostics.size() <= kMaxParseDiagnostics);
    CHECK(hasDiagnostic(record, ParseDiagnosticCode::LimitExceeded));
    CHECK_EQ(record.raw, line);
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
    CHECK_EQ(std::string(parseStatusName(ParseStatus::Unstructured)),
             std::string("unstructured"));
    CHECK_EQ(std::string(parseDiagnosticCodeName(ParseDiagnosticCode::InvalidUnicode)),
             std::string("invalid-unicode"));
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

void testAssemblerPreservesPollState() {
    RecordAssembler assembler;
    const std::vector<RecordDelta> first = assembler.consumeLines({
        "2026-08-26T04:15:22Z INFO first",
        "2026-08-26T04:15:23Z WARN second",
    });
    CHECK_EQ(first.size(), static_cast<std::size_t>(2));
    CHECK(first[0].kind == RecordDelta::Kind::Append);
    CHECK(first[1].kind == RecordDelta::Kind::Append);
    CHECK_EQ(first[0].record.line_number, static_cast<std::size_t>(1));
    CHECK_EQ(first[1].record.line_number, static_cast<std::size_t>(2));
    CHECK_EQ(first[0].physical_line_number, static_cast<std::size_t>(1));
    CHECK_EQ(first[1].physical_line_number, static_cast<std::size_t>(2));

    const std::vector<RecordDelta> second =
        assembler.consumeLines({"2026-08-26T04:15:24Z ERROR third"});
    CHECK_EQ(second.size(), static_cast<std::size_t>(1));
    CHECK_EQ(second[0].record.line_number, static_cast<std::size_t>(3));
    CHECK_EQ(second[0].record_index, static_cast<std::size_t>(2));
}

void testAssemblerExtendsPreviousRecordAcrossPolls() {
    RecordAssembler assembler;
    const std::vector<RecordDelta> root =
        assembler.consumeLines({"2026-08-26T04:15:22Z ERROR boom"});
    CHECK_EQ(root.size(), static_cast<std::size_t>(1));
    CHECK(root[0].kind == RecordDelta::Kind::Append);

    const std::vector<RecordDelta> continuation =
        assembler.consumeLines({"    at Service.call(Service.java:10)"});
    CHECK_EQ(continuation.size(), static_cast<std::size_t>(1));
    CHECK(continuation[0].kind == RecordDelta::Kind::Extend);
    CHECK_EQ(continuation[0].record_index, static_cast<std::size_t>(0));
    CHECK_EQ(continuation[0].physical_line_number, static_cast<std::size_t>(2));
    CHECK_EQ(continuation[0].record.line_number, static_cast<std::size_t>(1));
    CHECK_EQ(continuation[0].record.message,
             std::string("boom\n    at Service.call(Service.java:10)"));
    CHECK_EQ(continuation[0].record.raw,
             std::string("2026-08-26T04:15:22Z ERROR boom\n"
                          "    at Service.call(Service.java:10)"));
}

void testAssemblerDoesNotDropLeadingContinuation() {
    RecordAssembler assembler;
    const std::vector<RecordDelta> first = assembler.consumeLine("  detail before root");
    CHECK_EQ(first.size(), static_cast<std::size_t>(1));
    CHECK(first[0].kind == RecordDelta::Kind::Append);
    CHECK_EQ(first[0].record.line_number, static_cast<std::size_t>(1));
    CHECK_EQ(first[0].record.message, std::string("  detail before root"));

    const std::vector<RecordDelta> second =
        assembler.consumeLine("2026-08-26T04:15:22Z INFO root");
    CHECK_EQ(second.size(), static_cast<std::size_t>(1));
    CHECK_EQ(second[0].record.line_number, static_cast<std::size_t>(2));
}

void testAssemblerBuffersPartialBytesUntilNewline() {
    RecordAssembler assembler;
    CHECK(assembler.consumeBytes("2026-08-26T04:15:22Z INFO part").empty());
    CHECK_EQ(assembler.nextLineNumber(), static_cast<std::size_t>(1));

    const std::vector<RecordDelta> completed = assembler.consumeBytes(
        "ial\n2026-08-26T04:15:23Z INFO complete\n");
    CHECK_EQ(completed.size(), static_cast<std::size_t>(2));
    CHECK(completed[0].kind == RecordDelta::Kind::Append);
    CHECK(completed[1].kind == RecordDelta::Kind::Append);
    CHECK_EQ(completed[0].record.message, std::string("partial"));
    CHECK_EQ(completed[0].record.line_number, static_cast<std::size_t>(1));
    CHECK_EQ(completed[1].record.line_number, static_cast<std::size_t>(2));

    RecordAssembler mixed;
    CHECK(mixed.consumeBytes("2026-08-26T04:15:22Z INFO par").empty());
    const std::vector<RecordDelta> lineCompletion = mixed.consumeLine("tial");
    CHECK_EQ(lineCompletion.size(), static_cast<std::size_t>(1));
    CHECK_EQ(lineCompletion[0].record.message, std::string("partial"));

    RecordAssembler eof;
    CHECK(eof.consumeBytes("2026-08-26T04:15:22Z INFO final").empty());
    CHECK(eof.flush().size() == static_cast<std::size_t>(1));
    CHECK_EQ(eof.recordCount(), static_cast<std::size_t>(1));
}

void testAssemblerBoundsUnterminatedInputAcrossChunks() {
    constexpr std::size_t kLimit = 8;
    RecordAssembler assembler(Format::Auto, EncodingErrorPolicy::PreserveBytes, kLimit);

    CHECK(assembler.consumeBytes("1234").empty());
    CHECK_EQ(assembler.partialBytes(), static_cast<std::size_t>(4));
    CHECK(assembler.consumeBytes("56789").empty());
    CHECK_EQ(assembler.partialBytes(), kLimit);
    CHECK(assembler.consumeBytes("XYZ").empty());
    CHECK_EQ(assembler.partialBytes(), kLimit);

    const std::vector<RecordDelta> flushed = assembler.flush();
    CHECK_EQ(flushed.size(), static_cast<std::size_t>(1));
    CHECK_EQ(flushed[0].record.raw, std::string("12345678"));
    CHECK_EQ(flushed[0].record.input_bytes, static_cast<std::size_t>(12));
    CHECK_EQ(flushed[0].record.omitted_bytes, static_cast<std::size_t>(4));
    CHECK_EQ(flushed[0].record.input_bytes,
             flushed[0].record.raw.size() + flushed[0].record.omitted_bytes);
    CHECK_EQ(assembler.partialBytes(), static_cast<std::size_t>(0));
}

void testAssemblerBoundsOversizedNewlineTerminatedRoot() {
    constexpr std::size_t kLimit = 8;
    const std::string source = "0123456789ABC";
    RecordAssembler assembler(Format::Auto, EncodingErrorPolicy::PreserveBytes, kLimit);

    const std::vector<RecordDelta> deltas = assembler.consumeBytes(source + "\n");
    CHECK_EQ(deltas.size(), static_cast<std::size_t>(1));
    CHECK(deltas[0].kind == RecordDelta::Kind::Append);
    CHECK_EQ(deltas[0].record.raw, source.substr(0, kLimit));
    CHECK_EQ(deltas[0].record.input_bytes, source.size());
    CHECK_EQ(deltas[0].record.omitted_bytes, source.size() - kLimit);
    CHECK_EQ(assembler.partialBytes(), static_cast<std::size_t>(0));
}

void testAssemblerBoundsRepeatedOversizedContinuations() {
    constexpr std::size_t kLimit = 16;
    const std::string firstContinuation = "    " + std::string(40, 'a');
    const std::string secondContinuation = "\t" + std::string(40, 'b');
    RecordAssembler assembler(Format::Auto, EncodingErrorPolicy::PreserveBytes, kLimit);

    const std::vector<RecordDelta> deltas = assembler.consumeBytes(
        "root\n" + firstContinuation + "\n" + secondContinuation + "\n");
    CHECK_EQ(deltas.size(), static_cast<std::size_t>(3));
    CHECK(deltas[0].kind == RecordDelta::Kind::Append);
    CHECK(deltas[1].kind == RecordDelta::Kind::Extend);
    CHECK(deltas[2].kind == RecordDelta::Kind::Extend);
    CHECK_EQ(deltas[1].record_index, static_cast<std::size_t>(0));
    CHECK_EQ(deltas[2].record_index, static_cast<std::size_t>(0));

    const std::size_t afterFirstInput = 4 + 1 + firstContinuation.size();
    const std::size_t afterSecondInput = afterFirstInput + 1 + secondContinuation.size();
    CHECK_EQ(deltas[1].record.raw.size(), kLimit);
    CHECK_EQ(deltas[1].record.input_bytes, afterFirstInput);
    CHECK_EQ(deltas[1].record.omitted_bytes, afterFirstInput - kLimit);
    CHECK_EQ(deltas[2].record.raw.size(), kLimit);
    CHECK_EQ(deltas[2].record.input_bytes, afterSecondInput);
    CHECK_EQ(deltas[2].record.omitted_bytes, afterSecondInput - kLimit);
    CHECK(deltas[2].record.omitted_bytes > deltas[1].record.omitted_bytes);
    CHECK_EQ(assembler.partialBytes(), static_cast<std::size_t>(0));
}

void testAssemblerResetClearsPartialOmission() {
    constexpr std::size_t kLimit = 8;
    RecordAssembler assembler(Format::Auto, EncodingErrorPolicy::PreserveBytes, kLimit);

    CHECK(assembler.consumeBytes("0123456789").empty());
    CHECK_EQ(assembler.partialBytes(), kLimit);
    assembler.reset(42);
    CHECK_EQ(assembler.partialBytes(), static_cast<std::size_t>(0));
    CHECK_EQ(assembler.nextLineNumber(), static_cast<std::size_t>(1));
    CHECK_EQ(assembler.recordCount(), static_cast<std::size_t>(0));
    CHECK(assembler.flush().empty());

    const std::vector<RecordDelta> fresh = assembler.consumeBytes("fresh\n");
    CHECK_EQ(fresh.size(), static_cast<std::size_t>(1));
    CHECK_EQ(fresh[0].generation, static_cast<std::uint64_t>(42));
    CHECK_EQ(fresh[0].record.raw, std::string("fresh"));
    CHECK_EQ(fresh[0].record.input_bytes, static_cast<std::size_t>(5));
    CHECK_EQ(fresh[0].record.omitted_bytes, static_cast<std::size_t>(0));
}

void testAssemblerRejectsInvalidRecordByteLimits() {
    const auto throwsInvalidArgument = [](std::size_t limit) {
        try {
            RecordAssembler assembler(Format::Auto, EncodingErrorPolicy::PreserveBytes, limit);
            (void)assembler;
        } catch (const std::invalid_argument&) {
            return true;
        } catch (...) {
            return false;
        }
        return false;
    };

    CHECK(throwsInvalidArgument(0));
    CHECK(throwsInvalidArgument(loglens::kMaxRecordBytes + 1));
}

void testAssemblerResetDropsOldGenerationState() {
    RecordAssembler assembler;
    CHECK(assembler.consumeBytes("2026-08-26T04:15:22Z INFO stale").empty());
    assembler.reset(7);
    CHECK_EQ(assembler.generation(), static_cast<std::uint64_t>(7));
    CHECK_EQ(assembler.nextLineNumber(), static_cast<std::size_t>(1));
    CHECK_EQ(assembler.recordCount(), static_cast<std::size_t>(0));

    const std::vector<RecordDelta> fresh = assembler.consumeBytes(
        "2026-08-26T04:15:22Z INFO fresh\n  fresh continuation\n");
    CHECK_EQ(fresh.size(), static_cast<std::size_t>(2));
    CHECK(fresh[0].kind == RecordDelta::Kind::Append);
    CHECK(fresh[1].kind == RecordDelta::Kind::Extend);
    CHECK_EQ(fresh[0].generation, static_cast<std::uint64_t>(7));
    CHECK_EQ(fresh[1].generation, static_cast<std::uint64_t>(7));
    CHECK_EQ(fresh[0].record.line_number, static_cast<std::size_t>(1));
    CHECK_EQ(fresh[1].physical_line_number, static_cast<std::size_t>(2));
}

void testAssemblerMakesTheBytePreservingErrorPolicyExplicit() {
    RecordAssembler assembler(Format::Auto, EncodingErrorPolicy::PreserveBytes);
    CHECK(assembler.encodingErrorPolicy() == EncodingErrorPolicy::PreserveBytes);

    std::string invalid = "2026-08-26T04:15:22Z INFO raw ";
    invalid.push_back(static_cast<char>(0xff));
    invalid.push_back('\n');
    const std::vector<RecordDelta> deltas = assembler.consumeBytes(invalid);
    CHECK_EQ(deltas.size(), static_cast<std::size_t>(1));
    CHECK_EQ(deltas[0].record.raw, invalid.substr(0, invalid.size() - 1));
}

void testAssemblerResetPreservesTailWindowLineNumbers() {
    RecordAssembler assembler;
    assembler.reset(23, 401);

    const std::vector<RecordDelta> deltas = assembler.consumeBytes(
        "2026-08-26T04:15:22.123Z ERROR [api] retained\n  detail\n");
    CHECK_EQ(deltas.size(), static_cast<std::size_t>(2));
    CHECK_EQ(deltas[0].generation, static_cast<std::uint64_t>(23));
    CHECK_EQ(deltas[0].record.line_number, static_cast<std::size_t>(401));
    CHECK_EQ(deltas[1].physical_line_number, static_cast<std::size_t>(402));
    CHECK_EQ(deltas[1].record.line_number, static_cast<std::size_t>(401));
    CHECK_EQ(assembler.nextLineNumber(), static_cast<std::size_t>(403));
}

void testAssemblerResetRejectsZeroLineNumber() {
    RecordAssembler assembler;
    bool threw = false;
    try {
        assembler.reset(0, 0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

} // namespace

int main() {
    testDetection();
    testPlainIso();
    testSyslog();
    testSyslogValidation();
    testJsonLine();
    testJsonValidationAndUnicode();
    testTimestampOffsetsAreValidated();
    testMalformedJsonCorpusNeverThrows();
    testJsonStringBoundIsReported();
    testJsonDiagnosticsRemainBounded();
    testJsonFieldDiagnosticsRemainBoundedAfterReaderDiagnostics();
    testUnparseableKeepsData();
    testContinuation();
    testAssemblerPreservesPollState();
    testAssemblerExtendsPreviousRecordAcrossPolls();
    testAssemblerDoesNotDropLeadingContinuation();
    testAssemblerBuffersPartialBytesUntilNewline();
    testAssemblerBoundsUnterminatedInputAcrossChunks();
    testAssemblerBoundsOversizedNewlineTerminatedRoot();
    testAssemblerBoundsRepeatedOversizedContinuations();
    testAssemblerResetClearsPartialOmission();
    testAssemblerRejectsInvalidRecordByteLimits();
    testAssemblerResetDropsOldGenerationState();
    testAssemblerMakesTheBytePreservingErrorPolicyExplicit();
    testAssemblerResetPreservesTailWindowLineNumbers();
    testAssemblerResetRejectsZeroLineNumber();
    return checkSummary();
}
