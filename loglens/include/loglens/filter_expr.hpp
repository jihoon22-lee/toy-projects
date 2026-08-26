#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "loglens/log_record.hpp"

namespace loglens {

struct ParseError {
    std::size_t position = 0;
    std::string message;
};

// Maximum expression nesting accepted, so a pathological input cannot
// overflow the recursive-descent parser's stack.
constexpr int kMaxFilterDepth = 64;

// A predicate over records, parsed from a small expression language:
//
//   expr      := term { "OR" term }
//   term      := factor { "AND" factor }
//   factor    := "NOT" factor | "(" expr ")" | predicate
//   predicate := "level" (">=" | "==") LEVELNAME
//              | "source" ("==" | "~") STRING
//              | "message" ("~" | "!~") STRING
//
// "~" is a case-insensitive substring match, deliberately not a regex:
// std::regex is slow and throws, and neither is wanted on a hot log path.
class Filter {
public:
    // Returns std::nullopt with error populated on malformed input. Never throws.
    static std::optional<Filter> parse(const std::string& text, ParseError& error);

    bool matches(const LogRecord& record) const;

    // Public so the implementation file's parser and evaluator can name it.
    // The type itself stays opaque: it is declared only in filter_expr.cpp.
    struct Node;
    using NodePtr = std::shared_ptr<const Node>;

private:
    explicit Filter(NodePtr root);
    NodePtr root_;
};

} // namespace loglens
