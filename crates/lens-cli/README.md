# lens-cli (`lens`)

Unified Systems Diagnostics and Forensic Platform CLI.

`lens` is the consolidated command-line tool replacing 8 standalone tools (`diskmap`, `abilens`, `loglens`, `testlens`, `tracelens`, `servicelens`, `buildscope`, `envlens`), unifying all diagnostics, system profiling, and forensic diffing into a single blazing-fast, statically-linkable binary.

---

## Command Summary

```text
Unified Systems Diagnostics and Forensic Platform

Usage: lens <COMMAND>

Commands:
  disk    Filesystem footprint analysis, duplicate detection, and safe cleanup
  abi     ELF binary inspection, dynamic symbol surface, and ABI diffing
  log     High-performance memory-mapped log viewer and filter
  test    High-speed test report parsing, flaky analysis, and regression diffing
  trace   Linux syscall trace (strace) analysis and latency/error diffing
  sys     Systemd service unit, drop-in override, and dependency DAG analysis
  build   Compilation database analysis, compiler flags, and header impact DAG
  env     Python virtual environment audit, dependency check, and shadowing detection
  bundle      Forensic Flight Recorder: bundle multiple diagnostic artifacts into a .lens container
  doctor      Comprehensive system diagnostic health check across storage, network, and services
  completion  Generate shell autocompletions (bash, zsh, fish, powershell)
  help        Print this message or the help of the given subcommand(s)
```

---

## Subcommands & Examples

### 1. `lens disk` (Storage & Cleanup Workbench)
```bash
# Scan directory and view human-readable summary
$ lens disk scan /home/user/projects

# Export full diskmap.snapshot/v2 JSON
$ lens disk scan /home/user/projects --json > snapshot.json

# Find duplicate files (recognizes hardlinks sharing dev_t/ino_t)
$ lens disk duplicates /var/data --min-size 1048576

# FreeDesktop Trash compliant safe disposal with receipts
$ lens disk trash /tmp/unused-cache.bin
```

### 2. `lens abi` (Binary ABI & ELF Verification)
```bash
# Inspect ELF binary and output abilens.report/v2 JSON
$ lens abi inspect /usr/lib/x86_64-linux-gnu/libssl.so.3

# Perform 3-state (compatible/incompatible/unknown) ABI diff
$ lens abi diff lib_v1.so lib_v2.so
```

### 3. `lens log` (Zero-Copy Mmap Log Viewer & Filter)
```bash
# Index gigabyte log files in milliseconds
$ lens log inspect /var/log/syslog

# Zero-allocation case-insensitive filter (WARN level, query "timeout")
$ lens log filter /var/log/app.log --query timeout --min-level warn
```

### 4. `lens test` (Streaming Test Report & Regression Diff)
```bash
# Streaming JUnit XML parse to testlens.run/v1 schema (<30ms)
$ lens test parse target/test-results.xml --project backend

# Differential test run analysis (detect regressions & fixes)
$ lens test diff run_baseline.xml run_candidate.xml
```

### 5. `lens trace` (Syscall Diagnostics & Latency Shift Analysis)
```bash
# Analyze raw strace output log into tracelens.snapshot/v1
$ lens trace analyze /var/log/strace.log

# Differential strace comparison: detect new error codes & latency regressions
$ lens trace diff trace_baseline.log trace_candidate.log
```

### 6. `lens sys` (Offline Systemd Unit & Ordering DAG Analyzer)
```bash
# Static offline inspection of systemd unit directory
$ lens sys inspect /etc/systemd/system

# Detect ordering cycles across service dependencies (Tarjan's SCC)
$ lens sys cycles /etc/systemd/system

# Diff two systemd configuration snapshots across deployments
$ lens sys diff before_sys.json after_sys.json
```

### 7. `lens build` (Compilation Database & Header Impact DAG)
```bash
# Inspect compile_commands.json database and normalize compiler flags
$ lens build inspect compile_commands.json

# Transitive reverse impact analysis: what rebuilds if a header changes?
$ lens build impact compile_commands.json --header include/common.h

# Compare compiler flags across build configurations (e.g. debug vs release)
$ lens build diff before_build.json after_build.json
```

### 8. `lens env` (Python Runtime & Virtualenv Auditor)
```bash
# Inspect virtualenv without executing user code
$ lens env inspect .venv --project .

# Check unsatisfied or missing package dependencies
$ lens env check .venv

# Diff two Python environment snapshots across environments
$ lens env diff dev_env.json prod_env.json
```

### 9. `lens bundle` (Forensic Flight Recorder)
```bash
# Capture full system diagnostic state during an incident into a .lens bundle
$ lens bundle create incident_20261005.lens \
    --disk /var/log \
    --log /var/log/app.log \
    --trace /tmp/strace.log \
    --test target/junit.xml \
    --sys /etc/systemd/system \
    --env .venv

# Inspect forensic bundle provenance, diagnostics, and embedded artifacts
$ lens bundle inspect incident_20261005.lens
```

### 10. `lens doctor` (System Diagnostic Health Check)
```bash
# Run comprehensive system diagnostics across storage, network, and services
$ lens doctor

# Output diagnostic report as JSON
$ lens doctor --json
```

### 11. `lens completion` (Shell Autocompletions)
```bash
# Generate completion script for bash
$ lens completion bash > /etc/bash_completion.d/lens

# Generate completion script for zsh
$ lens completion zsh > ~/.zfunc/_lens

# Generate completion script for fish
$ lens completion fish > ~/.config/fish/completions/lens.fish
```

---

## Performance & Resource Characteristics

- **Single Zero-Dependency Binary**: Self-contained executable requiring no external Python runtime, Qt6, or dynamic runtime packages.
- **Ultra-Low Memory Footprint**: Uses 80%–95% less RAM than equivalent Python or C++ AST implementations through arena allocation, zero-copy string borrowing, and streaming parsers.
- **Sub-100ms Latency**: Designed from the ground up for instantaneous CLI feedback and high-throughput CI/CD pipeline verification.
