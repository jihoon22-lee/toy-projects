# DiskMap

DiskMap is a disk usage explorer for Linux. A Qt-free core does the scanning and
analysis, a small console tool exposes it, and a Qt Widgets shell adds the
treemap and the review workflows. It is built with CMake and targets Qt 6.

The scan is deliberately conservative about what it will claim. Directory
entries carry a `FileIdentity` of device and file id rather than a path, symlinked
directories are not followed unless asked, and filesystem boundaries are not
crossed unless asked. Anything the scan could not resolve is reported as
uncertain instead of being folded into a total.

## Binaries

| Target | Kind | What it is |
|---|---|---|
| `diskmap` | console | Qt-free CLI over the scanner, snapshots, and duplicate evidence |
| `diskmap-gui` | Qt Widgets app | treemap explorer with the cleanup, trash, and storage review workflows |
| `diskmap_core` | static library | scanner, treemap, snapshot, duplicate, cleanup, and trash logic |
| `diskmap_gui` | static library | widget layer, so the GUI is unit-tested like the core |

Cleanup and trash are GUI-only on purpose. They act on real files, so they stay
behind an interface that shows what will be touched before anything moves.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

CTest runs all 18 Qt Test binaries — one per test file.

## CLI

```text
Usage: diskmap <path> [options]
   or: diskmap --load-snapshot FILE [options]
  --max-depth N       limit scan traversal depth
  --follow-symlinks   follow symlinked directories
  --min-size BYTES    skip files smaller than BYTES
  --one-file-system   do not cross filesystem boundaries
  --exclude GLOB      skip matching entries (repeatable)
  --depth N           limit printed tree depth
  --top N             show the N largest files (default 10)
  --json              emit the tree as JSON instead of text
  --save-snapshot FILE save the scan as a bounded snapshot
  --load-snapshot FILE inspect a saved snapshot without scanning
  --compare-snapshot FILE compare the scan with a saved snapshot
  --diff-kind KIND    print only these change kinds (repeatable:
                      added removed grown shrunk moved uncertain)
  --diff-min-delta BYTES print only changes of at least BYTES
  --diff-certain-only print only certain (non-candidate) changes
  --duplicates        inspect duplicate evidence (review-only)
  --help              show this message
```

A snapshot comparison classifies each entry as added, removed, grown, shrunk,
moved, or uncertain, and reports whether the comparison as a whole was
uncertain. The `--diff-*` options narrow which changes are printed — they
filter the report only and never weaken the conservative classification. A
delta bound applies to the change in bytes and is proven only when both
metrics are known. The options combine and apply identically to text and
JSON output. Duplicate inspection is review-only: it reports evidence and
never deletes anything.

```sh
./build/gui/src/diskmap path/to/tree --save-snapshot before.json
./build/gui/src/diskmap path/to/tree --compare-snapshot before.json --json
./build/gui/src/diskmap path/to/tree --duplicates --json
./build/gui/src/diskmap --load-snapshot before.json --json
./build/gui/src/diskmap --load-snapshot before.json --duplicates
```

`--load-snapshot` does not require a scan path. Its JSON output is the
versioned snapshot document; `--compare-snapshot` and `--duplicates` emit
separate versioned report schemas. Snapshot writes use a same-directory
temporary file and atomic installation, and loading a snapshot is explicitly
read-only. The GUI exposes Save/Load/Compare snapshot buttons, a conservative
change table, and duplicate evidence (partial/full hashes, identity and
hard-link facts, and candidate confidence). A certain reclaimable copy can be
staged into the cleanup dry run, but nothing is moved until the normal
confirmation, revalidation, and recoverable Trash workflow completes.

Incomplete scans, stale identities, changing files, symlink targets, and
hard-link aliases remain visible as uncertain or non-reclaimable evidence
rather than becoming deletion instructions. Added/Removed certainty requires
complete structural evidence and a known size metric on the source entry plus
complete structural evidence on the opposite snapshot. Cleanup and Trash
paths reject relative `CleanupTarget.path` values before opening a parent
directory or mutating an entry; final execution revalidates identity, type,
size/allocation, and known hard-link evidence from the reviewed scan.

On Linux, snapshot installation takes a nonblocking advisory `flock` on the
anchored destination parent directory, and Trash move/restore takes one on the
anchored Trash root. This serializes only cooperating DiskMap mutating
operations for the same destination or Trash root; a contending operation
fails promptly instead of waiting or interleaving. Advisory locking does not
control a same-UID non-cooperating or malicious process that ignores the lock
and directly changes user-owned paths. No-follow descriptors, identity
revalidation, and rollback reduce ordinary path races and fail closed, but do
not claim to provide that stronger isolation boundary.

Malformed filesystem names cannot corrupt a report. Invalid UTF-8 bytes in a
path are escaped as `\u00XX`, so `--json` output stays valid JSON for any name
POSIX permits.

## Benchmark

`benchmarks/run_benchmark.py` runs a full scan and a cooperative cancellation
against a deterministic generated source without creating real files. The
runner's default budgets are full throughput `≥ 100000 entries/s`, full peak
RSS `≤ 1536 MiB`, full elapsed `≤ 30000 ms`, and cancellation elapsed
`≤ 2000 ms`, with a 60-second hard timeout per process. Pass `--skip-budgets`
when only harness correctness matters.

```sh
cd diskmap
benchmark_root="$(mktemp -d /tmp/diskmap-benchmark.XXXXXX)"
artifact_dir="$benchmark_root/artifact"
mkdir -p "$artifact_dir"
cmake -S . -B "$benchmark_root/build" -DCMAKE_BUILD_TYPE=Release \
  -DDISKMAP_BUILD_BENCHMARKS=ON
cmake --build "$benchmark_root/build" --parallel 2 \
  --target diskmap-scan-benchmark
python3.10 benchmarks/run_benchmark.py \
  --binary "$benchmark_root/build/benchmarks/diskmap-scan-benchmark" \
  --entries 1000000 --cancel-after 10000 --timeout-seconds 60 \
  --output-dir "$artifact_dir"
```

A local 1,000,000-entry run measured full-scan throughput at
`~207k entries/s` with `~1.06 GiB` peak RSS, and 10,000-entry cancellation in
`~2.7 ms`. Numbers vary with scheduler and host RSS. The scheduled CI
benchmark workflow runs the same harness on Qt6; a typical CI run measured
`~320k` entries/s.

## Status

DiskMap is `0.1.0`. Release history lives in
[CHANGELOG.md](../CHANGELOG.md).
