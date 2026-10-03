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

CTest runs all 19 Qt Test binaries — one per test file.

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
  --cleanup-plan      dry-run plan staging certain reclaimable
                      duplicate copies (nothing is moved)
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
./build/src/diskmap path/to/tree --save-snapshot before.json
./build/src/diskmap path/to/tree --compare-snapshot before.json --json
./build/src/diskmap path/to/tree --duplicates --json
./build/src/diskmap --load-snapshot before.json --json
./build/src/diskmap --load-snapshot before.json --duplicates
./build/src/diskmap --load-snapshot after.json --compare-snapshot before.json
./build/src/diskmap path/to/tree --cleanup-plan
```

`--cleanup-plan` is a dry run: it runs the duplicate analysis, stages every
certain reclaimable copy except each group's selected keeper — first path by default,
or the requested newest/oldest/preferred-directory policy — and prints the
targets, per-target rejections with stable reasons, and the reclaimable byte
total. Nothing is moved or deleted; the plan requires a live scan path and
cannot run against a loaded snapshot or inside `--compare-snapshot`.

`--load-snapshot` combined with `--compare-snapshot` diffs two saved
snapshots without scanning: the compare file is the baseline and the loaded
snapshot is the current state. The `--diff-*` report filters apply to both
online and offline comparisons.

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

DiskMap is at the **0.2.0 development checkpoint (not published)**. Release history and
unreleased changes live in [CHANGELOG.md](CHANGELOG.md).


## Storage review and recovery (0.2.0 development checkpoint)

The GUI separates **Explore**, **Duplicates**, **Snapshot changes**, and **Cleanup & recovery**.
Treemap selection and table selection are linked. Color modes group file types or names, expose
scan uncertainty, or show a completed snapshot comparison (orange added/grown, blue shrunk,
purple moved/uncertain, gray no observation). The uncertainty pattern remains visible in every mode.
Filters accept `500 MB`, `1.5 GiB` and plain bytes; SI units are decimal and IEC units are binary.
Displayed sizes use explicit KiB/MiB/GiB labels. Nonintegral byte quantities are rejected.

Duplicate review supports first-path, newest, oldest and preferred-folder keeper policies. Missing
mtime evidence falls back to path order after known timestamps. **Keep selected copy** pins an
explicit survivor; selecting its ancestor for cleanup is rejected too. Automatic staging respects
existing manual selections and keeps an unstaged surviving copy. Whole-plan validation rejects
removing all group copies. Immediately before Trash, both the selected duplicate and surviving
copy are rehashed against reviewed content evidence; identity, size, allocation, hardlinks and known
mtime/ctime are also revalidated. A read-only snapshot never authorizes cleanup.

Moving data to same-filesystem Trash does **not release the payload's allocated space**. Cleanup
shows potential space only after permanent disposal; DiskMap never permanently deletes data.
The audit does not claim moved bytes as freed bytes.

Each completed backend operation writes a private, fsynced receipt beneath
`$XDG_DATA_HOME/Trash/.diskmap-receipts/`. The bounded `diskmap.receipt/v1` text records contain
status, hexadecimal path/message bytes and the opaque restore token. This is encoding, not encryption.
Opening the recovery tab, or **Reload Trash history**, reloads durable records and reconciles the
standard `.trashinfo`/temporary metadata against payload identity. This also recovers moves interrupted
before the supplementary audit was written. Historical failures/restores remain visible, and missing,
changed or unverifiable payloads are not offered as recoverable items. History enumeration is capped
at 10,000 files per directory and reports truncation; it is not a complete log above that bound.

Snapshot load/save/compare and Trash move/restore use worker threads. The progress indicator stays
active and Cancel remains available. Snapshot cancellation is cooperative before atomic installation;
once commit begins it finishes. Trash cancellation stops **between files**, preserves completed receipts,
and never interrupts a filesystem mutation halfway through. Closing the window waits for an active
storage mutation to reach its safe boundary. Ordinary filesystem calls/fsync are not forcibly interrupted.

The comparison tab filters kind, certainty, minimum delta and path, sorts by absolute change, shows
before/after sizes and delta together, and summarizes changed non-directory entries by parent folder
without double-counting directory aggregates. It displays up to 5,000 matching rows; the complete
comparison remains in memory and the CLI exposes the full bounded report. Folder totals are descriptive
subtotals of observed changed entries, not a claim about unobserved data in partial snapshots.

## Scan limits and filename-preserving snapshots

Production filesystem listing is bounded before retaining entries. Default limits are **250,000 nodes**
and a **256 MiB conservative tree/listing memory estimate**. This is not a hard process RSS cap; Qt,
allocator and analysis overhead are additional. Reaching either budget returns a visibly incomplete
inventory with its reason. Use **Scan limits…** or:

```sh
./build/src/diskmap /data --max-nodes 100000 --max-memory "128 MiB" --exclude-preset build-caches
./build/src/diskmap /data --cleanup-plan --keep-policy newest
./build/src/diskmap /data --cleanup-plan --keep-policy preferred --keep-under /data/originals
```

The build-cache preset excludes `.git`, `node_modules`, `.venv`, `__pycache__`, `build`, and `.cache`.
Filtered totals remain explicitly filtered. The generated-source benchmark opts into a larger node/memory
budget to measure its requested inventory; it does not bypass the production defaults silently.

New captures write [`diskmap.snapshot/v2`](schemas/diskmap-snapshot-v2.schema.json).
Each node has UTF-8 display `name`/`path` plus authoritative lowercase hexadecimal `name_bytes`/`path_bytes`.
These preserve every non-NUL filename byte, including invalid UTF-8; displays are checked against raw bytes
when loading. v2 also persists ctime evidence. The strict [v1 reader/writer](schemas/diskmap-snapshot-v1.schema.json)
remains available for existing snapshots, and comparisons accept either version. Unknown/duplicate keys,
malformed encodings, NUL, inconsistent display paths, and resource violations fail closed. Snapshot defaults
remain 100,000 nodes / 64 MiB serialized output; larger scans produce explicitly truncated snapshots.
Saving over an inventoried source file or a hardlink alias is rejected.

## Install and validation

```sh
cmake --install build --prefix /desired/prefix
/desired/prefix/bin/diskmap --version
QT_QPA_PLATFORM=offscreen /desired/prefix/bin/diskmap-gui --version
/desired/prefix/bin/diskmap-gui --load-snapshot before.json
```

Installation includes both binaries, schemas, documentation, desktop entry and SVG icon, plus core headers
and static library. GUI runtime dependencies are Qt6 Widgets/Concurrent and the platform plugin; the install
tree does not bundle the host's Qt libraries. CLI dependencies are the system C++ runtime and libc.

The portfolio regression suite exercises real raw-byte filenames, budget termination, legacy/v2 snapshots,
keeper/ancestor protection, durable Trash/restart/restore, full GUI workflow and cancellation with a responsive
GUI timer. Address/undefined-behavior instrumentation is available with `-DDISKMAP_SANITIZE=ON`.
