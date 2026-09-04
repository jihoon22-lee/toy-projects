#include "report_internal.hpp"

#include <cctype>
#include <stdexcept>

namespace abilens {
namespace detail {

class JsonParser {
public:
    explicit JsonParser(const std::string& input) : input_(input) {}

    JsonValue parse() {
        if (input_.size() > 8U * 1024U * 1024U) {
            throw std::runtime_error("report JSON exceeds the 8 MiB bound");
        }
        JsonValue value = parse_value();
        skip_space();
        if (position_ != input_.size()) {
            throw std::runtime_error("trailing data follows report JSON");
        }
        return value;
    }

private:
    const std::string& input_;
    std::size_t position_ = 0;
    std::size_t depth_ = 0;
    std::size_t node_count_ = 0;
    static constexpr std::size_t kMaxNestingDepth = 64U;
    static constexpr std::size_t kMaxNodes = 100000U;
    static constexpr std::size_t kMaxContainerItems = 65536U;

    void enter_container() {
        if (depth_ >= kMaxNestingDepth) {
            throw std::runtime_error("report JSON nesting exceeds the 64-level bound");
        }
        ++depth_;
    }

    void leave_container() noexcept {
        --depth_;
    }

    void skip_space() {
        while (position_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[position_])) != 0) {
            ++position_;
        }
    }

    char take() {
        if (position_ >= input_.size()) {
            throw std::runtime_error("unexpected end of report JSON");
        }
        return input_[position_++];
    }

    void expect(char expected) {
        if (take() != expected) {
            throw std::runtime_error("malformed report JSON");
        }
    }

    static int hex_value(char character) {
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

    static void append_codepoint(std::string& output, unsigned int codepoint) {
        if (codepoint <= 0x7fU) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ffU) {
            output.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        } else if (codepoint <= 0xffffU) {
            output.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        } else if (codepoint <= 0x10ffffU) {
            output.push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        }
    }

    unsigned int parse_hex_quad() {
        unsigned int value = 0;
        for (unsigned int index = 0; index < 4U; ++index) {
            const int digit = hex_value(take());
            if (digit < 0) {
                throw std::runtime_error("invalid unicode escape in report JSON");
            }
            value = (value << 4U) | static_cast<unsigned int>(digit);
        }
        return value;
    }

    std::string parse_string() {
        expect('"');
        std::string result;
        while (position_ < input_.size()) {
            const unsigned char character = static_cast<unsigned char>(take());
            if (character == '"') {
                return result;
            }
            if (character < 0x20U) {
                throw std::runtime_error("control character in report JSON string");
            }
            if (character != '\\') {
                if (character >= 0x80U) {
                    const std::size_t begin = position_ - 1U;
                    const std::size_t length = valid_utf8_sequence_length(input_, begin);
                    if (length == 0U) {
                        throw std::runtime_error("invalid UTF-8 in report JSON string");
                    }
                    result.append(input_, begin, length);
                    position_ = begin + length;
                    continue;
                }
                result.push_back(static_cast<char>(character));
                continue;
            }
            const char escaped = take();
            switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    result.push_back(escaped);
                    break;
                case 'b':
                    result.push_back('\b');
                    break;
                case 'f':
                    result.push_back('\f');
                    break;
                case 'n':
                    result.push_back('\n');
                    break;
                case 'r':
                    result.push_back('\r');
                    break;
                case 't':
                    result.push_back('\t');
                    break;
                case 'u': {
                    unsigned int codepoint = parse_hex_quad();
                    if (codepoint >= 0xd800U && codepoint <= 0xdbffU) {
                        if (take() != '\\' || take() != 'u') {
                            throw std::runtime_error(
                                "high surrogate has no low surrogate in report JSON");
                        }
                        const unsigned int low = parse_hex_quad();
                        if (low < 0xdc00U || low > 0xdfffU) {
                            throw std::runtime_error(
                                "high surrogate has an invalid pair in report JSON");
                        }
                        codepoint = 0x10000U + ((codepoint - 0xd800U) << 10U)
                                    + (low - 0xdc00U);
                    } else if (codepoint >= 0xdc00U && codepoint <= 0xdfffU) {
                        throw std::runtime_error(
                            "unpaired low surrogate in report JSON");
                    }
                    append_codepoint(result, codepoint);
                    break;
                }
                default:
                    throw std::runtime_error("unknown escape in report JSON");
            }
        }
        throw std::runtime_error("unterminated report JSON string");
    }

    JsonValue parse_number() {
        const std::size_t begin = position_;
        if (input_[position_] == '-') {
            ++position_;
        }
        if (position_ >= input_.size()) {
            throw std::runtime_error("malformed report JSON number");
        }
        if (input_[position_] == '0') {
            ++position_;
        } else {
            if (std::isdigit(static_cast<unsigned char>(input_[position_])) == 0) {
                throw std::runtime_error("malformed report JSON number");
            }
            while (position_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                ++position_;
            }
        }
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            if (position_ >= input_.size() ||
                std::isdigit(static_cast<unsigned char>(input_[position_])) == 0) {
                throw std::runtime_error("malformed report JSON number");
            }
            while (position_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                ++position_;
            }
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) {
                ++position_;
            }
            if (position_ >= input_.size() ||
                std::isdigit(static_cast<unsigned char>(input_[position_])) == 0) {
                throw std::runtime_error("malformed report JSON number");
            }
            while (position_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                ++position_;
            }
        }
        JsonValue result;
        result.kind = JsonValue::Kind::Number;
        result.scalar = input_.substr(begin, position_ - begin);
        return result;
    }

    JsonValue parse_array() {
        expect('[');
        enter_container();
        JsonValue result;
        result.kind = JsonValue::Kind::Array;
        skip_space();
        if (position_ < input_.size() && input_[position_] == ']') {
            ++position_;
            leave_container();
            return result;
        }
        for (;;) {
            if (result.array.size() >= kMaxContainerItems) {
                throw std::runtime_error("report JSON array exceeds the item bound");
            }
            result.array.push_back(parse_value());
            skip_space();
            const char separator = take();
            if (separator == ']') {
                leave_container();
                return result;
            }
            if (separator != ',') {
                throw std::runtime_error("malformed report JSON array");
            }
            skip_space();
        }
    }

    JsonValue parse_object() {
        expect('{');
        enter_container();
        JsonValue result;
        result.kind = JsonValue::Kind::Object;
        skip_space();
        if (position_ < input_.size() && input_[position_] == '}') {
            ++position_;
            leave_container();
            return result;
        }
        for (;;) {
            if (result.object.size() >= kMaxContainerItems) {
                throw std::runtime_error("report JSON object exceeds the member bound");
            }
            skip_space();
            if (position_ >= input_.size() || input_[position_] != '"') {
                throw std::runtime_error("report JSON object key is not a string");
            }
            std::string key = parse_string();
            skip_space();
            expect(':');
            skip_space();
            if (!result.object.emplace(key, parse_value()).second) {
                throw std::runtime_error("duplicate key in report JSON");
            }
            skip_space();
            const char separator = take();
            if (separator == '}') {
                leave_container();
                return result;
            }
            if (separator != ',') {
                throw std::runtime_error("malformed report JSON object");
            }
        }
    }

    JsonValue parse_value() {
        if (node_count_ >= kMaxNodes) {
            throw std::runtime_error("report JSON exceeds the node bound");
        }
        ++node_count_;
        skip_space();
        if (position_ >= input_.size()) {
            throw std::runtime_error("missing value in report JSON");
        }
        switch (input_[position_]) {
            case '{':
                return parse_object();
            case '[':
                return parse_array();
            case '"':
                return JsonValue::string_value(parse_string());
            case 't':
                if (input_.compare(position_, 4U, "true") == 0) {
                    position_ += 4U;
                    return JsonValue::boolean_value(true);
                }
                break;
            case 'f':
                if (input_.compare(position_, 5U, "false") == 0) {
                    position_ += 5U;
                    return JsonValue::boolean_value(false);
                }
                break;
            case 'n':
                if (input_.compare(position_, 4U, "null") == 0) {
                    position_ += 4U;
                    return JsonValue{};
                }
                break;
            default:
                if (input_[position_] == '-' ||
                    std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                    return parse_number();
                }
                break;
        }
        throw std::runtime_error("malformed report JSON value");
    }
};


JsonValue parse_json(const std::string& input) {
    return JsonParser(input).parse();
}

}  // namespace detail
}  // namespace abilens
