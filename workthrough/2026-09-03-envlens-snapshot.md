# EnvLens deterministic environment snapshot core

## Overview

The `feat/envlens-snapshot` slice makes one explicitly selected Python
interpreter inspectable as a deterministic, offline `envlens.snapshot/v1`
document. It provides both an importable pure-Python API and a CLI, with
default-safe redaction, bounded probe execution, structured partial metadata
errors, and atomic private output. The package remains an unreleased `0.1.0`
development identity.

## Context

The portfolio needs an independent Python runtime/package evidence producer
before it can support environment diffing or compatibility decisions. A
snapshot must be reproducible enough for fixtures, safe enough to write to a
reviewable artifact, and bounded when the selected interpreter is missing,
malformed, slow, noisy, or has a child process that inherits the probe pipes.
This slice deliberately stops before comparison, dependency resolution, wheel
tag analysis, project imports, and runtime smoke.

## Changes Made

### 1. Shell-free interpreter probe

Files: `envlens/src/envlens/probe.py`, `envlens/src/envlens/snapshot.py`

- Accepts an explicit executable path, resolves its real target, and runs one
  fixed `python -c` probe with `shell=False`.
- Captures interpreter identity, prefixes, platform/compiler data, `sysconfig`,
  environment variables, and installed distribution metadata.
- Records display and normalized distribution names, versions,
  `Requires-Python`, `Requires-Dist`, entry points, locations, and
  distribution-specific errors.
- Preserves healthy distributions when another distribution’s metadata fails,
  reporting `collection.status = "partial"` with an error count.

### 2. Deterministic strict snapshot contract

Files: `envlens/src/envlens/snapshot.py`,
`envlens/schemas/envlens-snapshot-v1.schema.json`,
`envlens/src/envlens/__init__.py`

- Added the strict `envlens.snapshot/v1` envelope with producer, timestamp,
  redaction policy, source identity/sysconfig, environment, distributions, and
  collection accounting.
- Separates `captured_at` from source identity and normalizes explicit or
  generated timestamps to UTC with second precision.
- Sorts object keys, distributions, requirements, entry points, and errors;
  canonical output is ASCII-safe JSON with one final newline. Pretty output
  only changes indentation.
- Enforces bounded fields: 10,000 distributions, 4,096 environment/sysconfig
  fields, 100,000 nested collection items, and 65,536 characters per string.
  Duplicate keys, non-finite numbers, malformed protocol shapes, and oversized
  values fail closed.

### 3. Privacy, process, and output boundaries

Files: `envlens/src/envlens/redaction.py`, `envlens/src/envlens/probe.py`,
`envlens/src/envlens/io.py`, `envlens/src/envlens/__main__.py`

- Replaces target and host home paths with `<USER_HOME>` and redacts
  secret-bearing environment values as `<REDACTED>`.
- Covers token/password/API/access/private-key/auth/cookie/credential/secret/
  registry/repository name families and `_URL`/`_URI` suffixes.
- Scrubs URL userinfo and common secret query values across all captured
  strings, including distribution requirements, entry points, locations, and
  metadata errors. The CLI has no unredacted option; library callers can opt
  out only explicitly with `redact=False` in a controlled context.
- Bounds the default probe timeout at 10 seconds, stdout at 8 MiB, and retained
  stderr at 64 KiB. POSIX probes use a new session/process group; Windows probes
  use a new process group. Timeout or inherited-pipe cleanup terminates
  descendants with bounded escalation (`SIGTERM` then `SIGKILL` on POSIX and
  `taskkill /T /F` on Windows).
- Writes output through same-directory atomic replacement with POSIX mode
  `0600`, and refuses symlink/special-file destinations, a directly symlinked
  output directory, and replacing the selected interpreter or an existing
  hardlink alias.
- Converts invalid interpreter, probe, protocol, timeout, and output failures
  to concise CLI errors with exit status `2`; envlens remains a current-user
  process boundary, not an operating-system sandbox.

### 4. User and portfolio documentation

Files: `envlens/README.md`, `README.md`, `CHANGELOG.md`,
`docs/superpowers/2026-08-30-product-portfolio-master-plan.md`,
`docs/superpowers/2026-08-30-handover.md`

- Documented the quick start, schema shape, privacy policy, bounds, process
  tree behavior, atomic output, local gates, and deliberate future boundary.
- Added envlens to the portfolio status table as a local snapshot-core result
  with its path-aware manifest and Python 3.10/3.14 CI matrix integrated; EnvLens
  PR/exact-main/release evidence remains pending.
- Reconciled the shared Quality Zoo portfolio records with complete Q0 remote
  evidence: [PR #49 run `33693241255`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33693241255), exactly one sticky comment containing
  exactly one marker and three product HTML links at that time, merge
  `ed5fea2e881da77ac95482cf665e4e40bfe172f1`, and exact-main run
  [`33694452357`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33694452357).
  Q1~Q5 remain pending.
- Marked only the six local EnvLens snapshot tasks complete in the master plan.
  The full E0~E4 checkpoint, diff/compatibility, runtime smoke, EnvLens remote
  evidence, and release remain open.
- Kept the product identity at `0.1.0`/`Unreleased`; no tag, release, or version
  bump was made.

## Code Examples

### CLI capture

```bash
envlens snapshot \
  --interpreter /path/to/python \
  --captured-at 2026-09-03T00:00:00Z \
  --output snapshot.json
```

### Snapshot envelope

```json
{
  "schema_version": "envlens.snapshot/v1",
  "producer": {"name": "envlens", "version": "0.1.0"},
  "captured_at": "2026-09-03T00:00:00Z",
  "redaction": {"policy": "envlens-redaction/v1", "enabled": true},
  "source": {
    "kind": "python-interpreter",
    "requested_executable": "/usr/bin/python3",
    "resolved_executable": "/usr/bin/python3.10",
    "identity": {
      "implementation": "cpython",
      "version": "3.10.21",
      "version_info": [3, 10, 21, "final", 0],
      "cache_tag": "cpython-310",
      "platform": "linux",
      "machine": "x86_64",
      "reported_executable": "/usr/bin/python3.10",
      "prefix": "/usr",
      "base_prefix": "/usr",
      "exec_prefix": "/usr",
      "compiler": "GCC"
    },
    "sysconfig": {"paths": {}, "variables": {}}
  },
  "environment": {"variables": {}},
  "distributions": [],
  "collection": {"status": "complete", "distribution_count": 0, "error_count": 0}
}
```

## Verification Results

All commands below were run with the Python 3.10 interpreter selected by uv.

### Tests

```text
uv run --python 3.10 --project envlens --with pytest pytest envlens/tests -q
50 passed
```

The test breakdown is CLI `7`, atomic I/O `6`, probe/process boundary `12`,
redaction `7`, and snapshot normalization/schema `18`.

### Static quality

```text
Ruff check:             All checks passed!
Ruff format --check:    12 files already formatted
strict mypy:            Success: no issues found in 6 source files
```

The same 50 tests, Ruff checks, strict Python-3.10 typing contract, and a real
schema-validated snapshot also pass under Python 3.14.7.

`envlens/ici.toml` records the intended test/coverage contract: TEM at least
4.0, branch coverage at least 80%, and function coverage at least 90%.

The released ici `v0.10.2` local deep verification is `PASS` across 14 total
engines: `13 PASS / 0 WARN / 0 FAIL / 0 ERROR / 1 compile_db SKIP`. It records
TEM `5.00`, line/function/branch coverage `93.0% / 100.0% / 84.6%`, complexity
max `13`, and passing cycle/sanitize checks.

Two clean `SOURCE_DATE_EPOCH=1700000000` builds were byte-identical. The audited
artifacts were:

```text
envlens-0.1.0-py3-none-any.whl
  sha256 906c86270b2cad5c693816d43fa4d143e50be0cc3f3a852fdd538a453f51e3df
envlens-0.1.0.tar.gz
  sha256 d8bf786c6bb6569371bc27092f3ade01e03315abca4fb862b66d22cd6ac9e63e
```

The wheel and sdist had unique members, no native libraries or runtime
dependencies, exact `0.1.0`/`>=3.10` metadata, the schema and `py.typed`, and a
pure `py3-none-any` wheel tag. A clean wheel install produced a real snapshot
that validated against the byte-identical packaged schema. These are local
candidate artifacts only; they were not published as a release.

## Status and Next Steps

- Local snapshot-core implementation and its Python 3.10 test/static gates are
  complete.
- `ci/projects.json` now includes envlens, and the Python 3.10/3.14 CI matrix
  covers tests, schema validation, strict typing, reproducible wheel/sdist
  builds, pure `py3-none-any` metadata, and clean-wheel smoke. EnvLens remote PR
  CI, sticky report, exact-main verification, and a built/released wheel or
  source distribution are pending; the local released-ici evidence above is
  complete.
- E2 will add snapshot diff and offline compatibility policies; E3 will add
  project/runtime smoke. Neither is implied by the v1 snapshot.
- A stable release requires the portfolio’s native/package tests, released-ici
  verification, PR and exact-main evidence, user documentation/limitations,
  and reproducible release assets.
