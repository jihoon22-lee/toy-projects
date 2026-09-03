# LogLens trustworthy parsing and saved workflows

## Overview

LogLens now provides bounded, user-visible workflows for source profiles and
saved filter queries while treating both log text and persistence files as
untrusted input.  The GUI restores saved state, lets users save and apply
profiles and queries, and reports failures without losing valid in-memory
state.  Explicit RFC3164 syslog parsing also validates its required fields,
preserves malformed input, and distinguishes partial records from accepted
records.

## User-visible result

- Source profiles and saved queries are loaded at startup.  Missing stores are
  treated as empty, while malformed or unsafe stores produce a status message
  containing the persistence error code and byte offset.
- Users can edit and save bounded profile/query collections.  A save validates
  a copy and atomically persists it before updating the visible list, so a
  failed save does not replace the last known-good state.
- Applying a profile to an open source carries its format, multiline policy,
  and maximum record size through the background loader.  Tail-N selection uses
  the same multiline policy as record assembly.
- Applying a saved query reuses the existing filter parser.  Invalid applies
  leave the previously active filter intact.
- Profile and query names are capped at 128 UI characters, filter input at
  4,096 characters, record size at 1 MiB, and each collection at 128 items.

For explicit RFC3164 input, the parser accepts valid abbreviated month, day,
clock, host, and component fields.  Valid records intentionally have
`timestamp_ms == 0`: RFC3164 supplies neither a year nor a timezone.  Missing
or malformed required fields produce `Partial` status with bounded
diagnostics; `parseLine` always retains the complete input in `raw`.  An
invalid component is not promoted to `source`, and its text remains available
in the message field for the existing display behavior.

## Design and security invariants

### Bounded structured parsing

Syslog validation is shape- and range-aware: month is one of Jan–Dec, day is
one or two digits in 1–31, time is exactly `HH:MM:SS` within clock ranges,
host is a non-whitespace/non-control token of at most 255 bytes, and the
component is a non-empty name of at most 32 bytes with exactly one trailing
colon and no internal colon.  Required-token failures do not silently become
fully parsed records, and diagnostics remain bounded.

### Strict persistence contracts

The two stores use strict JSON schemas `loglens.source-profiles/v1` and
`loglens.saved-queries/v1`.  Unsupported versions, malformed data, duplicate
names, invalid filter expressions, files over 4 MiB, collections over 128
items, names over 128 UTF-8 bytes, queries over 4,096 bytes, or profile record
limits over 1 MiB are rejected.  There is no implicit schema migration, and
valid entries are sorted deterministically.

Writes use validation followed by atomic replacement.  This provides
consistent replacement semantics but deliberately does not promise fsync-level
crash durability.

### Filesystem race and link safety

On POSIX, the persistence backend opens the parent directory once and performs
source validation, bounded reads, temporary creation, replacement, and cleanup
relative to that descriptor (`openat`, `fstatat`, `renameat`, and `unlinkat`).
Final components are no-follow and are checked for size stability; symlinks,
directories, and special files are rejected.  Temporary files use exclusive,
no-follow creation with a bounded 128-attempt collision loop.

On Windows, the final source is opened with a no-follow reparse-point handle,
reparse points and directories are rejected, reads are bounded and size
checked, and temporary files use exclusive creation before `MoveFileExW`
replacement.  Platforms without the required no-follow primitives fail closed
with `UnsafePath` rather than using a weaker path-based fallback.

### GUI translation-unit boundary

Persistence-only `MainWindow` methods and their UTF-8/error-formatting helpers
are implemented in `src/gui/main_window_persistence.cpp`, linked through the
GUI CMake target.  This keeps behavior and the public header unchanged while
bringing both GUI translation units under ici's 500 pure-code-line policy:
`main_window.cpp` is 456 pure-code lines and
`main_window_persistence.cpp` is 278.

## Implementation and regression coverage

The integrated behavior spans the parser, persistence backends and JSON
validation, GUI main window and load worker, and their Qt tests.  Regression
coverage includes profile/query startup and round trips, parser-setting and
Tail-N propagation, malformed-store diagnostics, collection bounds, POSIX
symlink/special-file refusal and bounded temporary-name exhaustion, plus valid
and malformed syslog month/day/time/host/component and missing-token cases.

## Verification

- Qt 6 normal configure/build: exit 0; CTest: 14/14 passed.
- Qt 5 normal configure/build: exit 0; CTest: 14/14 passed.
- Qt 6 global `-Wall -Wextra -Wpedantic -Werror` configure/build: exit 0;
  CTest: 14/14 passed.
- Qt 5 global `-Wall -Wextra -Wpedantic -Werror` configure/build: exit 0;
  CTest: 14/14 passed.
- Direct strict `-fsyntax-only` checks for the parser, GUI main window,
  persistence GUI translation unit, and load worker passed for both Qt
  versions (Qt 5 with `-fPIC`).  The standalone strict parser test reported
  `All checks passed`.
- Released ici `0.10.2` checks from the LogLens root passed: line reported
  7,838 total lines (6,761 code, 241 comment, 836 blank) across 45 files;
  complexity reported a maximum of 15 (limit 15) across 388 functions with
  zero issues.
- The documentation consolidation itself passed
  `git diff --check -- loglens/workthrough` and a trailing-whitespace check.

## Limits and remaining scope

RFC3164 records cannot receive an absolute timestamp under the current input
format.  Persistence remains strict v1 with no automatic migration and no
fsync-level durability claim; unsupported no-follow platforms fail closed.
GUI support currently covers source profiles and saved queries.  Highlights,
bookmarks, annotations, and richer record-detail triage remain outside this
workflow.
