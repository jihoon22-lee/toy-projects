# EnvLens Runtime Selection and Project-Integrity Checks

## Overview

Runtime checks now preserve two previously silent integrity failures. A
requested entry point that is not present in project metadata becomes a
deterministic failed check, and a present but malformed or unreadable
`pyproject.toml` becomes a location-aware project-inspection failure. A
missing default `pyproject.toml` remains a normal no-metadata case.

## Context

`_select_entry_points` previously returned an empty list when a configured
selector did not match any entry point. Compile and import checks could then
pass while the requested runtime surface was never checked. Similarly,
`_project_info` retained `inspection_error` only under the project result;
the runtime summary considered compile/import checks alone and could report
PASS for malformed metadata.

## Changes Made

### Entry-point selection

- `runtime_execution.select_entry_points` still selects every entry matching a
  bare name and only entries matching an exact `group/name` selector.
- Unmatched selectors are deduplicated and sorted before synthetic records are
  appended, making their order deterministic without changing the order or
  multiplicity of matching project entries.
- Synthetic records carry the requested selector, `error_code:
  "missing-entry-point"`, a stable `{path, line: 0}` location, and a reason.
- Runtime execution and unavailable-interpreter records preserve those records
  as failed checks without importing or executing anything.

### Project metadata integrity

- When the caller leaves `pyproject` unset and the project root has no
  `pyproject.toml`, metadata inspection is skipped with a normal warning and
  no inspection failure.
- A present but malformed, unreadable, or explicitly requested missing
  metadata file retains the existing `inspection_error` and now also carries
  a `{path, line}` location. Parser messages with a line number retain that
  number; read errors use line `0`.
- Each runtime interpreter receives a `project-inspection` failed check with
  the original project error code, location, and message. The existing
  summary failure logic therefore cannot be masked by successful compile or
  import checks.

### Documentation and regression coverage

- `README.md` documents missing-default metadata, metadata failure behavior,
  qualified entry-point selection, and `missing-entry-point` evidence.
- Added focused selection, runtime, malformed-metadata, no-metadata, and CLI
  regression tests. The local E1–E3 count is now 112 tests.

## Code Examples

### Deterministic missing selector evidence

```python
# src/envlens/runtime_execution.py
missing = sorted(wanted - matched)
for selector in missing:
    selected.append(
        {
            "status": "missing",
            "requested": selector,
            "error_code": "missing-entry-point",
            "location": {"path": str(location_path), "line": 0},
        }
    )
```

The internal marker is converted into a failed runtime check by
`_entry_check` and by the unavailable-interpreter path.

### Project error as a summary-visible check

```python
# src/envlens/runtime.py
{
    "kind": "project-inspection",
    "name": str(project_info["path"]),
    "status": "failed",
    "error_code": str(raw_error["code"]),
    "location": {"path": str(project_info["path"]), "line": line},
    "reason": str(raw_error["message"]),
}
```

## Verification Results

All checks used `/tmp/toy-ici-python-tools/bin/python` (Python 3.10):

```text
pytest: 112 passed in 2.13s
ruff check src tests: All checks passed!
ruff format --check src tests: 31 files already formatted
mypy --strict src/envlens: Success: no issues found in 17 source files
```

A manual runtime probe also confirmed that an absent default metadata file
produces `summary.status == "passed"`, while a malformed present file produces
`summary.status == "failed"` and an `unsupported-pyproject` project check at
the parser's line number.

No dependencies, versions, commits, pushes, or changes outside EnvLens were
made.

## Next Steps

- Keep the new failure-code fields stable if additional runtime report
  consumers are added.
- Decide separately whether the project-inspection check should gain a richer
  user-facing renderer; JSON already preserves the complete evidence.
