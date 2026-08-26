#include "check.hpp"

#include "loglens/log_record.hpp"

using loglens::Level;
using loglens::levelAtLeast;
using loglens::levelIndex;
using loglens::levelName;
using loglens::parseLevel;

namespace {

void testAliases() {
    CHECK(parseLevel("TRACE") == Level::Trace);
    CHECK(parseLevel("verbose") == Level::Trace);
    CHECK(parseLevel("Debug") == Level::Debug);
    CHECK(parseLevel("dbg") == Level::Debug);
    CHECK(parseLevel("info") == Level::Info);
    CHECK(parseLevel("NOTICE") == Level::Info);
    CHECK(parseLevel("warn") == Level::Warn);
    CHECK(parseLevel("Warning") == Level::Warn);
    CHECK(parseLevel("err") == Level::Error);
    CHECK(parseLevel("ERROR") == Level::Error);
    CHECK(parseLevel("crit") == Level::Fatal);
    CHECK(parseLevel("CRITICAL") == Level::Fatal);
    CHECK(parseLevel("fatal") == Level::Fatal);
    CHECK(parseLevel("panic") == Level::Fatal);
    CHECK(parseLevel("") == Level::Unknown);
    CHECK(parseLevel("nonsense") == Level::Unknown);
}

void testNames() {
    CHECK_EQ(std::string(levelName(Level::Trace)), std::string("TRACE"));
    CHECK_EQ(std::string(levelName(Level::Warn)), std::string("WARN"));
    CHECK_EQ(std::string(levelName(Level::Fatal)), std::string("FATAL"));
    CHECK_EQ(std::string(levelName(Level::Unknown)), std::string("UNKNOWN"));
}

void testOrdering() {
    CHECK(levelAtLeast(Level::Error, Level::Warn));
    CHECK(levelAtLeast(Level::Warn, Level::Warn));
    CHECK(!levelAtLeast(Level::Info, Level::Warn));
    CHECK(levelAtLeast(Level::Fatal, Level::Trace));
    CHECK(!levelAtLeast(Level::Trace, Level::Debug));
    // Unknown never satisfies a threshold, so an unparsed line is not swept
    // into a "level >= INFO" filter by accident.
    CHECK(!levelAtLeast(Level::Unknown, Level::Trace));
}

void testIndexing() {
    CHECK_EQ(levelIndex(Level::Trace), static_cast<std::size_t>(0));
    CHECK_EQ(levelIndex(Level::Fatal), static_cast<std::size_t>(5));
    CHECK_EQ(levelIndex(Level::Unknown), loglens::kLevelCount - 1);
    CHECK(levelIndex(Level::Warn) < loglens::kLevelCount);
}

} // namespace

int main() {
    testAliases();
    testNames();
    testOrdering();
    testIndexing();
    return checkSummary();
}
