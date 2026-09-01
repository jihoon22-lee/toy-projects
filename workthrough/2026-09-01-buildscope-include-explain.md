# BuildScope B3 include explanation local candidate

## Overview

BuildScope B3 adds optional include explanation to the unreleased `0.4.0` local candidate on
`feat/buildscope-include-explain`. The existing v2 normalized compile-database contract remains
the default; callers opt into `buildscope.snapshot/v3` with `--include-analysis estimate` or
`--include-analysis compiler`. This record describes the implementation and the local evidence
available on the branch. It does not claim a B3 PR, remote CI, hosted report, release, B5
integration, or ici I3 cross-repository comparison.

## Context

B2 made normalized compiler, include-path, configuration, status, and command data available to the
Qt explorer, but it could not explain which same-named header was selected or why an include was
missing. B3 adds a per-entry include graph with explicit provenance, ordered search candidates,
collision alternatives, classification, source location, and a replay command where applicable.
The two analysis modes deliberately have different trust labels: lexical source scanning is an
estimate, while the compiler mode records the compiler's `-H` trace separately from the source scan
used to recover a directive line.

## Changes made

### CLI and versioned contracts

- `buildscope/python/buildscope/__main__.py` adds `--include-analysis {estimate,compiler}`,
  `--schema-version v3`, `--analysis-max-units`, and `--analysis-time-budget`. No analysis flag
  defaults to v2; analysis implies v3; v3 without an explicit mode selects `estimate`; and v1/v2
  plus analysis is rejected.
- `buildscope/python/buildscope/snapshot.py` keeps the v1 raw compatibility projection and v2
  projection while requiring analysis for v3. A v2 projection removes analysis fields instead of
  silently emitting a mixed contract.
- `buildscope/schemas/buildscope-snapshot-v3.schema.json` is a strict, self-contained schema. It
  requires `include_analysis` on every entry and bounds arrays/strings, edge records, search
  candidates, diagnostics, and explicit evidence/classification/location enums.
- `buildscope/pyproject.toml`, `buildscope/python/buildscope/__init__.py`,
  `buildscope/CMakeLists.txt`, and `buildscope/ici.toml` identify the candidate as `0.4.0`.

### Include analysis architecture

- `buildscope/python/buildscope/include_analysis.py` provides two paths over each normalized entry:
  `estimate_entry()` scans bounded source files and applies lexical search order; `analyze_entry()`
  assembles measured edges from the compiler trace. Compiler execution, argument sanitization, and
  process/trace bounds are isolated in the new `buildscope/python/buildscope/compiler_replay.py`;
  the split is currently 483 lines in `include_analysis.py` and 255 lines in `compiler_replay.py`.
- Each edge stores `parent`, `line`, `requested`, `delimiter`, `resolved`, `search`,
  `alternatives`, `classification`, `evidence`, and `location_evidence`. Measured resolution is
  compiler-derived, while the source scan recovers the parent directive line and delimiter. Missing
  compiler diagnostics use `compiler-diagnostic` location evidence; an unrecoverable location is
  `unavailable`.
- Search records preserve current/quote roots for quoted includes, followed by include/framework,
  system, and after roots in recorded order. The selected candidate and all other existing
  same-basename candidates remain visible. Classifications distinguish project, vendor, generated,
  system, missing, and unresolved paths.
- The strict v3 consumer checks that `resolved` equals exactly the one selected search candidate and
  that `alternatives` equals the distinct existing candidates left unselected. Duplicate search-path
  records preserve order, with only the first candidate selected.
- Per-entry failures and configured unit/time cutoffs produce an `unavailable` analysis record with
  a warning diagnostic, preserving the v3 shape for downstream consumers.

### Safety and bounds

- `buildscope/python/buildscope/_replay_policy.py` is a positive allowlist for direct GCC/Clang
  drivers. The compiler path must resolve to an executable system driver outside the project root.
- `compiler_replay.py` uses `subprocess.Popen` with an argv list, `shell=False` behavior, stdin from
  `DEVNULL`, a fixed minimal environment, `-E -H`, and output directed to the null device. It does
  not run a shell, expand environment variables/globs/command substitutions, expand response files,
  or perform a build.
- Compile/output/dependency flags are dropped; response files, stdin, extra input operands,
  plugins, linker/driver escape flags, and unapproved options are rejected. Limits are 32,768 argv
  items, 1 MiB argv text, 16 MiB trace output, 100,000 edges, 4 MiB per source scan, 15 seconds per
  compiler trace, and default 512 units/120 seconds overall (configurable up to 4,096 units/600
  seconds).

### Native consumer and GUI

- `buildscope/src/core/contract_parser.cpp`, `contract_parser_v2.cpp`, and the related declarations
  in `include/buildscope/contract.hpp` parse and validate v3's required analysis, edge, search,
  diagnostic, evidence, and location fields with bounded strict handling.
- `buildscope/src/gui/main_window.cpp`, `include/buildscope/main_window.hpp`, and
  `buildscope/ui/main_window.ui` add the **Include Edges** tab. It displays evidence, edge count,
  duration, requested/resolved paths, classification, source location, ordered candidates,
  alternatives, and the sanitized replay command. Selecting an edge opens its details; double-click
  or **Open Source Location** opens the recorded parent source location; **Compilation Command**
  returns to the command view.
- A C++ helper refactor keeps the native contract/UI helpers separated; the final complexity result
  is `PASS`, max `14` across `251` functions, with `0` issues.
- `buildscope/tests/cpp/test_contract.cpp` covers v3 parsing and malformed v3 rejection,
  `buildscope/tests/cpp/test_main_window.cpp` covers v3 population and edge navigation, and
  `buildscope/tests/integration/test_hybrid_include_contract.cmake` covers producer-to-native
  consumer v3 integration with same-basename selection.
- `buildscope/tests/python/test_include_analysis.py` covers replay sanitization, rejection paths,
  compiler-selected collision alternatives, and missing-header diagnostics.

## Key examples

```bash
# Existing compatibility behavior: normalized v2, no compiler execution.
python3.10 -m buildscope compile_commands.json --schema-version v2

# Source-scan estimate.
python3.10 -m buildscope compile_commands.json \
  --schema-version v3 --include-analysis estimate

# Compiler-measured include edges under the replay policy.
python3.10 -m buildscope compile_commands.json \
  --schema-version v3 --include-analysis compiler \
  --analysis-max-units 512 --analysis-time-budget 120
```

A measured edge has the following shape (abridged):

```json
{
  "evidence": "compiler-measured",
  "location_evidence": "source-scan",
  "parent": "src/main.cpp",
  "line": 3,
  "requested": "common.hpp",
  "resolved": "include/first/common.hpp",
  "classification": "project",
  "alternatives": ["include/second/common.hpp"],
  "search": [
    {"candidate": "include/first/common.hpp", "order": 0, "selected": true},
    {"candidate": "include/second/common.hpp", "order": 1, "selected": false}
  ]
}
```

## Verification results (2026-09-01)

### Python 3.10 quality gates

```text
pytest: 57/57 PASS
ruff check + format: 14 files PASS
mypy: 11 source files PASS
```

The pytest suite includes the B3 replay-policy, estimate/compiler, collision, missing-header,
duplicate-search selection consistency, and v3 contract coverage.

### Qt 5.15.18 and Qt 6.10.2

A clean Release CMake build under `/tmp` with each opposite Qt major disabled and Python 3.10
selected completed the full six-test CTest set for both supported Qt versions:

```text
Qt 6.10.2: 6/6 PASS
Qt 5.15.18: 6/6 PASS
```

This includes v3 contract parsing, GUI edge navigation, and the hybrid include contract. The builds
and tests were kept outside the repository.

### Local public-release validation (ici v0.8.0)

After the published `ici v0.8.0` release checksum passed, the final no-cache local public-release
validation of this candidate completed with `Suite WARN` (verification passed): engines `11 PASS / 2
WARN / 0 FAIL / 0 ERROR / 0 SKIP`, line `PASS` (`5,151` total / `4,591` code / `3` comment / `557`
blank across `25` files), lint `PASS`, `compile_db` `8/8` production units · `19` configurations ·
`0` failures/warnings, `63/63` tests, line/function/branch `92.6% / 98.9% / 79.1%`, TEM `4.94`,
complexity `PASS` (max `14` / `251` functions / `0` issues),
sanitize/security/resource/cycle/dead/exception `PASS`, duplication `11.65%` (raw display `11.7%`,
`78` groups, `179` findings), and total `34.71s`. WARNs were only type (C++ unsupported) and
duplication. The `/tmp` HTML result was `851,656` bytes with SHA-256
`07d25971e04ed6a4aece36724ce8cf5e3c0548b7c382941a810454d8521c3e34`, exact title
`ici Verification Report — buildscope`, and `0` external references. This is local public-release
validation, separate from B3 PR/remote Pages evidence.

The final benchmark used `100,000` entries / `25,000` sources and recorded model `61 ms`, filter
`1,126 ms`, budget `10,000 ms`, and correctness `true`. The pure
`buildscope-0.4.0-py3-none-any.whl` packaged `compiler_replay.py` and the v3 schema; schema
validation passed.

### Pending remote evidence

The local B3 candidate checks above are green. No code or test change is made by this documentation
task. B3 PR/remote CI, hosted reports, B5 release integration, and the ici I3 target-by-target
same-basename comparison remain pending.

## Next steps

- Run the B3 branch through the repository PR/remote evidence workflow before changing the status to
  complete.
- Keep B5 release integration and ici I3 cross-repository comparison separate from this local
  include-analysis candidate.
