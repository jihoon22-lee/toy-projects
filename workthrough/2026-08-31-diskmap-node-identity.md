# Diskmap D1 identity-safe filesystem model — Slice 1

## Overview

This slice implements and tests the first D1 boundary: the filesystem metadata
model and the `RealFsSource` adapter preserve physical identity,
link-versus-target facts, and incomplete-state information. The scanner's
cycle handling, hard-link aggregation, and portable path abstraction remain a
subsequent slice.

## Context

The old source test only checked basenames, compatibility booleans, and one
file size.  That was insufficient for cleanup-safe identity checks: a symlink
could be confused with its target, a hard-link pair could look like two
independent files, and a dangling link could lose the fact that its own lstat
record was still complete.  The D1 contract now has a shared `FsMetadata`
model and explicit target metadata, so the tests exercise those distinctions
against a real temporary filesystem.

## Changes Made

### 1. Production metadata and source contract

Files: `diskmap/include/diskmap/fs_metadata.hpp`,
`diskmap/include/diskmap/fs_source.hpp`, `diskmap/include/diskmap/fs_node.hpp`,
`diskmap/src/fs_source.cpp`, `diskmap/src/scanner.cpp`, and
`diskmap/src/fs_node.cpp`

- Added `FileIdentity` and `FsMetadata`. Portable or filesystem-dependent
  values are accompanied by explicit `*_known` flags for allocated bytes,
  hard-link count, permissions, ownership, and modification time. `complete`
  means that the metadata read itself succeeded; an unavailable optional field
  does not turn a successful read into a guessed value.
- On POSIX, `lstat(2)` records the link entry itself and `stat(2)` records a
  followed target. This keeps a symlink's identity/kind/size separate from its
  target's identity/kind/size, including for dangling links. The portable
  Windows fallback uses `symlink_status`/`status` and leaves unsupported
  identity and allocation facts unknown.
- `RealFsSource` propagates a normalized full entry path in `DirEntry`. The
  scanner copies that path and all metadata into `FsNode`; it no longer has to
  reconstruct a child path from a display name. The node still stores its
  public path as a string until Slice 2 completes the path abstraction.
- Scan completeness is separate from stat completeness. If opening or walking
  a directory fails, the corresponding `FsNode` is retained with
  `complete=false` and the error, while sibling work continues. A successful
  directory stat therefore cannot be mistaken for a complete child listing.
- `FsNode::size` remains the scan/aggregate value (the sum of included child
  sizes for directories), while `FsMetadata::logical_size` remains the value
  reported for that node by stat. This prevents aggregate totals from
  overwriting source metadata.
- Directory iteration uses an explicit `increment(error_code)` checkpoint;
  open and iterator-step failures are returned as listing errors instead of
  being silently ignored. Entries collected before a step error remain
  available from the source adapter, while the scanner marks the listing
  incomplete when it consumes the error.

### 2. Real filesystem source coverage

File: `diskmap/tests/test_fs_source.cpp`

- Added one atomically-created `ScopedTempDirectory` whose destructor uses
  `std::filesystem::remove_all` with an error code.  Standard filesystem
  removal removes symlink entries and does not traverse their targets.
- Added regular-file and directory checks for full paths, `FsKind`, complete
  identity, logical size, hard-link count, permissions, owner, group, and
  nanosecond modification time.  POSIX fields are compared with `stat(2)` or
  `lstat(2)` on POSIX hosts.
- Added a hard-link pair check requiring equal valid `FileIdentity` values and
  `hard_link_count >= 2`.
- Added a sparse-file check that always requires the logical size.  Allocation
  is checked only when `allocated_size_known` is true; an unknown allocation
  must retain the zero/default value.
- Added valid-symlink checks proving that lstat/link identity and kind differ
  from the followed target metadata, while the target identity matches the
  ordinary file.
- Added dangling-symlink checks proving that link metadata is complete while
  target metadata is incomplete and carries an error.
- Kept missing-directory and virtual `FsSource` dispatch/error coverage.

### 3. FsNode and identity model coverage

File: `diskmap/tests/test_fs_node.cpp`

- Added `FileIdentity` equality checks for equal, different, and invalid
  identities.
- Added a model fixture that stores link metadata and followed target
  metadata simultaneously, then changes only the target state to represent a
  dangling link.  The link kind/identity and compatibility fields remain
  intact while `complete`, `error`, and `has_target_metadata` describe the
  failed target lookup.
- Existing aggregate, sort, lookup, count, top-file, and depth-guard tests
  remain in place.

### 4. Scanner failure-state coverage

File: `diskmap/tests/test_scanner.cpp`

- A directory whose listing fails remains in the tree as an incomplete node
  with its full path and error, while successfully listed siblings still
  contribute to the aggregate.
- The scanner's compatibility counters and aggregate `FsNode::size` continue
  to operate on the propagated nodes; they do not mutate the stat-level
  `metadata.logical_size` contract.

## Code Examples

```cpp
CHECK(linkEntry->metadata.identity != fileEntry->metadata.identity);
CHECK(linkEntry->target_metadata.identity == fileEntry->metadata.identity);
CHECK(linkEntry->target_metadata.identity != linkEntry->metadata.identity);
```

```cpp
CHECK(sparseEntry->metadata.allocated_size_known ||
      sparseEntry->metadata.allocated_size == 0U);
if (sparseEntry->metadata.allocated_size_known) {
    CHECK(sparseEntry->metadata.allocated_size <= sparseLogicalSize);
}
```

## Verification Results

The two changed test translation units first passed compiler-only syntax checks
with the D1 headers:

```text
g++ -std=c++17 -Wall -Wextra -pedantic -fsyntax-only diskmap/tests/test_fs_source.cpp ...
exit code: 0
```

After the production source adapter was restored, both complete qmake/make
legs built every diskmap subproject and ran every `make check` target:

```text
Qt 6: qmake6 + full project make, all `make check` targets: All checks passed
Qt 5: qmake  + full project make, all `make check` targets: All checks passed
```

The evidence above is limited to the Qt5/Qt6 qmake builds and their complete
`make check` suites. No ici verification or GUI smoke result is claimed here;
those are release/PR evidence to be collected after the implementation is
reviewed and integrated.

## Next Steps

- In the next scanner slice, follow directory symlinks only with visited
  `FileIdentity` cycle protection, aggregate hard-link storage/reclaimable
  bytes without double-counting, and replace string path joins with the
  filesystem path abstraction.
