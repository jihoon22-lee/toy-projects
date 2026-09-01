# BuildScope B2 normalized Qt explorer

## Overview

BuildScope B2 completes the normalized C++/Qt explorer on
`feat/buildscope-qt-explorer`. The 0.3.0 shell consumes the accepted
`buildscope.snapshot/v2` contract, groups normalized sources with their configurations, exposes
status/search/detail views, and keeps a raw compiler command visibly separate from structured JSON
argv. The v1 compatibility projection remains available for legacy consumers.

This record documents the B2 implementation already present on the branch, its focused Qt tests,
the opt-in 100k model benchmark, and the public `ici v0.8.0` verification evidence.

## Context

B1 supplied a bounded, shell-free Python producer and native v2 contract validation, but the native
consumer still exposed a raw entry view. B2 is the presentation boundary: normalized entries need a
stable source/configuration tree, an inspectable mapping back to the source entry, and a Qt shell
that can answer status, search, and detail questions without reparsing or executing commands.

## Changes made

### Normalized C++ model

- `CompilationTreeModel` groups v2 entries by normalized source and presents source parents with
  configuration children. Node-kind, source-path, source-status, entry-index, and search-text roles
  make the hierarchy usable by both the view and proxy filter.
- `CompilationEntryView` maps a selected source or configuration to its underlying entry and
  exposes source/directory/status, target/compiler/standard, configuration identity, invocation
  source, structured arguments, and raw command.
- Source status aggregation follows the explicit strongest-state order `missing > stale > present
  > unknown`. Legacy v1 entries remain readable through the raw projection and use the compatible
  fallback view.
- `renderArgumentVector()` serializes argv as a compact JSON array, preserving spaces, quotes, and
  empty tokens. When both invocation forms exist, the structured `arguments` vector remains the
  authority while the original `command` string is retained unchanged.

### Qt explorer shell

- The main window loads v2 snapshots, reports contract/producer metadata, selects the first source
  automatically, and expands a source when it has multiple configurations.
- A case-insensitive recursive filter searches source, status, target, compiler, standard,
  configuration, define, and include text. Selecting an entry fills overview, command, define,
  include, and diagnostics tabs; malformed v2 input keeps the validation field location in the
  visible error.
- The status column uses four local SVG resources (`present`, `missing`, `stale`, and `unknown`)
  compiled through the Qt resource system. The explorer therefore has no CDN or other external
  runtime resource dependency.

### Fixtures, tests, and benchmark

- `fixtures/sample-v2.snapshot.json` exercises a present source and a missing source, one
  configuration with define/include/diagnostic records, an argument containing a space, and a raw
  command that is intentionally distinct from the JSON argv rendering.
- `test_compilation_model` uses QtTest and `QAbstractItemModelTester` to cover normalized grouping
  across configurations, parent/index/data/roles, v1 projection, status aggregation, `entryView`,
  and JSON argv rendering.
- `test_main_window` covers v2 load/tree construction, automatic details, raw-vs-structured command
  display, define/include/diagnostic tables, missing/source/define filters, and the located malformed
  v2 error.
- `BUILDSCOPE_BUILD_BENCHMARKS=ON` provides a deterministic model benchmark. Its Qt6 run builds a
  100,000-entry model with 25,000 source groups, filters `unit_024999`, and checks model/filter
  correctness as well as timing budgets.
- The B1 snapshot reader's post-read attestation was exercised through an internal, production-off
  test seam. POSIX tests deterministically remove or replace the path, append content, close the
  open descriptor, and pass a FIFO, covering identity/state changes without a timing-dependent
  race test. The public `loadSnapshotFile()` path always supplies no hook.

## Reproduction commands

These commands keep builds outside the repository and exercise both Qt major versions. The
benchmark option is enabled so the complete six-test CTest suite is included.

```bash
repo_root="$(git rev-parse --show-toplevel)"
scratch_root="$(mktemp -d /tmp/buildscope-b2.XXXXXX)"

cmake -S "$repo_root/buildscope" -B "$scratch_root/qt6" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=ON \
  -DBUILDSCOPE_BUILD_BENCHMARKS=ON
cmake --build "$scratch_root/qt6" --parallel
QT_QPA_PLATFORM=offscreen \
  ctest --test-dir "$scratch_root/qt6" --output-on-failure

cmake -S "$repo_root/buildscope" -B "$scratch_root/qt5" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON \
  -DBUILDSCOPE_BUILD_BENCHMARKS=ON
cmake --build "$scratch_root/qt5" --parallel
QT_QPA_PLATFORM=offscreen \
  ctest --test-dir "$scratch_root/qt5" --output-on-failure

# The opt-in benchmark's canonical input and budget.
"$scratch_root/qt6/buildscope-model-benchmark" 100000 10000
```

## Verification results (2026-09-01)

The public `ici v0.8.0` verification completed with `46/46` tests. Line/function/branch coverage
was `94.5% / 99.5% / 83.9%`, TEM was `4.98`, and `compile_db` covered `8/8` production units
across `19` configurations. Complexity passed at maximum 14 across 196 functions. The resulting
standalone HTML was 558,384 bytes with SHA-256
`cdaefa06c52de696e0340b698e37b88dde199bc5a7bd2bbba27421618f44e444`, title
`ici Verification Report — buildscope`, and zero external resource references. The snapshot
reader module reached 89.9% line coverage and no longer produced the earlier module-coverage
finding.

Local Qt 5.15.18 and Qt 6.10.2 complete suites each passed `6/6`. The Qt6 100k benchmark passed
with `100,000` entries / `25,000` source groups, model build `45 ms`, filter `1,071 ms`, peak RSS
`132,612 KiB`, and a `10,000 ms` budget.

## Scope after B2

B2 normalized model, Qt explorer, local-resource packaging, focused tests, and benchmark evidence
are complete for the 0.3.0 local candidate. Remote PR CI, the ici sticky comment, and independent
audits of all three PR Pages reports remain required before B2 is marked complete. B3
compiler-measured include explanation, B4 configuration diff, B5 hybrid release integration, and
the ici I3 target-by-target comparison remain future work.
