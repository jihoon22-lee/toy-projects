# DiskMap explorer workbench local GUI milestone

## Overview

The local `feat/diskmap-explorer-ui` branch completes the DiskMap explorer GUI
slice on top of the previously recorded D3 Qt-free projection. `MainWindow`,
the treemap, and the sortable table now consume one shared immutable scan
document and use `NodeKey` as their stable navigation identity. This gives the
user one coherent workbench for navigating, filtering, comparing size
meanings, and preserving selection while a scan is refreshed.

This is a local implementation milestone only. It keeps DiskMap at `0.1.0`
under `Unreleased`; it does not claim a new PR, remote CI run, `main` merge,
Pages publication, or product release.

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

### Known limitations

- Local `clang-tidy` and `clazy` are unavailable.
- ici does not yet provide C++ type analysis, and exact C++ dead-symbol
  analysis remains pending.
- The heuristic duplicate check warns at `6.42%` across `34` groups. This is
  tracked with ici I4-3's robust duplicate backlog; false-positive clone shapes
  are not a reason to contort the DiskMap product code.
- This evidence is local to `feat/diskmap-explorer-ui`. PR/remote/main/Pages
  verification and a release decision remain future work.

## Next Steps

- Submit and verify the GUI milestone through the repository's PR and exact
  `main` quality gates.
- Keep DiskMap `0.1.0`/`Unreleased` until a comparable release checkpoint is
  complete.
- Continue with D4 cleanup staging, then D5 trash/audit and D6 snapshot/
  duplicate candidate work; D7 remains the eventual release gate.
