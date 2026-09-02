# DiskMap explorer workbench — merged PR and main evidence

## Overview

The historical `feat/diskmap-explorer-ui` branch completed the DiskMap explorer GUI
slice on top of the previously recorded D3 Qt-free projection. `MainWindow`,
the treemap, and the sortable table now consume one shared immutable scan
document and use `NodeKey` as their stable navigation identity. This gives the
user one coherent workbench for navigating, filtering, comparing size
meanings, and preserving selection while a scan is refreshed.

The implementation was merged by [PR #46](https://github.com/jihoon22-lee/toy-projects/pull/46)
as `main` commit `0cdd63953179a1dc885ed660e955b399d54243b7`. The feature branch
and its local worktree were deleted after the merge. DiskMap remains `0.1.0`
under `Unreleased`; this evidence does not create a product release.

## Context

The earlier D3 core slice already supplied deterministic projection, filtering,
largest-file ordering, and uncertainty provenance without importing Qt. The
remaining GUI work needed to make that projection useful was navigation and
presentation across two views, explicit metric explanations, safe rescan
behavior, and deterministic worker/UI lifetime handling.

The implementation also needed to preserve the scanner's conservative facts:
logical bytes are additive per entry, allocated bytes can overlap through hard
links, and reclaimable bytes are only exact when the subtree owns every known
hard-link reference. Incomplete, unknown, cycle, depth, mount, and scanner-
filtered states must remain visible to the user.

## Changes Made

### 1. Shared document and stable navigation

- `MainWindow` owns a `std::shared_ptr<const diskmap::ScanResult>` document.
- `TreemapWidget` and `NodeTableModel` receive that same immutable document;
  they do not maintain competing copies or borrow nodes from a worker result.
- `NodeKey` identifies the current trail and selected entry across treemap
  tiles, table rows, breadcrumb segments, filters, model resets, and rescan.
- Breadcrumb segments expose accessible actions, and table activation follows
  the same `NodeKey` navigation path as treemap activation. A directory
  activation descends; a leaf activation is a no-op.

Key ownership and navigation state:

```cpp
std::shared_ptr<const diskmap::ScanResult> document_;
std::vector<diskmap::NodeKey> trail_;
std::optional<diskmap::NodeKey> selectedKey_;
```

### 2. Treemap/table explorer presentation

- Added a treemap and sortable table splitter backed by the same immutable
  document, current root, filters, and metric. The table can additionally use
  a recursive largest-files projection.
- Added name/path search, type, minimum/maximum size, age, and scan-state
  filters.
- Added logical, allocated, and reclaimable metric selection, with sorting
  synchronized to the selected metric column.
- Added a bounded largest-files table mode that projects regular files and
  preserves completeness/unknown status.
- Added exact metric explanations and a visible uncertainty legend/banner.
  Unknown and non-additive physical values are not silently converted into
  exact totals.
- Kept hover descriptions, stable object names, accessible labels, and safe
  empty/missing-root rendering available to headless tests and assistive
  technology.

### 3. Rescan, generation, and interaction safety

- Rescan captures the current `NodeKey` trail and selection, then restores the
  deepest path still present in the new document. Missing or identity-changed
  entries fall back to a valid ancestor; disappeared selections are cleared.
- Every scan carries a generation. Older result/progress callbacks and result
  metadata whose generation does not match are rejected before touching the
  visible document or widgets.
- A new scan cancels the prior token. A cancelled partial result is discarded,
  while the previously visible complete document remains in place.
- During a scan, explorer controls, table, treemap, and navigation are disabled
  so filters or activation cannot mutate state against an in-flight document.
- Progress is held in shared atomic state and polled by the GUI. The state is
  retained through late worker callbacks, including the window-destruction
  path, so callback lifetime cannot become a use-after-free.

The central stale-result guard is intentionally simple:

```cpp
if (generation != activeGeneration_ || !activeCancellation_
    || activeCancellation_->isCancelled()) {
    return;
}
```

### 4. Native test expansion

The qmake test subproject now has 11 native targets:

```text
test_format
test_fs_node
test_fs_source
test_scanner
test_scanner_safety
test_scanner_real_safety
test_treemap
test_treemap_widget
test_main_window
test_view
test_node_table_model
```

The focused GUI test counts are MainWindow `29`, TreemapWidget `12`, and
NodeTableModel `11`. Coverage includes shared-document lifetime, keyed row
projection, cross-view key coherence, accessible controls, metric knownness,
filter and sort reprojection, largest-files mode, breadcrumb navigation,
rescan restoration/fallback, cancellation, stale generations, progress
visibility/lifetime, generation metadata rejection, and scan-time interaction
freeze.

## Verification Results

### Native qmake matrix

Both clean full builds use `-Werror` and pass all native targets:

```text
Qt 5.15.18: clean full qmake build; make check 11/11 PASS
Qt 6.10.2: clean full qmake build; make check 11/11 PASS
```

### ici deep no-cache verification

The public ici `v0.10.2` asset used for this local run has SHA-256:

```text
8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4
```

The deep no-cache result is `WARN` with `10 PASS / 2 WARN / 0 FAIL / 0 ERROR /
2 SKIP`. TEM is `4.95`; `compile_db` covers `16/16` production units across
`30` configurations; line/function/branch coverage is `96.1% / 99.1% /
83.4%`; maximum complexity is `14`; and sanitizer is `PASS`.

The generated HTML was checked directly with `stat` and `sha256sum`:

```text
stat: 499265 bytes
sha256: 9b624303b6191c6ead73079aa42636f318b495e807699601cd403a960cf059c3
title: ici Verification Report — diskmap
exact-title / Zero-CDN checker: PASS
```

### PR #46 and exact-main publication evidence

PR workflow [run `33627322683`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33627322683)
was green across the required matrix, including public ici `v0.10.2`. Its sticky
comment has exactly one marker and exactly three project report links. Each PR
report artifact is byte-identical to its corresponding Pages response.

Exact-main workflow [run `33628585439`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33628585439)
was also green. Each main artifact is byte-identical to Pages; all responses were
HTTP `200` with `text/html; charset=utf-8`, the exact project title, and Zero-CDN
resources.

The complete PR/main artifact and Pages byte table is kept here as the canonical
DiskMap D3 publication record:

| Project | PR report (Pages path; bytes / SHA-256) | Main report (Pages path; bytes / SHA-256) | Exact title |
|---|---|---|---|
| diskmap | [diskmap/pr/46](https://jihoon22-lee.github.io/toy-projects/diskmap/pr/46/) · 545,766 / `37fbf785ce2020a4ba95f41c61f737b52b6bc6a42fa31c5fd4487e9c2fe8e211` | [diskmap/main](https://jihoon22-lee.github.io/toy-projects/diskmap/main/) · 545,766 / `5966f59e747c278a0132758fe1b3958754330a4cf73a7ef86000ac89b3707376` | `ici Verification Report — diskmap` |
| buildscope | [buildscope/pr/46](https://jihoon22-lee.github.io/toy-projects/buildscope/pr/46/) · 1,345,473 / `cc9f6f9091d2f054d7935481a643fa0d0e139100fa43b44ab8c51a112d4652cb` | [buildscope/main](https://jihoon22-lee.github.io/toy-projects/buildscope/main/) · 1,345,473 / `a62db54f81025dd8a19c008aedf57a830fb5dafdd3def2ff0510576b92f39ba7` | `ici Verification Report — buildscope` |
| loglens | [loglens/pr/46](https://jihoon22-lee.github.io/toy-projects/loglens/pr/46/) · 487,150 / `eb1ae8fc0fda455b1918bf3ebd29f2b73ae5a5e6eacb503dd85cce887175febb` | [loglens/main](https://jihoon22-lee.github.io/toy-projects/loglens/main/) · 487,155 / `95db580822bf92e1f07038ee1728fbcb27b5d015c26e29a9462e7dcfe8212b9c` | `ici Verification Report — loglens` |

This closes D3's implementation, PR, and exact-main evidence. It does not bump
DiskMap's `0.1.0`/`Unreleased` status or imply a release; D4 cleanup staging,
D5 trash/audit, D6 snapshot/duplicate candidates, and D7 release criteria remain
pending.

### Known limitations

- Local `clang-tidy` and `clazy` are unavailable.
- ici does not yet provide C++ type analysis, and exact C++ dead-symbol
  analysis remains pending.
- The heuristic duplicate check warns at `6.42%` across `34` groups. This is
  tracked with ici I4-3's robust duplicate backlog; false-positive clone shapes
  are not a reason to contort the DiskMap product code.
- The local tool limitation and heuristic duplicate warning remain historical
  limitations of the D3 evidence; they do not reopen the merged PR/main result.

## Next Steps

- Keep DiskMap `0.1.0`/`Unreleased` until a comparable release checkpoint is
  complete.
- Continue with D4 cleanup staging, then D5 trash/audit and D6 snapshot/
  duplicate candidate work; D7 remains the eventual release gate.
