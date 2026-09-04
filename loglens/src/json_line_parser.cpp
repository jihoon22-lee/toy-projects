#include "json_line_parser.hpp"
#include "json_string_parser.hpp"

#include "loglens/log_parser.hpp"

#include <utility>
#include <vector>

namespace loglens::detail {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

constexpr std::size_t kMaxJsonDepth = 32;
constexpr std::size_t kMaxJsonMembers = 256;
constexpr std::size_t kMaxJsonStringBytes = 256 * 1024;
constexpr std::size_t kMaxJsonDiagnostics = kMaxParseDiagnostics;

void addDiagnostic(LogRecord& record, ParseDiagnosticCode code,
                   const std::string& field, std::size_t offset,
                   const std::string& message) {
    constexpr const char* kLimitMessage =
        "parse diagnostic limit exceeded; additional diagnostics were dropped";
    if (record.diagnostics.size() < kMaxParseDiagnostics) {
        record.diagnostics.push_back(ParseDiagnostic{code, field, offset, message});
    } else if (record.diagnostics.back().message != kLimitMessage) {
        record.diagnostics.back() =
            ParseDiagnostic{ParseDiagnosticCode::LimitExceeded, {}, offset, kLimitMessage};
    }
    if (record.parse_status == ParseStatus::Parsed) {
        record.parse_status = ParseStatus::Partial;
    }
}

enum class JsonKind { Null, Boolean, Number, String, Object, Array };

struct JsonValue {
    JsonKind kind = JsonKind::Null;
    std::string text;
};

struct JsonMember {
    std::string name;
    JsonValue value;
    std::size_t offset = 0;
};

class JsonReader {
public:
    explicit JsonReader(std::string_view input) : input_(input) {
        root_members_.reserve(kMaxJsonMembers);
        diagnostics_.reserve(kMaxJsonDiagnostics);
    }

    bool parse() {
        skipWhitespace();
        JsonValue root;
        if (!parseValue(root, 0, true)) {
            return false;
        }
        if (root.kind != JsonKind::Object) {
            syntaxError(ParseDiagnosticCode::InvalidJson,
                        "JSON log record must be an object", 0);
            return false;
        }
        skipWhitespace();
        if (position_ != input_.size()) {
            syntaxError(ParseDiagnosticCode::TrailingData,
                        "JSON log record has data after the object", position_);
            return false;
        }
        return syntax_valid_;
    }

    const std::vector<JsonMember>& rootMembers() const { return root_members_; }
    const std::vector<ParseDiagnostic>& diagnostics() const { return diagnostics_; }

private:
    static constexpr std::size_t kNoField = std::string::npos;

    void skipWhitespace() {
        while (position_ < input_.size()) {
            const char value = input_[position_];
            if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
                break;
            }
            ++position_;
        }
    }

    void syntaxError(ParseDiagnosticCode code, const char* message,
                     std::size_t offset = kNoField, const std::string& field = {}) {
        syntax_valid_ = false;
        storeDiagnostic(ParseDiagnostic{code, field,
                                        offset == kNoField ? position_ : offset, message});
    }

    void limitError(std::size_t offset, const std::string& field, const char* message) {
        storeDiagnostic(ParseDiagnostic{ParseDiagnosticCode::LimitExceeded, field, offset,
                                         message});
    }

    void storeDiagnostic(ParseDiagnostic diagnostic) {
        if (diagnostics_.size() < kMaxJsonDiagnostics) {
            diagnostics_.push_back(std::move(diagnostic));
            return;
        }
        if (!diagnostic_limit_reported_) {
            diagnostics_.back() = ParseDiagnostic{
                ParseDiagnosticCode::LimitExceeded, {}, diagnostic.offset,
                "JSON diagnostic limit exceeded; additional diagnostics were dropped"};
            diagnostic_limit_reported_ = true;
        }
    }

    bool parseValue(JsonValue& value, std::size_t depth, bool captureRoot) {
        skipWhitespace();
        if (position_ >= input_.size()) {
            syntaxError(ParseDiagnosticCode::InvalidJson, "JSON value is missing");
            return false;
        }
        const char first = input_[position_];
        if (first == '"') {
            value.kind = JsonKind::String;
            return parseString(value.text, position_, {});
        }
        if (first == '{') {
            return parseObject(value, depth, captureRoot);
        }
        if (first == '[') {
            return parseArray(value, depth);
        }
        if (first == 't' && parseLiteral("true")) {
            value.kind = JsonKind::Boolean;
            value.text = "true";
            return true;
        }
        if (first == 'f' && parseLiteral("false")) {
            value.kind = JsonKind::Boolean;
            value.text = "false";
            return true;
        }
        if (first == 'n' && parseLiteral("null")) {
            value.kind = JsonKind::Null;
            value.text = "null";
            return true;
        }
        if (first == '-' || isDigit(first)) {
            if (parseNumber(value.text)) {
                value.kind = JsonKind::Number;
                return true;
            }
            return false;
        }
        syntaxError(ParseDiagnosticCode::InvalidJson, "invalid JSON value", position_);
        return false;
    }

    bool parseObject(JsonValue& value, std::size_t depth, bool captureRoot) {
        if (depth > kMaxJsonDepth) {
            limitError(position_, {}, "JSON nesting depth exceeds the safety limit");
            syntax_valid_ = false;
            return false;
        }
        ++position_; // '{'
        value.kind = JsonKind::Object;
        skipWhitespace();
        if (position_ < input_.size() && input_[position_] == '}') {
            ++position_;
            return true;
        }
        while (position_ < input_.size()) {
            if (!parseObjectMember(depth, captureRoot)) {
                return false;
            }
            skipWhitespace();
            if (position_ >= input_.size()) {
                syntaxError(ParseDiagnosticCode::InvalidJson,
                            "JSON object is not terminated", position_);
                return false;
            }
            if (input_[position_] == '}') {
                ++position_;
                return true;
            }
            if (input_[position_] != ',') {
                syntaxError(ParseDiagnosticCode::InvalidJson,
                            "JSON object member must be followed by ',' or '}'",
                            position_);
                return false;
            }
            ++position_;
            skipWhitespace();
            if (position_ < input_.size() && input_[position_] == '}') {
                syntaxError(ParseDiagnosticCode::InvalidJson,
                            "JSON object has a trailing comma", position_);
                return false;
            }
        }
        syntaxError(ParseDiagnosticCode::InvalidJson, "JSON object is not terminated",
                    position_);
        return false;
    }

    bool parseObjectMember(std::size_t depth, bool captureRoot) {
        const std::size_t keyOffset = position_;
        std::string name;
        if (!parseString(name, keyOffset, {})) {
            return false;
        }
        skipWhitespace();
        if (position_ >= input_.size() || input_[position_] != ':') {
            syntaxError(ParseDiagnosticCode::InvalidJson,
                        "JSON object member is missing ':'", position_);
            return false;
        }
        ++position_;
        JsonValue memberValue;
        if (!parseValue(memberValue, depth + 1, false)) {
            return false;
        }
        if (memberCount_ < kMaxJsonMembers) {
            ++memberCount_;
            if (captureRoot) {
                storeRootMember(std::move(name), std::move(memberValue), keyOffset);
            }
        } else if (!member_limit_reported_) {
            limitError(keyOffset, {}, "JSON object member limit exceeded");
            member_limit_reported_ = true;
        }
        return true;
    }

    void storeRootMember(std::string name, JsonValue value, std::size_t keyOffset) {
        for (const JsonMember& member : root_members_) {
            if (member.name == name) {
                storeDiagnostic(ParseDiagnostic{
                    ParseDiagnosticCode::DuplicateField, name, keyOffset,
                    "duplicate JSON object member; first value retained"});
                return;
            }
        }
        root_members_.push_back(
            JsonMember{std::move(name), std::move(value), keyOffset});
    }

    bool parseArray(JsonValue& value, std::size_t depth) {
        if (depth > kMaxJsonDepth) {
            limitError(position_, {}, "JSON nesting depth exceeds the safety limit");
            syntax_valid_ = false;
            return false;
        }
        ++position_; // '['
        value.kind = JsonKind::Array;
        skipWhitespace();
        if (position_ < input_.size() && input_[position_] == ']') {
            ++position_;
            return true;
        }
        while (position_ < input_.size()) {
            JsonValue item;
            if (!parseValue(item, depth + 1, false)) {
                return false;
            }
            skipWhitespace();
            if (position_ >= input_.size()) {
                syntaxError(ParseDiagnosticCode::InvalidJson,
                            "JSON array is not terminated", position_);
                return false;
            }
            if (input_[position_] == ']') {
                ++position_;
                return true;
            }
            if (input_[position_] != ',') {
                syntaxError(ParseDiagnosticCode::InvalidJson,
                            "JSON array item must be followed by ',' or ']'",
                            position_);
                return false;
            }
            ++position_;
            skipWhitespace();
            if (position_ < input_.size() && input_[position_] == ']') {
                syntaxError(ParseDiagnosticCode::InvalidJson,
                            "JSON array has a trailing comma", position_);
                return false;
            }
        }
        syntaxError(ParseDiagnosticCode::InvalidJson, "JSON array is not terminated",
                    position_);
        return false;
    }

    bool parseString(std::string& output, std::size_t start, const std::string& field) {
        const JsonStringResult result =
            parseJsonString(input_, position_, kMaxJsonStringBytes, start, field);
        output = result.value;
        if (!result.valid) {
            syntaxError(result.diagnostic.code, result.diagnostic.message.c_str(),
                        result.diagnostic.offset, field);
            return false;
        }
        if (result.limited) {
            limitError(result.diagnostic.offset, field,
                       result.diagnostic.message.c_str());
        }
        return true;
    }

    bool parseLiteral(std::string_view literal) {
        if (literal.size() > input_.size() - position_
            || input_.substr(position_, literal.size()) != literal) {
            syntaxError(ParseDiagnosticCode::InvalidJson, "invalid JSON literal", position_);
            return false;
        }
        position_ += literal.size();
        return true;
    }

    bool parseNumberInteger(std::size_t start) {
        if (input_[position_] == '-') {
            ++position_;
            if (position_ >= input_.size()) {
                syntaxError(ParseDiagnosticCode::InvalidJson, "JSON number is incomplete",
                            start);
                return false;
            }
        }
        if (input_[position_] == '0') {
            ++position_;
            if (position_ < input_.size() && isDigit(input_[position_])) {
                syntaxError(ParseDiagnosticCode::InvalidJson,
                            "JSON number has a leading zero", start);
                return false;
            }
            return true;
        }
        if (input_[position_] < '1' || input_[position_] > '9') {
            syntaxError(ParseDiagnosticCode::InvalidJson, "JSON number is invalid", start);
            return false;
        }
        while (position_ < input_.size() && isDigit(input_[position_])) {
            ++position_;
        }
        return true;
    }

    bool parseNumberFraction(std::size_t start) {
        if (position_ >= input_.size() || input_[position_] != '.') {
            return true;
        }
        ++position_;
        const std::size_t fraction = position_;
        while (position_ < input_.size() && isDigit(input_[position_])) {
            ++position_;
        }
        if (fraction == position_) {
            syntaxError(ParseDiagnosticCode::InvalidJson,
                        "JSON number fraction is missing digits", start);
            return false;
        }
        return true;
    }

    bool parseNumberExponent(std::size_t start) {
        if (position_ >= input_.size()
            || (input_[position_] != 'e' && input_[position_] != 'E')) {
            return true;
        }
        ++position_;
        if (position_ < input_.size()
            && (input_[position_] == '+' || input_[position_] == '-')) {
            ++position_;
        }
        const std::size_t exponent = position_;
        while (position_ < input_.size() && isDigit(input_[position_])) {
            ++position_;
        }
        if (exponent == position_) {
            syntaxError(ParseDiagnosticCode::InvalidJson,
                        "JSON number exponent is missing digits", start);
            return false;
        }
        return true;
    }

    bool parseNumber(std::string& output) {
        const std::size_t start = position_;
        if (!parseNumberInteger(start) || !parseNumberFraction(start)
            || !parseNumberExponent(start)) {
            return false;
        }
        output.assign(input_.substr(start, position_ - start));
        return true;
    }

    std::string_view input_;
    std::size_t position_ = 0;
    std::size_t memberCount_ = 0;
    bool syntax_valid_ = true;
    bool member_limit_reported_ = false;
    bool diagnostic_limit_reported_ = false;
    std::vector<JsonMember> root_members_;
    std::vector<ParseDiagnostic> diagnostics_;
};

const JsonMember* findJsonField(const std::vector<JsonMember>& members, const char* name) {
    for (const JsonMember& member : members) {
        if (member.name == name) {
            return &member;
        }
    }
    return nullptr;
}

} // namespace

LogRecord parseJsonLine(const std::string& line) {
    LogRecord record;
    if (line.size() > kMaxRecordBytes) {
        record.parse_status = ParseStatus::Invalid;
        addDiagnostic(record, ParseDiagnosticCode::LimitExceeded, {}, 0,
                      "JSON line exceeds the supported record byte limit");
        record.message = line;
        return record;
    }

    JsonReader reader(line);
    if (!reader.parse()) {
        record.parse_status = ParseStatus::Invalid;
        record.diagnostics = reader.diagnostics();
        record.message = line;
        return record;
    }
    record.diagnostics = reader.diagnostics();
    record.parse_status = record.diagnostics.empty() ? ParseStatus::Parsed
                                                     : ParseStatus::Partial;
    const std::vector<JsonMember>& fields = reader.rootMembers();

    const JsonMember* timestamp = findJsonField(fields, "ts");
    if (timestamp == nullptr) {
        addDiagnostic(record, ParseDiagnosticCode::MissingField, "ts", 0,
                      "JSON record has no timestamp field");
    } else if (timestamp->value.kind != JsonKind::String) {
        addDiagnostic(record, ParseDiagnosticCode::InvalidFieldType, "ts", timestamp->offset,
                      "JSON timestamp field must be a string");
    } else {
        const TimestampResult parsed = parseIsoTimestamp(timestamp->value.text);
        if (parsed.valid) {
            record.timestamp_ms = parsed.value;
        } else {
            addDiagnostic(record, parsed.code, "ts", timestamp->offset, parsed.message);
        }
    }

    const JsonMember* level = findJsonField(fields, "level");
    if (level == nullptr) {
        addDiagnostic(record, ParseDiagnosticCode::MissingField, "level", 0,
                      "JSON record has no level field");
    } else if (level->value.kind != JsonKind::String) {
        addDiagnostic(record, ParseDiagnosticCode::InvalidFieldType, "level", level->offset,
                      "JSON level field must be a string");
    } else {
        record.level = parseLevel(level->value.text);
        if (record.level == Level::Unknown) {
            addDiagnostic(record, ParseDiagnosticCode::InvalidField, "level", level->offset,
                          "JSON level field is not a recognized level");
        }
    }

    const JsonMember* logger = findJsonField(fields, "logger");
    if (logger != nullptr) {
        if (logger->value.kind != JsonKind::String) {
            addDiagnostic(record, ParseDiagnosticCode::InvalidFieldType, "logger",
                          logger->offset, "JSON logger field must be a string");
        } else {
            record.source = logger->value.text;
        }
    }

    const JsonMember* message = findJsonField(fields, "msg");
    if (message == nullptr) {
        message = findJsonField(fields, "message");
    }
    if (message == nullptr) {
        addDiagnostic(record, ParseDiagnosticCode::MissingField, "msg", 0,
                      "JSON record has no msg or message field");
    } else if (message->value.kind != JsonKind::String) {
        addDiagnostic(record, ParseDiagnosticCode::InvalidFieldType, "msg", message->offset,
                      "JSON message field must be a string");
        record.message = message->value.text;
    } else {
        record.message = message->value.text;
    }
    return record;
}

} // namespace loglens::detail
