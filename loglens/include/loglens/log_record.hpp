#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace loglens {

enum class Level { Trace, Debug, Info, Warn, Error, Fatal, Unknown };

// Structured parsing outcome carried with every record.  Unstructured is the
// deliberate result for a free-form line that auto-detection could not map to
// one of the supported formats; it is not an error and keeps the raw line
// searchable/exportable.
enum class ParseStatus { Parsed, Partial, Invalid, Unstructured };

// Diagnostics are data, not log output.  Consumers can render them in a GUI,
// include them in an export, or count them without having to repeat parser
// heuristics.  The byte offset is relative to LogRecord::raw and is zero when
// a diagnostic applies to the whole record.
enum class ParseDiagnosticCode {
    MissingField,
    InvalidField,
    InvalidFieldType,
    InvalidTimestamp,
    InvalidTimestampOffset,
    InvalidJson,
    InvalidEscape,
    InvalidUnicode,
    DuplicateField,
    TrailingData,
    LimitExceeded,
};

struct ParseDiagnostic {
    ParseDiagnosticCode code = ParseDiagnosticCode::InvalidField;
    std::string field;
    std::size_t offset = 0;
    std::string message;
};

// Number of Level enumerators; used to size per-level count arrays.
constexpr std::size_t kLevelCount = 7;
// Diagnostics are bounded because a malformed record must not become a
// memory-growth vector for either the JSON reader or format-level validation.
constexpr std::size_t kMaxParseDiagnostics = 64;

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
    ParseStatus parse_status = ParseStatus::Unstructured;
    std::vector<ParseDiagnostic> diagnostics;
};

// Case-insensitive. Accepts the common spellings: WARN/WARNING, ERR/ERROR,
// CRIT/CRITICAL/FATAL.
Level parseLevel(const std::string& text);
const char* levelName(Level level);
bool levelAtLeast(Level value, Level threshold);
std::size_t levelIndex(Level level);
const char* parseStatusName(ParseStatus status);
const char* parseDiagnosticCodeName(ParseDiagnosticCode code);

} // namespace loglens
