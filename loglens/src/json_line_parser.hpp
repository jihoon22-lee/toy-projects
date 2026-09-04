#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "loglens/log_record.hpp"

namespace loglens::detail {

struct TimestampResult {
    std::uint64_t value = 0;
    bool present = false;
    bool valid = false;
    ParseDiagnosticCode code = ParseDiagnosticCode::InvalidTimestamp;
    const char* message = "timestamp is not a valid ISO-8601 value";
};

TimestampResult parseIsoTimestamp(std::string_view text);
LogRecord parseJsonLine(const std::string& line);

} // namespace loglens::detail
