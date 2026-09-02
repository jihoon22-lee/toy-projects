#include "loglens/filter_expr.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>

namespace loglens {

namespace {

enum class Op : std::uint8_t { And, Or, Not, LevelAtLeast, LevelEquals, SourceEquals,
                               SourceContains, MessageContains, MessageExcludes };

bool isAsciiSpace(unsigned char byte) {
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' || byte == '\f'
           || byte == '\v';
}

bool isWordByte(unsigned char byte) {
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')
           || (byte >= '0' && byte <= '9') || byte == '_' || byte == '-' || byte == '.';
}

char toUpperAscii(char c) {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

std::string toLowerAscii(const std::string& text) {
    std::string lower;
    lower.reserve(text.size());
    for (char c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
        lower += c;
    }
    return lower;
}

bool containsInsensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    return toLowerAscii(haystack).find(toLowerAscii(needle)) != std::string::npos;
}

} // namespace

struct Filter::Node {
    Op op = Op::And;
    std::vector<NodePtr> children;
    Level level = Level::Unknown;
    std::string text;
};

namespace {

using Node = Filter::Node;

struct TokenRange {
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct PredicateTokens {
    std::string field;
    std::string op;
    std::string value;
    TokenRange fieldRange;
    TokenRange opRange;
    TokenRange valueRange;
};

struct TextPredicateSpec {
    const char* firstOperator;
    const char* secondOperator;
    const char* unsupportedMessage;
    Op firstOp;
    Op secondOp;
};

// Evaluates one leaf predicate. Split out so eval() stays shallow.
bool evalPredicate(const Node& node, const LogRecord& record) {
    switch (node.op) {
        case Op::LevelAtLeast: return levelAtLeast(record.level, node.level);
        case Op::LevelEquals: return record.level == node.level;
        case Op::SourceEquals: return record.source == node.text;
        case Op::SourceContains: return containsInsensitive(record.source, node.text);
        case Op::MessageContains: return containsInsensitive(record.message, node.text);
        case Op::MessageExcludes: return !containsInsensitive(record.message, node.text);
        default: return false;
    }
}

bool eval(const Node& node, const LogRecord& record) {
    if (node.op == Op::Not) {
        return node.children.empty() || !eval(*node.children.front(), record);
    }
    if (node.op != Op::And && node.op != Op::Or) {
        return evalPredicate(node, record);
    }
    const bool isAnd = node.op == Op::And;
    for (const auto& child : node.children) {
        // Short-circuits: AND stops at the first false, OR at the first true.
        if (eval(*child, record) != isAnd) {
            return !isAnd;
        }
    }
    return isAnd;
}

} // namespace

namespace {

class ExprParser {
public:
    ExprParser(const std::string& text, ParseError& error) : text_(text), error_(error) {}

    std::shared_ptr<const Node> run() {
        auto root = parseExpr(0);
        if (!root) {
            return nullptr;
        }
        skipSpaces();
        if (pos_ != text_.size()) {
            // The complete remaining suffix is reported so a pasted query
            // with more than one unknown token has one stable diagnostic.
            return failAt(pos_, text_.size(), "unexpected trailing input");
        }
        return root;
    }

private:
    const std::string& text_;
    ParseError& error_;
    std::size_t pos_ = 0;
    std::size_t node_count_ = 0;

    bool atEnd() const { return pos_ >= text_.size(); }

    void skipSpaces() {
        while (!atEnd() && isAsciiSpace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    std::size_t tokenEnd(std::size_t begin) const {
        std::size_t end = std::min(begin, text_.size());
        while (end < text_.size()
               && !isAsciiSpace(static_cast<unsigned char>(text_[end]))) {
            ++end;
        }
        return end;
    }

    std::size_t utf8ScalarEnd(std::size_t begin) const {
        if (begin >= text_.size()) {
            return begin;
        }
        const unsigned char lead = static_cast<unsigned char>(text_[begin]);
        std::size_t width = 1;
        if (lead >= 0xc2U && lead <= 0xdfU) {
            width = 2;
        } else if (lead >= 0xe0U && lead <= 0xefU) {
            width = 3;
        } else if (lead >= 0xf0U && lead <= 0xf4U) {
            width = 4;
        }
        if (begin + width > text_.size()) {
            return begin + 1;
        }
        for (std::size_t offset = 1; offset < width; ++offset) {
            const unsigned char continuation =
                static_cast<unsigned char>(text_[begin + offset]);
            if ((continuation & 0xc0U) != 0x80U) {
                return begin + 1;
            }
        }
        return begin + width;
    }

    std::nullptr_t failAt(std::size_t begin, std::size_t end, const std::string& message) {
        if (error_.message.empty()) {
            const std::size_t clampedBegin = std::min(begin, text_.size());
            const std::size_t clampedEnd = std::min(std::max(end, clampedBegin), text_.size());
            error_.position = clampedBegin;
            error_.end = clampedEnd;
            error_.message = message;
        }
        return nullptr;
    }

    std::shared_ptr<Node> makeNode(std::size_t begin, std::size_t end) {
        if (node_count_ >= kMaxFilterNodes) {
            failAt(begin, end, "filter AST exceeds 256-node limit");
            return nullptr;
        }
        ++node_count_;
        return std::make_shared<Node>();
    }

    // Consumes an identifier-ish word: letters, digits, and a few operators'
    // characters are handled separately by consumeOperator.
    std::string peekWord() const {
        std::size_t i = pos_;
        while (i < text_.size() && isWordByte(static_cast<unsigned char>(text_[i]))) {
            ++i;
        }
        return text_.substr(pos_, i - pos_);
    }

    bool consumeKeyword(const char* keyword, std::size_t* begin = nullptr,
                        std::size_t* end = nullptr) {
        skipSpaces();
        const std::size_t candidateBegin = pos_;
        const std::string word = peekWord();
        std::string upper;
        upper.reserve(word.size());
        for (char c : word) {
            upper += toUpperAscii(c);
        }
        if (upper != keyword) {
            return false;
        }
        pos_ += word.size();
        if (begin != nullptr) {
            *begin = candidateBegin;
        }
        if (end != nullptr) {
            *end = pos_;
        }
        return true;
    }

    bool consumeChar(char c) {
        skipSpaces();
        if (atEnd() || text_[pos_] != c) {
            return false;
        }
        ++pos_;
        return true;
    }

    // Returns the operator token at the cursor, or an empty string.
    std::string consumeOperator(std::size_t& begin, std::size_t& end) {
        skipSpaces();
        begin = pos_;
        static const char* const kOperators[] = {">=", "==", "!~", "~"};
        for (const char* candidate : kOperators) {
            const std::size_t length = std::string(candidate).size();
            if (text_.compare(pos_, length, candidate) == 0) {
                pos_ += length;
                end = pos_;
                return candidate;
            }
        }
        end = begin;
        return std::string();
    }

    // A value is either a quoted string or a bare word. Quoted strings decode
    // only quote/backslash escapes; every other byte, including UTF-8 bytes,
    // is copied unchanged.
    bool consumeValue(std::string& out, std::size_t& begin, std::size_t& end) {
        skipSpaces();
        begin = pos_;
        end = pos_;
        if (atEnd()) {
            return false;
        }
        if (text_[pos_] != '"') {
            out = peekWord();
            pos_ += out.size();
            end = pos_;
            if (out.empty()) {
                return false;
            }
            if (out.size() > kMaxFilterLiteralBytes) {
                failAt(begin, end, "filter literal exceeds 1024-byte limit");
                return false;
            }
            return true;
        }

        const std::size_t quoteBegin = pos_;
        ++pos_; // opening quote
        out.clear();
        out.reserve(std::min(kMaxFilterLiteralBytes, text_.size() - pos_));
        while (!atEnd()) {
            const unsigned char byte = static_cast<unsigned char>(text_[pos_]);
            if (byte == '"') {
                ++pos_;
                end = pos_;
                return true;
            }
            if (byte == '\\') {
                const std::size_t escapeBegin = pos_;
                ++pos_;
                if (atEnd()) {
                    failAt(escapeBegin, pos_, "unterminated escape sequence");
                    return false;
                }
                const char escaped = text_[pos_];
                if (escaped != '"' && escaped != '\\') {
                    failAt(escapeBegin, utf8ScalarEnd(pos_),
                           "unsupported escape sequence (only \\\" and \\\\ are allowed)");
                    return false;
                }
                out.push_back(escaped);
                ++pos_;
            } else {
                // Copy the original char object so bytes >= 0x80 retain
                // their exact representation in the decoded literal.
                out.push_back(text_[pos_]);
                ++pos_;
            }
            if (out.size() > kMaxFilterLiteralBytes) {
                failAt(quoteBegin, pos_, "filter literal exceeds 1024-byte limit");
                return false;
            }
        }
        failAt(quoteBegin, text_.size(), "unterminated quoted value");
        return false;
    }

    std::shared_ptr<const Node> parseExpr(int depth) {
        return parseBinary(depth, "OR", Op::Or,
                           [this](int d) { return parseTerm(d); });
    }

    std::shared_ptr<const Node> parseTerm(int depth) {
        return parseBinary(depth, "AND", Op::And,
                           [this](int d) { return parseFactor(d); });
    }

    // Shared shape for the two left-associative binary levels. The internal
    // node is charged when its operator is consumed, making node-limit errors
    // point at the operator rather than an arbitrary later token.
    template <typename ParseOperand>
    std::shared_ptr<const Node> parseBinary(int depth, const char* keyword, Op op,
                                            ParseOperand parseOperand) {
        if (depth > kMaxFilterDepth) {
            skipSpaces();
            return failAt(pos_, tokenEnd(pos_), "expression nested too deeply");
        }
        auto first = parseOperand(depth);
        if (!first) {
            return nullptr;
        }
        std::shared_ptr<Node> combined;
        while (true) {
            std::size_t keywordBegin = pos_;
            std::size_t keywordEnd = pos_;
            if (!consumeKeyword(keyword, &keywordBegin, &keywordEnd)) {
                break;
            }
            if (!combined) {
                combined = makeNode(keywordBegin, keywordEnd);
                if (!combined) {
                    return nullptr;
                }
                combined->op = op;
                combined->children.push_back(first);
            }
            auto next = parseOperand(depth);
            if (!next) {
                return nullptr;
            }
            combined->children.push_back(next);
        }
        return combined ? std::shared_ptr<const Node>(std::move(combined)) : first;
    }

    std::shared_ptr<const Node> parseFactor(int depth) {
        if (depth > kMaxFilterDepth) {
            skipSpaces();
            return failAt(pos_, tokenEnd(pos_), "expression nested too deeply");
        }
        std::size_t notBegin = pos_;
        std::size_t notEnd = pos_;
        if (consumeKeyword("NOT", &notBegin, &notEnd)) {
            if (depth >= kMaxFilterDepth) {
                return failAt(notBegin, notEnd, "expression nested too deeply");
            }
            auto node = makeNode(notBegin, notEnd);
            if (!node) {
                return nullptr;
            }
            node->op = Op::Not;
            auto inner = parseFactor(depth + 1);
            if (!inner) {
                return nullptr;
            }
            node->children.push_back(inner);
            return node;
        }
        skipSpaces();
        const std::size_t parenthesisBegin = pos_;
        if (consumeChar('(')) {
            if (depth >= kMaxFilterDepth) {
                return failAt(parenthesisBegin, parenthesisBegin + 1,
                              "expression nested too deeply");
            }
            auto inner = parseExpr(depth + 1);
            if (!inner) {
                return nullptr;
            }
            if (consumeChar(')')) {
                return inner;
            }
            skipSpaces();
            return failAt(pos_, tokenEnd(pos_), "expected ')'");
        }
        return parsePredicate();
    }

    std::shared_ptr<const Node> parsePredicate() {
        skipSpaces();
        PredicateTokens tokens;
        tokens.fieldRange.begin = pos_;
        tokens.field = peekWord();
        if (tokens.field.empty()) {
            return failAt(tokens.fieldRange.begin, tokenEnd(tokens.fieldRange.begin),
                          "expected a predicate");
        }
        pos_ += tokens.field.size();
        tokens.fieldRange.end = pos_;

        tokens.opRange.begin = pos_;
        tokens.opRange.end = pos_;
        tokens.op = consumeOperator(tokens.opRange.begin, tokens.opRange.end);
        if (tokens.op.empty()) {
            return failAt(tokens.opRange.begin, tokenEnd(tokens.opRange.begin),
                          "expected an operator after '" + tokens.field + "'");
        }

        tokens.valueRange.begin = pos_;
        tokens.valueRange.end = pos_;
        if (!consumeValue(tokens.value, tokens.valueRange.begin, tokens.valueRange.end)) {
            if (!error_.message.empty()) {
                return nullptr;
            }
            return failAt(tokens.valueRange.begin, tokenEnd(tokens.valueRange.begin),
                          "expected a value after '" + tokens.op + "'");
        }
        return makePredicate(tokens);
    }

    std::shared_ptr<const Node> makePredicate(const PredicateTokens& tokens) {
        if (tokens.field == "level") {
            return makeLevelPredicate(tokens);
        }
        if (tokens.field == "source") {
            static const TextPredicateSpec sourceSpec{"==", "~",
                                                      "source supports '==' or '~'",
                                                      Op::SourceEquals, Op::SourceContains};
            return makeTextPredicate(tokens, sourceSpec);
        }
        if (tokens.field != "message") {
            return failAt(tokens.fieldRange.begin, tokens.fieldRange.end,
                          "unknown field '" + tokens.field + "'");
        }
        static const TextPredicateSpec messageSpec{"!~", "~",
                                                   "message supports '~' or '!~'",
                                                   Op::MessageExcludes, Op::MessageContains};
        return makeTextPredicate(tokens, messageSpec);
    }

    std::shared_ptr<const Node> makeLevelPredicate(const PredicateTokens& tokens) {
        const Level level = parseLevel(tokens.value);
        if (level == Level::Unknown) {
            return failAt(tokens.valueRange.begin, tokens.valueRange.end,
                          "unknown level '" + tokens.value + "'");
        }
        if (tokens.op != ">=" && tokens.op != "==") {
            return failAt(tokens.opRange.begin, tokens.opRange.end,
                          "level supports '>=' or '=='");
        }
        auto node = makeNode(tokens.fieldRange.begin, tokens.valueRange.end);
        if (!node) {
            return nullptr;
        }
        node->op = tokens.op == ">=" ? Op::LevelAtLeast : Op::LevelEquals;
        node->level = level;
        return node;
    }

    std::shared_ptr<const Node> makeTextPredicate(const PredicateTokens& tokens,
                                                  const TextPredicateSpec& spec) {
        if (tokens.op != spec.firstOperator && tokens.op != spec.secondOperator) {
            return failAt(tokens.opRange.begin, tokens.opRange.end,
                          spec.unsupportedMessage);
        }
        auto node = makeNode(tokens.fieldRange.begin, tokens.valueRange.end);
        if (!node) {
            return nullptr;
        }
        node->op = tokens.op == spec.firstOperator ? spec.firstOp : spec.secondOp;
        node->text = tokens.value;
        return node;
    }
};

} // namespace

Filter::Filter(NodePtr root) : root_(std::move(root)) {}

std::optional<Filter> Filter::parse(const std::string& text, ParseError& error) {
    error = ParseError{};
    if (text.size() > kMaxFilterQueryBytes) {
        error.position = kMaxFilterQueryBytes;
        error.end = text.size();
        error.message = "filter query exceeds 4096-byte limit";
        return std::nullopt;
    }
    if (std::all_of(text.begin(), text.end(), [](char byte) {
            return isAsciiSpace(static_cast<unsigned char>(byte));
        })) {
        error.position = text.size();
        error.end = text.size();
        error.message = "empty filter expression";
        return std::nullopt;
    }

    ExprParser parser(text, error);
    auto root = parser.run();
    if (!root) {
        if (error.message.empty()) {
            error.position = text.size();
            error.end = text.size();
            error.message = "empty filter expression";
        }
        return std::nullopt;
    }
    return Filter(root);
}

bool Filter::matches(const LogRecord& record) const {
    return root_ ? eval(*root_, record) : true;
}

} // namespace loglens
