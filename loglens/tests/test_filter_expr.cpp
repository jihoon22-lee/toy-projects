#include "check.hpp"
#include "fake_source.hpp"

#include "loglens/filter_expr.hpp"

#include <string>

using loglens::Filter;
using loglens::Level;
using loglens::LogRecord;
using loglens::ParseError;

namespace {

const LogRecord& sample() {
    static const LogRecord record = makeRecord(Level::Warn, "api", "slow query took 200ms");
    return record;
}

// Parses an expression expected to be valid and reports whether it matches rec.
bool matches(const std::string& text, const LogRecord& record) {
    ParseError error;
    const auto filter = Filter::parse(text, error);
    if (!filter) {
        std::fprintf(stderr, "unexpected reject %s: %s\n", text.c_str(), error.message.c_str());
        ++g_checkFailures;
        return false;
    }
    return filter->matches(record);
}

void expectRejected(const std::string& text) {
    ParseError error;
    CHECK(!Filter::parse(text, error).has_value());
    CHECK(!error.message.empty());
    CHECK(error.position <= error.end);
    CHECK(error.end <= text.size());
}

void expectRejectedAt(const std::string& text, std::size_t begin, std::size_t end,
                     const std::string& message) {
    ParseError error;
    CHECK(!Filter::parse(text, error).has_value());
    CHECK_EQ(error.position, begin);
    CHECK_EQ(error.end, end);
    CHECK_EQ(error.message, message);
}

void testLevelPredicates() {
    CHECK(matches("level>=WARN", sample()));
    CHECK(matches("level>=INFO", sample()));
    CHECK(!matches("level>=ERROR", sample()));
    CHECK(matches("level==WARN", sample()));
    CHECK(!matches("level==INFO", sample()));
    CHECK(matches("level >= warn", sample()));
}

void testSourceAndMessage() {
    CHECK(matches("source==api", sample()));
    CHECK(!matches("source==db", sample()));
    CHECK(matches("source~AP", sample()));
    CHECK(matches("message~query", sample()));
    CHECK(matches("message~QUERY", sample()));
    CHECK(!matches("message~missing", sample()));
    CHECK(matches("message!~missing", sample()));
    CHECK(!matches("message!~query", sample()));
    CHECK(matches("message~\"slow query\"", sample()));

    const LogRecord metacharacters =
        makeRecord(Level::Info, "api", "literal a.*? [x] (y) $^");
    CHECK(matches("message~\"a.*? [x]\"", metacharacters));
    CHECK(!matches("message~\"a.+? [x]\"", metacharacters));
}

void testEscapedAndUtf8Literals() {
    const LogRecord escaped = makeRecord(Level::Info, "api", "quote: \" slash: \\");
    CHECK(matches("message~\"quote: \\\" slash: \\\\\"", escaped));

    const std::string utf8 = u8"장애 🚨 구간";
    const LogRecord unicode = makeRecord(Level::Info, "api", utf8);
    CHECK(matches("message~\"" + utf8 + "\"", unicode));

    const std::string invalidEscape = u8"message~\"한\\q\"";
    const std::size_t slash = invalidEscape.find('\\');
    expectRejectedAt(invalidEscape, slash, slash + 2,
                     "unsupported escape sequence (only \\\" and \\\\ are allowed)");

    const std::string unicodeEscape = u8"message~\"bad\\é\"";
    const std::size_t unicodeSlash = unicodeEscape.find('\\');
    expectRejectedAt(unicodeEscape, unicodeSlash,
                     unicodeSlash + 1 + std::string(u8"é").size(),
                     "unsupported escape sequence (only \\\" and \\\\ are allowed)");
}

void testCombinators() {
    CHECK(matches("level>=WARN AND message~query", sample()));
    CHECK(!matches("level>=ERROR AND message~query", sample()));
    CHECK(matches("level>=ERROR OR message~query", sample()));
    CHECK(!matches("level>=ERROR OR message~missing", sample()));
    CHECK(matches("NOT message~missing", sample()));
    CHECK(!matches("NOT message~query", sample()));
    CHECK(matches("(level>=ERROR OR source==api) AND message!~healthy", sample()));
    CHECK(matches("NOT (level>=ERROR AND message~query)", sample()));
    CHECK(matches("level>=TRACE AND level>=DEBUG AND level>=INFO", sample()));
    CHECK(matches("level>=FATAL OR level>=ERROR OR source==api", sample()));
    CHECK(matches("not message~missing", sample()));
}

void testShortCircuit() {
    // An unparsed record must not satisfy a level threshold.
    const LogRecord unknown = makeRecord(Level::Unknown, "", "raw text");
    CHECK(!matches("level>=TRACE", unknown));
    CHECK(matches("message~raw", unknown));
    CHECK(matches("source==\"\" OR message~raw", unknown));
}

void testRejections() {
    expectRejected("");
    expectRejected("level");
    expectRejected("level>=");
    expectRejected("level>=BOGUS");
    expectRejected("level~WARN");
    expectRejected("source>=api");
    expectRejected("message==x");
    expectRejected("unknownfield~x");
    expectRejected("(level>=WARN");
    expectRejected("level>=WARN AND");
    expectRejected("level>=WARN extra");
    expectRejected("message~\"unterminated");
}

void testDepthCap() {
    std::string exact;
    for (int i = 0; i < loglens::kMaxFilterDepth; ++i) {
        exact += "NOT ";
    }
    exact += "message~x";
    ParseError exactError;
    CHECK(Filter::parse(exact, exactError).has_value());

    const std::string deep = "NOT " + exact;
    expectRejectedAt(deep, static_cast<std::size_t>(loglens::kMaxFilterDepth) * 4,
                     static_cast<std::size_t>(loglens::kMaxFilterDepth) * 4 + 3,
                     "expression nested too deeply");

    const std::string exactParentheses(
        static_cast<std::size_t>(loglens::kMaxFilterDepth), '(');
    const std::string exactNested = exactParentheses + "message~x"
                                    + std::string(
                                        static_cast<std::size_t>(loglens::kMaxFilterDepth), ')');
    ParseError exactNestedError;
    CHECK(Filter::parse(exactNested, exactNestedError).has_value());

    const std::string deepParentheses = "(" + exactNested + ")";
    expectRejectedAt(deepParentheses,
                     static_cast<std::size_t>(loglens::kMaxFilterDepth),
                     static_cast<std::size_t>(loglens::kMaxFilterDepth) + 1,
                     "expression nested too deeply");
}

void testDiagnosticRanges() {
    expectRejectedAt("", 0, 0, "empty filter expression");
    expectRejectedAt("   ", 3, 3, "empty filter expression");
    expectRejectedAt("unknown~x", 0, 7, "unknown field 'unknown'");
    expectRejectedAt("level>=WARN extra", 12, 17, "unexpected trailing input");
    expectRejectedAt("source>=api", 6, 8, "source supports '==' or '~'");

    const std::string unicodeTrailing = u8"message~x 余計";
    const std::size_t trailing = unicodeTrailing.find(u8"余");
    expectRejectedAt(unicodeTrailing, trailing, unicodeTrailing.size(),
                     "unexpected trailing input");
}

void testQueryAndLiteralBounds() {
    const std::string atLimit = "message~x"
                                + std::string(loglens::kMaxFilterQueryBytes - 9, ' ');
    ParseError atLimitError;
    CHECK(Filter::parse(atLimit, atLimitError).has_value());

    const std::string oversized(loglens::kMaxFilterQueryBytes + 1, 'x');
    expectRejectedAt(oversized, loglens::kMaxFilterQueryBytes, oversized.size(),
                     "filter query exceeds 4096-byte limit");

    const std::string exactLiteral(loglens::kMaxFilterLiteralBytes, 'x');
    CHECK(matches("message~\"" + exactLiteral + "\"",
                  makeRecord(Level::Info, "api", exactLiteral)));
    const std::string oversizedLiteral = exactLiteral + "x";
    const std::string oversizedLiteralQuery = "message~\"" + oversizedLiteral + "\"";
    expectRejectedAt(oversizedLiteralQuery, 8, 8 + loglens::kMaxFilterLiteralBytes + 2,
                     "filter literal exceeds 1024-byte limit");
}

std::string repeatedOrPredicates(std::size_t count) {
    std::string expression;
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0) {
            expression += " OR ";
        }
        expression += "message~x";
    }
    return expression;
}

void testAstNodeBound() {
    // One OR node plus one leaf per predicate reaches the cap exactly.
    ParseError acceptedError;
    CHECK(Filter::parse(repeatedOrPredicates(loglens::kMaxFilterNodes - 1), acceptedError).has_value());

    const std::string oversized = repeatedOrPredicates(loglens::kMaxFilterNodes);
    ParseError error;
    CHECK(!Filter::parse(oversized, error).has_value());
    CHECK_EQ(error.message, "filter AST exceeds 256-node limit");
    CHECK(error.position < error.end);
    CHECK(error.end <= oversized.size());
}

} // namespace

int main() {
    testLevelPredicates();
    testSourceAndMessage();
    testEscapedAndUtf8Literals();
    testCombinators();
    testShortCircuit();
    testRejections();
    testDepthCap();
    testDiagnosticRanges();
    testQueryAndLiteralBounds();
    testAstNodeBound();
    return checkSummary();
}
