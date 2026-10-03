# TraceLens

TraceLens investigates **saved Linux strace text**. A Qt-free C++20 core parses and aggregates evidence; a CLI exports versioned snapshots and streaming events; an optional Qt6 desktop app provides process, syscall, error, path and source views. It never starts a tracer, executes traced commands, attaches to a process or uploads evidence. Release history is in [CHANGELOG.md](CHANGELOG.md).

## Build and run

Linux dependencies: CMake 3.22+, C++20 compiler, Qt6 Core; GUI additionally needs Qt6 Widgets and Concurrent. Tests need Python 3 (standard library only). On Debian/Ubuntu the usual packages are `build-essential cmake qt6-base-dev python3`. The parser library itself does not link Qt.

Run the following commands from the repository root:

```sh
cmake -S tracelens -B /tmp/tracelens-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/tracelens-build -j2
ctest --test-dir /tmp/tracelens-build --output-on-failure
/tmp/tracelens-build/tracelens-gui tracelens/tests/fixtures/mixed.strace
```

Use `-DTRACELENS_BUILD_GUI=OFF` for CLI-only builds. `-DBUILD_TESTING=OFF` removes the Python/test dependency. Build directories should stay outside the product source tree.

These examples use the checked-in fixtures and write outputs to a temporary directory. Replace the fixture paths with your saved traces for an actual investigation.

```sh
export PATH="/tmp/tracelens-build:$PATH"
trace_demo_dir=$(mktemp -d /tmp/tracelens-demo.XXXXXX)
tracelens inspect tracelens/tests/fixtures/mixed.strace
tracelens inspect tracelens/tests/fixtures/mixed.strace --format json --output "$trace_demo_dir/before.json"
tracelens inspect tracelens/tests/fixtures/split.201 tracelens/tests/fixtures/split.202 --format json --output "$trace_demo_dir/after.json"
tracelens events tracelens/tests/fixtures/mixed.strace --errno ENOENT --limit 100 --output "$trace_demo_dir/errors.jsonl"
tracelens events tracelens/tests/fixtures/mixed.strace --pid 100 --syscall openat --min-duration-ns 1000
tracelens source tracelens/tests/fixtures/mixed.strace --line 5 --context 3
tracelens source "$trace_demo_dir/before.json" --source-id 0 --line 5 --context 0
tracelens diff "$trace_demo_dir/before.json" "$trace_demo_dir/after.json" --output "$trace_demo_dir/diff.json"
tracelens inspect tracelens/tests/fixtures/mixed.strace --max-bytes 10485760 --max-events 100000 --strict
```

`source` recognizes snapshots by their `.json` extension. It validates inode/device, modification time, observed size and SHA-256 of exact source bytes before presenting evidence. Source replacement, modification or a partial fingerprint is an explicit error; saved byte positions are never silently applied to another file. Verification hashes the original file and may take time on large traces. Output uses atomic same-directory replacement; input paths, symlink aliases and hardlink aliases are rejected. Duplicate input identities are also rejected.

Exit codes: **0** successful observation/export (possibly partial), **1** option-parser errors such as an unknown option or missing option value, **2** command, input, schema or output failure, **3** partial evidence under `--strict`, **130** `inspect`/`events` analysis cancelled by SIGINT/SIGTERM. Event exports include counts, partial-result reasons and source manifests in a final JSONL footer. `--limit` bounds emitted matches, while scanning continues for exact matching counts within the analysis budgets. PID/syscall/errno/path/duration filters and `--limit` apply to `events`; `inspect` aggregates the whole observed input. `source --context` accepts 0–100 lines before and after the target.

## Evidence contract

Supported shapes include ordinary single-process files, mixed PID prefixes (`123 ...` and `[pid 123] ...`), explicitly selected `-ff` files named with numeric PID suffixes, clock/numeric timestamps, `-T` durations, escaped/nested arguments, errno returns, signals, exits, notices and unfinished/resumed calls. Unknown syscall names remain ordinary calls. Unrecognized lines are retained as unknown events with diagnostics.

Unfinished/resumed calls pair by TID and syscall. Their event retains the start timestamp and both source ranges; missing, mismatched and interrupted pairs remain partial evidence. Signals do not prematurely discard a pending call. Each source is read in fixed 64 KiB chunks up to its initially observed size. A growing file cannot make analysis run forever, and detected source changes mark the report partial.

Only explicit `<duration>` evidence contributes to nanosecond totals. Missing timing is counted separately; timestamp differences never estimate duration. Clock and numeric timestamp strings retain their original kind without guessing date, timezone, relative-vs-epoch meaning or midnight rollover. Concurrent syscall totals are **not elapsed wall-clock time**.

Successful fork/vfork returns establish process relationships. Clone/clone3 relationships are labeled threads only with observed `CLONE_THREAD`; other clone relationships remain unknown. Numeric PID/TID identities include an observed generation after an exit. This cannot detect reuse without exit evidence. Split files are visited in selection order, not globally sorted by timestamp. Child-first selection supports ordinary relationships; reused TIDs across split sources remain explicitly ambiguous. PID-less input uses TID 0 as “unobserved”; multiple anonymous sources stay separate and mark that ambiguity partial.

Paths come only from documented argument positions for open/openat/openat2/creat, stat/lstat/statx/newfstatat, access/faccessat/faccessat2, execve/execveat, unlink/unlinkat, rename/renameat/renameat2, mkdir/mkdirat/rmdir, chdir, readlink/readlinkat, chmod/chown, link and symlink. Truncated string arguments are not invented as paths. Relative paths remain relative; original arguments preserve CWD/dirfd evidence. No FD history, host filesystem resolution or semantic path normalization is guessed. A path metric counts observed path arguments, so a two-path syscall can contribute twice.

Non-ASCII and invalid UTF-8 bytes are displayed/exported as reversible C-style escapes (`\\`, `\n`, `\r`, `\t`, `\xHH`), rather than replaced with Unicode replacement characters. Raw bytes remain in the original source, identified by SHA-256.

Unsupported formats include strace summary tables (`-c`), perf/ftrace/eBPF exports and stack dumps. These produce unknown-line diagnostics instead of silently becoming syscall data. Explicitly selected sources should belong to one tracing session.

## Resource limits and partial results

The defaults are controls, not RSS or latency guarantees. Increase CLI limits deliberately when the observed range is too small.

| Resource | Default | CLI control |
|---|---:|---|
| Total input | 1 GiB | `--max-bytes` |
| Logical events | 1,000,000 | `--max-events` |
| Analysis time | unlimited | `--max-ms` (0 disables) |
| Physical line | 1 MiB | `--max-line-bytes` |
| Argument nesting | 64 | `--max-nesting` |
| Distinct paths | 100,000 | `--max-paths` |
| Process generations / TIDs | 65,536 | `--max-processes` |
| Pending unfinished calls | 65,536 | `--max-pending` |
| Syscall / errno keys | 4,096 each | `--max-syscalls` |
| Detailed diagnostics | 1,000 | `--max-diagnostics` |
| Slow-call evidence | 1,000 | `--top` (0 disables) |
| Explicit source files | 128 | core `Limits::sources` |
| GUI matching rows | 5,000 | rescan with narrower filters |
| Imported snapshot | 64 MiB, nesting 64 | fixed reader limit |

Cardinality overflow uses a reserved `<overflow>` aggregate and reports omitted occurrences. That bucket is not an exact distinct-path count. Diagnostic totals continue after detailed evidence reaches its cap. Events and large argument strings are not retained wholesale by the CLI; JSONL is written incrementally. Memory still depends on retained argument/key sizes, selected limits and GUI evidence rows. Extremely large custom-limit snapshots can exceed the reader's 64 MiB import limit; use lower `--top`/cardinality limits when sharing snapshots.

## GUI

The 1280×800, palette-aware interface has a process tree, summary/slow-call table, calls/events table, syscall/error/path aggregates, comparison tab and verified original-evidence pane. Open one or several files, apply TID/syscall/errno/minimum-duration/path filters, and select calls to inspect original bytes. Filtering performs a fresh cancellable scan and retains at most 5,000 matching rows; the status line reports displayed and matching counts. Background workers use generation IDs to discard stale results from reopening or changing filters. Evidence selection also discards stale replies.

Keyboard controls: **Ctrl+O** open, **Ctrl+S** save snapshot, **Ctrl+D** compare a baseline snapshot, **Escape** cancel; **Enter** in a filter applies it. Activate a process item to filter its numeric TID. The comparison tab shows structured deltas with exact original keys and coverage cautions.

## Interchange and comparison

The checked-in JSON Schemas describe [tracelens.snapshot/v1](schemas/snapshot-v1.schema.json), [tracelens.event/v1](schemas/event-v1.schema.json), [tracelens.events-end/v1](schemas/events-end-v1.schema.json) and [tracelens.diff/v1](schemas/diff-v1.schema.json). Offsets, lengths, counters, TIDs and nanoseconds are decimal strings, preserving unsigned 64-bit values across JSON consumers. Source indices are small JSON integers. Snapshot imports reject duplicate keys, unsupported versions, invalid integer encodings and invalid evidence ranges. Snapshots store aggregates and bounded slow-call evidence, not the complete event stream. Output ordering is deterministic for the same source files, metadata, selection order and limits; modification timestamps and file identities deliberately distinguish changed source evidence.

Diff compares **syscall, errno and exact observed-path** axes. It records before/after source manifests, signed call-count/error-count/timed-call-count/total-duration deltas and coverage cautions. It never naively matches numeric PIDs between sessions. Added keys have an empty `before` object; removed keys have an empty `after` object. Different workloads, tracing options and sample durations may explain differences; the output does not label them performance regressions.

## Verification and measurement

```sh
cmake -S tracelens -B /tmp/tracelens-asan -DCMAKE_BUILD_TYPE=Debug -DTRACELENS_SANITIZE=ON
cmake --build /tmp/tracelens-asan -j2
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir /tmp/tracelens-asan --output-on-failure
python3 tracelens/benchmarks/run.py /tmp/tracelens-build/tracelens --mib 64
# Opt-in large input; creates a temporary generated trace:
python3 tracelens/benchmarks/run.py /tmp/tracelens-build/tracelens --mib 1024
```

ASan and UBSan cover the core, adapters, CLI and GUI. `detect_leaks=0` avoids platform-plugin leak noise; address/undefined-behavior checks remain active. CTest configures the GUI test with `QT_QPA_PLATFORM=offscreen`. Tests cover paired source offsets, nested/escaped/non-UTF-8 input, split files in both selection orders, PID reuse, EOF/unknown lines, source mutation, budgets, SHA-256, strict exits, duplicate/malformed schemas, output aliases, deterministic snapshots, streaming exports, rescan/reopen generations and verified GUI evidence.

`benchmarks/run.py` requires Linux `/usr/bin/time` and records wall time, peak RSS, observed calls and signal cancellation latency. The checked-in local Release sample processed 67,108,800 bytes / 671,088 calls in 0.807 seconds with 12,000 KiB peak RSS; a separate cancellation run exited in 11.418 ms. These are measurements from one machine/build, not guaranteed budgets.

## Install and package

```sh
cmake --install /tmp/tracelens-build --prefix /tmp/tracelens-install
/tmp/tracelens-install/bin/tracelens --version
cpack --config /tmp/tracelens-build/CPackConfig.cmake -B /tmp/tracelens-packages
```

The installation contains `bin/tracelens`, optional `bin/tracelens-gui`, schemas under `share/tracelens/schemas`, documentation under the CMake GNUInstallDirs documentation directory (by default `share/doc/TraceLens`), and optional desktop entry/icon under `share/applications` and `share/icons/hicolor/scalable/apps`. Fixtures and benchmark scripts remain in the source repository. Installed schemas are under the data directory above; the relative schema links in this README are for the repository layout. TGZ packages are platform binaries and require the matching Qt6 runtime libraries; they do not bundle Qt. For a custom installation prefix, add its `bin` directory to `PATH` before launching the desktop entry. Version ownership is `CMakeLists.txt`; a generated header supplies application/banner metadata. Independent release tags use `tracelens/vX.Y.Z`.
