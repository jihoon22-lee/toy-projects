# diskmap D2 cancellable scan and latest-generation evidence

## Overview

Completed the `diskmap` D2 local candidate after the D1 identity-safe scanner.
The scan can be cancelled cooperatively, GUI progress and completion are guarded
by a monotonically increasing generation, and a cancelled partial result is
discarded so it cannot replace the tree already visible to the user. The CLI
now exposes the scan filters and traversal controls needed by the same core
contract, while the scanner and analysis/layout helpers remain safe for deep
trees.

This is local candidate evidence only. The toy remote PR/CI, full post-refactor
`ici verify`, and sticky comment/Pages evidence are still pending and are not
claimed as complete here.

## Context

D1 made path and physical-identity semantics explicit, but a long scan still
needed a cancellation boundary and a result-ordering contract. If a user
started scan B while scan A was running, A could finish later and overwrite B's
treemap or progress. Deep directory chains also made recursive traversal,
aggregation, and layout a stack-safety risk. D2 therefore treats cancellation,
generation, partial errors, CLI option semantics, and benchmark budgets as one
observable contract.

The implementation history after the D2 starting point includes latest-scan
ownership (`a67b108`), the generated million-entry benchmark (`a2f40f4`), CLI
option wiring (`7fed4bf`), one-shot fatal reporting (`5826030`), benchmark edge
case hardening (`3a7c97d`), and the quality-gate-driven scan-state refactor
(`b7218c6`).

## Changes Made

### 1. Cooperative cancellation and progress

- `ScanCancellationToken` uses an atomic release/acquire flag shared by the
  worker and the caller.
- The iterative scanner checks the token before scheduling work, around
  directory listing, and while consuming a source listing. A source can also
  stop its listing through the cancellation callback.
- A cancelled scan returns `ScanResult.cancelled=true`, marks the root and
  pending directory work incomplete, and emits a final progress snapshot when a
  progress callback is present. Cancellation is not treated as a root fatal
  error.
- `ProgressFn` reports scanned directory/file counts. The GUI forwards those
  updates through queued callbacks so the worker never mutates widgets.

### 2. Latest-generation GUI and partial-result policy

- `MainWindow::scanPath()` cancels the previous token, increments the active
  generation, and attaches that generation to options, progress, and the result.
- Queued progress and completion callbacks whose generation is no longer
  active are ignored. A late worker A therefore cannot overwrite a newer scan B.
- The explicit cancellation policy is **discard partial results**. The GUI
  leaves the previous visible tree/breadcrumb in place and reports
  `Scan cancelled — partial result discarded`; if no previous result exists,
  the view remains empty.
- A current-generation successful result replaces the view. A current-generation
  fatal result is shown as a scan failure; stale fatal or successful results are
  ignored just like stale progress.

### 3. CLI controls and error semantics

The CLI keeps legacy `--depth` as an output-only tree depth limit and adds the
following scan controls:

| Option | Contract |
|---|---|
| `--max-depth N` | Limits scanner traversal. `0` lists only the root; omitted/default and values above the safety cap use the effective 512 limit. |
| `--depth N` | Legacy text/JSON output depth only; it does not shorten traversal, and its output cap is also 512. |
| `--follow-symlinks` | Follows descendant directory symlinks through target identity tracking; the explicitly selected root symlink is always dereferenced. |
| `--min-size BYTES` | Filters regular files smaller than the threshold while retaining directories and symlink entries. |
| `--one-file-system` | Leaves a directory on another device visible and incomplete without expanding it; an unverifiable identity is reported conservatively. |
| `--exclude GLOB` | Applies `*`/`?` to the entry basename and root-relative generic path. The option is repeatable and matching entries are omitted from aggregation. |

Root metadata/open failure and a root directory listing failure with no
entries are fatal: CLI prints one `fatal:` line and exits 1. Child
stat/list/iterator failures, and a listing that returns entries before an
error, are partial/non-fatal: the affected node remains visible with
`complete=false`, its error is counted/retained, and sibling work continues.
Intentional mount-boundary exclusion is represented by a visible incomplete
node and a separate `mount_boundaries_skipped` count rather than being hidden
as an unreadable error. Error retention is bounded by the scanner's
`max_errors` setting while `error_count` still records the total.

### 4. Iterative deep-tree safety

- Directory traversal uses an explicit work stack rather than call-stack
  recursion.
- `aggregateSizes`, `aggregateStorage`, sorting/counting/top-files helpers, and
  treemap layout use explicit stacks for their tree walks.
- `kMaxTreeDepth=512` is the structural safety/ownership cap. Scanner
  `max_depth` and CLI output depth are clamped to that effective limit. A
  directory pruned at the limit stays visible, is marked incomplete, and gets
  `scan depth limit reached`, so physical totals are not presented as exact.
- The native core tests exercise a 10,000-level value-owned tree through the
  iterative helpers. The production depth cap also bounds value-owned tree
  destruction, whose C++ vector teardown is recursive.

### 5. Generated-source benchmark and workflow policy

`diskmap/benchmarks/run_benchmark.py` uses a deterministic fake source and
executes a full scan plus a cancellation run without creating a million-entry
filesystem fixture. Its defaults are 1,000,000 requested entries,
`--cancel-after 10000`, 60-second process timeout, full throughput at least
100,000 entries/s, full peak RSS at most 1,536 MiB, full elapsed at most
30,000 ms, and cancellation elapsed at most 2,000 ms. `--skip-budgets` retains
correctness checks while bypassing performance thresholds.

The ordinary PR path in `.github/workflows/ci.yml` runs
`diskmap-benchmark-smoke` on a Qt5/Qt6 matrix with 10,000 entries, cancellation
after 1,000 entries, a 30-second timeout, and `--skip-budgets`; `Merge Gate`
requires both matrix legs. The full million-entry matrix is intentionally
opt-in/nightly in `.github/workflows/diskmap-benchmark.yml`: `workflow_dispatch`
and Sunday `03:37 UTC`, Qt5/Qt6 legs, 30-minute job timeout, aggregate and
verdict jobs, and report-only JSON/MD/TXT artifacts. Generated input and process
logs stay out of artifacts, and the workflow uses the runner defaults as the
single budget policy rather than inventing workflow-specific thresholds.

## Code Examples

Build the Qt6 benchmark in a scratch tree and run the local full/cancellation
measurement:

```bash
cd diskmap
repo_root="$(pwd)"
benchmark_root="$(mktemp -d /tmp/diskmap-benchmark.XXXXXX)"
artifact_dir="$benchmark_root/artifact"
mkdir -p "$benchmark_root/src" "$benchmark_root/benchmarks" "$artifact_dir"
(
  cd "$benchmark_root/src"
  /usr/bin/qmake6 "$repo_root/src/src.pro"
  make -j"$(nproc)"
)
(
  cd "$benchmark_root/benchmarks"
  /usr/bin/qmake6 "$repo_root/benchmarks/scan_benchmark.pro"
  make -j"$(nproc)"
)
python3.10 benchmarks/run_benchmark.py \
  --binary "$benchmark_root/benchmarks/diskmap-scan-benchmark" \
  --entries 1000000 --cancel-after 10000 --timeout-seconds 60 \
  --output-dir "$artifact_dir"
sha256sum "$artifact_dir/summary.json"
```

For Qt5, use `/usr/bin/qmake` in both build subshells and a separate scratch
directory. The PR smoke command is the same runner with
`--entries 10000 --cancel-after 1000 --timeout-seconds 30 --skip-budgets`.

## Verification Results

### Native Qt matrix

Fresh local scratch builds used the selected qmake executable for both the full
project and `make check`:

| Qt | qmake | Result |
|---|---|---|
| 5.15.18 | `/usr/bin/qmake` | full build + `make check` PASS; `test_main_window` 10/10 PASS |
| 6.10.2 | `/usr/bin/qmake6` | full build + `make check` PASS; `test_main_window` 10/10 PASS |

### CLI integration smoke

The local fixture smoke created `keep/nested`, `skipme`, an 8-byte root file,
and 32/64/128-byte payloads. It combined `--json`, `--max-depth 1`, legacy
`--depth 3`, `--min-size 16`, and `--exclude 'skip*'`. Its JSON assertions
passed and reported:

```text
CLI_INTEGRATION_SMOKE=PASS
```

The assertion checked that the filtered/limited tree retained `keep`, its
32-byte `large.bin`, and the visible incomplete `nested` directory; omitted
`small.bin` and `skipme`; and did not serialize descendants below the scanner
depth bound. Separate parser wiring makes `--exclude` repeatable, while this
integration smoke exercised one pattern.

### Generated-source benchmark

The recorded local summary is one artifact, so the hash below identifies the
exact report rather than a re-run's host-dependent timing:

| Scenario | Requested / generated | Elapsed | Throughput | Peak RSS | Correctness |
|---|---:|---:|---:|---:|:---:|
| full | 1,000,000 / 1,000,000 | 4820.934 ms | 207428.692 entries/s | 1063.496 MiB | PASS |
| cancellation | 1,000,000 / 10,000 | 2.676 ms | 3737316.017 entries/s | 15.414 MiB | PASS |

The full sample retained 1,000,001 nodes and both samples passed their
correctness checks. The local `summary.json` SHA-256 is
`743d5c5409101cfd9ef889da2da421e94cc205f585770ab19bb611472926246d`.

### ici quality gate boundary

An initial candidate `ici complexity` run failed on scan complexity/nesting.
The behavior-preserving `b7218c6` scan-state refactor split those transitions;
the follow-up complexity-only result is PASS with maximum cyclomatic 14 against
the limit 15, 129 functions, and 0 issues. Full post-refactor `ici verify` has
not yet been run, so no suite, coverage, sanitizer, remote PR/CI, sticky, or
Pages result is claimed for this D2 candidate.

## Next Steps

- Open the candidate PR only after the local docs/evidence commit is reviewed;
  wait for the toy PR/CI and `Merge Gate` result.
- Run and record full post-refactor `ici verify`, then verify the sticky comment
  and linked Pages HTML before calling D2 remotely complete.
- Keep the cancelled-result discard and fatal/partial error policy stable as
  D3 explorer UX adds refresh and selection behavior.
