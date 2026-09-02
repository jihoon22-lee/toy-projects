#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "loglens/log_record.hpp"

namespace loglens {

struct ParseError {
    // Byte offset of the first byte in the half-open diagnostic range
    // [position, end). This remains the original public API used by the CLI.
    std::size_t position = 0;
    std::string message;
    // One-past-the-last byte in the diagnostic range. A zero-width range at
    // EOF is used for a missing token. Appending this field preserves existing
    // two-field aggregate initialization of position/message.
    std::size_t end = 0;
};

// Maximum expression nesting accepted, so a pathological input cannot
// overflow the recursive-descent parser's stack.
constexpr int kMaxFilterDepth = 64;
// Resource bounds for a single filter query. All sizes are bytes except for
// the AST node count; byte offsets in ParseError are UTF-8 input byte offsets.
constexpr std::size_t kMaxFilterQueryBytes = 4096;
constexpr std::size_t kMaxFilterNodes = 256;
constexpr std::size_t kMaxFilterLiteralBytes = 1024;

// A predicate over records, parsed from a small expression language:
//
//   expr      := term { "OR" term }
//   term      := factor { "AND" factor }
//   factor    := "NOT" factor | "(" expr ")" | predicate
//   predicate := "level" (">=" | "==") LEVELNAME
//              | "source" ("==" | "~") STRING
//              | "message" ("~" | "!~") STRING
//
// Quoted STRING values decode only escaped quote (\\") and escaped backslash
// (\\\\); all other bytes are literal input bytes.
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
