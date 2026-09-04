# Consolidated quality portfolio expansion — 2026-09-04

## Overview

The portfolio worktree now combines four complementary quality surfaces:
path-aware CI for the user-facing projects, AbiLens for ELF/ABI artifacts, a
sixteen-scenario candidate-only Quality Zoo corpus, and a LogLens investigation
workbench.  The changes are intentionally kept under the existing product
versions; they describe implementation and local verification only.  No stable
release, PR/Pages acceptance, or remote merge result is recorded here.

## Context

The original CI ran every project for every small change, while the existing
Quality Zoo covered only a narrow set of known answers.  The toy applications
also needed evidence surfaces that make ici findings useful to a developer:
native binary compatibility for C++ artifacts and a way to preserve and compare
the exact log records behind an investigation.

## Changes Made

### 1. Affected-scope CI

- `ci/check_manifest.py` now maps changed repository paths to affected projects
  and stable Quality Zoo scenarios.
- Changes to shared CI, workflow, runner/test, or manifest contracts fan out to
  the full stable scope.  `main` pushes and manual runs remain full-scope.
- Selected and skipped projects are separate outputs.  A skipped item is shown
  in the ledger instead of being treated as a green matrix result.
- A docs-only change with no selected project emits a scope-only report shape;
  it does not reuse stale project report links.
- GUI projects continue to expand to both Qt5 and Qt6 legs from one manifest
  entry, and non-GUI native checks remain independently visible.

The relevant workflow boundary is an explicit selection, not a shell-generated
matrix:

```text
changed paths
    ├─ project-owned path  → affected project matrix
    ├─ Quality Zoo scenario → affected scenario set
    └─ shared contract      → complete stable scope
```

### 2. AbiLens artifact evidence

The new `abilens/` project is a dependency-free C++20/Linux command-line
inspector.  Its direct parser validates ELF identification and table bounds
before invoking a bounded, shell-free GNU `readelf` process.  Reports include
ELF class/endian/type/machine, dynamic/static and stripped evidence,
`DT_NEEDED`/RPATH/RUNPATH, and typed GLIBC/GLIBCXX/CXXABI requirements.

`inspect` produces deterministic `abilens.report/v1` JSON and text output;
`diff` compares two reports or binaries; and the policy input checks expected
class/machine, ABI floors, absolute RPATH, and forbidden dependencies.  The
report reader is strict about version, namespace, status, tool identity,
duplicate fields, bounded nesting, unique ABI tuples, and maximum consistency.
The Make adapter protects output trees with ownership markers and isolates
release, coverage, sanitiser, and thread-sanitiser directories.
Header validation and GNU `readelf` now consume the same once-opened descriptor.
Metadata and the original path identity are revalidated before accepting the
report, so ordinary path replacement and in-place mutation fail closed. Parallel
Make recipes also wait for one shared output-marker prerequisite.

### 3. Candidate-only Quality Zoo expansion

`quality-zoo/manifest.json` remains the released six-scenario registry.  The
explicit `candidate-manifest.json` now contains sixteen entries, adding:

- two CMake/CTest ThreadSanitizer cases (a real project-owned race and a
  mutex-protected counterpart);
- three C++ build/quality cases (target build context, malformed compilation
  database, and coverage/complexity/dead-code/duplication signals);
- three Python depth cases (compatibility/package metadata, import-cycle and
  exception handling, and maintainability thresholds);
- one Python security/resource/correctness case; and
- one Make-to-ELF/integration case.

Every candidate entry selects its full strict expectation by the exact
candidate executable digest.  The candidate channel remains non-stable and
does not alter the released six-scenario path or toy product versions.

### 4. LogLens investigation workbench

The GUI now has an Investigation dock with three focused views:

- **Record** shows parsed fields, diagnostics, source/line identity, byte
  accounting, raw evidence, bookmark state, and an annotation. Selected rows
  stream as bounded `loglens.selection/v1` JSON with byte-preserving
  `raw_base64`, `message_base64`, and `source_base64` fields.
- **Highlights** persists literal or whole-row rules, priority and safe colour
  values in bounded `loglens.triage/v1` state.  CRUD, reorder, bookmark and
  annotation updates use the existing atomic persistence backend; legacy v0
  rule-only files are explicitly marked for migration.
- **Compare** selects half-open timeline ranges and compares baseline with
  comparison.  Deterministic new-pattern/rate-spike findings and raw
  correlation groups retain counts, rates, explanations and source-line
  locations, and an activated result navigates back to the table.

The custom Qt delegate converts UTF-8 byte spans to UTF-16 paint positions and
keeps the model as the single source of truth for highlights.  Timeline range
selection is composed with the existing filter/search projection, and invalid
or empty actions leave the current investigation intact.

## Code examples

The public contracts are deliberately small and versioned:

```json
{"schema":"loglens.triage/v1","rules":[
  {"name":"Timeout","pattern":"timeout","whole_line":false,
   "priority":40,"style":"#ffcc00"}
],"entries":[
  {"source_path":"service.log","line_number":42,
   "bookmarked":true,"annotation":"check upstream retry"}
]}
```

```json
{"schema":"loglens.selection/v1","source_path":"service.log",
 "records":[{"line_number":"42","raw_base64":"...","message_base64":"...",
              "parse_status":"Parsed","diagnostics":[]}]}
```

The analysis core compares timestamp windows as `[begin_ms, end_ms)` and
reports signals such as `new-pattern` and `rate-spike`; it is a measured
heuristic and intentionally does not claim a diagnosis.

## Verification Results

All values below are local evidence for the current worktree.

```text
LogLens Qt6 normal CTest:       18/18 PASS
LogLens Qt5 normal CTest:       18/18 PASS
LogLens TSan partition:         41/41 PASS
Quality Zoo unit tests:         58/58 PASS
Quality Zoo new six scenarios:   6/6 contract PASS
```

The current exact ici candidate was built from
`ccb2067c656492c549dae8f4abc198a69ea013c2` by run `33852539205` after exact-main
run `33851206764` passed. Artifact `9928909508` has raw ZIP SHA-256
`c21437a7abb1b9016d351584b97a8ffc813ae17a699a3153262b31e9fa53af4b`; its
non-stable `ici.pyz` has SHA-256
`23d9922b94b2ba34ab8884cd2d39c8eda358ccb32d0925af5c0a3d52a7ddc893` and package
version `0.10.2`. The local full Quality Zoo run passed 15 of 16 contracts with
zero errors in those fifteen; only the Qt lifetime scenario failed because this
host has no `clazy`.

The same candidate's AbiLens deep no-cache run completed with exit `0`: suite
`WARN`, `11 PASS / 3 WARN / 0 FAIL / 0 ERROR / 5 SKIP`, TEM `4.75`, complexity
maximum `14` across 194 functions, duplication `3.71%`, and measured sanitizer,
ThreadSanitizer, build, binary-compatibility, and integration PASS results. The
two tests passed, while coverage remained estimated and therefore is not used
as threshold evidence.

The earlier accepted candidate remains the exact LogLens evidence: test engine
`18/18 PASS`, line/function/branch coverage `90.5% / 96.1% / 78.0%`, complexity
maximum `15`, sanitizer `PASS`, and TEM `4.81`. The native TSan partition was
`41/41 PASS`. A short WSLg GUI smoke remained healthy until its deliberately
bounded timeout and produced no stderr.

These results do not imply a remote PR gate, Pages publication, merge, or
stable release.  The local host still lacks `clazy` for the Qt Quality Zoo
fixture, the candidate artifact remains non-stable, and AbiLens requires Linux
`/proc/self/fd` for descriptor-backed readelf evidence. Its metadata revalidation
does not claim a cryptographic snapshot against a privileged actor that changes
and restores both content and identity evidence.

## Next Steps

- Complete the consolidated branch's remote CI and candidate-to-toy acceptance
  using the exact revisions and artifacts.
- Keep the released ici pin and all toy product versions unchanged until the
  corresponding stable release boundary is independently satisfied.
- Resolve or explicitly retain any remaining ici lint/duplication and external
  tool capability boundaries in the next evidence record.
