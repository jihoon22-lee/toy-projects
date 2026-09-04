# DiskMap Qt-free view projection

## Overview

The first D3 explorer slice adds a view-only core API for projecting a scanned
`FsNode` tree. It keeps logical, allocated, and reclaimable values explicit,
preserves incomplete/cycle/depth/mount diagnostics, and provides deterministic
filtering and ordering without importing Qt or changing the existing scanner
contract.

## Changes Made

- Added `diskmap/include/diskmap/view.hpp` and `diskmap/src/view.cpp`:
  - `SizeMetric` and `MetricValue` select the three size meanings.
  - Logical known-ness is derived iteratively from subtree completeness and
    cycle/depth/mount markers. Scanner-level `ScanResult::totals_filtered`
    remains explicit input rather than being confused with view predicates.
  - `NodeKey` normalizes paths and includes entry kind, followed state, and
    only a valid optional entry identity.
  - `NodeIssue` retains cycle, mount-boundary, depth-limit, incomplete,
    metadata, generic-error, and scanner-filtered classifications.
  - `visibleChildren` and `largestFiles` apply conjunctive type/search/size/age
    filters, use literal ASCII case-insensitive basename/full-path search,
    place unknown size values last in either sort direction, and use key-based
    deterministic ties. The largest-files result contains regular entries only;
    symlink aliases are not duplicated as target files.
- Added the Qt-free `test_view` target and coverage for sparse allocation,
  hard-link identity, symlink target facts, incomplete/cycle/depth/mount
  states, unknown metric/age handling, ordering, literal search, and Unicode /
  space-bearing paths. The repository has no Catch2 dependency (and its
  `ICI-GAPS.md` records that gap), so this target follows the existing
  hand-rolled `assert.hpp` convention rather than introducing an unpinned test
  framework.
- Added the core source/header to qmake and centralized an explicit C++17 mode
  in `diskmap/cxx17.pri`, which every compile-producing qmake leaf includes so
  shadow builds do not depend on top-level configuration inheritance.
- Corrected the README, master-plan, and `test_fs_node` overflow wording and
  assertion to document and verify `FsNode::logical_size_known`.
- Recorded this as the completed D3 core projection checkbox and an Unreleased changelog entry.
  The GUI treemap/table integration and explorer UX were subsequently completed and merged by
  [PR #46](https://github.com/jihoon22-lee/toy-projects/pull/46); the canonical PR/main evidence
  table is in the [D3 explorer workbench workthrough](2026-09-02-diskmap-explorer-workbench.md).

### Audit corrections

- Propagated completeness uncertainty through logical and physical projections,
  including incomplete, cycle, mount-boundary, depth-limit, and scanner-filtered
  provenance.
- Preserved the current node's provenance while recursively collecting largest
  files, and kept the top-N candidate state bounded at `O(depth + limit)`.
- Marked physical metrics as non-additive (`additive=false`) when identity or
  completeness facts cannot support an exact total. Existing physical
  aggregates intentionally exclude directory metadata blocks; a future UI must
  label that scope explicitly, and physical values from sibling subtrees must
  not be summed because hard-linked identities can overlap.
- Applied the shared explicit C++17 qmake configuration to every compile
  target, including independently configured shadow-build leaves.

## Verification Results

### Native qmake builds

- The implementation was merged by PR #45 as exact toy-projects `main`
  `0688e44fa99d1ec69aba0c9bf9995a4a857fea9e`.
- PR workflow `33607634973` and exact-main workflow `33608884643` both completed
  successfully, including the required checks, Qt5/Qt6 matrix, ici verification,
  benchmarks, report publication, and Merge Gate.
- Qt 6.10.2 and Qt 5.15.18 both completed a full DiskMap build; all 10
  `make check` binaries passed, including GUI test suites of 6/6 and 10/10.
- Standalone `g++ -std=c++17 -Wall -Wextra -Werror -pedantic` compilation
  passed.

### ici verification

The final no-cache verification with the versioned ici v0.10.2 local candidate
exited 0 with `WARN` only because local `clang-tidy` and `clazy` are unavailable: 11 pass, 1 warn, and 2
skip; test coverage is 10/10, TEM is 4.95, and line/function/branch coverage is
95.3%/99.0%/81.3%. The compile database check is 11/11 across 23
configurations; maximum complexity is 14 across 176 functions with no issues,
and duplication is 3.24%.

This historical local verification used a versioned ici v0.10.2 local candidate
(not the public release asset) with SHA-256
`2af5198d1348a64c39f4f37d12657aa9a2c4bf3ddf034a9099909c41e86e30e7`.
The generated HTML report is 356,798 bytes with SHA-256
`6c5c2346ac1b309c7fb827608b05c1cac7ffb8d83666c92843a8bbd9b59450a1`; its
title, UTF-8 encoding, and zero-CDN invariant were verified.

The project version and release were intentionally unchanged. PR #45 and its
exact-main CI evidence are complete, and the overall D3 GUI integration was later
merged by PR #46 with exact-main evidence. D4-D7 cleanup, trash, snapshot, and
release criteria remain pending.
