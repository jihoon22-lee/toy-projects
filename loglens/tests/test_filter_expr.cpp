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
    std::string deep;
    for (int i = 0; i < loglens::kMaxFilterDepth + 5; ++i) {
        deep += "NOT ";
    }
    deep += "message~x";
    ParseError error;
    CHECK(!Filter::parse(deep, error).has_value());
}

} // namespace

int main() {
    testLevelPredicates();
    testSourceAndMessage();
    testCombinators();
    testShortCircuit();
    testRejections();
    testDepthCap();
    return checkSummary();
}
