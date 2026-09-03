# envlens

envlens is a pure-Python library and CLI for making deterministic, offline
inventories of explicitly selected Python interpreters, comparing those
inventories, and checking a project against configured runtimes.

The package metadata and `--version` output are currently `0.1.0`. This is an
unreleased development identity: no envlens tag, GitHub Release, or stable
artifact has been published.

## Capture a snapshot

Select an executable path when inspecting another environment. envlens does not
resolve command names through `PATH` and does not invoke a shell. With no
`--interpreter`, it uses the interpreter running envlens.

```bash
envlens snapshot \
  --interpreter /path/to/python \
  --output snapshot.json \
  --pretty

# The default output is stdout; an explicit instant makes fixtures reproducible.
python -m envlens snapshot \
  --captured-at 2026-09-03T00:00:00Z \
  --output -
```

The selected executable is run once with a fixed `python -c` probe and
`shell=False`. The probe records:

- implementation, Python version and `sys.version_info`, cache tag, executable
  paths, prefixes, platform, machine, and compiler;
- `sysconfig` paths and scalar configuration variables;
- environment variables; and
- every discovered distribution’s display name, PEP 503 normalized name,
  version, raw `Requires-Python`, `Requires-Dist`, entry points, install
  location, status, and any metadata errors; when available, import names
  (`top_level.txt`/installed file evidence) and wheel `Tag:` records.

One malformed distribution does not discard healthy distributions. Its error
records are retained in that distribution and the overall collection is marked
`partial` while the command still succeeds.

## The `envlens.snapshot/v1` contract

The checked-in strict schema is
[`schemas/envlens-snapshot-v1.schema.json`](schemas/envlens-snapshot-v1.schema.json).
Its top-level fields are:

```text
schema_version  producer  captured_at  redaction
source          environment  distributions  collection
```

`source` contains the requested and resolved executable plus `identity` and
`sysconfig` objects. `environment.variables` contains the captured environment.
Each distribution has `metadata`, `entry_points`, `location`, `status` (`ok` or
`error`), and structured `errors`. `collection` reports `complete` or `partial`,
the distribution count, and the total error count.

The schema version and source identity are separate from `captured_at`.
`captured_at` is normalized to a UTC ISO-8601 instant with second precision;
tests and reproducible automation can provide it explicitly. By default,
serialization sorts object keys and every unordered collection, emits compact
ASCII-safe JSON, and ends with one newline. `--pretty` changes indentation only.

The public normalization boundary is bounded: at most 10,000 distributions,
4,096 environment or sysconfig fields, 100,000 items in a distribution’s
requirements/entry-point/error arrays, and 65,536 characters in a string field.
Malformed shapes, duplicate JSON keys, non-finite numbers, or values beyond
these limits are rejected instead of being silently normalized.

## Privacy and output safety

Redaction is enabled by default for CLI snapshots. Host and target home paths
are replaced with `<USER_HOME>` (including case and Windows slash variants).
Environment-variable names that look secret-bearing retain their names but get
`<REDACTED>` values. The detector covers token, password, API/access key,
private key, authorization, cookie, credential, secret, registry, and repository
families, as well as names ending in `_URL` or `_URI`.

Every captured string is also scanned for URL credentials and common secret
query parameters. For example, userinfo in
`https://alice:password@example.invalid/` and values for `token`, `api_key`,
`access_token`, `password`, `secret`, and related keys are replaced even when
they occur in distribution requirements, entry points, locations, or metadata
errors. A variable such as `PIP_INDEX_URL` is fully redacted by its sensitive
name. The CLI deliberately has no unredacted option. Library callers may
explicitly pass `redact=False` to `collect_snapshot` only in a controlled,
trusted context; that mode is represented by `redaction.enabled: false`.

Probe execution has bounded failure behavior:

- the default timeout is 10 seconds (`--timeout-seconds` changes it);
- probe stdout is capped at 8 MiB and retained stderr is capped at 64 KiB;
- POSIX probes run in a new session/process group, and Windows probes in a new
  process group; timeout or inherited-pipe cleanup terminates descendants
  (`SIGTERM` then bounded `SIGKILL` on POSIX, `taskkill /T /F` on Windows); and
- missing or non-executable interpreters, nonzero probe exits, timeouts,
  oversized output, malformed protocol JSON, invalid protocol shapes, and
  output failures produce exit status `2` with a concise user-facing error.

The process-group handling prevents a child that inherits stdout/stderr from
making envlens wait forever, but it is best-effort cleanup rather than an OS
sandbox. envlens executes the selected interpreter as the current user and does
not block that interpreter’s filesystem or network access. Inspect unknown
interpreters without secrets and, when appropriate, inside an externally
enforced disposable container or VM.

Snapshot files are written by an atomic same-directory replacement. On POSIX a
successful file is mode `0600`; symlink and special-file destinations, a
directly symlinked output directory, and replacing the selected interpreter
(including an existing hardlink alias) are rejected. `--output -` writes the
canonical JSON to stdout.

## Compare snapshots

The `diff` command consumes only local snapshot files. It classifies
distributions by normalized project name as added, removed, upgraded,
downgraded, or uncertainly changed. It keeps project names separate from
observed import names, checks recorded `Requires-Python` expressions and wheel
tags against the “after” interpreter, and reports direct missing or conflicting
dependencies from offline metadata.

```bash
envlens diff \
  --before before.json \
  --after after.json \
  --project pyproject.toml \
  --format markdown \
  --output diff.md

# text is the default; json supports --pretty
envlens diff --before before.json --after after.json --format json
```

Compatibility and dependency records always include `certainty`. `certain`
means the bounded metadata and version evaluator reached a direct conclusion;
`unknown` is used for partial snapshots, unsupported requirement/marker
syntax, absent import/wheel evidence, or versions outside the evaluator. No
resolver, package index, wheel download, or network request is performed.

## Project and runtime smoke checks

`runtime` (also available as `smoke`) reads `pyproject.toml` without importing
or executing project code, then runs compileall and each explicit import for
every configured interpreter. Entry points are reported with their file and
line location and remain dry-inspected unless execution is explicitly opted in.

```bash
envlens runtime \
  --project-root . \
  --interpreter /path/to/python \
  --interpreter /path/to/another/python \
  --import myproject \
  --import myproject.cli \
  --entry-point console-name \
  --format text

# Entry-point execution is opt-in and still uses argv, no shell.
envlens runtime --project-root . --entry-point console-name \
  --execute-entry-points --format json --pretty
```

Runtime results distinguish `passed`, `missing-interpreter`,
`missing-import`, `import-error`, `timeout`, `signal`, and ordinary process
failure. Source enumeration is bounded to 10,000 Python files and 64 MiB;
probe/check output and process lifetime are bounded, and POSIX/Windows process
groups are cleaned up on timeout. Compile bytecode is redirected to a temporary
cache, and the compileall argv is capped at 65,536 UTF-8 bytes. Import checks
set `PYTHONPATH` to the project root and `src/` only; a host `PYTHONPATH` is not
inherited. Runtime checks execute with the current user’s permissions and are
not a sandbox; inspect untrusted projects in an externally isolated environment.

## Validation and CI

The current local E1–E3 slice is covered on Python 3.10 by 78/78 tests:

```text
10 CLI/E2-E3 CLI · 6 atomic I/O · 12 probe/process-boundary ·
7 redaction · 19 snapshot normalization/schema · 10 snapshot diff ·
8 project/runtime smoke
```

The same checkout also passes Ruff check and format validation, and strict mypy
for the eleven envlens source modules. Released ici `v0.10.2` local deep verification
passes with 14 total engines: 13 PASS, one `compile_db` SKIP, and no
WARN/FAIL/ERROR; TEM is 5.00, line/function/branch coverage is
93.0%/100.0%/84.6%, and complexity max is 13 (cycle and sanitize also PASS).
`envlens/ici.toml` records the intended
Python quality gate (test pass/fail with coverage, TEM at least 4.0, branch
coverage at least 80%, and function coverage at least 90%). The path-aware CI
manifest runs a dedicated Python 3.10/latest matrix for tests, schema
validation, strict typing, reproducible wheel/sdist builds, pure
`py3-none-any` metadata, and clean-wheel smoke. [PR #50](https://github.com/jihoon22-lee/toy-projects/pull/50)
merged as `c307ac1ab01e12e4ac81a34623eb669da0e43641`, and exact-main run
[`33698248293`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33698248293)
passed. The exact artifact IDs, four main Pages, and byte-level evidence are
centralized in the [EnvLens workthrough](../workthrough/2026-09-03-envlens-snapshot.md).
Stable release remains pending.

## Deliberate current boundary

This release-free E1–E3 slice compares captured evidence and performs explicit
runtime checks; it does not resolve dependencies, build or install wheels, or
claim compatibility when metadata is incomplete. E4 release work remains
pending: Python-version matrix publication, clean-wheel release smoke,
packaging evidence, and the ici release gate are separate policies.
