#include "storage_json.hpp"

#include "json_string_parser.hpp"

#include <cstdint>
#include <utility>

namespace loglens::detail {

namespace {

bool isDigit(char value) { return value >= '0' && value <= '9'; }

class Reader {
public:
    Reader(std::string_view input, const StorageJsonLimits& limits)
        : input_(input), limits_(limits) {}

    bool parse(StorageJsonNode& root, StorageJsonError& error) {
        error_ = &error;
        skipWhitespace();
        if (!parseValue(root, 0)) {
            return false;
        }
        if (root.kind != StorageJsonKind::Object) {
            return fail("document root must be an object", 0);
        }
        skipWhitespace();
        if (position_ != input_.size()) {
            return fail("trailing data after document root", position_);
        }
        return true;
    }

private:
    bool fail(const char* message, std::size_t offset) {
        if (error_ != nullptr && error_->message.empty()) {
            error_->offset = offset;
            error_->message = message;
        }
        return false;
    }

    void skipWhitespace() {
        while (position_ < input_.size()) {
            const char value = input_[position_];
            if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
                break;
            }
            ++position_;
        }
    }

    bool countNode() {
        if (node_count_ >= limits_.max_nodes) {
            return fail("JSON node limit exceeded", position_);
        }
        ++node_count_;
        return true;
    }

    bool parseValue(StorageJsonNode& node, std::size_t depth) {
        skipWhitespace();
        if (position_ >= input_.size()) {
            return fail("JSON value is missing", position_);
        }
        if (!countNode()) {
            return false;
        }
        const char first = input_[position_];
        if (first == '"') {
            node.kind = StorageJsonKind::String;
            return parseString(node.text);
        }
        if (first == '{') {
            node.kind = StorageJsonKind::Object;
            return parseObject(node, depth);
        }
        if (first == '[') {
            node.kind = StorageJsonKind::Array;
            return parseArray(node, depth);
        }
        if (first == 't') {
            node.kind = StorageJsonKind::Boolean;
            if (!parseLiteral("true")) return false;
            node.text = "true";
            return true;
        }
        if (first == 'f') {
            node.kind = StorageJsonKind::Boolean;
            if (!parseLiteral("false")) return false;
            node.text = "false";
            return true;
        }
        if (first == 'n') {
            node.kind = StorageJsonKind::Null;
            return parseLiteral("null");
        }
        if (first == '-' || isDigit(first)) {
            node.kind = StorageJsonKind::Number;
            return parseNumber(node.text);
        }
        return fail("invalid JSON value", position_);
    }

    bool parseObject(StorageJsonNode& node, std::size_t depth) {
        if (depth > limits_.max_depth) {
            return fail("JSON nesting depth limit exceeded", position_);
        }
        ++position_; // '{'
        skipWhitespace();
        if (position_ < input_.size() && input_[position_] == '}') {
            ++position_;
            return true;
        }
        while (position_ < input_.size()) {
            if (!parseObjectMember(node, depth)) {
                return false;
            }
            skipWhitespace();
            if (position_ >= input_.size()) {
                return fail("JSON object is not terminated", position_);
            }
            if (input_[position_] == '}') {
                ++position_;
                return true;
            }
            if (input_[position_] != ',') {
                return fail("JSON object member must be followed by ',' or '}'",
                            position_);
            }
            ++position_;
            skipWhitespace();
            if (position_ < input_.size() && input_[position_] == '}') {
                return fail("JSON object has a trailing comma", position_);
            }
        }
        return fail("JSON object is not terminated", position_);
    }

    bool parseObjectMember(StorageJsonNode& node, std::size_t depth) {
        if (node.object.size() >= limits_.max_object_members) {
            return fail("JSON object member limit exceeded", position_);
        }
        std::string key;
        const std::size_t keyOffset = position_;
        if (!parseString(key)) {
            return false;
        }
        for (const auto& member : node.object) {
            if (member.first == key) {
                return fail("duplicate JSON object key", keyOffset);
            }
        }
        skipWhitespace();
        if (position_ >= input_.size() || input_[position_] != ':') {
            return fail("JSON object member is missing ':'", position_);
        }
        ++position_;
        StorageJsonNode value;
        if (!parseValue(value, depth + 1)) {
            return false;
        }
        node.object.emplace_back(std::move(key), std::move(value));
        return true;
    }

    bool parseArray(StorageJsonNode& node, std::size_t depth) {
        if (depth > limits_.max_depth) {
            return fail("JSON nesting depth limit exceeded", position_);
        }
        ++position_; // '['
        skipWhitespace();
        if (position_ < input_.size() && input_[position_] == ']') {
            ++position_;
            return true;
        }
        while (position_ < input_.size()) {
            if (node.array.size() >= limits_.max_array_items) {
                return fail("JSON array item limit exceeded", position_);
            }
            StorageJsonNode item;
            if (!parseValue(item, depth + 1)) {
                return false;
            }
            node.array.push_back(std::move(item));
            skipWhitespace();
            if (position_ >= input_.size()) {
                return fail("JSON array is not terminated", position_);
            }
            if (input_[position_] == ']') {
                ++position_;
                return true;
            }
            if (input_[position_] != ',') {
                return fail("JSON array item must be followed by ',' or ']'",
                            position_);
            }
            ++position_;
            skipWhitespace();
            if (position_ < input_.size() && input_[position_] == ']') {
                return fail("JSON array has a trailing comma", position_);
            }
        }
        return fail("JSON array is not terminated", position_);
    }

    bool parseString(std::string& output) {
        const JsonStringResult result =
            parseJsonString(input_, position_, limits_.max_string_bytes, position_, {});
        output = result.value;
        if (!result.valid) {
            return fail(result.diagnostic.message.c_str(), result.diagnostic.offset);
        }
        if (result.limited) {
            return fail("JSON string byte limit exceeded", result.diagnostic.offset);
        }
        return true;
    }

    bool parseLiteral(std::string_view literal) {
        if (literal.size() > input_.size() - position_
            || input_.substr(position_, literal.size()) != literal) {
            return fail("invalid JSON literal", position_);
        }
        position_ += literal.size();
        return true;
    }

    bool parseNumberInteger(std::size_t start) {
        if (input_[position_] == '-') {
            ++position_;
            if (position_ >= input_.size()) {
                return fail("JSON number is incomplete", start);
            }
        }
        if (input_[position_] == '0') {
            ++position_;
            if (position_ < input_.size() && isDigit(input_[position_])) {
                return fail("JSON number has a leading zero", start);
            }
            return true;
        }
        if (input_[position_] < '1' || input_[position_] > '9') {
            return fail("JSON number is invalid", start);
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
            return fail("JSON number fraction is missing digits", start);
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
            return fail("JSON number exponent is missing digits", start);
        }
        return true;
    }

    bool parseNumber(std::string& output) {
        const std::size_t start = position_;
        if (!parseNumberInteger(start) || !parseNumberFraction(start)
            || !parseNumberExponent(start)) {
            return false;
        }
        if (position_ - start > limits_.max_string_bytes) {
            return fail("JSON number byte limit exceeded", start);
        }
        output.assign(input_.substr(start, position_ - start));
        return true;
    }

    std::string_view input_;
    const StorageJsonLimits& limits_;
    StorageJsonError* error_ = nullptr;
    std::size_t position_ = 0;
    std::size_t node_count_ = 0;
};

} // namespace

bool parseStorageJson(std::string_view input, const StorageJsonLimits& limits,
                      StorageJsonNode& root, StorageJsonError& error) {
    root = StorageJsonNode{};
    error = StorageJsonError{};
    if (limits.max_depth == 0 || limits.max_nodes == 0 || limits.max_string_bytes == 0
        || limits.max_object_members == 0 || limits.max_array_items == 0) {
        error.message = "JSON parser limits must be positive";
        return false;
    }
    Reader reader(input, limits);
    return reader.parse(root, error);
}

const StorageJsonNode* findStorageJsonField(const StorageJsonNode& object,
                                            std::string_view name) {
    if (object.kind != StorageJsonKind::Object) {
        return nullptr;
    }
    for (const auto& member : object.object) {
        if (member.first == name) {
            return &member.second;
        }
    }
    return nullptr;
}

} // namespace loglens::detail
