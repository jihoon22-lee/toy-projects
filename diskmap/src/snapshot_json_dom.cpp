#include "snapshot_json_dom.hpp"

#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace diskmap {
namespace detail {
namespace {

constexpr std::size_t kParserDepthOverhead = 32;

class JsonParser final {
public:
    JsonParser(std::string_view input, const SnapshotLimits& limits)
        : input_(input), limits_(limits), max_depth_(limits.max_depth * 3U + kParserDepthOverhead) {}

    JsonValue parse() {
        JsonValue value = parseValue(0);
        skipWhitespace();
        if (position_ != input_.size()) {
            fail("trailing data after JSON value");
        }
        return value;
    }

private:
    [[noreturn]] void fail(const char* message) const { throw SnapshotError(message); }

    void skipWhitespace() {
        while (position_ < input_.size()) {
            const char character = input_[position_];
            if (character != ' ' && character != '\n' && character != '\r' && character != '\t') {
                return;
            }
            ++position_;
        }
    }

    void expect(char expected) {
        if (position_ >= input_.size() || input_[position_] != expected) {
            fail("malformed JSON punctuation");
        }
        ++position_;
    }

    bool consume(char expected) {
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    JsonValue parseValue(std::size_t depth) {
        if (depth > max_depth_) {
            fail("JSON nesting exceeds configured bound");
        }
        skipWhitespace();
        if (position_ >= input_.size()) {
            fail("unexpected end of JSON");
        }
        switch (input_[position_]) {
        case '{':
            return parseObject(depth + 1);
        case '[':
            return parseArray(depth + 1);
        case '"': {
            JsonValue value;
            value.kind = JsonKind::String;
            value.string_value = parseString();
            return value;
        }
        case 't':
            consumeLiteral("true");
            {
                JsonValue value;
                value.kind = JsonKind::Boolean;
                value.boolean = true;
                return value;
            }
        case 'f':
            consumeLiteral("false");
            {
                JsonValue value;
                value.kind = JsonKind::Boolean;
                value.boolean = false;
                return value;
            }
        case 'n':
            consumeLiteral("null");
            return JsonValue{};
        default:
            if (input_[position_] == '-' || std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                return parseNumber();
            }
            fail("unexpected JSON value");
        }
    }

    JsonValue parseObject(std::size_t depth) {
        expect('{');
        JsonValue value;
        value.kind = JsonKind::Object;
        skipWhitespace();
        if (consume('}')) {
            return value;
        }
        while (true) {
            skipWhitespace();
            if (position_ >= input_.size() || input_[position_] != '"') {
                fail("JSON object key must be a string");
            }
            const std::string key = parseString();
            if (value.object_value.find(key) != value.object_value.end()) {
                fail("duplicate JSON object key");
            }
            skipWhitespace();
            expect(':');
            value.object_value.emplace(key, parseValue(depth));
            skipWhitespace();
            if (consume('}')) {
                return value;
            }
            expect(',');
        }
    }

    JsonValue parseArray(std::size_t depth) {
        expect('[');
        JsonValue value;
        value.kind = JsonKind::Array;
        skipWhitespace();
        if (consume(']')) {
            return value;
        }
        while (true) {
            value.array_value.push_back(parseValue(depth));
            skipWhitespace();
            if (consume(']')) {
                return value;
            }
            expect(',');
        }
    }

    static int hexValue(char character) {
        if (character >= '0' && character <= '9') {
            return character - '0';
        }
        if (character >= 'a' && character <= 'f') {
            return character - 'a' + 10;
        }
        if (character >= 'A' && character <= 'F') {
            return character - 'A' + 10;
        }
        return -1;
    }

    std::uint32_t parseUnicodeEscape() {
        expect('u');
        std::uint32_t value = 0;
        for (int index = 0; index < 4; ++index) {
            if (position_ >= input_.size()) {
                fail("truncated JSON unicode escape");
            }
            const int digit = hexValue(input_[position_++]);
            if (digit < 0) {
                fail("invalid JSON unicode escape");
            }
            value = (value << 4U) | static_cast<std::uint32_t>(digit);
        }
        return value;
    }

    static void appendUtf8(std::string& output, std::uint32_t codepoint) {
        if (codepoint <= 0x7fU) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ffU) {
            output.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        } else if (codepoint <= 0xffffU) {
            output.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        } else {
            output.push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        }
    }

    void appendUnicodeEscape(std::string& value) {
        --position_;
        std::uint32_t codepoint = parseUnicodeEscape();
        if (codepoint >= 0xd800U && codepoint <= 0xdbffU) {
            if (position_ + 6 > input_.size() || input_[position_] != '\\'
                || input_[position_ + 1] != 'u') {
                fail("unpaired JSON high surrogate");
            }
            ++position_;
            const std::uint32_t low = parseUnicodeEscape();
            if (low < 0xdc00U || low > 0xdfffU) {
                fail("invalid JSON surrogate pair");
            }
            codepoint = 0x10000U + ((codepoint - 0xd800U) << 10U)
                        + (low - 0xdc00U);
        } else if (codepoint >= 0xdc00U && codepoint <= 0xdfffU) {
            fail("unpaired JSON low surrogate");
        }
        appendUtf8(value, codepoint);
    }

    void appendEscape(std::string& value) {
        if (position_ >= input_.size()) {
            fail("truncated JSON escape");
        }
        const char escaped = input_[position_++];
        switch (escaped) {
        case '"':
            value.push_back('"');
            break;
        case '\\':
            value.push_back('\\');
            break;
        case '/':
            value.push_back('/');
            break;
        case 'b':
            value.push_back('\b');
            break;
        case 'f':
            value.push_back('\f');
            break;
        case 'n':
            value.push_back('\n');
            break;
        case 'r':
            value.push_back('\r');
            break;
        case 't':
            value.push_back('\t');
            break;
        case 'u':
            appendUnicodeEscape(value);
            break;
        default:
            fail("invalid JSON escape");
        }
    }

    std::string parseString() {
        expect('"');
        std::string value;
        while (position_ < input_.size()) {
            const unsigned char character = static_cast<unsigned char>(input_[position_++]);
            if (character == '"') {
                if (!isValidUtf8(value)) {
                    fail("JSON string is not valid UTF-8");
                }
                if (value.size() > limits_.max_string_bytes) {
                    fail("JSON string exceeds configured bound");
                }
                return value;
            }
            if (character < 0x20U) {
                fail("unescaped JSON control character");
            }
            if (character != '\\') {
                value.push_back(static_cast<char>(character));
                continue;
            }
            appendEscape(value);
            if (value.size() > limits_.max_string_bytes) {
                fail("JSON string exceeds configured bound");
            }
        }
        fail("unterminated JSON string");
    }

    void consumeLiteral(const char* literal) {
        for (const char* character = literal; *character != '\0'; ++character) {
            if (position_ >= input_.size() || input_[position_++] != *character) {
                fail("malformed JSON literal");
            }
        }
    }

    void consumeIntegerDigits() {
        if (position_ >= input_.size()
            || !std::isdigit(static_cast<unsigned char>(input_[position_]))) {
            fail("malformed JSON number");
        }
        if (input_[position_] == '0') {
            ++position_;
            if (position_ < input_.size()
                && std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                fail("JSON numbers may not have leading zeroes");
            }
            return;
        }
        while (position_ < input_.size()
               && std::isdigit(static_cast<unsigned char>(input_[position_]))) {
            ++position_;
        }
    }

    void rejectFractionalNumber() {
        if (position_ < input_.size()
            && (input_[position_] == '.' || input_[position_] == 'e'
                || input_[position_] == 'E')) {
            fail("fractional JSON numbers are unsupported");
        }
    }

    std::uint64_t parseMagnitude(std::size_t start) {
        std::uint64_t magnitude = 0;
        for (std::size_t index = start; index < position_; ++index) {
            const std::uint64_t digit = static_cast<std::uint64_t>(input_[index] - '0');
            if (magnitude > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
                fail("JSON integer exceeds uint64 range");
            }
            magnitude = magnitude * 10U + digit;
        }
        return magnitude;
    }

    JsonValue numberValue(bool negative, std::uint64_t magnitude) {
        JsonValue value;
        if (!negative) {
            value.kind = JsonKind::Unsigned;
            value.unsigned_value = magnitude;
            return value;
        }
        constexpr std::uint64_t minimumMagnitude =
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1U;
        if (magnitude > minimumMagnitude) {
            fail("JSON integer exceeds int64 range");
        }
        value.kind = JsonKind::Signed;
        value.signed_value = magnitude == minimumMagnitude
                                 ? std::numeric_limits<std::int64_t>::min()
                                 : -static_cast<std::int64_t>(magnitude);
        return value;
    }

    JsonValue parseNumber() {
        const std::size_t start = position_;
        const bool negative = consume('-');
        consumeIntegerDigits();
        rejectFractionalNumber();
        const std::size_t digitsStart = negative ? start + 1 : start;
        return numberValue(negative, parseMagnitude(digitsStart));
    }

    std::string_view input_;
    const SnapshotLimits& limits_;
    std::size_t position_ = 0;
    std::size_t max_depth_ = 0;
};

} // namespace

JsonValue parseJson(std::string_view input, const SnapshotLimits& limits) {
    return JsonParser(input, limits).parse();
}

} // namespace detail
} // namespace diskmap
