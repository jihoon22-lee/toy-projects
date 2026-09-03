# EnvLens E2/E3 Quality-Gate Refactor

## Overview

The EnvLens implementation was split into bounded compatibility, dependency,
runtime, and reporting helpers while preserving the existing public schemas and
CLI behavior. Boundary tests were added for project parsing, snapshot input,
reports, CLI errors, and configured-interpreter runtime checks.

## Context

The released `ici v0.10.2` gate initially reported over-complex render/parser
functions, runtime source files above the 500 pure-code-line recommendation,
and coverage below the configured 90% function and 80% branch thresholds.
The work remained offline, dependency-free, Python 3.10-compatible, and did
not change `envlens/ici.toml` thresholds.

## Changes Made

### Runtime and compatibility decomposition

- Moved conservative version, marker, Requires-Python, and wheel-tag evidence
  into `src/envlens/diff_compat.py`.
- Moved bounded dependency issue evaluation into
  `src/envlens/diff_dependencies.py`.
- Moved runtime source enumeration, process classification, error type, and
  interpreter record helpers into `runtime_files.py`, `runtime_process.py`,
  `runtime_types.py`, and `runtime_execution.py`.
- Kept runtime `PYTHONPATH` limited to the project root and optional `src/`
  directory, and retained bounded compileall argv checks.

### Parser and report refactors

- Split the deliberately bounded TOML parser into quote, structure, array,
  table, and entry-point helpers; unsupported bare values remain explicit
  `ProjectError` failures.
- Split text and Markdown reporting into section/row helpers so malformed
  additive fields are skipped deterministically and cells remain escaped.
- Updated `README.md` to reflect 108 tests, 17 source modules, and current
  released-gate evidence.

### Tests

- Added `tests/test_cli_boundaries.py`.
- Added `tests/test_project_boundaries.py`.
- Added `tests/test_report_boundaries.py`.
- Added `tests/test_runtime_boundaries.py`.
- Added `tests/test_snapshot_input_boundaries.py`.

No dependencies or quality thresholds were added or changed.

## Code Examples

### Callback-preserving runtime classification

```python
# src/envlens/runtime.py
return classify_process(
    stdout=stdout,
    stderr=stderr,
    return_code=return_code,
    timeout_seconds=timeout_seconds,
    kind=kind,
    name=name,
    max_output_chars=MAX_OUTPUT_CHARS,
    text_renderer=_bounded_text,
    status_renderer=_failed_process_status,
)
```

This keeps the compatibility helpers observable while centralizing process
status classification and retaining the same output schema.

## Verification Results

All commands below used `/tmp/toy-ici-python-tools/bin/python` (Python 3.10)
and released `/tmp/ici-v0.10.2-released/ici.pyz` where noted.

```text
pytest: 108 passed
ruff check/format: All checks passed; 33 files already formatted
mypy --strict: Success: no issues found in 17 source files
released ici test: 108/108 Tests Passed | Line: 92.4%, Func: 97.5%, Branch: 83.0% | TEM: 4.87 / 5.0
released ici complexity: Max Cyclomatic Complexity 14 (limit 15), 0 issues
released ici cognitive: Max cognitive complexity 21
released ici line: 4,193 total / 3,633 code lines, no per-file warning
released ici verify: 11 PASS, 1 SKIP, 1 WARN; no FAIL or ERROR
```

The remaining verification warning is the existing duplication inventory
(12.8%, 54 clone groups). Generated JSON reports, `.venv`, and `uv.lock` were
not left in EnvLens; all changes remain uncommitted for the parent integration.

## Next Steps

- Decide separately whether the duplication inventory merits a future
  behavior-preserving cleanup.
- Parent integration should review the new helper module boundaries alongside
  the existing live refactors before committing.
