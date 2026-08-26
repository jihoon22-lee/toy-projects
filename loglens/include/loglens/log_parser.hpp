#pragma once

#include <cstddef>
#include <string>

#include "loglens/log_record.hpp"

namespace loglens {

enum class Format {
    Auto,
    PlainIso, // 2026-08-26T04:15:22.123Z INFO  [component] message
    Syslog,   // Aug 26 04:15:22 host component: message
    JsonLine, // {"ts":"...","level":"warn","logger":"x","msg":"..."}
};

Format detectFormat(const std::string& line);

// Never throws and never drops input: an unparseable line still comes back
// with raw set, level Unknown, and message holding the whole line.
LogRecord parseLine(const std::string& line, Format format, std::size_t lineNumber);

// True for stack-trace style continuations that belong to the previous record.
bool isContinuation(const std::string& line);

} // namespace loglens
