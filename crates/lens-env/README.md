# `lens-env`

Python runtime environment inspector, virtual environment (`venv`) analyzer, package dependency auditor, import shadowing detector, and environment diff engine.

## Overview

`lens-env` is the modernized Rust replacement for `envlens`. It performs completely safe, read-only static analysis on Python virtual environments and project source trees **without importing or executing any Python code**.

It inspects `pyvenv.cfg`, discovers installed distributions via PEP 376 / PEP 503 `*.dist-info/METADATA`, validates dependency satisfaction (`Requires-Dist`), audits source directories for standard library or third-party package module shadowing (e.g. `json.py`, `email.py`, `requests.py`), and generates differential environment diagnostics across build machines, staging, and production.

## Key Features

- **Zero-Execution Inspection**:
  Audits environments purely through static filesystem inspection and metadata parsing. Never invokes `python` or executes user code, preventing arbitrary code execution during inspection.
- **PEP 503 & PEP 376 Compliant**:
  Parses RFC 822 `METADATA` structures, normalizes distribution names (handling dashes, underscores, and dots), and maps exact installed package versions.
- **Import Shadowing Detection**:
  Identifies local project files (`foo.py`, `src/bar.py`) whose names collide with Python standard library modules (`json`, `csv`, `email`, `re`, `io`, etc.) or installed third-party package top-level modules, preempting insidious runtime bugs and circular import errors.
- **Missing Dependency Auditor**:
  Evaluates `Requires-Dist` specifications across all installed distributions and flags uninstalled or unsatisfied prerequisite packages.
- **Differential Environment Analysis (`envlens.diff/v1`)**:
  Compares two Python environments to pinpoint:
  - Added or removed packages.
  - Upgraded or downgraded package versions.
  - Newly introduced namespace shadowing conflicts.

## Architecture

```
       Virtual Environment (venv/)              Project Directory (src/)
              │                                      │
              ▼                                      ▼
       ┌──────────────┐                       ┌──────────────┐
       │  pyvenv.cfg  │                       │ Source Files │
       │  site-pkgs/  │                       │  (*.py)      │
       └──────┬───────┘                       └──────┬───────┘
              │                                      │
              ▼                                      ▼
       ┌──────────────┐                       ┌──────────────┐
       │   Metadata   │                       │  Shadowing   │
       │  (dist-info) │                       │   Detector   │
       └──────┬───────┘                       └──────┬───────┘
              │                                      │
              └───────────────────┬──────────────────┘
                                  ▼
                        ┌───────────────────┐
                        │   inspect_venv    │
                        └─────────┬─────────┘
                                  ▼
                        ┌───────────────────┐
                        │    EnvSnapshot    │
                        └─────────┬─────────┘
                                  ▼ (diff_environments)
                        ┌───────────────────┐
                        │      EnvDiff      │
                        └───────────────────┘
```

## Schemas Supported

- **Snapshot**: `envlens.snapshot/v1`
- **Diff**: `envlens.diff/v1`

## API Usage

```rust
use std::path::Path;
use lens_env::{inspect_venv, detect_shadowing, diff_environments};

let venv_path = Path::new("/path/to/.venv");
let mut venv = inspect_venv(venv_path).expect("Failed to inspect venv");

// Check project for shadowing
let project_root = Path::new("/path/to/project");
venv.shadowing_issues = detect_shadowing(project_root, &venv.packages);

for issue in &venv.shadowing_issues {
    println!("Shadowing warning: {} at {} ({})", issue.module_name, issue.local_path, issue.shadows);
}
```

## CLI Subcommand

```bash
# Inspect a virtual environment and check project shadowing
lens env inspect .venv --project . --output env-snapshot.json

# Check for missing dependencies
lens env check .venv

# Diff two virtual environments (e.g. dev vs prod)
lens env diff dev-env.json prod-env.json
```
