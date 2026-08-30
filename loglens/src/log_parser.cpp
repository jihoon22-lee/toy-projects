#include "loglens/log_parser.hpp"

#include <cctype>
#include <cstdlib>
#include <ctime>
#include <utility>

namespace loglens {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Maps a backslash escape to its character. A lookup keeps each mapping on one
// line rather than repeating a case/append/break block per escape.
char unescapeJson(char c) {
    switch (c) {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case 'b': return '\b';
        case 'f': return '\f';
        default: return c;
    }
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
    const std::size_t space = text.find(' ');
    if (space == std::string::npos) {
        token = text;
        rest.clear();
        return false;
    }
    token = text.substr(0, space);
    std::size_t next = space;
    while (next < text.size() && text[next] == ' ') {
        ++next;
    }
    rest = text.substr(next);
    return true;
}

// Reads a JSON string literal starting at the opening quote, honouring simple
// escapes. \uXXXX is left as-is: log messages rarely use it and decoding here
// would duplicate a real parser for no benefit.
std::string readJsonString(const std::string& line, std::size_t quote) {
    std::string value;
    std::size_t i = quote + 1;
    while (i < line.size() && line[i] != '"') {
        if (line[i] != '\\' || i + 1 >= line.size()) {
            value += line[i];
            ++i;
            continue;
        }
        value += unescapeJson(line[i + 1]);
        i += 2;
    }
    return value;
}

// Extracts a "key":"value" or "key":value pair from a JSON line without a full
// parser. Good enough for flat log records and keeps the module dependency-free.
std::string jsonField(const std::string& line, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const std::size_t at = line.find(needle);
    if (at == std::string::npos) {
        return std::string();
    }
    std::size_t colon = line.find(':', at + needle.size());
    if (colon == std::string::npos) {
        return std::string();
    }
    ++colon;
    while (colon < line.size() && std::isspace(static_cast<unsigned char>(line[colon]))) {
        ++colon;
    }
    if (colon >= line.size()) {
        return std::string();
    }
    if (line[colon] != '"') {
        const std::size_t end = line.find_first_of(",}", colon);
        return trim(line.substr(colon, end == std::string::npos ? std::string::npos
                                                                : end - colon));
    }
    return readJsonString(line, colon);
}

const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

bool isMonthName(const std::string& token) {
    for (const char* month : kMonths) {
        if (token == month) {
            return true;
        }
    }
    return false;
}

// "2026-08-26T04:15:22.123Z" -> epoch millis. Returns 0 when unparseable;
// callers treat 0 as "no timestamp" rather than as the epoch.
std::uint64_t parseIsoTimestamp(const std::string& text) {
    if (text.size() < 19 || text[4] != '-' || text[7] != '-') {
        return 0;
    }
    std::tm parts{};
    parts.tm_year = std::atoi(text.substr(0, 4).c_str()) - 1900;
    parts.tm_mon = std::atoi(text.substr(5, 2).c_str()) - 1;
    parts.tm_mday = std::atoi(text.substr(8, 2).c_str());
    parts.tm_hour = std::atoi(text.substr(11, 2).c_str());
    parts.tm_min = std::atoi(text.substr(14, 2).c_str());
    parts.tm_sec = std::atoi(text.substr(17, 2).c_str());
    const std::time_t seconds = timegm(&parts);
    if (seconds < 0) {
        return 0;
    }
    std::uint64_t millis = static_cast<std::uint64_t>(seconds) * 1000;
    if (text.size() > 20 && text[19] == '.') {
        millis += static_cast<std::uint64_t>(std::atoi(text.substr(20, 3).c_str()));
    }
    return millis;
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
    std::string timestamp;
    std::string rest;
    splitToken(line, timestamp, rest);
    record.timestamp_ms = parseIsoTimestamp(timestamp);

    std::string levelToken;
    std::string tail;
    splitToken(rest, levelToken, tail);
    record.level = parseLevel(levelToken);

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
    std::string rest = line;
    // Month, day, time, host are positional; drop them one token at a time.
    for (int i = 0; i < 4; ++i) {
        std::string token;
        std::string tail;
        splitToken(rest, token, tail);
        rest = tail;
    }
    std::string component;
    std::string body;
    splitToken(rest, component, body);
    if (!component.empty() && component.back() == ':') {
        record.source = component.substr(0, component.size() - 1);
        record.message = body;
        return record;
    }
    record.message = rest;
    return record;
}

LogRecord parseJsonLine(const std::string& line) {
    LogRecord record;
    record.timestamp_ms = parseIsoTimestamp(jsonField(line, "ts"));
    record.level = parseLevel(jsonField(line, "level"));
    record.source = jsonField(line, "logger");
    record.message = jsonField(line, "msg");
    if (record.message.empty()) {
        record.message = jsonField(line, "message");
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

LogRecord parseLine(const std::string& line, Format format, std::size_t lineNumber) {
    const Format resolved = format == Format::Auto ? detectFormat(line) : format;
    LogRecord record;
    if (resolved == Format::PlainIso) {
        record = parsePlainIso(line);
    } else if (resolved == Format::Syslog) {
        record = parseSyslog(line);
    } else if (resolved == Format::JsonLine) {
        record = parseJsonLine(line);
    } else {
        record.message = line;
    }
    record.raw = line;
    record.line_number = lineNumber;
    if (record.message.empty() && resolved != Format::JsonLine) {
        record.message = line;
    }
    return record;
}

RecordAssembler::RecordAssembler(Format format) : format_(format) {}

std::vector<RecordDelta> RecordAssembler::consumeCompleteLine(const std::string& input) {
    // std::getline() removes LF but leaves CR when a Windows-written file is
    // read in binary mode. Treat CRLF as one newline while keeping raw useful
    // to callers instead of leaking a carriage return into the table/filter.
    std::string line = input;
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    const std::size_t physicalLine = next_line_number_++;
    std::vector<RecordDelta> deltas;
    if (isContinuation(line) && has_pending_) {
        pending_record_.message += "\n" + line;
        pending_record_.raw += "\n" + line;
        deltas.push_back(RecordDelta{RecordDelta::Kind::Extend, pending_index_, physicalLine,
                                     generation_, pending_record_});
        return deltas;
    }

    pending_record_ = parseLine(line, format_, physicalLine);
    pending_index_ = record_count_++;
    has_pending_ = true;
    deltas.push_back(RecordDelta{RecordDelta::Kind::Append, pending_index_, physicalLine,
                                 generation_, pending_record_});
    return deltas;
}

std::vector<RecordDelta> RecordAssembler::consumeBytes(std::string_view bytes) {
    if (!bytes.empty()) {
        partial_.append(bytes.data(), bytes.size());
    }

    std::vector<RecordDelta> deltas;
    std::size_t start = 0;
    while (true) {
        const std::size_t newline = partial_.find('\n', start);
        if (newline == std::string::npos) {
            break;
        }
        std::vector<RecordDelta> lineDeltas =
            consumeCompleteLine(partial_.substr(start, newline - start));
        deltas.insert(deltas.end(), lineDeltas.begin(), lineDeltas.end());
        start = newline + 1;
    }
    if (start != 0) {
        partial_.erase(0, start);
    }
    return deltas;
}

std::vector<RecordDelta> RecordAssembler::consumeLine(const std::string& line) {
    // Line-oriented callers still participate in the same partial-byte state:
    // a line obtained after a previous byte chunk completes that chunk rather
    // than creating a second record.
    std::string framed = line;
    framed.push_back('\n');
    return consumeBytes(framed);
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
    return consumeCompleteLine(line);
}

void RecordAssembler::reset(std::uint64_t generation) {
    generation_ = generation;
    next_line_number_ = 1;
    record_count_ = 0;
    partial_.clear();
    pending_record_ = LogRecord{};
    pending_index_ = 0;
    has_pending_ = false;
}

void RecordAssembler::setFormat(Format format) { format_ = format; }

Format RecordAssembler::format() const { return format_; }

std::uint64_t RecordAssembler::generation() const { return generation_; }

std::size_t RecordAssembler::nextLineNumber() const { return next_line_number_; }

std::size_t RecordAssembler::recordCount() const { return record_count_; }

} // namespace loglens
