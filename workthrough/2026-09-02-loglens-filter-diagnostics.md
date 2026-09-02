# LogLens bounded filter diagnostics

## Overview

The LogLens filter expression parser now reports deterministic byte ranges for
syntax and resource-limit failures while retaining the existing
`ParseError::position` start-offset API. Quoted literals support escaped quotes
and backslashes without rewriting UTF-8 bytes, and the parser has explicit
query, AST, literal, and nesting bounds.

## Changes Made

- `loglens/include/loglens/filter_expr.hpp`
  - Added `ParseError::end` as the exclusive end of the `[position, end)` byte
    range.
  - Published the 4,096-byte query, 256-node AST, and 1,024-byte decoded
    literal limits alongside the existing depth-64 limit.
- `loglens/src/filter_expr.cpp`
  - Added a pre-parse query-size guard and node accounting for leaves,
    combinators, and `NOT` nodes.
  - Reworked quoted-value scanning to decode only `\\"` and `\\\\`, reject
    unsupported/unterminated escapes, and copy all other input bytes unchanged.
  - Added stable ranges/messages for malformed, unknown, trailing, and
    oversized input.
- `loglens/src/gui/main_window.cpp`
  - Displays filter diagnostics as byte ranges while keeping the prior table
    filter active after a failed apply.
- `loglens/src/main.cpp` and `loglens/tests/test_cli_stream.cmake`
  - Display and verify the same half-open diagnostic range in CLI failures.
- `loglens/tests/test_filter_expr.cpp`
  - Covers exact and oversized query/literal/node bounds, UTF-8 byte offsets,
    escaped literals, literal metacharacters, precedence, and diagnostics.
- `README.md`, `CHANGELOG.md`, and
  `docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md`
  - Document the completed bounded diagnostics slice without closing the
    remaining L3 parser-pipeline work.

## Code Examples

The public compatibility surface remains simple:

```cpp
loglens::ParseError error;
if (!loglens::Filter::parse(query, error)) {
    // `position` is the legacy start offset; `end` is exclusive.
    showError(error.position, error.end, error.message);
}
```

Quoted filter values preserve literal semantics:

```text
message~"quote: \" and slash: \\"
```

Only the escaped quote and escaped backslash are decoded. `~` and `!~` remain
case-insensitive substring operations; metacharacters such as `.*` are not
regular-expression syntax.

## Verification Results

### Native build matrix

```text
Qt 5.15.18: configure and full build passed.
Qt 6.10.2: configure and full build passed.
```

### Tests

```text
ctest --test-dir /tmp/toy-loglens-filter-qt5 --output-on-failure
100% tests passed, 0 tests failed out of 12

ctest --test-dir /tmp/toy-loglens-filter-qt6 --output-on-failure
100% tests passed, 0 tests failed out of 12
```

The CLI stream regression verifies the exact `[12,17)` diagnostic and the
focused `test_filter_expr` passed in both full suites. `git diff --check`
reported no whitespace errors. No release build was run for this bounded
slice.

## Follow-up

Saved queries, source profiles, malformed log-line metadata, and the remaining
L3 parser contract are intentionally still open for later slices.
