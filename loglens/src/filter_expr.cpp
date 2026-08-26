#include "loglens/filter_expr.hpp"

#include <algorithm>
#include <cctype>

namespace loglens {

namespace {

enum class Op { And, Or, Not, LevelAtLeast, LevelEquals, SourceEquals, SourceContains,
                MessageContains, MessageExcludes };

std::string toLowerAscii(const std::string& text) {
    std::string lower;
    lower.reserve(text.size());
    for (char c : text) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
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
            fail("unexpected trailing input");
            return nullptr;
        }
        return root;
    }

private:
    const std::string& text_;
    ParseError& error_;
    std::size_t pos_ = 0;

    bool atEnd() const { return pos_ >= text_.size(); }

    void skipSpaces() {
        while (!atEnd() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    std::nullptr_t fail(const std::string& message) {
        if (error_.message.empty()) {
            error_.position = pos_;
            error_.message = message;
        }
        return nullptr;
    }

    // Consumes an identifier-ish word: letters, digits, and a few operators'
    // characters are handled separately by consumeOperator.
    std::string peekWord() const {
        std::size_t i = pos_;
        while (i < text_.size() && (std::isalnum(static_cast<unsigned char>(text_[i])) ||
                                    text_[i] == '_' || text_[i] == '-' || text_[i] == '.')) {
            ++i;
        }
        return text_.substr(pos_, i - pos_);
    }

    bool consumeKeyword(const char* keyword) {
        skipSpaces();
        const std::string word = peekWord();
        std::string upper;
        for (char c : word) {
            upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        if (upper != keyword) {
            return false;
        }
        pos_ += word.size();
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
    std::string consumeOperator() {
        skipSpaces();
        static const char* const kOperators[] = {">=", "==", "!~", "~"};
        for (const char* candidate : kOperators) {
            const std::size_t length = std::string(candidate).size();
            if (text_.compare(pos_, length, candidate) == 0) {
                pos_ += length;
                return candidate;
            }
        }
        return std::string();
    }

    // A value is either a quoted string or a bare word.
    bool consumeValue(std::string& out) {
        skipSpaces();
        if (atEnd()) {
            return false;
        }
        if (text_[pos_] != '"') {
            out = peekWord();
            pos_ += out.size();
            return !out.empty();
        }
        const std::size_t close = text_.find('"', pos_ + 1);
        if (close == std::string::npos) {
            return false;
        }
        out = text_.substr(pos_ + 1, close - pos_ - 1);
        pos_ = close + 1;
        return true;
    }

    std::shared_ptr<const Node> parseExpr(int depth) {
        return parseBinary(depth, "OR", Op::Or,
                           [this](int d) { return parseTerm(d); });
    }

    std::shared_ptr<const Node> parseTerm(int depth) {
        return parseBinary(depth, "AND", Op::And,
                           [this](int d) { return parseFactor(d); });
    }

    // Shared shape for the two left-associative binary levels.
    template <typename ParseOperand>
    std::shared_ptr<const Node> parseBinary(int depth, const char* keyword, Op op,
                                            ParseOperand parseOperand) {
        if (depth > kMaxFilterDepth) {
            return fail("expression nested too deeply");
        }
        auto first = parseOperand(depth + 1);
        if (!first) {
            return nullptr;
        }
        std::vector<std::shared_ptr<const Node>> operands{first};
        while (consumeKeyword(keyword)) {
            auto next = parseOperand(depth + 1);
            if (!next) {
                return nullptr;
            }
            operands.push_back(next);
        }
        if (operands.size() == 1) {
            return first;
        }
        auto node = std::make_shared<Node>();
        node->op = op;
        node->children = std::move(operands);
        return node;
    }

    std::shared_ptr<const Node> parseFactor(int depth) {
        if (depth > kMaxFilterDepth) {
            return fail("expression nested too deeply");
        }
        if (consumeKeyword("NOT")) {
            auto inner = parseFactor(depth + 1);
            if (!inner) {
                return nullptr;
            }
            auto node = std::make_shared<Node>();
            node->op = Op::Not;
            node->children.push_back(inner);
            return node;
        }
        if (consumeChar('(')) {
            auto inner = parseExpr(depth + 1);
            if (!inner) {
                return nullptr;
            }
            return consumeChar(')') ? inner : fail("expected ')'");
        }
        return parsePredicate();
    }

    std::shared_ptr<const Node> parsePredicate() {
        skipSpaces();
        const std::string field = peekWord();
        if (field.empty()) {
            return fail("expected a predicate");
        }
        pos_ += field.size();
        const std::string op = consumeOperator();
        if (op.empty()) {
            return fail("expected an operator after '" + field + "'");
        }
        std::string value;
        if (!consumeValue(value)) {
            return fail("expected a value after '" + op + "'");
        }
        return makePredicate(field, op, value);
    }

    std::shared_ptr<const Node> makePredicate(const std::string& field, const std::string& op,
                                              const std::string& value) {
        auto node = std::make_shared<Node>();
        if (field == "level") {
            return makeLevelPredicate(op, value, node);
        }
        if (field == "source") {
            node->op = op == "==" ? Op::SourceEquals : Op::SourceContains;
            node->text = value;
            return (op == "==" || op == "~") ? node : fail("source supports '==' or '~'");
        }
        if (field != "message") {
            return fail("unknown field '" + field + "'");
        }
        node->op = op == "!~" ? Op::MessageExcludes : Op::MessageContains;
        node->text = value;
        return (op == "~" || op == "!~") ? node : fail("message supports '~' or '!~'");
    }

    std::shared_ptr<const Node> makeLevelPredicate(const std::string& op, const std::string& value,
                                                   std::shared_ptr<Node> node) {
        const Level level = parseLevel(value);
        if (level == Level::Unknown) {
            return fail("unknown level '" + value + "'");
        }
        if (op != ">=" && op != "==") {
            return fail("level supports '>=' or '=='");
        }
        node->op = op == ">=" ? Op::LevelAtLeast : Op::LevelEquals;
        node->level = level;
        return node;
    }
};

} // namespace

Filter::Filter(NodePtr root) : root_(std::move(root)) {}

std::optional<Filter> Filter::parse(const std::string& text, ParseError& error) {
    error = ParseError{};
    ExprParser parser(text, error);
    auto root = parser.run();
    if (!root) {
        if (error.message.empty()) {
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
