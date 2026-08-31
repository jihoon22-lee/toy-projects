#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace loglens {

enum class Level { Trace, Debug, Info, Warn, Error, Fatal, Unknown };

// Number of Level enumerators; used to size per-level count arrays.
constexpr std::size_t kLevelCount = 7;

struct LogRecord {
    std::uint64_t timestamp_ms = 0; // epoch millis; 0 when the line carried no time
    Level level = Level::Unknown;
    std::string source;             // logger or component name, may be empty
    std::string message;
    // Retained source bytes. RecordAssembler caps pathological records and
    // reports the exact omitted byte count instead of growing without bound.
    std::string raw;
    std::size_t line_number = 0;
    std::size_t input_bytes = 0;
    std::size_t omitted_bytes = 0;
};

// Case-insensitive. Accepts the common spellings: WARN/WARNING, ERR/ERROR,
// CRIT/CRITICAL/FATAL.
Level parseLevel(const std::string& text);
const char* levelName(Level level);
bool levelAtLeast(Level value, Level threshold);
std::size_t levelIndex(Level level);

} // namespace loglens
