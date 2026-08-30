# diskmap D1 safe-scan contract tests

## Overview

Added deterministic scanner contract tests for the second diskmap D1 slice. The
tests define the public behavior needed before storage cleanup features can use
scan results: filesystem-path traversal, visited-identity cycle prevention,
hard-link-aware logical/allocated/reclaimable totals, and explicit unknown
physical metadata.

## Context

The first D1 slice preserved link/target metadata and POSIX identity facts, but
the scanner still had no executable contract for following directory symlinks
or aggregating physical storage. A real temporary filesystem would make these
checks depend on allocation units and host symlink support, so the new tests use
`FakeFsSource` and fixed identities/allocated sizes.

## Changes Made

- `diskmap/tests/test_scanner_safety.cpp`
  - verifies nested paths constructed from `std::filesystem::path` boundaries,
    including spaces in path components;
  - verifies a followed directory link whose target identity was already
    visited remains visible, is marked `cycle_skipped`, and is not expanded;
  - verifies two hard-link entries add logical bytes per entry but allocated
    bytes once, and that a subtree containing only one reference is not
    reclaimable;
  - verifies missing allocation/link-count facts leave aggregate and node
    storage totals explicitly unknown rather than silently reporting zero.
- `diskmap/tests/test_scanner_safety.pro` and `diskmap/tests/tests.pro`
  - register the contract binary in qmake's test subdirectory.

## Verification Results

The contract tests intentionally target the D1 Slice 2 API (`FsNode` storage
totals and `cycle_skipped`) which is implemented by the companion production
branch. Until that branch is integrated, this test-only branch cannot compile
the new binary against the Slice 1 model. Existing Slice 1 sources remain
unchanged; Qt5/Qt6 execution is to be rerun after the production branch adds
the declared fields and traversal behavior.

## Next Steps

- Integrate the companion scanner implementation and run the complete qmake
  Qt5/Qt6 matrix plus ici verification.
- Keep the tests as the D1 storage semantics gate for later cleanup staging.
