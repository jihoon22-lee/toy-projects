# DiskMap storage evidence workbench integration

## Overview

DiskMap now exposes the existing bounded snapshot and duplicate-analysis cores
through the aggregate qmake build, Qt GUI, and CLI. The GUI keeps loaded
snapshots read-only and sends only certain reclaimable duplicate copies into
the existing cleanup dry run; the final action remains the confirmed,
revalidated recoverable-Trash operation.

## Context

The storage cores and focused standalone tests were present, but the parent
qmake graph did not compile all of their translation units. The explorer also
had no user-facing path to save/load/compare a snapshot or inspect duplicate
hash and identity evidence. This left the practical D6 workflow disconnected
from the already safety-gated D4/D5 cleanup path.

## Changes Made

### Aggregate build and tests

- `diskmap/src/src.pro` now includes duplicate analysis, snapshot persistence,
  and split Trash implementation units in `diskmap_core`.
- `diskmap/tests/tests.pro` registers duplicate and snapshot tests alongside the
  existing qmake tests and serializes subproject builds to avoid same-directory
  object-name collisions. The aggregate now has 17 `test_*.pro` leaves, including
  the new `test_storage_cli`; the existing cleanup and Trash leaves are included.
- Every qmake test leaf links the actual `libdiskmap_core.a` through `LIBS` and
  `PRE_TARGETDEPS` (GUI leaves also link `libdiskmap_gui.a`) instead of compiling
  core sources again in the leaf. This keeps the aggregate and focused tests on
  the same production library and avoids duplicate coverage objects.
- `diskmap/tests/test_storage_workbench.cpp` and its `.pro` cover GUI control
  discovery, snapshot round-trip/read-only state, conservative comparison
  rows, and duplicate staging into cleanup review.
- `ci/test_check_manifest.py` now compares every `diskmap/tests/test_*.pro` file
  with the names registered by `diskmap/tests/tests.pro`, so a new qmake test
  cannot silently remain outside the aggregate check.
- `diskmap/src/gui/gui.pro` includes the storage workbench translation unit;
  `diskmap/src/cli.pro` links the report formatting helpers.

### GUI workflow

- `MainWindow` adds Save/Load/Compare snapshot actions and a bounded change
  table showing added, removed, grown, shrunk, moved, and uncertain evidence.
- Duplicate analysis runs through `QtConcurrent` with cancellation/progress,
  displays confidence, paths, hashes, and reclamation decisions, and stages
  only certain reclaimable copies through the existing `QUndoStack` cleanup
  staging command.
- Loaded snapshots are marked read-only in the banner/status and disable
  rescan, staging, and Move-to-Trash controls; the cleanup slots also reject
  snapshot documents if invoked programmatically. A live rescan clears stale
  snapshot/duplicate evidence.

### CLI and documentation

- `diskmap/src/main.cpp` supports `--save-snapshot`, `--load-snapshot`,
  `--compare-snapshot`, and `--duplicates`.
- `diskmap/src/storage_cli.cpp` emits text and versioned JSON schemas for
  snapshot diffs and duplicate evidence while preserving the legacy scan-tree
  JSON shape for ordinary scans. Its shared string encoder preserves valid
  UTF-8 and escapes malformed POSIX filename bytes as `\u00XX`, so legal
  filesystem names cannot invalidate report JSON.
- `README.md` documents CLI examples and the read-only/recoverable-Trash
  safety boundary.

### Storage I/O safety hardening

- On Linux, snapshot paths are converted to an absolute lexical path and the
  parent directory is walked component-by-component with no-follow directory
  descriptors. Snapshot filenames are then opened relative to that anchored
  parent, so a symlinked parent cannot redirect the read or write.
- Snapshot reads open with `O_NOFOLLOW` and `O_NONBLOCK`, require a bounded
  regular file, read exactly the checked size, and recheck descriptor
  device/inode, size, and mtime after the read. Snapshot writes use a private
  same-directory temporary; a missing destination is installed with a
  no-replace link, while an existing regular-file destination is installed via
  atomic `RENAME_EXCHANGE` after destination revalidation. A displaced entry
  that no longer matches is exchanged back, with the temporary preserved if
  recovery itself fails.
- Production duplicate reads use bounded `pread` ranges on an
  `O_NOFOLLOW`/`O_NONBLOCK` descriptor. The descriptor must remain a regular
  file and match the retained device/inode, logical size, mtime, and any known
  hard-link count both before and after each range; a race is reported as
  uncertain evidence rather than accepted bytes.
- Trash setup and capability probing walk absolute directory components without
  following symlinks, create only after a no-follow preflight, and verify owned
  `0700` directories, writability, and same-device placement. Restore metadata
  is opened no-follow and nonblocking, then accepted only as a bounded regular
  file, so a raced FIFO cannot block recovery.
- If a post-move validation or metadata-finalization failure blocks no-replace
  rollback, the moved payload remains in Trash. Recovery metadata is rewritten
  from the payload's actual identity/kind and a restore token is retained when
  that metadata can be finalized. Snapshot truncation/incomplete evidence,
  duplicate read/revalidation failures, and cleanup subtree incompleteness
  continue to propagate to incomplete/uncertain downstream decisions.
- Snapshot JSON preserves valid UTF-8 and normalizes escaped surrogate pairs to
  UTF-8 while rejecting malformed sequences and unpaired surrogates. The CLI
  report encoder preserves valid UTF-8 and escapes malformed POSIX bytes as
  `\u00XX`.

### Cooperative operation serialization and threat model

- Linux snapshot installation takes a nonblocking advisory `flock(LOCK_EX|LOCK_NB)`
  on the anchored destination parent directory. Trash move/restore takes a
  corresponding lock on the anchored Trash root for the operation lifetime.
  Cooperating DiskMap mutating operations for the same destination or Trash root
  therefore fail promptly on contention instead of interleaving; they are not
  queued.
- This lock scope does not make snapshot reads, duplicate analysis, cleanup
  planning, or capability probes globally serialized. It coordinates only the
  DiskMap operations that acquire the same advisory lock.
- A same-UID non-cooperating or malicious process can ignore `flock` and directly
  change user-owned paths, which is outside this guarantee. The no-follow,
  descriptor-revalidation, and rollback checks reduce ordinary path races and
  fail closed, but do not claim mandatory isolation from that actor.

### Deterministic recovery interleavings

- A Linux-only internal thread-local hook lets real-filesystem tests mutate paths
  immediately before metadata reservation, payload moves, rollback, and restore.
  Production does not install the hook.
- Tests cover foreign metadata/payload collisions, post-move identity changes,
  source/restore parent replacement, blocked rollback, missing payloads, and a
  restore destination appearing after the absence check. A foreign no-replace
  claimant is preserved rather than unlinked.
- Metadata files, payload directories, and metadata directories are synced in
  publication order. Rollback sync failure is reported as `IoError`; false
  recovery tokens are omitted, while a valid token for a payload retained in
  Trash remains visible in the GUI even when the operation status is not
  `Moved`.

### Cleanup target safety

- Move-to-Trash validation rejects a `CleanupTarget.path` that is relative,
  malformed, or lacks a stable identity before opening a parent directory or
  mutating an entry. A relative target therefore returns `InvalidRequest` and
  is never accepted as a mutation target; any normalized path is used only for
  the bounded rejection receipt.
- Cleanup planning continues to compare path components (rather than string
  prefixes), and final execution revalidates identity, type, logical size,
  allocation, and known hard-link count against the reviewed target.
- Unsupported-kind/deletion rejection is fail-closed: it does not publish a
  false Trash path or restore token. The move and restore implementations are
  split into separate translation units so each remains within the line gate.

### Evidence correctness and quality-gate follow-up

- `snapshot_json_dom.hpp/.cpp` now owns the bounded JSON tokenizer/value parser;
  `snapshot_json_parser.cpp` is the smaller snapshot-to-model validation layer.
  The public `diskmap.snapshot/v1` schema and rejection behavior are unchanged.
- `MainWindow::updateControlState()` delegates activity, navigation, explorer,
  cleanup, evidence, and restore state transitions to focused helpers.
  CLI escaping and duplicate JSON rendering use similarly focused helpers;
  enum-name mappings are table-driven so the released ici complexity gate sees
  the same behavior without high-complexity switch bodies.
- Duplicate evidence now limits `max_groups` by retained result groups, not by
  partial buckets. Hash-candidate byte verification distinguishes true
  `HashMismatch` from `ReadError`, while cancellation remains `Cancelled`.
  Regression coverage injects a mismatch/read failure in an earlier bucket and
  confirms a later valid group is retained without a false `GroupLimit`; it
  also covers two full-hash groups in one partial bucket and a later bucket
  whose full hashes do not form a group.
- Snapshot Added/Removed certainty now requires complete, structurally complete
  evidence on both the source and opposite snapshot. Incomplete-before and
  incomplete-after comparisons remain visible changes but are not marked
  certain.

## Code Examples

```bash
./build/gui/src/diskmap /path/to/tree --save-snapshot before.json
./build/gui/src/diskmap /path/to/tree --compare-snapshot before.json --json
./build/gui/src/diskmap --load-snapshot before.json --duplicates
```

The GUI's “Stage safe duplicate copies” action only updates the dry-run review;
the existing “Move to Trash…” action remains the sole mutation path.

## Verification Results

### Focused evidence tests

```text
Qt 6.10.2: test_duplicates, test_snapshot: All checks passed
QtTest 6.10.2: TestStorageWorkbench 5 passed, 0 failed (3 test slots plus QTest lifecycle hooks)
Qt 5.15.18: test_duplicates, test_snapshot: All checks passed
QtTest 5.15.18: TestStorageWorkbench 5 passed, 0 failed (3 test slots plus QTest lifecycle hooks)
```

### Aggregate Qt matrix

```text
Qt 6.10.2 (/usr/bin/qmake6): fresh shadow qmake + make -j2 PASS
  QT_QPA_PLATFORM=offscreen make check: exit 0, 17/17 check targets PASS
  TestTreemapWidget: 12; TestMainWindow: 29 slots / 31 PASS; TestStorageWorkbench: 3 slots / 5 PASS;
  TestNodeTableModel: 11; test_storage_cli: PASS; test_cleanup/test_trash aggregate targets: PASS
Qt 5.15.18 (/usr/bin/qmake): fresh shadow qmake + make -j2 PASS
  QT_QPA_PLATFORM=offscreen make check: exit 0, 17/17 check targets PASS
  TestTreemapWidget: 12; TestMainWindow: 29 slots / 31 PASS; TestStorageWorkbench: 3 slots / 5 PASS;
  TestNodeTableModel: 11; test_storage_cli: PASS; test_cleanup/test_trash aggregate targets: PASS
```

The pre-existing cleanup/trash leaves remain available as individual focused
tests and are now also registered in the aggregate qmake manifest, which has 17
check subprojects. The new storage workbench and CLI report tests passed under
both Qt majors. A
three-second offscreen application smoke stayed alive until timeout for both
`diskmap-gui` binaries (exit 124, expected).

### Released ici v0.10.2 deep verification

```text
ici line --report: PASS — 13,041 total lines, 11,500 code lines across 60 files, 0 non-info findings
ici complexity --report: PASS — max CC 15 (limit 15) across 594 functions, 0 issues
```

The generated `line_report.json` and `complexity_report.json` files were used
for review and removed after verification; they are not part of the worktree.

The released ici `v0.10.2` deep no-cache result for this integrated tree was
`WARN` (`10 PASS / 2 WARN / 0 FAIL / 0 ERROR / 2 SKIP`). It recorded `17/17`
tests, `37/37` compile-database production units across `58` configurations,
line/function/branch coverage `94.4% / 99.2% / 81.0%`, TEM `4.96`, and sanitizer
`PASS`. Lint and duplication are the only WARN sources. The verified released
artifact SHA-256 is
`8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4`.
The JSON artifact records source commit
`873b34a92f85da850df6282d08d78f3ac1d5079c`; the Zero-CDN HTML is `1,251,949`
bytes with SHA-256
`2d5dc67d0e2c48adfd6f050bf7d9179e18c235d639206a6d8be71dc8249ff697`
and exact title `ici Verification Report — diskmap`. The WARN/SKIP boundary
remains explicit; this evidence does not create a DiskMap release or change its
`0.1.0`/`Unreleased` status.

### CLI smoke

```text
Qt 6 CLI: save/compare/load/duplicates JSON smoke PASS
Qt 5 CLI: save/compare/load/duplicates JSON smoke PASS
Qt 6 GUI: 3-second offscreen smoke alive, expected timeout 124
Qt 5 GUI: 3-second offscreen smoke alive, expected timeout 124
```

`--duplicates --json` on two identical temporary files reported one group with
two entries and `reclaimable=true`. `git diff --check` also passed.

No DiskMap version bump or release is part of this integration evidence.

## Next Steps

- Keep the two WARN and two SKIP reasons, cross-project acceptance, and D7
  release decision separate from this local deep evidence.
- Keep release/version and merge decisions separate from this integration.
