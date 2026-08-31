# BuildScope B0 hybrid skeleton

## Overview

BuildScope B0 establishes the first usable boundary for a hybrid build explorer. A dependency-free
Python 3.10 producer reads a compilation database without running its command strings and emits a
deterministic `buildscope.snapshot/v1` document. A C++20/Qt CLI and GUI validate and consume that
document. This record first captured the local skeleton while remote PR/release evidence was
pending; the follow-up below records the completed release evidence.

## Context

The product needs a stable process contract before configuration comparison and include
explanation can be built. B0 therefore keeps the raw compile invocation and focuses on bounded
input, version validation, independent native consumers, and one producer-to-consumer integration
path. It does not claim B1 command/compiler/configuration normalization or the ici I3
target-by-target comparison.

## Implementation recorded

### Python producer (`buildscope/python/buildscope/`)

- `load_compilation_database()` accepts JSON arrays up to 64 MiB and 100,000 entries.
- It validates `directory`, `file`, and either non-empty `arguments` or `command`, while preserving
  the optional `output` and the original invocation form.
- It never invokes a shell or compiler. Entries are sorted deterministically and serialized with
  the `buildscope.snapshot/v1` schema.
- The CLI is runnable with `PYTHONPATH`; no package installation is required.

### C++20/Qt consumer (`buildscope/include/`, `buildscope/src/`)

- The C++ contract reader validates schema, producer version, source entry count, entry fields,
  and the same 64 MiB/100,000-entry limits.
- `buildscope-cli SNAPSHOT.json` prints a contract summary.
- The Qt `MainWindow` opens a supplied or selected snapshot and displays source, working directory,
  and raw invocation rows.
- `CMakeLists.txt` enables `CMAKE_AUTOMOC`, `CMAKE_AUTOUIC`, `CMAKE_AUTORCC`, C++20, and
  `CMAKE_EXPORT_COMPILE_COMMANDS`.

### Tests and integration

The CMake test set has four entries: Python unit tests, C++ contract QtTest, MainWindow QtTest, and
the CMake-scripted Python producer → C++ CLI consumer contract. The latter writes a temporary
snapshot from `fixtures/compile_commands.json` and checks the C++ consumer's
`buildscope.snapshot/v1` output.

### CI and repository hygiene

- `ci/projects.json` makes BuildScope the third mandatory ici project and expands its CMake GUI
  across both Qt 5 and Qt 6.
- Manifest validation fails closed on malformed roots, duplicate or missing projects,
  build-system/descriptor mismatches, missing smoke inputs, unsafe paths, and symlinks that
  resolve outside a project. Eleven dependency-free tests cover the current matrix and these
  negative cases.
- The workflow consumes the public ici v0.7.1 release with the standard
  `sha256sum --check ici.pyz.sha256` contract and provisions pytest, coverage, mypy, and Ruff in
  the exact Python 3.10 interpreter exported as `ICI_PYTHON`.
- Python tool caches, coverage files, and package outputs are explicitly ignored. Package metadata
  includes the BuildScope README; the resulting wheel is pure `py3-none-any` and is accompanied
  by an sdist.

## Reproduction commands

These commands build under `/tmp`, import the Python package through `PYTHONPATH`, and use an
external ici Python environment. They do not install BuildScope into the repository.

```bash
repo_root="$(git rev-parse --show-toplevel)"
scratch_root="$(mktemp -d /tmp/buildscope-b0.XXXXXX)"
py310_bin="$(command -v python3.10)"
ici_bin="/home/jihoon/projects/ici/dist/ici.pyz"
ici_python="/tmp/buildscope-ici-py310/bin/python"  # external env with pytest/coverage/mypy

PYTHONPATH="$repo_root/buildscope/python" \
  "$py310_bin" -m unittest discover \
  -s "$repo_root/buildscope/tests/python" -p 'test_*.py'

PYTHONPATH="$repo_root/buildscope/python" \
  "$py310_bin" -m buildscope \
  "$repo_root/buildscope/fixtures/compile_commands.json" \
  --output "$scratch_root/buildscope.snapshot.json" --pretty

cmake -S "$repo_root/buildscope" -B "$scratch_root/qt6" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=ON \
  -DPython3_EXECUTABLE="$py310_bin"
cmake --build "$scratch_root/qt6" --parallel
QT_QPA_PLATFORM=offscreen \
  ctest --test-dir "$scratch_root/qt6" --output-on-failure

cmake -S "$repo_root/buildscope" -B "$scratch_root/qt5" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON \
  -DPython3_EXECUTABLE="$py310_bin"
cmake --build "$scratch_root/qt5" --parallel
QT_QPA_PLATFORM=offscreen \
  ctest --test-dir "$scratch_root/qt5" --output-on-failure

"$scratch_root/qt6/src/core/buildscope-cli" \
  "$scratch_root/buildscope.snapshot.json"

# The external interpreter must already contain pytest, coverage, and mypy.
(
  cd "$repo_root/buildscope"
  ICI_PYTHON="$ici_python" \
    "$ici_bin" verify --report --html "$scratch_root/buildscope-ici.html"
)
```

## Verification results (2026-09-01)

- Qt 5.15.18: build succeeded; CTest `4/4` passed.
- Qt 6.10.2: build succeeded; CTest `4/4` passed.
- Manifest discovery reported three mandatory projects and six Qt matrix entries; all `11/11`
  manifest tests passed.
- `uv build` produced `buildscope-0.1.0-py3-none-any.whl` with `Root-Is-Purelib: true` and
  `buildscope-0.1.0.tar.gz`.
- Earlier isolated local ici 0.7.1 candidate, with `ICI_PYTHON` pointing to a Python 3.10
  interpreter: suite `WARN`, `12 PASS / 1 WARN / 0 FAIL / 0 ERROR / 0 SKIP`, `9/9` tests, TEM
  `5.00`, line/function/branch `96.3% / 100.0% / 86.8%`, complexity `14 PASS`, compile DB
  `4/4` production units and `13` configurations, total `22.78s`.
- `python -m` probes for pytest, coverage, and mypy were all `READY` in that same interpreter.
  The mypy argv received only the `python` source root (C++ roots excluded) and returned `rc=0`.
- The sole ici warning is the documented C++ type limitation: C++ type checking is unsupported.

At the time of this initial record, the B0 checkboxes were locally satisfied. The D11/I5
interpreter/tool capability gap was verified resolved in the v0.7.1 candidate, while remote
PR/release evidence was still pending. B1 normalization and I3 target comparison remain future
work.

## Remote follow-up (2026-09-01)

The public `ici v0.7.1` release asset was cold-verified in isolation with `ICI_PYTHON` pointing to
the same Python 3.10 environment. Its `python -m` probes for pytest, coverage, and mypy were all
`READY`; mypy received only the `python` root (C++ roots excluded) and returned `rc=0`. The report
was suite `WARN`, `12 PASS / 1 WARN / 0 FAIL / 0 ERROR / 0 SKIP`, `9/9` tests, TEM `5.00`,
line/function/branch `96.3% / 100.0% / 86.8%`, complexity `14 PASS`, compile DB `4/4` production
units and `13` configurations, total `63.37s`. The sole warning was unsupported C++ type checking.

This release revalidated the D11/I5 interpreter/tool capability path.
ici [PR #109](https://github.com/jihoon22-lee/ici/pull/109) completed its sticky report and two
Pages checks; exact `main` `b87afba` passed [CI run
`33419851128`](https://github.com/jihoon22-lee/ici/actions/runs/33419851128), and [release run
`33420348698`](https://github.com/jihoon22-lee/ici/actions/runs/33420348698) succeeded. The [v0.7.1
release](https://github.com/jihoon22-lee/ici/releases/tag/v0.7.1) provides nine assets, and
`sha256sum --check ici.pyz.sha256` passed. B1 normalization and I3 target comparison remain
future work.
