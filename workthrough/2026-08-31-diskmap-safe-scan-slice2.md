# diskmap D1 safe traversal and path semantics — Slice 2

## Overview

Completed the second D1 slice for diskmap. The scanner now has an identity-safe
traversal contract, portable filesystem-path boundaries, and physical storage
aggregation suitable for the cleanup features planned in D2–D6. The slice is
covered by deterministic fake-source tests and real POSIX filesystem tests;
Qt5/Qt6 qmake builds and the public ici 0.6.0 verification were also rerun.

Remote PR CI, the sticky HTML comment, and the published Pages responses are
not part of this local evidence yet. The branch must pass those checks before it
is merged.

## Context

The previous D1 slice preserved `lstat`/`stat` metadata, link-versus-target
facts, and explicit incomplete states, but it still left three safety gaps:

- following a directory symlink could revisit the same physical directory;
- hard-link and symlink aliases could distort physical storage totals;
- scanner code still reconstructed child paths as strings.

The new contract keeps every directory entry visible while preventing unsafe
expansion and distinguishes logical bytes from physical allocation and
reclaimable bytes. It also retains unknown facts instead of presenting a
guessed zero.

## Changes Made

### 1. Identity-safe traversal and path boundary

Production commits: `f4bc717` and `9c50fd0`.

- `FsNode::path`, `DirEntry::path`, `FsSource::list`, and `scanner::scan` now
  use `std::filesystem::path` at the source/scanner boundary.
- The scanner tracks valid physical `FileIdentity` values while expanding
  followed directories. A cycle or previously visited target remains as a
  visible `cycle_skipped` node with no children.
- A followed directory whose target identity is unavailable is not expanded;
  the node is retained as `complete=false` with an explanatory error.
- Directory scheduling is flattened into an iterative pass so the safety
  checks do not add avoidable nesting or recursion risk.

### 2. Logical, allocated, and reclaimable storage

Production commit: `f4bc717`.

- `FsNode::size` remains the logical aggregate and counts each directory entry.
- `allocated_size` deduplicates valid physical identities within each subtree.
- `reclaimable_size` is known only when all known hard-link references are
  owned by that subtree; a subtree containing one reference of a multi-link
  file is therefore not marked reclaimable.
- A followed symlink target is not treated as an owned hard-link reference.
- Unknown allocation/link-count facts and incomplete descendants propagate
  through the `*_known` flags instead of being silently reported as zero.
- A finite `max_depth` leaves each pruned directory visible but marks it
  `complete=false` with `scan depth limit reached`; its allocated and
  reclaimable aggregates therefore remain unknown.
- Logical size has no separate known bit, so `uint64_t` overflow saturates at
  `std::numeric_limits<uint64_t>::max()` instead of wrapping.

### 3. Contract and real-filesystem coverage

Test commits: `02851f7`, `3fdeb55`, and `0a7b013`.

- `FakeFsSource` scenarios cover nested paths containing spaces, canonical and
  symlink aliases, directory back-edges, identityless followed directories,
  hard-link ownership, unknown physical metadata, and aggregate overflow.
- A real POSIX temporary-filesystem test checks root identity, hard-link
  deduplication, and a symlink back-edge using the host filesystem metadata.
- The qmake test manifest now contains nine test binaries, including the new
  safe-traversal and real-filesystem safety targets.

### 4. Reproducible qmake coverage linkage

Build fix commit: `c65f0d8`.

Static qmake consumers now declare the archive through `PRE_TARGETDEPS`, so a
test binary is relinked when the static library changes. This closes the stale
archive path that can otherwise leave old `.gcda` files paired with newer
`.gcno` files and produce misleading coverage.

### 5. Conservative truncation follow-up

Test commit: `5ceb059`; production commit: `ebe3d86`.

- Added regression coverage for finite-depth pruning and logical-size overflow.
- The scanner now records the exact `scan depth limit reached` error on every
  directory it cannot expand because of `max_depth`.
- Physical totals become unknown when that incomplete subtree is present, while
  logical totals remain conservative through saturating addition.

The native and ici measurements below were collected before this follow-up.
They are retained as the Slice 2 baseline; full re-verification after these
commits and all remote PR/report checks are still pending.

### 6. Scan-root semantics follow-up

Test commit: `6861b7a`; production commit: `2f56caf`.

- A regular-file root is now a complete one-node scan rather than a failed
  directory listing. Its logical, allocated, and reclaimable facts are
  aggregated normally and `files_scanned` is one.
- The explicitly selected root symlink is always dereferenced, independent of
  the descendant `follow_symlinks` setting. A symlink-to-file is a leaf.
- A broken root target remains incomplete and contributes its inspection error
  to `ScanResult.errors`.
- A real CLI file-root smoke emitted JSON with `name=main.cpp`,
  `is_dir=false`, and a source-dependent logical `size`; the size is not
  pinned because the source file may change.

The follow-up refactor `31d8b48` separated root inspection stages. Its public
ici complexity-only check passed with maximum cyclomatic 14 across 101
functions and 0 issues. The final candidate verification is recorded below.

After this root-semantics follow-up, the latest local native verification was
Qt 5.15.18 (`/usr/bin/qmake`) and Qt 6.10.2 (`/usr/bin/qmake6`) with full build
target plus `make check` 9/9 PASS on both. The Qt5 and Qt6 offscreen GUI smokes
also survived 8 seconds and exited with the expected timeout 124.

The final local candidate ici qmake-clean branch reported `Suite PASS`, 10 pass
/ 0 warn / 0 fail / 0 error / 2 skip, 9/9 tests, line 96.6% / function 98.0%
/ branch 85.0%, TEM 4.90, complexity 14 across 101 functions / 0 issues,
duplication 2.0, sanitizer clean, and duration 85.96 s. Its capability
inventory contained 30 tools / 21 ready / 0 incomplete / 9 unavailable;
required `g++` was ready and health was `READY`. Candidate JSON retained
successful `/usr/bin/make clean` evidence for both test and sanitize, and the
tool snapshot was rendered. HTML was 281264 bytes with external `src`/`href`
references 0. This supersedes the immediately earlier public ici 0.6.0
complexity WARN-era result and confirms the freshness guard. The final local
ici evidence is complete. The guard itself merged through ici
[PR #94](https://github.com/jihoon22-lee/ici/pull/94) as commit
[`1af6d64`](https://github.com/jihoon22-lee/ici/commit/1af6d64bef346720c2cfad656e16cd1a108324f5):
all checks in [run 33338430771](https://github.com/jihoon22-lee/ici/actions/runs/33338430771)
passed, the [sticky comment](https://github.com/jihoon22-lee/ici/pull/94#issuecomment-5471591533)
contained both current report links, and ici/viewer Pages returned HTTP 200
with zero external references. The DiskMap toy PR CI, sticky HTML comment, and
Pages verification remain pending.

## Code Examples

The public boundary is now path-typed:

```cpp
ScanResult scan(const FsSource& source,
                const std::filesystem::path& rootPath,
                const ScanOptions& options,
                const ProgressFn& progress = nullptr);
```

Physical aggregation is intentionally separate from logical aggregation:

```cpp
aggregateSizes(node);   // logical bytes per directory entry
aggregateStorage(node); // identity-aware allocated/reclaimable facts
```

## Verification Results

### Initial coverage failure and correction

The first post-test run was intentionally treated as a real failure. Its
`.gcda` files carried stamp `1417858375` while the matching `.gcno` files had
stamp `1418147347`; as a result `scanner.cpp` was measured as 0% and the suite
reported line 73.4%, function 83.3%, and branch 60.2%. The qmake
`PRE_TARGETDEPS` fix was then applied and all static consumers were rebuilt.

### Native Qt matrix

Both fresh qmake legs passed:

```text
Qt 5.15.18  /usr/bin/qmake  : full build + make check — 9/9 PASS
Qt 6.10.2   /usr/bin/qmake6 : full build + make check — 9/9 PASS
```

The Qt5 and Qt6 GUI executables also remained alive for the complete 8-second
offscreen smoke window. The timeout exit code 124 is the expected result for
this long-running GUI smoke check.

### ici verification

Using the public ici 0.6.0 release asset before the conservative truncation
follow-up:

```text
Suite PASS
10 pass / 0 warn / 0 fail / 0 error / 2 skip
TEM 4.90
line 96.9% / function 97.9% / branch 85.2%
maximum complexity 14 · duplication 2.1 · duration 24.24 s
HTML: /tmp/diskmap-safe-scan-relinked.html, 180624 bytes, external refs: 0
```

The HTML report uses inline assets only; no external `src` or `href` reference
was found.

## Next Steps

- Open the diskmap Slice 2 PR and wait for every native/ici matrix leg and
  `Merge Gate` to pass.
- Confirm the actual sticky PR comment contains the diskmap HTML link and that
  the linked GitHub Pages document responds as `text/html` with no external
  references.
- After that remote evidence is recorded, continue with D2 cancellation,
  rescan generation guards, and stale-result rejection.
- Keep D4 cleanup staging dependent on the identity and `*_known` contracts
  documented here.
