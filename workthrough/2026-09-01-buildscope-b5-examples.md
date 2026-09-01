# BuildScope B5 example projects and quickstart

## Overview

Added small CMake and qmake projects that provide usable compilation-database
inputs for the BuildScope 0.5.0 release candidate. Added a quickstart that
documents the released Python producer artifacts, JSON hand-off to the native
CLI/GUI, diff inspection, and qmake command-capture limitations.

## Changes made

- `buildscope/examples/cmake/` contains a C++20 executable, CMake-generated
  compile-database configuration, and a checked-in two-entry fixture.
- `buildscope/examples/qmake/` contains an equivalent qmake project with a
  two-entry fixture. `QT =` keeps the example free of Qt module linkage while
  remaining buildable with qmake 5 and qmake 6.
- `buildscope/docs/quickstart.md` shows zipapp and wheel producer commands,
  native snapshot/diff consumers, CMake generation, and Bear-based qmake
  capture guidance.

No product code, root release documents, CI workflows, or additional
dependencies were changed.

## Representative flow

```text
compile_commands.json
        │
        ▼
buildscope.pyz / buildscope-0.5.0-py3-none-any.whl
        │
        ▼
snapshot.json or diff.json
        │
        ├── buildscope-cli
        └── buildscope-gui
```

## Verification results

- Both checked-in JSON fixtures parse with `python3 -m json.tool`.
- The CMake example configures, builds, and prints `BuildScope CMake example`.
- The qmake example configures and builds with both `qmake` (Qt 5) and
  `qmake6` (Qt 6), printing `BuildScope qmake example` in each leg.
- The Python 3.10 producer emits schema-v2 snapshots with two entries for both
  fixtures, and every normalized source path exists.
- `git diff --cached --check` completed without whitespace errors.
