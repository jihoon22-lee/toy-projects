# `lens-trace`

High-throughput, zero-allocation Linux system call trace (`strace`) analyzer and differential diagnostics engine.

## Overview

`lens-trace` is the modernized Rust replacement for `tracelens`. It ingests raw `strace` output (or streaming trace lines), tracks multi-threaded processes across clone/fork events, reconstructs interrupted/resumed syscalls (`<unfinished ...>` / `<... resumed>`), aggregates granular call latencies and errors, and performs differential comparisons across software runs to detect regressions, new error codes, and latency spikes.

## Key Features

- **Multithreaded Interleaving Reconstruction**:
  Handles real-world asynchronous strace streams where concurrent threads interleave syscall entries and returns using thread-keyed pending call queues.
- **Microsecond & Nanosecond Duration Extraction**:
  Accurately extracts `<0.000045>` latency annotations into nanoseconds (`u64`), tracking cumulative latency, max latency, and statistical shifts without float conversion bugs.
- **Automated Error Classification**:
  Parses errno tokens (e.g. `-1 ENOENT (No such file or directory)`) and tallies both per-syscall error distributions and system-wide error patterns.
- **Differential Diagnostics (`tracelens.diff/v1`)**:
  Compares baseline and candidate execution traces, immediately isolating:
  - New errors introduced in candidate runs (e.g. newly occurring `EACCES` or `ENOENT`).
  - Resolved errors.
  - Significant latency shifts (>50% increase in syscall latency).
  - Per-syscall volume and error count deltas.
- **Memory-Safe & Bounded Overhead**:
  Limits retained raw event buffers while calculating full aggregate counters in streaming $O(1)$ space.

## Architecture

```
                    Raw strace Log
                          │
                          ▼
               ┌──────────────────────┐
               │  split_tid_and_body  │
               └──────────┬───────────┘
                          │
          ┌───────────────┴───────────────┐
          ▼                               ▼
  <unfinished ...>                <... resumed>
  (enqueue in PendingCall)        (stitch with PendingCall)
          │                               │
          └───────────────┬───────────────┘
                          ▼
               ┌──────────────────────┐
               │    TraceAnalyzer     │
               └──────────┬───────────┘
                          ▼
               ┌──────────────────────┐
               │    TraceSnapshot     │
               │  - SyscallStats      │
               │  - ProcessInfo       │
               │  - Errors Map        │
               └──────────┬───────────┘
                          ▼ (diff_snapshots)
               ┌──────────────────────┐
               │      TraceDiff       │
               │  - Call/Error Deltas │
               │  - Latency Shifts    │
               │  - New/Fixed Errors  │
               └──────────────────────┘
```

## Schemas Supported

- **Snapshot**: `tracelens.snapshot/v1`
- **Diff**: `tracelens.diff/v1`

## API Usage

```rust
use lens_trace::{TraceAnalyzer, diff_snapshots};

let baseline_log = r#"
100 openat(AT_FDCWD, "/etc/config", O_RDONLY) = 3 <0.000020>
100 read(3, "data", 1024) = 1024 <0.000010>
100 close(3) = 0 <0.000005>
"#;

let candidate_log = r#"
100 openat(AT_FDCWD, "/etc/config", O_RDONLY) = -1 ENOENT (No such file) <0.000015>
"#;

let analyzer = TraceAnalyzer::new();
let baseline = analyzer.analyze_lines(baseline_log.lines());
let candidate = analyzer.analyze_lines(candidate_log.lines());

let diff = diff_snapshots(&baseline, &candidate);
println!("New errors: {:?}", diff.new_errors); // ["ENOENT"]
println!("Call delta: {}", diff.call_delta);    // -2
```

## CLI Subcommand

```bash
# Capture and analyze a process execution
strace -f -T -tt -o trace.log ./my-app
lens trace analyze trace.log --output snapshot.json

# Compare baseline vs regression trace
lens trace diff baseline.log candidate.log --format markdown
```
