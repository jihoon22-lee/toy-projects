# LogLens bounded filter diagnostics

## Overview

The LogLens filter expression parser now reports deterministic byte ranges for
syntax and resource-limit failures while retaining the existing
`ParseError::position` start-offset API. Quoted literals support escaped quotes
and backslashes without rewriting UTF-8 bytes, and the parser has explicit
query, AST, literal, and nesting bounds.
CLI arguments and GUI input retain their own UTF-8 byte coordinate spaces, and
failed GUI filter applies preserve the prior valid filter.

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
  - Structured parser token metadata as TokenRange/PredicateTokens to remove
    new clang-tidy swapped-parameter warnings. Depth-limit errors point at the
    extra nesting token, and an unsupported UTF-8 escape spans the complete
    scalar beginning at its backslash.
- `loglens/src/gui/main_window.cpp`
  - Displays filter diagnostics as byte ranges while keeping the prior table
    filter active after a failed apply.
  - Parses the untrimmed UTF-8 text so displayed byte ranges stay aligned,
    parses into a candidate-local filter state, and commits that candidate only
    after a successful apply so the prior valid filter survives failures.
  - Replaces adjacent capacity/chunk constructor arguments with
    `MainWindowOptions` and passes `LoadBatch` by const reference, keeping the
    API intent explicit and removing the corresponding clang-tidy findings.
- `loglens/src/main.cpp` and `loglens/tests/test_cli_stream.cmake`
  - Display and verify the same half-open diagnostic range in CLI failures.
  - Parses `--level` and `--filter` independently so each diagnostic maps
    to the corresponding user argument rather than a synthesized conjunction.
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
focused `test_filter_expr` passed in both full suites. Final local validation
used a versioned `ici v0.10.2` local candidate `ici.pyz` (not the public release
asset) with SHA-256
`2af5198d1348a64c39f4f37d12657aa9a2c4bf3ddf034a9099909c41e86e30e7`. The
uncached deep suite is `WARN` only because clazy is unavailable and
pre-existing lint findings remain. The changed `filter_expr.cpp`, `main.cpp`,
and `main_window.cpp` have zero actionable lint targets; the overall lint
result contains 26 targets, including 16 clang-tidy `note:` lines that ici
currently counts separately. That note-line inflation is a known ici engine
follow-up. `compile_db` is `PASS` for 14/14 production units across 40
configurations; `test` is `PASS` for 12/12 with line/function/branch coverage
`93.3% / 96.7% / 82.4%`; `complexity` is `PASS` with max 15 across 218; and
`sanitize` is `PASS`. The HTML artifact is 484,899 bytes with exact title
`ici Verification Report — loglens` and Zero-CDN. `git diff --check` reported
no whitespace errors. The LogLens product version/release remains pending;
this bounded slice does not claim broader L3 completion.

## Follow-up

Saved queries, source profiles, malformed log-line metadata, and the remaining
L3 parser contract are intentionally still open for later slices.
