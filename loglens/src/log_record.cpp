#include "loglens/log_record.hpp"

#include <algorithm>
#include <cctype>

namespace loglens {

namespace {

struct LevelAlias {
    const char* text;
    Level level;
};

// Table-driven so each spelling stays one line; a switch with a case per
// alias would read as a copy-paste block.
const LevelAlias kAliases[] = {
    {"TRACE", Level::Trace},   {"VERBOSE", Level::Trace}, {"DEBUG", Level::Debug},
    {"DBG", Level::Debug},     {"INFO", Level::Info},     {"NOTICE", Level::Info},
    {"WARN", Level::Warn},     {"WARNING", Level::Warn},  {"ERR", Level::Error},
    {"ERROR", Level::Error},   {"CRIT", Level::Fatal},    {"CRITICAL", Level::Fatal},
    {"FATAL", Level::Fatal},   {"PANIC", Level::Fatal},
};

const LevelAlias kCanonical[] = {
    {"TRACE", Level::Trace}, {"DEBUG", Level::Debug}, {"INFO", Level::Info},
    {"WARN", Level::Warn},   {"ERROR", Level::Error}, {"FATAL", Level::Fatal},
    {"UNKNOWN", Level::Unknown},
};

std::string toUpper(const std::string& text) {
    std::string upper;
    upper.reserve(text.size());
    for (char c : text) {
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return upper;
}

// Ordering used by levelAtLeast. Unknown sorts below everything so a
// "level >= INFO" filter never matches an unparsed line by accident.
int rank(Level level) {
    switch (level) {
        case Level::Unknown: return -1;
        case Level::Trace: return 0;
        case Level::Debug: return 1;
        case Level::Info: return 2;
        case Level::Warn: return 3;
        case Level::Error: return 4;
        case Level::Fatal: return 5;
    }
    return -1;
}

} // namespace

Level parseLevel(const std::string& text) {
    const std::string upper = toUpper(text);
    for (const LevelAlias& alias : kAliases) {
        if (upper == alias.text) {
            return alias.level;
        }
    }
    return Level::Unknown;
}

const char* levelName(Level level) {
    for (const LevelAlias& alias : kCanonical) {
        if (level == alias.level) {
            return alias.text;
        }
    }
    return "UNKNOWN";
}

bool levelAtLeast(Level value, Level threshold) {
    const int valueRank = rank(value);
    return valueRank >= 0 && valueRank >= rank(threshold);
}

std::size_t levelIndex(Level level) {
    const int value = rank(level);
    return value < 0 ? kLevelCount - 1 : static_cast<std::size_t>(value);
}

} // namespace loglens
