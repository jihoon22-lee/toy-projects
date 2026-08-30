# loglens filtered append backing-index fix

## Overview

Fixed `LogModel::appendRecords()` so filtered rows continue to reference the
correct records after a batch append. Added regression coverage for batches with
leading and trailing nonmatches and multiple matching records.

## Context

The model stores all records in `records_` and keeps the rows passing the active
filter in `visible_`. The old append loop derived each incoming backing index
from `records_.size() + arriving.size()`. Because `arriving` grows only when a
record matches, a nonmatching record before a match caused the visible row to
point at an earlier, unrelated record.

## Changes Made

### 1. Correct incoming backing indexes

- File: `loglens/src/gui/log_model.cpp`
- Iterate with the incoming record offset and compute its future backing index as
  `records_.size() + offset`.
- Preserve the existing contiguous insert signal, filter ownership, public API,
  and Qt5/Qt6 behavior.

### 2. Regression coverage

- File: `loglens/tests/test_log_model.cpp`
- Added a filtered append test containing a leading nonmatch, two distinct
  matches, and a trailing nonmatch.
- Asserted each visible record's level and message through `recordAt()`, not just
  the row count.

## Code Examples

### Index calculation

```cpp
for (std::size_t offset = 0; offset < records.size(); ++offset) {
    const loglens::LogRecord& record = records[offset];
    const int index = static_cast<int>(records_.size() + offset);
    if (!filter_ || filter_->matches(record)) {
        arriving.push_back(index);
    }
}
```

The offset includes filtered-out records, matching the positions those records
will occupy after the full batch is appended to `records_`.

## Verification Results

### Historical focused Qt6 model check

These focused checks were run immediately after the implementation, before the
full release verification below.

```text
cmake -S loglens -B /tmp/ici-loglens-filter-qt6 -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/ici-loglens-filter-qt6 --target test_log_model -j2
ctest --test-dir /tmp/ici-loglens-filter-qt6 -R '^test_log_model$' --output-on-failure
1/1 Test #9: test_log_model ... Passed
100% tests passed, 0 tests failed out of 1
```

Configured with Qt6 `6.10.2`.

### Historical focused Qt5 model check

```text
cmake -S loglens -B /tmp/ici-loglens-filter-qt5 -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE
cmake --build /tmp/ici-loglens-filter-qt5 --target test_log_model -j2
ctest --test-dir /tmp/ici-loglens-filter-qt5 -R '^test_log_model$' --output-on-failure
1/1 Test #9: test_log_model ... Passed
100% tests passed, 0 tests failed out of 1
```

Configured with Qt5 `5.15.18`. `git diff --check` also passed. No standalone
`clang-format` or `clang-tidy` executable was available, so no additional native
formatter/linter command was run; the focused CMake builds compile the changed
translation unit and test.

### Full release verification (completed later)

The complete Release matrix and headless GUI smoke checks subsequently passed:

| Build | CTest | Offscreen GUI smoke |
|---|---|---|
| Qt6 `6.10.2` Release | 10/10 PASS | 8 seconds PASS |
| Qt5 `5.15.18` Release | 10/10 PASS | 8 seconds PASS |

The public ici `v0.6.0` release asset checksum was verified. Running
`ici verify --report --html verify_report.html --github-summary` for `loglens`
also passed with 10/10 tests, line coverage 93.2%, function coverage 96.9%,
branch coverage 81.8%, TEM 4.84, and 10 Pass / 2 Skip. The two skips are the
known C++ type-check and Python dead-code limitations; the generated HTML had
zero external references.

## Commits

- `42ee930 fix(loglens): preserve filtered append indexes`
- `docs(loglens): record filtered append fix` (this document)

## Known Limitations / Next Steps

- This change addresses only filtered append backing indexes. File identity and
  rotation semantics remain outside this P0 fix.
