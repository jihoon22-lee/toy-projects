#include "format.hpp"

#include <cstdio>

namespace diskmap {

namespace {

constexpr const char* kUnits[] = {"B", "KB", "MB", "GB", "TB", "PB"};
constexpr std::size_t kMaxUnitIndex = 5; // index of "PB"
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
