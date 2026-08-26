#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "loglens/log_record.hpp"

namespace loglens {

struct Bucket {
    std::uint64_t start_ms = 0;
    std::array<std::size_t, kLevelCount> level_counts{};
};

// Replaces the variable parts of a message so that near-identical lines group
// together: digit runs become <N>, hex-looking runs <HEX>, quoted text <STR>.
std::string normalizeMessage(const std::string& message);

class Stats {
public:
    void add(const LogRecord& record);
    std::size_t total() const;

    // Records without a timestamp are excluded; they have no bucket to land in.
    std::vector<Bucket> buckets(std::uint64_t bucket_ms) const;

    // The n most frequent normalized messages, descending by count then text.
    std::vector<std::pair<std::string, std::size_t>> topPatterns(std::size_t n) const;

private:
    std::vector<LogRecord> records_;
};

} // namespace loglens
