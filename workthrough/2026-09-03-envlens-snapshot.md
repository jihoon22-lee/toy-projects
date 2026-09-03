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

Files: `envlens/README.md`, `quality-zoo/README.md`, `README.md`, `CHANGELOG.md`,
`docs/superpowers/2026-08-30-product-portfolio-master-plan.md`,
`docs/superpowers/2026-08-30-handover.md`, and the related Quality Zoo workthrough.

- Documented the quick start, schema shape, privacy policy, bounds, process
  tree behavior, atomic output, local gates, and deliberate future boundary.
- Recorded the merged PR and exact-main evidence for the path-aware EnvLens
  manifest. The exact-main run has four current project outputs; the full
  artifact/Page table is kept below as the canonical byte-level record.
- Reconciled the shared Quality Zoo portfolio records with the same exact-main
  run and retained Q1~Q5 as pending.
- Marked E1 complete in the master plan while leaving E2, E3, and the E4 release
  boundary open.
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

### Merged PR and exact-main evidence

[PR #50](https://github.com/jihoon22-lee/toy-projects/pull/50) merged as
`c307ac1ab01e12e4ac81a34623eb669da0e43641`. Its exact-head push passed exact-main
run [`33698248293`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33698248293):
all jobs, the main publisher, and `Merge Gate` succeeded; the PR-only publisher
was skipped as expected for a push. The dynamic manifest-backed ici verify now
contains four project outputs; the PR-only sticky publisher derives its link
cardinality from that same manifest. The exact-main artifacts were:

| Artifact | ID |
|---|---:|
| EnvLens ici report | [`9872574260`](https://github.com/jihoon22-lee/toy-projects/actions/artifacts/9872574260) |
| EnvLens Python 3.10 | [`9872561889`](https://github.com/jihoon22-lee/toy-projects/actions/artifacts/9872561889) |
| EnvLens latest Python | [`9872564898`](https://github.com/jihoon22-lee/toy-projects/actions/artifacts/9872564898) |
| Quality Zoo | [`9872561713`](https://github.com/jihoon22-lee/toy-projects/actions/artifacts/9872561713) |

The EnvLens report was `PASS` with 13 total engines, 12 `PASS`, and one C++
`SKIP`; its test result was `50/50`, with line/function/branch coverage
`93.0% / 100.0% / 84.6%` and TEM `5.0`. Python 3.10 and latest produced the
same pure wheel SHA-256
`906c86270b2cad5c693816d43fa4d143e50be0cc3f3a852fdd538a453f51e3df` and sdist
SHA-256 `d8bf786c6bb6569371bc27092f3ade01e03315abca4fb862b66d22cd6ac9e63e`.
The Quality Zoo artifact recorded contract `PASS`, one stable scenario with the
expected observed `WARN`, zero errors, and released ici `v0.10.2` executable
SHA-256 `8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4`.

All four main Pages were byte-identical to their exact-main artifacts and passed
the exact title and Zero-CDN checks:

| Project | Main Page | Bytes | SHA-256 |
|---|---|---:|---|
| buildscope | [buildscope/main](https://jihoon22-lee.github.io/toy-projects/buildscope/main/) | 1,345,473 | `301e18c625928cccc5e4b69fb2f132229156751d2b1b2e8b28fbb34d840bb86e` |
| diskmap | [diskmap/main](https://jihoon22-lee.github.io/toy-projects/diskmap/main/) | 545,766 | `b85f2276e6a57a680ee20a80b0e66fe39da390e991e03f0474b0c864e38a5af6` |
| envlens | [envlens/main](https://jihoon22-lee.github.io/toy-projects/envlens/main/) | 279,859 | `03ecc3b3e852d22ec233b37c1a86478cf3ea8a361aabaf329ae1e03648e50279` |
| loglens | [loglens/main](https://jihoon22-lee.github.io/toy-projects/loglens/main/) | 487,155 | `f72fe631339edc4d954bb45235627527befd35b21fa33e1a9fcca833fac11712` |

This closes EnvLens E1 and Quality Zoo Q0 remote/main acceptance. EnvLens
remains `0.1.0`/`Unreleased`; E2 snapshot diff/compatibility, E3 project/runtime
smoke, E4 release criteria, and Quality Zoo Q1~Q5 remain pending.

## Status and Next Steps

- Local snapshot-core implementation and its Python 3.10 test/static gates are
  complete.
- `ci/projects.json` includes envlens, and the Python 3.10/latest CI matrix
  covers tests, schema validation, strict typing, reproducible wheel/sdist
  builds, pure `py3-none-any` metadata, and clean-wheel smoke. PR #50 and the
  exact-main verification are complete; a published stable wheel or source
  distribution remains an E4 release concern.
- E2 will add snapshot diff and offline compatibility policies; E3 will add
  project/runtime smoke. E4 covers the release boundary. None is implied by
  the v1 snapshot.
- A stable release requires the portfolio’s native/package tests, released-ici
  verification, PR and exact-main evidence, user documentation/limitations,
  and reproducible release assets.
