#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "loglens/log_record.hpp"

namespace loglens::detail {

struct JsonStringResult {
    bool valid = false;
    bool limited = false;
    std::string value;
    ParseDiagnostic diagnostic;
};

// Decodes one JSON string at input[position].  The cursor is advanced through
// the complete string even when its decoded value reaches maxBytes, allowing
// the caller to return a bounded Partial record instead of desynchronizing the
// enclosing JSON object.
JsonStringResult parseJsonString(std::string_view input, std::size_t& position,
                                 std::size_t maxBytes, std::size_t start,
                                 const std::string& field);

} // namespace loglens::detail
