#include "check.hpp"
#include "fake_source.hpp"

#include "loglens/window_analysis.hpp"

#include <string>
#include <vector>

namespace {

loglens::LogRecord record(std::uint64_t timestamp, loglens::Level level,
                          std::string source, std::string message,
                          std::string raw, std::size_t line) {
    loglens::LogRecord value = makeRecord(level, std::move(source), std::move(message), timestamp);
    value.raw = std::move(raw);
    value.line_number = line;
    return value;
}

void testDeterministicWindowSignals() {
    std::vector<loglens::LogRecord> records;
    records.push_back(record(1000, loglens::Level::Info, "api", "request 1 ok",
                             "request_id=base", 1));
    records.push_back(record(61000, loglens::Level::Error, "worker", "timeout job 10",
                             "request_id=req-7 thread=worker-2", 10));
    records.push_back(record(62000, loglens::Level::Error, "worker", "timeout job 11",
                             "request_id=req-7 thread=worker-2", 11));
    records.push_back(record(63000, loglens::Level::Error, "worker", "timeout job 12",
                             "correlation_id=c-9", 12));
    records.push_back(record(0, loglens::Level::Fatal, "ignored", "no timestamp", "", 99));

    const auto analysis = loglens::compareWindows(
        records, {0, 60000}, {60000, 120000});
    CHECK_EQ(analysis.baseline_records, static_cast<std::size_t>(1));
    CHECK_EQ(analysis.comparison_records, static_cast<std::size_t>(3));
    CHECK(!analysis.signals.empty());
    bool pattern = false;
    bool level = false;
    for (const auto& signal : analysis.signals) {
        CHECK(!signal.explanation.empty());
        CHECK(signal.score > 0.0);
        CHECK_EQ(signal.first_line, static_cast<std::size_t>(10));
        CHECK_EQ(signal.last_line, static_cast<std::size_t>(12));
        pattern = pattern || (signal.dimension == "pattern"
                              && signal.key == "timeout job <N>"
                              && signal.kind == loglens::SignalKind::NewPattern);
        level = level || (signal.dimension == "level" && signal.key == "ERROR");
    }
    CHECK(pattern);
    CHECK(level);
    CHECK_EQ(analysis.correlations.size(), static_cast<std::size_t>(3));
    CHECK_EQ(analysis.correlations.front().field, std::string("request_id"));
    CHECK_EQ(analysis.correlations.front().value, std::string("req-7"));
    CHECK_EQ(analysis.correlations.front().count, static_cast<std::size_t>(2));
}

void testRateSpikeAndInvalidWindows() {
    std::vector<loglens::LogRecord> records;
    records.push_back(record(1000, loglens::Level::Warn, "api", "slow", "", 1));
    for (std::size_t index = 0; index < 4; ++index) {
        records.push_back(record(61000 + index, loglens::Level::Warn, "api", "slow", "",
                                 2 + index));
    }
    const auto analysis = loglens::compareWindows(records, {0, 60000}, {60000, 120000});
    bool spike = false;
    for (const auto& signal : analysis.signals) {
        if (signal.dimension == "pattern" && signal.kind == loglens::SignalKind::RateSpike) {
            spike = true;
            CHECK_EQ(signal.baseline_count, static_cast<std::size_t>(1));
            CHECK_EQ(signal.comparison_count, static_cast<std::size_t>(4));
            CHECK(signal.explanation.find("measured rate") != std::string::npos);
        }
    }
    CHECK(spike);
    CHECK(loglens::compareWindows(records, {10, 10}, {20, 30}).signals.empty());
}

} // namespace

int main() {
    testDeterministicWindowSignals();
    testRateSpikeAndInvalidWindows();
    return checkSummary();
}
