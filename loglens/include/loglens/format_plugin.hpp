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
    std::string document_fingerprint; // SHA-256 of the exact document compiled by the loader
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

// Compiles a plugin pattern (ECMAScript grammar). On libstdc++ the
// non-recursive executor is selected: the default backtracking executor
// recurses once per input character and overflows the stack on long lines.
// Back-references are therefore rejected. Throws std::regex_error.
std::regex compileFormatPattern(const std::string& pattern);

// Loads and validates a plugin document. On success `plugin.pattern` holds
// the compiled expression; on failure `error` names the offending field.
FormatPluginError loadFormatPlugin(const std::string& path, FormatPlugin& plugin,
                                   std::string& error);

// Parses one line through a plugin. A non-matching line returns
// ParseStatus::Unstructured with the raw line preserved — the same fallback
// contract the built-in formats honour. A line the matcher cannot evaluate
// safely (regex resource limits, or an over-long line on standard libraries
// whose matcher recurses per character) also stays Unstructured, with a
// LimitExceeded diagnostic.
LogRecord parsePluginLine(const std::string& line, const FormatPlugin& plugin,
                          std::size_t lineNumber);

} // namespace loglens
