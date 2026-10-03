#pragma once

#include "loglens/log_record.hpp"

#include <cstddef>
#include <regex>
#include <string>

namespace loglens {

// A declarative parser plugin loaded from a `loglens.format/v1` document:
// a regular expression whose numbered capture groups map onto record fields.
// Custom formats flow through the same RecordAssembler pipeline as the
// built-in formats, so multiline/limits/filters behave identically.
struct FormatFieldMap {
    // Capture-group numbers; 0 means the field is not mapped. `message` is
    // required — a plugin that captures no message cannot produce a record.
    std::size_t timestamp = 0;
    std::size_t level = 0;
    std::size_t source = 0;
    std::size_t message = 0;
};

struct FormatPlugin {
    std::string name;
    std::string pattern_text;
    std::regex pattern;
    FormatFieldMap fields;
};

enum class FormatPluginError {
    None,
    ReadFailed,
    MalformedDocument,
    UnsupportedVersion,
    MissingField,
    InvalidField,
};

const char* formatPluginErrorName(FormatPluginError code);

// Loads and validates a plugin document. On success `plugin.pattern` holds
// the compiled expression; on failure `error` names the offending field.
FormatPluginError loadFormatPlugin(const std::string& path, FormatPlugin& plugin,
                                   std::string& error);

// Parses one line through a plugin. A non-matching line returns
// ParseStatus::Unstructured with the raw line preserved — the same fallback
// contract the built-in formats honour.
LogRecord parsePluginLine(const std::string& line, const FormatPlugin& plugin,
                          std::size_t lineNumber);

} // namespace loglens
