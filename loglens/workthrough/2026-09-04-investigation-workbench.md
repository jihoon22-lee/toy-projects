# LogLens investigation workbench — 2026-09-04

## Overview

LogLens now connects its bounded log model to an evidence-preserving triage
workflow and a deterministic time-window comparison.  The GUI can highlight
records, bookmark and annotate source lines, export selected rows without
losing raw bytes, and navigate from comparison results back to source lines.
The product remains `0.1.0`/`Unreleased`; this document records local
implementation and test evidence only.

## Context

The parser already retained raw record bytes and diagnostics, but the GUI had
no durable way to mark an important line or carry its evidence into a later
review.  A timeline histogram also showed volume without allowing an analyst to
compare two windows or inspect the records behind a change.  The work therefore
keeps the parser/model as the source of truth and adds bounded adapters around
it.

## Changes Made

### Triage persistence and highlighting

- Added the strict `loglens.triage/v1` state with named rules and source-line
  entries.  Rules support literal spans or whole-row highlighting, priority,
  and safe named/hex colour styles.
- Added CRUD and reorder helpers that validate a copied state before replacing
  the current state.  Duplicate rule names and duplicate `(source_path,
  line_number)` identities are rejected.
- Accepted legacy `loglens.triage/v0` rule-only input as an explicit migration
  result; the GUI tells the user that the next save upgrades it to v1.
- Enforced bounds before persistence grows: 128 rules, 8,192 entries, 1,024
  pattern bytes, 4,096 annotation bytes, 4,096 source-path bytes, and a
  64-byte safe style.
- Connected the model's background/font/tooltip roles to bookmark and
  annotation state.  The custom `HighlightDelegate` paints byte-ranged spans
  in the message column and converts UTF-8 byte offsets to Qt UTF-16 positions.

### Record detail and byte-preserving export

The Record tab displays the source path and physical line, timestamp, level,
parse status, input/omitted bytes, all diagnostics, parsed message, and raw
evidence.  A selected row can be bookmarked and annotated and is keyed by its
source path and line number.

The export seam writes `loglens.selection/v1` with selected records in visible
row order.  It includes parsed fields, diagnostics, triage state, byte
accounting, plus `raw_base64`, `message_base64`, and `source_base64` fields.
Base64 is intentional: malformed UTF-8 remains recoverable even when display
strings cannot represent it faithfully in a `QString`.  Export writes one
encoded record at a time, enforces a 16 MiB
cumulative limit, and uses `QSaveFile` with direct fallback disabled, so failed
writes do not replace the destination. The exporter also rejects the currently
open source path before opening its destination.

### Timeline filtering and comparison

`TimelineWidget` now publishes a selected bucket interval as a half-open
`[begin_ms, end_ms)` window.  A right-click or **Clear range** removes the
selection.  `LogModel::setTimeWindow` composes that window with the existing
structured filter and raw search; analysis can request the base projection so
the comparison sides are not accidentally narrowed to the current selection.

`compareWindows` counts timestamped records by level, source, and normalized
message pattern.  It emits:

- `new-pattern` when a key appears in comparison but not baseline;
- `rate-spike` when an existing key has at least two comparison records and its
  measured per-minute rate is at least twice the baseline rate; and
- correlation groups for `correlation_id`, `request_id`, `thread_id`, and
  `thread` values found in comparison raw text.

Signals and correlations retain counts, rates, explanations, and first/last
physical line locations.  Stable sorting by score and dimension/key makes the
result reproducible.  Activating a result selects and centers the corresponding
table row; evicted or filtered-out evidence is reported in the status line.
Overlapping windows count a shared timestamp in both sides by design, and
untimestamped records are excluded.

### GUI structure and failure behavior

The investigation controls live in a dock with Record, Highlights, and Compare
tabs.  Persistence paths can be injected through `MainWindowOptions` so tests
and embedders use deterministic files; default stores live under Qt's
per-user application configuration directory.  Empty selections, invalid
colours, malformed triage state, missing baseline/comparison ranges, failed
exports, and out-of-range navigation leave valid state intact and explain the
failure in the status line.

## Code examples

The two new versioned documents are intentionally small:

```json
{"schema":"loglens.triage/v1","rules":[
  {"name":"Timeout","pattern":"timeout","whole_line":false,
   "priority":40,"style":"#ffcc00"}
],"entries":[
  {"source_path":"service.log","line_number":42,
   "bookmarked":true,"annotation":"check upstream retry"}
]}
```

```cpp
const auto analysis = loglens::compareWindows(
    records, {baseline_begin, baseline_end}, {comparison_begin, comparison_end});
for (const auto& signal : analysis.findings) {
    // signal.explanation and signal.first_line/last_line remain navigable evidence.
}
```

## Verification Results

Local native and analyzer checks for the current worktree:

```text
Qt6 normal CTest:                 18/18 PASS
Qt5 normal CTest:                 18/18 PASS
TSan-partitioned CTest:           41/41 PASS
WSLg headless GUI smoke:          healthy for the bounded run; no stderr
```

The exact ici candidate executable
`50d41d36775394f66f6620091f42a7a0333ee90758e19449a848d7ee0875a93c` completed
the uncached deep verification with exit `0`.  The suite was `WARN` only for
unavailable lint tooling and duplication (`10 PASS / 2 WARN / 4 SKIP`); the test
engine passed `18/18`, line/function/branch coverage was
`90.5% / 96.1% / 78.0%`, complexity maximum was `15`, sanitizer was `PASS`, and
TEM was `4.81`.  The native TSan partition passed `41/41`.  The candidate is
non-stable local evidence, not a release artifact.

## Limits and remaining scope

- Comparison signals are deterministic heuristics over parsed records and raw
  key/value tokens; they do not establish causation or provide an AI diagnosis.
- RFC3164 records remain intentionally untimestamped, so they cannot enter a
  time-window comparison until an external timestamp is supplied.
- Raw export is bounded at 16 MiB; a larger selection must be split into smaller
  selections.
- Remote PR CI, Pages equality, and the product release gate are separate
  follow-up evidence and are not claimed by this local workthrough.
