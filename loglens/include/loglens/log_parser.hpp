#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loglens/log_record.hpp"

namespace loglens {

constexpr std::size_t kDefaultMaxRecordBytes = 64 * 1024;
constexpr std::size_t kMaxRecordBytes = 1024 * 1024;

enum class Format {
    Auto,
    PlainIso, // 2026-08-26T04:15:22.123Z INFO  [component] message
    Syslog,   // Aug 26 04:15:22 host component: message
    JsonLine, // {"ts":"...","level":"warn","logger":"x","msg":"..."}
    Raw,      // Preserve every line as an unstructured message.
};

// Controls whether indented/"at " physical lines extend the preceding record
// or stay as independent records.  FoldContinuations is the historical
// default and keeps existing CLI/GUI behaviour unchanged.
enum class MultilinePolicy { FoldContinuations, SeparateLines };

const char* multilinePolicyName(MultilinePolicy policy);
std::optional<MultilinePolicy> parseMultilinePolicyName(const std::string& name);

// Parsing is byte preserving at this layer: invalid UTF-8 is neither dropped
// nor silently rewritten, so raw evidence can always be exported. UI adapters
// decide how to render those bytes. Naming the policy makes that choice part
// of the stream state instead of an undocumented platform conversion.
enum class EncodingErrorPolicy { PreserveBytes };

Format detectFormat(const std::string& line);
const char* formatName(Format format);
std::optional<Format> parseFormatName(const std::string& name);

// Never throws: an unparseable line still comes back with raw set, level
// Unknown, and message holding the whole supplied line.
LogRecord parseLine(const std::string& line, Format format, std::size_t lineNumber);

// True for stack-trace style continuations that belong to the previous record.
bool isContinuation(const std::string& line);

// A stream parser must tell its consumers whether a new logical record was
// created or an already-published record grew by one physical continuation
// line. Keeping that distinction here lets the CLI and GUI apply exactly the
// same parser semantics without sharing storage ownership or UI code.
struct RecordDelta {
    enum class Kind { Append, Extend };

    Kind kind = Kind::Append;
    // The logical record index within the current source generation. For an
    // Extend this identifies the record that received the continuation.
    std::size_t record_index = 0;
    // The physical line that caused this delta. An Extend keeps the original
    // record.line_number but exposes the continuation's physical location.
    std::size_t physical_line_number = 0;
    std::uint64_t generation = 0;
    // Append carries a new record; Extend carries the complete updated record
    // so consumers never need a dangling pointer into the assembler.
    LogRecord record;
};

// Stateful newline/record assembler shared by one-shot and follow mode.
// consumeBytes() deliberately withholds a final non-newline-terminated
// fragment. Call flush() only when the caller has an explicit EOF policy.
class RecordAssembler {
public:
    explicit RecordAssembler(
        Format format = Format::Auto,
        EncodingErrorPolicy encodingPolicy = EncodingErrorPolicy::PreserveBytes,
        std::size_t maxRecordBytes = kDefaultMaxRecordBytes,
        MultilinePolicy multilinePolicy = MultilinePolicy::FoldContinuations);

    std::vector<RecordDelta> consumeBytes(std::string_view bytes);
    std::vector<RecordDelta> consumeLine(const std::string& line);
    std::vector<RecordDelta> consumeLines(const std::vector<std::string>& lines);

    // Publishes the currently buffered partial line as an explicit EOF/flush
    // decision. Normal polling must not call this, or a line split across two
    // polls would be published prematurely.
    std::vector<RecordDelta> flush();

    // Drops partial/continuation state and starts a new source generation.
    // A tail-window loader can preserve the physical line number of its first
    // selected byte instead of relabelling the retained suffix from line 1.
    void reset(std::uint64_t generation = 0, std::size_t firstLineNumber = 1);

    void setFormat(Format format);
    Format format() const;
    EncodingErrorPolicy encodingErrorPolicy() const;
    std::uint64_t generation() const;
    std::size_t nextLineNumber() const;
    std::size_t recordCount() const;
    std::size_t maxRecordBytes() const;
    MultilinePolicy multilinePolicy() const;
    std::size_t partialBytes() const;

private:
    Format format_ = Format::Auto;
    EncodingErrorPolicy encoding_error_policy_ = EncodingErrorPolicy::PreserveBytes;
    std::uint64_t generation_ = 0;
    std::size_t next_line_number_ = 1;
    std::size_t record_count_ = 0;
    std::string partial_;
    std::size_t partial_omitted_bytes_ = 0;
    LogRecord pending_record_;
    std::size_t pending_index_ = 0;
    bool has_pending_ = false;
    std::size_t max_record_bytes_ = kDefaultMaxRecordBytes;
    MultilinePolicy multiline_policy_ = MultilinePolicy::FoldContinuations;

    std::vector<RecordDelta> consumeCompleteLine(const std::string& line,
                                                 std::size_t omittedBytes = 0);
};

} // namespace loglens
