#include "loglens/log_parser.hpp"

#include "json_line_parser.hpp"

#include <algorithm>
#include <cstdint>
#include <cctype>
#include <limits>
#include <string_view>
#include <stdexcept>
#include <utility>

namespace loglens {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

std::size_t saturatingAdd(std::size_t left, std::size_t right) {
    const std::size_t maximum = std::numeric_limits<std::size_t>::max();
    return right > maximum - left ? maximum : left + right;
}

bool startsWithDigits(const std::string& text, std::size_t count) {
    if (text.size() < count) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!isDigit(text[i])) {
            return false;
        }
    }
    return true;
}

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

// Splits on the first run of whitespace. Returns false when there is no
// remainder, which keeps the callers' loops flat.
bool splitToken(const std::string& text, std::string& token, std::string& rest) {
    std::size_t begin = 0;
    while (begin < text.size()
           && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    if (begin == text.size()) {
        token.clear();
        rest.clear();
        return false;
    }
    std::size_t end = begin;
    while (end < text.size()
           && !std::isspace(static_cast<unsigned char>(text[end]))) {
        ++end;
    }
    token = text.substr(begin, end - begin);
    while (end < text.size()
           && std::isspace(static_cast<unsigned char>(text[end]))) {
        ++end;
    }
    rest = text.substr(end);
    return end < text.size();
}

const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

constexpr std::size_t kMaxSyslogHostBytes = 255;
constexpr std::size_t kMaxSyslogComponentBytes = 32;

struct SyslogToken {
    std::string_view value;
    std::size_t offset = 0;
};

std::size_t skipWhitespace(std::string_view text, std::size_t cursor) {
    while (cursor < text.size()
           && std::isspace(static_cast<unsigned char>(text[cursor]))) {
        ++cursor;
    }
    return cursor;
}

bool takeSyslogToken(std::string_view text, std::size_t& cursor,
                     SyslogToken& token) {
    cursor = skipWhitespace(text, cursor);
    if (cursor == text.size()) {
        token = SyslogToken{};
        return false;
    }
    const std::size_t begin = cursor;
    while (cursor < text.size()
           && !std::isspace(static_cast<unsigned char>(text[cursor]))) {
        ++cursor;
    }
    token = SyslogToken{text.substr(begin, cursor - begin), begin};
    return true;
}

bool isMonthName(std::string_view token) {
    for (const char* month : kMonths) {
        if (token == month) {
            return true;
        }
    }
    return false;
}

bool isSyslogToken(std::string_view token, std::size_t maximum) {
    if (token.empty() || token.size() > maximum) {
        return false;
    }
    for (const char character : token) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte <= 0x20U || byte == 0x7fU) {
            return false;
        }
    }
    return true;
}

bool isSyslogDay(std::string_view token) {
    if (token.empty() || token.size() > 2U) {
        return false;
    }
    unsigned value = 0;
    for (const char character : token) {
        if (!isDigit(character)) {
            return false;
        }
        value = value * 10U + static_cast<unsigned>(character - '0');
    }
    return value >= 1U && value <= 31U;
}

bool isSyslogTime(std::string_view token) {
    if (token.size() != 8U || token[2] != ':' || token[5] != ':') {
        return false;
    }
    for (std::size_t index = 0; index < token.size(); ++index) {
        if (index != 2U && index != 5U && !isDigit(token[index])) {
            return false;
        }
    }
    const unsigned hour = static_cast<unsigned>(token[0] - '0') * 10U
                          + static_cast<unsigned>(token[1] - '0');
    const unsigned minute = static_cast<unsigned>(token[3] - '0') * 10U
                            + static_cast<unsigned>(token[4] - '0');
    const unsigned second = static_cast<unsigned>(token[6] - '0') * 10U
                            + static_cast<unsigned>(token[7] - '0');
    return hour <= 23U && minute <= 59U && second <= 59U;
}

bool isSyslogComponent(std::string_view token) {
    if (token.size() < 2U || token.back() != ':') {
        return false;
    }
    const std::string_view name = token.substr(0, token.size() - 1U);
    return name.find(':') == std::string_view::npos
           && isSyslogToken(name, kMaxSyslogComponentBytes);
}

void addDiagnostic(LogRecord& record, ParseDiagnosticCode code,
                   const std::string& field, std::size_t offset,
                   const std::string& message) {
    constexpr const char* kLimitMessage =
        "parse diagnostic limit exceeded; additional diagnostics were dropped";
    if (record.diagnostics.size() < kMaxParseDiagnostics) {
        record.diagnostics.push_back(ParseDiagnostic{code, field, offset, message});
    } else if (record.diagnostics.empty()
               || record.diagnostics.back().message != kLimitMessage) {
        record.diagnostics.back() =
            ParseDiagnostic{ParseDiagnosticCode::LimitExceeded, {}, offset, kLimitMessage};
    }
    if (record.parse_status == ParseStatus::Parsed) {
        record.parse_status = ParseStatus::Partial;
    }
}

// "[component]" -> "component"; returns false when the token is not bracketed.
bool takeBracketed(const std::string& token, std::string& out) {
    if (token.size() < 2 || token.front() != '[' || token.back() != ']') {
        return false;
    }
    out = token.substr(1, token.size() - 2);
    return true;
}

LogRecord parsePlainIso(const std::string& line) {
    LogRecord record;
    record.parse_status = ParseStatus::Parsed;
    std::string timestamp;
    std::string rest;
    splitToken(line, timestamp, rest);
    if (timestamp.empty()) {
        addDiagnostic(record, ParseDiagnosticCode::MissingField, "timestamp", 0,
                      "plain ISO record has no timestamp token");
    } else {
        const detail::TimestampResult parsed = detail::parseIsoTimestamp(timestamp);
        if (parsed.valid) {
            record.timestamp_ms = parsed.value;
        } else {
            addDiagnostic(record, parsed.code, "timestamp", 0, parsed.message);
        }
    }

    std::string levelToken;
    std::string tail;
    splitToken(rest, levelToken, tail);
    record.level = parseLevel(levelToken);
    if (levelToken.empty()) {
        addDiagnostic(record, ParseDiagnosticCode::MissingField, "level", timestamp.size(),
                      "plain ISO record has no level token");
    } else if (record.level == Level::Unknown) {
        addDiagnostic(record, ParseDiagnosticCode::InvalidField, "level", timestamp.size(),
                      "plain ISO record has an unknown level token");
    }

    std::string maybeSource;
    std::string body;
    if (splitToken(tail, maybeSource, body) && takeBracketed(maybeSource, record.source)) {
        record.message = body;
        return record;
    }
    record.message = tail;
    return record;
}

LogRecord parseSyslog(const std::string& line) {
    LogRecord record;
    record.parse_status = ParseStatus::Parsed;
    std::size_t cursor = 0;
    SyslogToken month;
    SyslogToken day;
    SyslogToken time;
    SyslogToken host;
    const bool hasMonth = takeSyslogToken(line, cursor, month);
    const bool hasDay = takeSyslogToken(line, cursor, day);
    const bool hasTime = takeSyslogToken(line, cursor, time);
    const bool hasHost = takeSyslogToken(line, cursor, host);

    if (!hasMonth) {
        addDiagnostic(record, ParseDiagnosticCode::MissingField, "month", cursor,
                      "syslog record has no month token");
    } else if (!isMonthName(month.value)) {
        addDiagnostic(record, ParseDiagnosticCode::InvalidField, "month", month.offset,
                      "syslog month is not a valid abbreviated month");
    }
    if (!hasDay) {
        addDiagnostic(record, ParseDiagnosticCode::MissingField, "day", cursor,
                      "syslog record has no day token");
    } else if (!isSyslogDay(day.value)) {
        addDiagnostic(record, ParseDiagnosticCode::InvalidField, "day", day.offset,
                      "syslog day must be one or two digits in the range 1..31");
    }
    if (!hasTime) {
        addDiagnostic(record, ParseDiagnosticCode::MissingField, "time", cursor,
                      "syslog record has no time token");
    } else if (!isSyslogTime(time.value)) {
        addDiagnostic(record, ParseDiagnosticCode::InvalidTimestamp, "time", time.offset,
                      "syslog time must be HH:MM:SS in the range 00:00:00..23:59:59");
    }
    if (!hasHost) {
        addDiagnostic(record, ParseDiagnosticCode::MissingField, "host", cursor,
                      "syslog record has no host token");
    } else if (!isSyslogToken(host.value, kMaxSyslogHostBytes)) {
        addDiagnostic(record, ParseDiagnosticCode::InvalidField, "host", host.offset,
                      "syslog host must be a bounded non-whitespace token");
    }

    const std::size_t componentOffset = skipWhitespace(line, cursor);
    SyslogToken component;
    const bool hasComponent = takeSyslogToken(line, cursor, component);
    if (!hasComponent) {
        addDiagnostic(record, ParseDiagnosticCode::MissingField, "component", componentOffset,
                      "syslog record has no component token");
        record.message = line.substr(componentOffset);
        return record;
    }
    if (isSyslogComponent(component.value)) {
        record.source.assign(component.value.data(), component.value.size() - 1U);
        record.message = line.substr(skipWhitespace(line, cursor));
    } else {
        addDiagnostic(record, ParseDiagnosticCode::InvalidField, "component", component.offset,
                      "syslog component must be a bounded name followed by one colon");
        record.message = line.substr(component.offset);
    }
    return record;
}

} // namespace

bool isContinuation(const std::string& line) {
    if (line.empty()) {
        return false;
    }
    if (line[0] == ' ' || line[0] == '\t') {
        return true;
    }
    return line.compare(0, 3, "at ") == 0;
}

Format detectFormat(const std::string& line) {
    const std::string trimmed = trim(line);
    if (!trimmed.empty() && trimmed.front() == '{') {
        return Format::JsonLine;
    }
    if (startsWithDigits(trimmed, 4) && trimmed.size() > 4 && trimmed[4] == '-') {
        return Format::PlainIso;
    }
    std::string first;
    std::string rest;
    splitToken(trimmed, first, rest);
    if (isMonthName(first)) {
        return Format::Syslog;
    }
    return Format::Auto;
}

const char* formatName(Format format) {
    switch (format) {
        case Format::Auto: return "auto";
        case Format::PlainIso: return "iso";
        case Format::Syslog: return "syslog";
        case Format::JsonLine: return "jsonl";
        case Format::Raw: return "raw";
    }
    return "auto";
}

std::optional<Format> parseFormatName(const std::string& name) {
    if (name == "auto") {
        return Format::Auto;
    }
    if (name == "iso" || name == "plain") {
        return Format::PlainIso;
    }
    if (name == "syslog") {
        return Format::Syslog;
    }
    if (name == "jsonl" || name == "json") {
        return Format::JsonLine;
    }
    if (name == "raw") {
        return Format::Raw;
    }
    return std::nullopt;
}

const char* multilinePolicyName(MultilinePolicy policy) {
    switch (policy) {
        case MultilinePolicy::FoldContinuations: return "fold-continuations";
        case MultilinePolicy::SeparateLines: return "separate-lines";
    }
    return "unknown";
}

std::optional<MultilinePolicy> parseMultilinePolicyName(const std::string& name) {
    if (name == "fold-continuations") {
        return MultilinePolicy::FoldContinuations;
    }
    if (name == "separate-lines") {
        return MultilinePolicy::SeparateLines;
    }
    return std::nullopt;
}

LogRecord parseLine(const std::string& line, Format format, std::size_t lineNumber) {
    const Format resolved = format == Format::Auto ? detectFormat(line) : format;
    LogRecord record;
    if (resolved == Format::PlainIso) {
        record = parsePlainIso(line);
    } else if (resolved == Format::Syslog) {
        record = parseSyslog(line);
    } else if (resolved == Format::JsonLine) {
        record = detail::parseJsonLine(line);
    } else {
        record.message = line;
    }
    record.raw = line;
    record.line_number = lineNumber;
    record.input_bytes = line.size();
    if (record.message.empty() && resolved != Format::JsonLine) {
        record.message = line;
    }
    return record;
}

RecordAssembler::RecordAssembler(Format format, EncodingErrorPolicy encodingPolicy,
                                 std::size_t maxRecordBytes, MultilinePolicy multilinePolicy)
    : format_(format), encoding_error_policy_(encodingPolicy),
      max_record_bytes_(maxRecordBytes), multiline_policy_(multilinePolicy) {
    if (maxRecordBytes == 0 || maxRecordBytes > kMaxRecordBytes) {
        throw std::invalid_argument("record byte limit is outside the supported range");
    }
}

std::vector<RecordDelta> RecordAssembler::consumeCompleteLine(const std::string& input,
                                                              std::size_t omittedBytes) {
    // std::getline() removes LF but leaves CR when a Windows-written file is
    // read in binary mode. Treat CRLF as one newline while keeping raw useful
    // to callers instead of leaking a carriage return into the table/filter.
    std::string line = input;
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    const std::size_t physicalLine = next_line_number_++;
    std::vector<RecordDelta> deltas;
    if (multiline_policy_ == MultilinePolicy::FoldContinuations && isContinuation(line)
        && has_pending_) {
        const std::size_t incomingBytes = saturatingAdd(1, saturatingAdd(line.size(), omittedBytes));
        pending_record_.input_bytes =
            saturatingAdd(pending_record_.input_bytes, incomingBytes);
        const std::size_t room = max_record_bytes_ - pending_record_.raw.size();
        if (room > 0) {
            pending_record_.message.push_back('\n');
            pending_record_.raw.push_back('\n');
            const std::size_t lineBytes = std::min(line.size(), room - 1);
            pending_record_.message.append(line.data(), lineBytes);
            pending_record_.raw.append(line.data(), lineBytes);
        }
        pending_record_.omitted_bytes =
            pending_record_.input_bytes - pending_record_.raw.size();
        deltas.push_back(RecordDelta{RecordDelta::Kind::Extend, pending_index_, physicalLine,
                                     generation_, pending_record_});
        return deltas;
    }

    pending_record_ = parseLine(line, format_, physicalLine);
    pending_record_.input_bytes = saturatingAdd(line.size(), omittedBytes);
    pending_record_.omitted_bytes =
        pending_record_.input_bytes - pending_record_.raw.size();
    pending_index_ = record_count_++;
    has_pending_ = true;
    deltas.push_back(RecordDelta{RecordDelta::Kind::Append, pending_index_, physicalLine,
                                 generation_, pending_record_});
    return deltas;
}

std::vector<RecordDelta> RecordAssembler::consumeBytes(std::string_view bytes) {
    std::vector<RecordDelta> deltas;
    std::size_t start = 0;
    while (start < bytes.size()) {
        const std::size_t newline = bytes.find('\n', start);
        const std::size_t end = newline == std::string_view::npos ? bytes.size() : newline;
        const std::size_t segmentBytes = end - start;
        const std::size_t room = max_record_bytes_ - partial_.size();
        const std::size_t retained = std::min(room, segmentBytes);
        partial_.append(bytes.data() + start, retained);
        partial_omitted_bytes_ =
            saturatingAdd(partial_omitted_bytes_, segmentBytes - retained);

        if (newline == std::string_view::npos) {
            break;
        }
        std::vector<RecordDelta> lineDeltas =
            consumeCompleteLine(partial_, partial_omitted_bytes_);
        deltas.insert(deltas.end(), lineDeltas.begin(), lineDeltas.end());
        partial_.clear();
        partial_omitted_bytes_ = 0;
        start = newline + 1;
    }
    return deltas;
}

std::vector<RecordDelta> RecordAssembler::consumeLine(const std::string& line) {
    // Line-oriented callers still participate in the same partial-byte state:
    // a line obtained after a previous byte chunk completes that chunk rather
    // than creating a second record.
    std::vector<RecordDelta> deltas = consumeBytes(line);
    std::vector<RecordDelta> completed = consumeBytes("\n");
    deltas.insert(deltas.end(), completed.begin(), completed.end());
    return deltas;
}

std::vector<RecordDelta> RecordAssembler::consumeLines(const std::vector<std::string>& lines) {
    std::vector<RecordDelta> deltas;
    for (const std::string& line : lines) {
        std::vector<RecordDelta> lineDeltas = consumeLine(line);
        deltas.insert(deltas.end(), lineDeltas.begin(), lineDeltas.end());
    }
    return deltas;
}

std::vector<RecordDelta> RecordAssembler::flush() {
    if (partial_.empty()) {
        return {};
    }
    std::string line = std::move(partial_);
    partial_.clear();
    const std::size_t omittedBytes = partial_omitted_bytes_;
    partial_omitted_bytes_ = 0;
    return consumeCompleteLine(line, omittedBytes);
}

void RecordAssembler::reset(std::uint64_t generation, std::size_t firstLineNumber) {
    if (firstLineNumber == 0) {
        throw std::invalid_argument("first physical line number must be positive");
    }
    generation_ = generation;
    next_line_number_ = firstLineNumber;
    record_count_ = 0;
    partial_.clear();
    partial_omitted_bytes_ = 0;
    pending_record_ = LogRecord{};
    pending_index_ = 0;
    has_pending_ = false;
}

void RecordAssembler::setFormat(Format format) { format_ = format; }

Format RecordAssembler::format() const { return format_; }

EncodingErrorPolicy RecordAssembler::encodingErrorPolicy() const {
    return encoding_error_policy_;
}

std::uint64_t RecordAssembler::generation() const { return generation_; }

std::size_t RecordAssembler::nextLineNumber() const { return next_line_number_; }

std::size_t RecordAssembler::recordCount() const { return record_count_; }

std::size_t RecordAssembler::maxRecordBytes() const { return max_record_bytes_; }

MultilinePolicy RecordAssembler::multilinePolicy() const { return multiline_policy_; }

std::size_t RecordAssembler::partialBytes() const { return partial_.size(); }

} // namespace loglens
