# LogLens parser and GUI contract

LogLens keeps the source bytes of every record in `LogRecord::raw`.  Parsing is
best effort and never treats malformed input as permission to discard evidence:
the structured fields are populated when they are trustworthy, while
`parse_status` and `diagnostics` explain what could not be trusted.

## Record outcomes

`LogRecord::parse_status` has four values:

- `Parsed`: the selected format and all fields that were present were valid.
- `Partial`: the syntax was valid, but a field was missing, had an unexpected
  type, or a timestamp was not usable.
- `Invalid`: the input could not be parsed as the selected structured format.
  The whole source line is retained as the message as well as in `raw`.
- `Unstructured`: auto-detection intentionally treated a free-form line as a
  plain message.

Each `ParseDiagnostic` contains a stable code, optional field name, byte offset
in the original line, and human-readable detail.  Callers should render these
diagnostics instead of reimplementing parser heuristics.  The helper functions
`parseStatusName()` and `parseDiagnosticCodeName()` provide stable display
labels for CLI/GUI adapters.

## JSON Lines

JSON Lines input is validated as one complete JSON object.  The parser accepts
the standard string escapes and decodes `\\uXXXX`, including valid UTF-16
surrogate pairs, to UTF-8.  It rejects truncated strings, invalid escapes,
unpaired surrogates, unescaped control bytes, malformed numbers, trailing
values, and non-object top-level values with explicit diagnostics.  Duplicate
root keys are reported and the first value is retained so an attacker cannot
silently replace a previously interpreted field.  Unescaped source bytes are
also validated as UTF-8; invalid sequences are rejected while the original
bytes remain available in `raw`.

Unknown nested objects and arrays are syntax-checked but are not copied into a
`LogRecord`; this keeps memory bounded.  Nesting is capped at 32 levels,
objects at 256 members, strings at 256 KiB, and a direct JSON line at the
project-wide 1 MiB record limit.  `RecordAssembler` applies its own smaller
per-record cap before invoking the parser, and always reports omitted source
bytes separately.

The supported fields are `ts`, `level`, `logger`, and `msg`; `message` remains
an alias for `msg`.  Scalar `msg` values retain their old display behaviour
while producing an `invalid-field-type` diagnostic.  This is useful for
triaging mixed JSON logs without losing a numeric or boolean payload.

## ISO timestamps

ISO timestamps require a complete date, time, and timezone.  UTC (`Z`) and
signed `+HH:MM`/`+HHMM` offsets are accepted; calendar dates, clock ranges,
fraction length (one to nine digits), and offset hour/minute ranges are
validated before conversion.  Offset errors use the dedicated
`invalid-timestamp-offset` diagnostic so a malformed timezone is not confused
with a missing timestamp.

The parser is dependency-free and compiled as part of `loglens_core`, so the
same contract is exercised by the CLI, background loader, and Qt5/Qt6 GUI.
Epoch conversion uses checked integer civil-date arithmetic rather than
platform-specific `timegm()`/`mktime()` behavior; pre-epoch and unrepresentable
values are rejected explicitly.

## RFC3164 syslog

The supported syslog shape is `Mon DD HH:MM:SS host component: message`.  RFC3164
does not carry a year or timezone, so a valid syslog record intentionally keeps
`timestamp_ms == 0`; LogLens does not guess either value from the local clock.
The month, day (1–31), time (`00:00:00`–`23:59:59`), host (at most 255
non-whitespace bytes), and component (a nonempty name of at most 32 bytes plus
one trailing colon) are validated before they are promoted to structured
fields.  A missing or malformed required token makes an explicitly selected
syslog record `Partial` and adds a bounded diagnostic; the original line stays
available in `raw` and malformed components are not copied into `source`.

## Persistent profiles and saved queries

The core persistence API in `include/loglens/persistence.hpp` stores source
profiles and filter queries without depending on the GUI.  A missing optional
file is a successful empty load (`found == false`); a present file must match
its complete versioned schema.  The current schemas are
`loglens.source-profiles/v1` and `loglens.saved-queries/v1`.

Source profiles use the exact shape below.  `format` is one of the canonical
values `auto`, `iso`, `syslog`, `jsonl`, or `raw`; `multiline` is either
`fold-continuations` or `separate-lines`.

```json
{"schema":"loglens.source-profiles/v1","profiles":[
  {"name":"service","format":"jsonl","multiline":"fold-continuations","max_record_bytes":65536}
]}
```

Saved queries have the same strict root shape and reuse `Filter::parse()` for
validation, so a query loaded from disk has exactly the same syntax and match
semantics as a CLI or GUI query.

```json
{"schema":"loglens.saved-queries/v1","queries":[
  {"name":"timeouts","expression":"level>=WARN AND message~timeout"}
]}
```

Object fields and JSON keys are strict: unknown or duplicate fields, duplicate
names, malformed JSON, invalid enum/number values, invalid filter expressions,
and an unknown schema version are rejected without returning partial data.
Profiles and queries are sorted by name when saved and loaded, making the
serialized bytes and returned order deterministic.  Files are bounded to
4 MiB, each collection to 128 items, names to 128 UTF-8 bytes, and query
expressions to the filter parser's 4096-byte limit.  Profile record limits are
between one byte and the parser's 1 MiB maximum.

Saves validate and serialize completely before writing a same-directory
temporary file, then atomically replace the destination.  On POSIX, source
reads and writes are descriptor-relative: parent directories and source files
are opened without following the final symlink, reads are bounded from the
opened descriptor, and replacement uses `renameat()`; temporary files use
exclusive `openat()` creation.  On Windows the final source handle is opened
with reparse-point no-follow flags and read with the same bounded-size check;
the fallback uses exclusive CRT creation and `MoveFileExW()` replacement.
Symlink/special-file destinations are refused; no fsync-level durability is
claimed.  Schema versions are
intentionally not migrated implicitly: callers must handle
`UnsupportedVersion` and choose an explicit migration policy.

## Qt GUI profile and query workflow

`loglens-gui` loads the two stores when the window starts.  With the default
constructor, files live below Qt's per-user `AppConfigLocation` as
`source-profiles.json` and `saved-queries.json`; embedding code and tests can
pass `MainWindowOptions::sourceProfilesPath` and
`MainWindowOptions::savedQueriesPath` to select different stores.

The source-profile row exposes the profile name, format, multiline policy,
and bounded maximum record size (1 byte through 1 MiB).  **Save profile**
validates and atomically writes the complete collection; **Apply profile**
reloads the currently open source with those parser settings.  A profile is
also applied to the next **Open log** operation.  The saved-query row lists
loaded names, **Save query** stores the current filter expression, and
**Apply query** copies the selected expression into the filter editor before
applying it.  Applying a persisted query therefore uses the same parser and
filter semantics as typing it manually.

The GUI keeps at most 128 profiles and 128 queries in memory, mirrors the
core 4 MiB store and 4,096-byte query bounds, and refuses a 129th item before
mutating the visible list.  Missing stores are treated as empty; malformed,
unsupported, or invalid stores remain empty and are reported in the status
line with an error code and byte offset.  Failed saves and failed filter
applications leave the previous store or active filter intact, so an input
mistake cannot blank a working investigation.

The focused `test_gui_persistence` QtTest covers load/save/apply, parser
settings reaching the background worker, malformed-store diagnostics, and
the item limits on both Qt 5.15 and Qt 6.

## Investigation workbench

The GUI includes a separate **Investigation** dock for preserving evidence and
turning a suspicious time range into a reproducible comparison.  The dock is
backed by the same core objects used by the non-GUI tests; it does not parse a
second, GUI-specific representation of a record.

### Triage state and highlighting

Triage state is stored as the strict, versioned `loglens.triage/v1` document:

```json
{"schema":"loglens.triage/v1","rules":[
  {"name":"Timeout","pattern":"timeout","whole_line":false,
   "priority":40,"style":"#ffcc00"}
],"entries":[
  {"source_path":"/var/log/service.log","line_number":42,
   "bookmarked":true,"annotation":"check upstream retry"}
]}
```

The **Highlights** tab supports literal byte-ranged spans or whole-row
highlighting, priority ordering, safe named/hex colour values, create/update,
delete, and reorder.  `loglens.triage/v0` rule-only files are accepted as an
explicit legacy input and are marked as migrated; the next successful save
writes v1.  Empty or malformed stores never replace the last valid in-memory
state.

The state is bounded before it is parsed or written: at most 128 rules and
8,192 source-line entries, 1,024 bytes per pattern, 4,096 bytes per
annotation, 4,096 bytes per source path, and 64 bytes per style.  Rule names,
source/line identities, and entry contents are validated strictly, including
duplicate identities and unbookmarked empty entries.  Saves use the existing
same-directory atomic persistence backend.

### Record evidence and export

The **Record** tab shows the selected source path and physical line, timestamp,
level, parser status, input/omitted byte counts, every parse diagnostic, the
parsed message, and the original `LogRecord::raw` bytes.  A selected row can be
bookmarked and annotated against its source/line identity.  **Export selected…**
creates a compact `loglens.selection/v1` JSON document with the source path and
one object per selected record.  Each object includes parsed fields,
diagnostics, triage state, byte accounting, and base64 copies of the raw,
message, and source bytes.  The byte fields keep the original evidence available
even when malformed UTF-8 cannot be rendered as a normal `QString`.  The display
strings are UTF-8-normalized conveniences, not the lossless representation.
Export is sorted by visible row, streams one record at a time, stops at 16 MiB,
and is committed atomically. It also refuses to replace the currently open
source log. Empty selections, output-size violations, and failed destinations
leave the source view unchanged.

### Timeline range comparison

The timeline accepts a left-click/drag selection over timestamp buckets and a
right-click (or **Clear range**) to remove it.  Ranges are half-open
`[begin_ms, end_ms)`, so a record exactly at the end boundary belongs only to
the following range.  The selected range is composed with the current
structured filter and raw-text search; clearing it restores the same filtered
view rather than resetting the investigation.

The **Compare** tab stores two selected ranges as baseline and comparison and
computes deterministic, measured signals over the visible records:

- `new-pattern` for a level, source, or normalized message pattern absent from
  the baseline;
- `rate-spike` when an existing key has at least two comparison records and its
  per-minute rate is at least twice the baseline rate; and
- raw correlation groups for `correlation_id`, `request_id`, `thread_id`, and
  `thread` values present in the comparison window.

Every signal and correlation keeps counts, rates where applicable, a stable
explanation, and its first/last physical source line.  Results are sorted by
score and then by dimension/key for reproducibility.  Activating a result
navigates back to the corresponding table row; if that row has been evicted by
bounded storage or filtered out, the status line explains that the evidence is
outside the current visible range.  These heuristics describe measured changes
and intentionally do not claim a diagnosis.

### Verification and current boundary

The Qt5 and Qt6 focused investigation tests cover timeline mouse interaction,
UTF-8 highlight rendering (including byte-to-UTF-16 offset conversion), triage
CRUD/migration and persistence, bookmark/annotation display, byte-preserving export,
diagnostic rendering, comparison navigation, and empty/error paths.  The
  current local focused CTest result is `18/18` for each Qt major, and the native
  TSan partition is `41/41 PASS`.  The final exact ici candidate deep local run
  reports test engine `18/18 PASS`, line/function/branch coverage
  `90.5% / 96.1% / 78.0%`, and TEM `4.81`.  Remote PR/Pages acceptance is a
  separate gate; this README does not treat a local candidate or an unreleased
  toy build as a stable LogLens release.  The product remains `0.1.0`/`Unreleased`.
