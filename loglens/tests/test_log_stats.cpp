#include "check.hpp"
#include "fake_source.hpp"

#include "loglens/log_stats.hpp"

#include <string>

using loglens::Bucket;
using loglens::Level;
using loglens::normalizeMessage;
using loglens::Stats;

namespace {

void testNormalize() {
    CHECK_EQ(normalizeMessage("user 123 failed"), std::string("user <N> failed"));
    CHECK_EQ(normalizeMessage("user 456 failed"), std::string("user <N> failed"));
    CHECK_EQ(normalizeMessage("took 2100ms"), std::string("took <N>ms"));
    CHECK_EQ(normalizeMessage("no digits here"), std::string("no digits here"));
    CHECK_EQ(normalizeMessage(""), std::string(""));
    // Long hex runs become <HEX>; short ones stay put so ordinary words survive.
    CHECK_EQ(normalizeMessage("id deadbeef99 gone"), std::string("id <HEX> gone"));
    CHECK_EQ(normalizeMessage("cafe shop"), std::string("cafe shop"));
    CHECK_EQ(normalizeMessage("said \"hello world\" once"), std::string("said <STR> once"));
    // An unterminated quote must not swallow the rest or loop forever.
    CHECK(!normalizeMessage("bad \"quote").empty());
}

void testGrouping() {
    Stats stats;
    stats.add(makeRecord(Level::Info, "a", "user 1 failed"));
    stats.add(makeRecord(Level::Info, "a", "user 22 failed"));
    stats.add(makeRecord(Level::Warn, "a", "disk full"));
    CHECK_EQ(stats.total(), static_cast<std::size_t>(3));

    const auto top = stats.topPatterns(10);
    CHECK_EQ(top.size(), static_cast<std::size_t>(2));
    if (!top.empty()) {
        CHECK_EQ(top[0].first, std::string("user <N> failed"));
        CHECK_EQ(top[0].second, static_cast<std::size_t>(2));
    }

    // n caps the result.
    CHECK_EQ(stats.topPatterns(1).size(), static_cast<std::size_t>(1));
    CHECK(stats.topPatterns(0).empty());
    CHECK(Stats().topPatterns(5).empty());
}

void testBuckets() {
    Stats stats;
    stats.add(makeRecord(Level::Info, "a", "x", 1000));
    stats.add(makeRecord(Level::Warn, "a", "y", 1500));
    stats.add(makeRecord(Level::Error, "a", "z", 2500));
    // No timestamp: excluded, since it has no bucket to land in.
    stats.add(makeRecord(Level::Info, "a", "w", 0));

    const auto buckets = stats.buckets(1000);
    CHECK_EQ(buckets.size(), static_cast<std::size_t>(2));
    if (buckets.size() == 2) {
        CHECK_EQ(buckets[0].start_ms, static_cast<std::uint64_t>(1000));
        CHECK_EQ(buckets[0].level_counts[loglens::levelIndex(Level::Info)],
                 static_cast<std::size_t>(1));
        CHECK_EQ(buckets[0].level_counts[loglens::levelIndex(Level::Warn)],
                 static_cast<std::size_t>(1));
        CHECK_EQ(buckets[1].start_ms, static_cast<std::uint64_t>(2000));
        CHECK_EQ(buckets[1].level_counts[loglens::levelIndex(Level::Error)],
                 static_cast<std::size_t>(1));
    }
    // Boundary: a record exactly on a boundary belongs to the later bucket.
    Stats edge;
    edge.add(makeRecord(Level::Info, "a", "x", 2000));
    const auto edgeBuckets = edge.buckets(1000);
    CHECK_EQ(edgeBuckets.size(), static_cast<std::size_t>(1));
    if (!edgeBuckets.empty()) {
        CHECK_EQ(edgeBuckets[0].start_ms, static_cast<std::uint64_t>(2000));
    }
    // Zero bucket size is rejected rather than dividing by zero.
    CHECK(stats.buckets(0).empty());
    CHECK(Stats().buckets(1000).empty());
}

} // namespace

int main() {
    testNormalize();
    testGrouping();
    testBuckets();
    return checkSummary();
}
