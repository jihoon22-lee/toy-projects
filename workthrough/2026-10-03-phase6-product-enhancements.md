# Phase 6 — Per-Product Feature Round 1

## Overview

First feature round after the ici decoupling: each product received one real
capability aligned with its ROADMAP direction. All work landed through
independent PRs with the native Merge Gate green.

| Product | Feature | PR |
|---------|---------|----|
| AbiLens | exported dynamic-symbol surface + ABI diff axis | #98 |
| DiskMap | snapshot diff report filters | #99 |
| LogLens | `loglens.session/v1` investigation sessions | #100 |
| BuildScope | `delayed` include-analysis with per-unit replay globs | #101 |
| EnvLens | snapshot v2 + interpreter environment detection | `3ff1037` |

## Changes Made

### AbiLens — dynamic symbols (#98)

- `abilens/src/inspect.cpp`: parse `.dynsym` directly from ELF program headers
  (works on stripped binaries with no section headers); bounds-checked symbol
  and string-table reads.
- `abilens/src/report_json.cpp`: additive `symbols` array on report v1; the
  parser accepts older reports without the field and still rejects unknown
  fields.
- `abilens/src/diff.cpp`: `symbols` set diff — additions/removals count as
  ABI-affecting changes.
- Tests: synthetic fixture exports a real dynsym; integration covers inspect
  and diff. Schema files and README updated.

### DiskMap — diff filters (#99)

- New CLI options: `--diff-kind KIND` (repeatable; added|removed|grown|shrunk|
  moved|uncertain), `--diff-min-delta BYTES`, `--diff-certain-only`.
- `diskmap/src/storage_cli.{hpp,cpp}`: `SnapshotDiffFilter` presentation layer;
  the conservative diff engine is untouched. A delta bound is `|before-after|`
  bytes and is provable only when both metrics are known — unknown metrics can
  never satisfy a bound. Text header reports `N change(s) shown of M` whenever
  a filter is active; JSON applies the same predicate.
- `--diff-*` without `--compare-snapshot` is a usage error.

### LogLens — investigation sessions (#100)

- New versioned store `loglens.session/v1`: `{schema, name?, source{path,
  format?, multiline?, max_record_bytes?}, filter?, level?}` — one document
  bundles a source and the active query of an investigation.
- `persistence_validation.cpp`: `validSession` reuses the same fail-closed
  rules (strict field whitelists, canonical format names, `Filter::parse`,
  UTF-8/control-byte checks, bounds); `source.path` bounded to 4096 bytes.
- CLI: `--session FILE` loads a session and fills only options the command
  line did not set explicitly (explicit flags win); `--save-session FILE`
  writes the effective options before scanning. Session `multiline` and
  `max_record_bytes` now reach the CLI assembler, matching the GUI contract.

### BuildScope — delayed replay (#101)

- `--include-analysis delayed`: every translation unit is source-scan
  estimated; only units whose normalized `file` path matches a repeatable
  `--analysis-unit GLOB` get a bounded compiler `-E -H` replay.
- Replays share the existing unit/time budget; an attempt spends
  `--analysis-max-units` budget whether it succeeds or fails. A matched unit
  whose replay is skipped/failed keeps its estimate plus a coded warning
  diagnostic (`include-analysis-unit-limit`/`include-analysis-replay-failed`)
  instead of becoming `unavailable`.
- `--analysis-unit` without `delayed` is rejected.

### EnvLens — snapshot v2 (3ff1037, pre-existing in main)

- `identity.environment_kind` detects `venv`/`virtualenv`/`conda`/`system`.
- Real fix: probing now runs the *requested* interpreter path, so a venv's
  `pyvenv.cfg`/`conda-meta` context is not lost through symlink resolution;
  the resolved binary path is kept as metadata.
- `envlens-snapshot-v2.schema.json` added alongside v1; diff accepts both.

## Verification Results

- DiskMap: `ctest` 18/18; new `test_storage_cli` cases cover kind sets,
  certain-only, delta bounds (boundary/above), unknown-metric rejection,
  JSON/text parity. E2E flags verified on real snapshots.
- LogLens: `ctest` 18/18 incl. new `testSessionRoundTripAndValidation`
  (round-trip canonical bytes, minimal-doc defaults, schema/value rejections,
  failed-save preserves store) and `test_cli_stream.cmake` session
  save→load/override/missing-file coverage.
- BuildScope: `ctest` 9/9 incl. `delayedAnalysisSelection`; delayed output
  validates against `buildscope-snapshot-v3.schema.json`.
- AbiLens: `make check` + sanitizer variants green (PR #98).
- All four PRs merged after full native CI + Merge Gate.

## Decisions and Limits

- All four features are additive to existing contracts; no schema versions
  were bumped except envlens's planned v2.
- Delayed replay matches on the snapshot's normalized `file` field, not argv.
- LogLens session load treats a missing file as a CLI error (unlike the
  optional named stores, where missing is a successful empty load).
- GUI wiring for sessions is intentionally deferred; the persistence layer is
  GUI-independent.

## Next Steps

- ROADMAP remainder: DiskMap incremental rescans and cleanup-policy expansion;
  LogLens parser plugins and GUI session save/load; BuildScope streaming load
  for large databases; AbiLens symbol versions, vtable layout, diff policy DSL.
