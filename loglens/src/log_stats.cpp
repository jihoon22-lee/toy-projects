#include "loglens/log_stats.hpp"

#include <algorithm>
#include <cctype>
#include <map>

namespace loglens {

namespace {

bool isHexDigit(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

// Consumes a run of characters satisfying pred starting at i; returns its length.
std::size_t runLength(const std::string& text, std::size_t i, bool (*pred)(char)) {
    std::size_t length = 0;
    while (i + length < text.size() && pred(text[i + length])) {
        ++length;
    }
    return length;
}

bool isDigitChar(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }

// A hex run only counts as one when it is long enough to be an id rather than
// an ordinary word; below that it stays as written.
constexpr std::size_t kMinHexRun = 6;

std::size_t appendQuoted(const std::string& text, std::size_t i, std::string& out) {
    const std::size_t close = text.find('"', i + 1);
    if (close == std::string::npos) {
        return 0;
    }
    out += "<STR>";
    return close - i + 1;
}

std::size_t appendRun(const std::string& text, std::size_t i, std::string& out) {
    const std::size_t hexRun = runLength(text, i, isHexDigit);
    if (hexRun >= kMinHexRun) {
        out += "<HEX>";
        return hexRun;
    }
    const std::size_t digitRun = runLength(text, i, isDigitChar);
    if (digitRun > 0) {
        out += "<N>";
        return digitRun;
    }
    return 0;
}

bool countPrecedes(const std::pair<std::string, std::size_t>& a,
                   const std::pair<std::string, std::size_t>& b) {
    if (a.second != b.second) {
        return a.second > b.second;
    }
    return a.first < b.first;
}

} // namespace

std::string normalizeMessage(const std::string& message) {
    std::string out;
    out.reserve(message.size());
    std::size_t i = 0;
    while (i < message.size()) {
        if (message[i] == '"') {
            const std::size_t consumed = appendQuoted(message, i, out);
            i += consumed > 0 ? consumed : 1;
            if (consumed > 0) {
                continue;
            }
            out += '"';
            continue;
        }
        const std::size_t consumed = appendRun(message, i, out);
        if (consumed > 0) {
            i += consumed;
            continue;
        }
        out += message[i];
        ++i;
    }
    return out;
}

void Stats::add(const LogRecord& record) { records_.push_back(record); }

std::size_t Stats::total() const { return records_.size(); }

std::vector<Bucket> Stats::buckets(std::uint64_t bucket_ms) const {
    std::vector<Bucket> result;
    if (bucket_ms == 0) {
        return result;
    }
    std::map<std::uint64_t, Bucket> byStart;
    for (const LogRecord& record : records_) {
        if (record.timestamp_ms == 0) {
            continue;
        }
        const std::uint64_t start = record.timestamp_ms / bucket_ms * bucket_ms;
        Bucket& bucket = byStart[start];
        bucket.start_ms = start;
        bucket.level_counts[levelIndex(record.level)] += 1;
    }
    result.reserve(byStart.size());
    for (const auto& entry : byStart) {
        result.push_back(entry.second);
    }
    return result;
}

std::vector<std::pair<std::string, std::size_t>> Stats::topPatterns(std::size_t n) const {
    std::map<std::string, std::size_t> counts;
    for (const LogRecord& record : records_) {
        counts[normalizeMessage(record.message)] += 1;
    }
    std::vector<std::pair<std::string, std::size_t>> ranked(counts.begin(), counts.end());
    std::sort(ranked.begin(), ranked.end(), countPrecedes);
    if (ranked.size() > n) {
        ranked.resize(n);
    }
    return ranked;
}

} // namespace loglens
