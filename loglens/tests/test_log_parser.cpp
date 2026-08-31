#include "check.hpp"

#include "loglens/log_parser.hpp"

#include <stdexcept>

using loglens::detectFormat;
using loglens::EncodingErrorPolicy;
using loglens::Format;
using loglens::isContinuation;
using loglens::Level;
using loglens::LogRecord;
using loglens::parseLine;
using loglens::RecordAssembler;
using loglens::RecordDelta;

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

} // namespace

int main() {
    testDetection();
    testPlainIso();
    testSyslog();
    testJsonLine();
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
    return checkSummary();
}
