#include "diskmap/format.hpp"

#include <cstdio>

namespace diskmap {

namespace {

constexpr const char* kUnits[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
constexpr std::size_t kMaxUnitIndex = 5; // index of "PiB"
constexpr double kUnitBase = 1024.0;

std::string formatWithUnit(double value, const char* unit) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, unit);
    return std::string(buffer);
}

} // namespace

std::string humanBytes(std::uint64_t bytes) {
    if (bytes < static_cast<std::uint64_t>(kUnitBase)) {
        return std::to_string(bytes) + " B";
    }

    double value = static_cast<double>(bytes);
    std::size_t unitIndex = 0;
    while (value >= kUnitBase && unitIndex < kMaxUnitIndex) {
        value /= kUnitBase;
        ++unitIndex;
    }
    return formatWithUnit(value, kUnits[unitIndex]);
}

std::string formatPercent(double ratio) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f%%", ratio * 100.0);
    return std::string(buffer);
}

std::string truncateMiddle(const std::string& text, std::size_t maxLen) {
    if (text.size() <= maxLen) {
        return text;
    }

    static const std::string kEllipsis = "...";
    if (maxLen <= kEllipsis.size()) {
        return text.substr(0, maxLen);
    }

    const std::size_t keep = maxLen - kEllipsis.size();
    const std::size_t prefixLen = (keep + 1) / 2;
    const std::size_t suffixLen = keep - prefixLen;
    return text.substr(0, prefixLen) + kEllipsis + text.substr(text.size() - suffixLen);
}

} // namespace diskmap

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <numeric>
#include <regex>

namespace diskmap {
std::optional<std::uint64_t> parseHumanBytes(const std::string& value) {
    if (value.size() > 128) return std::nullopt;
    static const std::regex syntax(R"(^\s*([0-9]+)(?:\.([0-9]{1,9}))?\s*([a-zA-Z]*)\s*$)");
    std::smatch match;
    if (!std::regex_match(value, match, syntax)) return std::nullopt;
    std::string wholeText = match[1];
    std::uint64_t whole = 0;
    if (std::from_chars(wholeText.data(), wholeText.data() + wholeText.size(), whole).ec != std::errc()) return std::nullopt;
    std::string unit = match[3];
    std::transform(unit.begin(), unit.end(), unit.begin(), [](unsigned char c) { return std::tolower(c); });
    std::uint64_t multiplier = 1;
    if (!unit.empty() && unit != "b") {
        const std::string prefix = "kmgtp";
        const auto position = prefix.find(unit.front());
        if (position == std::string::npos || (unit.size() != 2 && unit.size() != 3) || unit.back() != 'b' || (unit.size() == 3 && unit[1] != 'i')) return std::nullopt;
        for (std::size_t i = 0; i <= position; ++i) multiplier *= unit.size() == 3 ? 1024 : 1000;
    }
    if (whole > std::numeric_limits<std::uint64_t>::max() / multiplier) return std::nullopt;
    std::uint64_t fraction = 0, scale = 1;
    for (char c : match[2].str()) { fraction = fraction * 10 + (c - '0'); scale *= 10; }
    const auto common = std::gcd(scale, multiplier);
    const auto denominator = scale / common;
    if (fraction % denominator != 0) return std::nullopt;
    const auto extra = (fraction / denominator) * (multiplier / common);
    const auto total = whole * multiplier;
    if (extra > std::numeric_limits<std::uint64_t>::max() - total) return std::nullopt;
    return total + extra;
}
}
