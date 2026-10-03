# Phase 6 — Per-Product Feature Round 1

> **역사적 작업 기록** — 아래 기능 범위, 계획, 테스트 수, 버전 및 Git/배포 상태는
> 해당 작업 단계에서 기록한 내용이다. 현재 사용법이나 최신 검증·배포 상태를 뜻하지 않는다.
> 현재 제품별 안내는 [저장소 README](../README.md), 변경 이력은
> [CHANGELOG](../CHANGELOG.md), 후속 계획은 [ROADMAP](../ROADMAP.md)을 참고한다.

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
  wrote the effective options before scanning at this stage. Session `multiline` and
  `max_record_bytes` now reach the CLI assembler, matching the GUI contract.

The write-before-scan behavior and deferred GUI wiring below are historical. The
[current LogLens guide](../loglens/README.md) documents session v2, GUI plugins/triage,
and saving only after input validation and scanning succeed.

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

The normalized-path matching and estimate-retention statements above describe this
round's reported contract. Later review found defects in path/glob matching and
partial replay handling; the subsequent fixes are recorded in the
[portfolio implementation ledger](2026-10-03-portfolio-expansion.md).
Current behavior is documented in the [BuildScope guide](../buildscope/README.md).

### EnvLens — snapshot v2 (3ff1037, pre-existing in main)

- `identity.environment_kind` detects `venv`/`virtualenv`/`conda`/`system`.
- Real fix: probing now runs the *requested* interpreter path, so a venv's
  `pyvenv.cfg`/`conda-meta` context is not lost through symlink resolution;
  the resolved binary path is kept as metadata.
- `envlens-snapshot-v2.schema.json` added alongside v1; diff accepts both.

## Verification Results at That Checkpoint

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

## Decisions and Limits at That Checkpoint

- All four features are additive to existing contracts; no schema versions
  were bumped except envlens's planned v2.
- Delayed replay matches on the snapshot's normalized `file` field, not argv.
- LogLens session load treats a missing file as a CLI error (unlike the
  optional named stores, where missing is a successful empty load).
- GUI wiring for sessions is intentionally deferred; the persistence layer is
  GUI-independent.

## Next Steps Recorded at That Checkpoint

- ROADMAP remainder: DiskMap incremental rescans and cleanup-policy expansion;
  LogLens parser plugins and GUI session save/load; BuildScope streaming load
  for large databases; AbiLens symbol versions, vtable layout, diff policy DSL.

The deferred items above are not a current backlog. Later rounds implemented several
of them; consult the linked current product guides and ROADMAP before planning work.
