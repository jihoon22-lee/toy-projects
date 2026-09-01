# BuildScope

BuildScope 0.5.0 is the unreleased B4 configuration-diff candidate developed on
`feat/buildscope-config-diff` and shipped to `main` by PR #36. B4 implementation plus
PR/remote/hosted/merged-main evidence is complete; local B5 release-candidate groundwork is now on
`feat/buildscope-b5-release`, while the product release remains pending. B0 established
the producer/consumer boundary:
Python 3.10+ reads a `compile_commands.json` without executing its commands and emits a
deterministic `buildscope.snapshot/v1` document; a C++20/Qt CLI and GUI validate and consume that
document. B1 adds the Python compile-database normalization core and emits the additive
`buildscope.snapshot/v2` contract. B2 completes the normalized C++/Qt explorer over that contract:
sources are grouped with their configurations, status/search/detail views are available, and raw
commands remain separate from structured JSON argv. The B1 public ici v0.7.1 cold verification and
the B2 public ici v0.8.0 evidence recorded here remain separate from the hosted evidence below:
B1 was verified in [PR #31](https://github.com/jihoon22-lee/toy-projects/pull/31), and B2 was
verified in [PR #32](https://github.com/jihoon22-lee/toy-projects/pull/32). B3 adds optional
include explanation while keeping v2 as the default. B3 implementation and remote evidence are
complete on `main`; B5 hybrid release integration remains before the product version is released.
B4 adds semantic comparison of two raw compile databases. The implementation, native contract tests,
PR/remote CI, sticky report, hosted Pages evidence, and merged-main CI are complete on `main`; the
0.5.0 release artifact and B5 hybrid release boundary remain pending. The B5 candidate adds
reproducible standalone packaging, installable native/docs/example assets, and a release workflow
contract; none of those remote release gates has run yet.

## B0 scope

The Python producer in `python/buildscope/` is dependency-free and bounded:

- it rejects databases larger than 64 MiB or with more than 100,000 entries;
- it performs JSON parsing and validation only—no shell or compiler process is started;
- it preserves the raw `arguments` array or `command` string, plus `directory`, `file`, and optional
  `output` fields;
- it emits the `schema_version`, producer, source-count, and sorted-entry fields in stable JSON.
- its package metadata includes this README and builds as a pure `py3-none-any` wheel plus sdist.

The original B0 C++ consumer in `include/` and `src/` validates the v1 core contract and declared
entry count. The Python producer bounds the input compile database at 64 MiB/100,000 entries;
serialized snapshots and native reads are bounded separately at 256 MiB. The CLI prints a compact
summary:

```text
buildscope-cli SNAPSHOT.json
```

The Qt window accepts an optional snapshot path or opens one through its file chooser, then shows
source, working directory, and raw compiler invocation rows. CMake enables C++20,
`CMAKE_AUTOMOC`, `CMAKE_AUTOUIC`, `CMAKE_AUTORCC`, and compile-command export. The B0 CTest set
has four entries: Python unit tests, the C++ v1 contract, the Qt window, and the Python-producer →
C++ consumer hybrid contract. B1 extends native acceptance with legacy v1 core validation and
bounded/core/cross-entry v2 validation; B2 completes the normalized C++ model/UI transition.

The B0 `v1` snapshot remains the raw compatibility boundary. B1 normalization is implemented in
the Python producer, while B2 presents the normalized view and retains the raw compatibility fields.
B1 owns contract acceptance; B2 owns the normalized C++ model/UI transition and is complete. B3's
include explanation is implemented and remotely verified in the `0.4.0` main candidate. B4's
configuration diff implementation and PR/remote/hosted/merged-main evidence are recorded below, while the 0.5.0
release artifact and hybrid release integration (B5) remain pending and the ici I3 target-by-target
comparison is complete.

## B1 compile-database normalization (`buildscope.snapshot/v2`)

The 0.2.0 Python core keeps every B0 raw entry field and adds deterministic derived data. At the
top level, `source` now includes `project_root`; each entry has:

The machine-readable contracts `buildscope-snapshot-v1.schema.json` and
`buildscope-snapshot-v2.schema.json` are published under `schemas/` and included in the pure Python
wheel under `buildscope/schemas/`. In v2, `producer.version` uses the public schema's bounded
`maxLength` of 1 MiB and the same limit in the native reader.

- `normalized.argv`, `command_style`, `invocation_source` (`arguments` or `command`), `compiler`,
  `language`, `standard`, `defines`, `include_paths`, `sysroot`, `target`, `directory`, `source`,
  `output`, and a sha256 `configuration` identity;
- `state.duplicate`, `entry_index`, `source_configuration_count`, and `source_status`; and
- `diagnostics` records with stable `code`, `message`, and `severity` fields.

`normalized.compiler` records the compiler family/name/path and launch wrappers. `defines` preserve
ordered define/undefine actions; include records preserve kind and order. Path records contain
`path`, originating `style` (`posix` or `windows`), `scope` (`project`, `vendor`, or `system`), and
an `exists` value. Entries sort by normalized source path, configuration identity, and original
entry index; JSON keys and the digest input are canonicalized.

Duplicate status is scoped to the normalized source plus configuration identity, and
`source_configuration_count` counts unique configurations for that source. The configuration digest
identifies the same source's recorded invocation (canonical argv, normalized directory, and output
when present); it is not a relocation-stable semantic-equivalence or diff key. B4 owns semantic
configuration comparison. Source aggregation uses `command_style` plus the normalized source path
(case-folded for Windows), matching the native C++ source key.

Invocation rules are bounded and shell-free. When both forms are present, `arguments` is the token
authority and the original `command` string is retained. A command-only entry is tokenized with
POSIX quoting or Windows C-runtime quoting. No shell, environment expansion, globbing, command
substitution, compiler, or response-file expansion occurs; an `@response-file` token stays opaque
and produces a diagnostic. The database remains bounded at 64 MiB and 100,000 entries, with bounded
argument/command lengths. The serialized JSON snapshot is capped at 256 MiB, and the native reader
uses the same 256 MiB serialized-input cap.

The CLI defaults to normalized v2; `--schema-version v1` explicitly emits the raw compatibility
projection (without `project_root`, `normalized`, `state`, or `diagnostics`), while
`--schema-version v2` makes the default explicit. Metadata and output-option scanning stops at
`--`. POSIX output is recognized only as separated `-o output`; Windows supports both `/Fo output`
and joined `/Fooutput`. MSVC option matching is case-sensitive; `/Fo` and `/Fo:` separated/joined
forms are recognized to avoid false positives from similarly named switches. Drive, UNC, and
backslash compiler paths are classified as Windows too, including GCC paths such as
`C:\\MinGW\\bin\\g++.exe`.

Input opening is hardened with a final-name `lstat`, a regular-file descriptor opened with
no-follow where supported, and before/after checks of descriptor/name identity plus size, mtime, and
ctime; a final input symlink is rejected. `--output` refuses the database itself and
self/hardlink/symlink aliases. On POSIX,
output uses a no-follow parent-directory descriptor, exclusive mode-0600 temporary creation,
flush/fsync, and descriptor-relative rename, which provides the anchored atomic-race guarantee.
Platforms without those primitives use a portable fallback that pins the resolved real parent and
performs temporary-file/fsync/cleanup/replace plus parent-identity and alias checks; it does not
claim the POSIX dir-fd atomic-race guarantee. The fallback re-checks the newly created temporary's
identity and regular-file type with `lstat` before replacement and does not resolve a temporary
symlink.

`--project-root` controls classification (the CLI default is the current working directory; the
Python API defaults to the database directory). Paths under that root are `project`; known vendor
components such as `vendor`, `third_party`, `third-party`, `external`, `externals`, `deps`, and
`_deps` are `vendor`; other paths are `system`. Lexical normalization does not require a path to
exist. Native-host paths report file/directory existence and can derive `present`, `missing`, or
`stale` from source/output timestamps; foreign-platform paths report unknown status instead. A
missing source is `missing`, and a missing or older output is `stale`. BuildScope never invokes a
compiler to resolve these states. A foreign Windows `project_root` is kept in lexical Windows form
for scope classification without host filesystem probing; dedicated scope tests cover this path.

For v1 consumer compatibility, v2 retains the B0 raw `arguments`, `command`, `directory`, `file`,
and `output` keys, so consumers that tolerate additive fields can continue using the raw view. The
native reader's B1 scope is legacy v1 core validation plus v2 bounded/core/cross-entry validation:

- legacy v1 requires exactly one raw invocation, preserves compatibility with empty argv elements,
  and tolerates legacy extension keys;
- v2 requires at least one invocation, rejects duplicate JSON keys, and validates required/unknown
  fields, field/item bounds, enums, normalized/state/diagnostics core shapes, `invocation_source`,
  normalized argv equality when raw `arguments` are authoritative, and include array order; and
- v2 also checks `entry_index`, duplicate status, and `source_configuration_count` consistently
  across entries.

The native reader also rejects a final snapshot symlink before reading. This is bounded/core contract
validation, not full semantic attestation: for command-only entries the reader does not re-tokenize
the raw command and compare it with `normalized.argv`. A strict external v1 consumer that rejects
additive fields must still receive a v1 document or use an explicit adapter. The B0
`fixtures/sample.snapshot.json` remains the v1 consumer smoke input; B2's normalized UI consumes
`fixtures/sample-v2.snapshot.json` in its Qt shell tests.

## B2 normalized Qt explorer (`0.3.0`)

B2 completes the native model/UI transition for the accepted `buildscope.snapshot/v2` contract.
The Qt5/Qt6 shell keeps the v1 compatibility path while exposing the normalized data directly:

- `CompilationTreeModel` groups entries by normalized source and presents source nodes with
  configuration children. Source rows expose stable node/source/status/search roles, and each
  configuration maps back to its source entry through `entryView`.
- A source's aggregate status uses the strongest observed state in the order `missing > stale >
  present > unknown`. The status column uses four local, compiled-in SVG resources, so the explorer
  does not depend on a CDN or another network resource.
- The case-insensitive filter searches source, status, target, compiler, standard, configuration,
  define, and include text through the model's search role, while recursive filtering keeps matching
  source groups visible.
- Selecting the automatically focused source or one of its configurations fills the overview and
  detail tabs with source metadata, target/compiler/standard, ordered define/include tables, and
  diagnostic severity/code/message records. Malformed v2 input retains a field location in the
  displayed validation error.
- The command view renders the structured argument vector as a compact JSON array, preserving
  spaces, quotes, and empty arguments, while showing the original raw `command` string separately.
  When both forms are present, the v2 `arguments` vector remains authoritative and the raw command
  is still retained for inspection. The explicit v1 projection remains available for strict legacy
  consumers.

### B2 model benchmark (opt-in)

`BUILDSCOPE_BUILD_BENCHMARKS=ON` builds `buildscope-model-benchmark`, which constructs a deterministic
model and recursively filters it. The Qt6 measurement used 100,000 entries grouped into 25,000
source nodes and a 10,000 ms budget:

| Qt | entries / sources | model build | filter (`unit_024999`) | peak RSS | budget | result |
|---|---:|---:|---:|---:|---:|:---:|
| 6 | 100,000 / 25,000 | 45 ms | 1,071 ms | 132,612 KiB | 10,000 ms | PASS |

The benchmark checks entry/source counts, parent-child data, the final source role, and the
filtered-source count in addition to both timing budgets.

## B3 include explanation (`0.4.0` main candidate, unreleased)

B3 adds an optional include graph to the normalized snapshot. The input is still a bounded
`compile_commands.json`; no external `ici` context is required. The producer can either explain
the include paths lexically or ask the compiler for its actual include trace. B3 is implemented and
remote-verified on `main`; it is not a released BuildScope version because B5 hybrid release
integration remains pending.

### CLI modes and compatibility

The CLI remains backward-compatible by default. With no analysis flag it emits normalized v2, and
`--schema-version v1` still emits the raw compatibility projection. `--include-analysis` accepts
`estimate` or `compiler` and implies v3; it may also be written explicitly as:

The examples use the `repo_root`, `scratch_root`, and `py310_bin` variables initialized in the
run section below; choose a scratch directory outside the repository.

```bash
# v2 remains the default and does not execute a compiler.
PYTHONPATH="$repo_root/buildscope/python" \
  "$py310_bin" -m buildscope "$repo_root/buildscope/fixtures/compile_commands.json" \
  --project-root "$repo_root/buildscope" \
  --output "$scratch_root/buildscope.snapshot.v2.json" --pretty

# Lexical/source-scan explanation; no subprocess is started.
PYTHONPATH="$repo_root/buildscope/python" \
  "$py310_bin" -m buildscope "$repo_root/buildscope/fixtures/compile_commands.json" \
  --project-root "$repo_root/buildscope" \
  --schema-version v3 --include-analysis estimate \
  --output "$scratch_root/buildscope.snapshot.estimate.json" --pretty

# Compiler-measured explanation through the bounded replay policy.
PYTHONPATH="$repo_root/buildscope/python" \
  "$py310_bin" -m buildscope "$repo_root/buildscope/fixtures/compile_commands.json" \
  --project-root "$repo_root/buildscope" \
  --schema-version v3 --include-analysis compiler \
  --analysis-max-units 512 --analysis-time-budget 120 \
  --output "$scratch_root/buildscope.snapshot.compiler.json" --pretty
```

`--schema-version v3` without an explicit mode selects `estimate`. Supplying
`--include-analysis` with v1 or v2 is rejected, so a caller cannot silently drop the analysis
fields. The published `schemas/buildscope-snapshot-v3.schema.json` is self-contained and strict,
and is packaged with the pure wheel under `buildscope/schemas/` alongside v1/v2:
the root, entries, analysis records, edges, search candidates, diagnostics, and normalized fields
reject unknown keys and use bounded arrays/strings and explicit enums. Every v3 entry contains an
`include_analysis` record; if a unit cannot be inspected, the record keeps the reason in a warning
diagnostic with `evidence: "unavailable"` instead of changing the shape of the contract. Existing
v1/v2 consumers can continue to request their original projection.

### Replay boundary and resolution evidence

`estimate` scans bounded source files for `#include` directives and labels the result
`evidence: "estimated"`; it never starts a shell, compiler, or other subprocess. `compiler` uses
the normalized compiler entry to construct an argv-only, shell-free `-E -H` trace with output sent
to the null device. Only a direct, executable GCC/Clang driver resolved from the system search path
is accepted. The replay policy applies a positive option allowlist, removes compile/output/dependency
flags, rejects response files, stdin, extra input operands, plugins, linker/driver escape options,
and runs with a fixed minimal environment. It is a bounded read-oriented replay, not a general
build invocation.

Compiler execution, argument sanitization, and process/trace bounds are isolated in
`buildscope/python/buildscope/compiler_replay.py`; `include_analysis.py` retains source scanning,
edge assembly, and compiler-trace interpretation. The current split is 483 lines in
`include_analysis.py` and 255 lines in `compiler_replay.py`.

The limits are explicit: 32,768 argv items and 1 MiB of argv text per unit, 16 MiB of compiler trace,
100,000 edges, 4 MiB per source scan, 15 seconds per compiler trace, and (by default) 512
translation units within a 120-second overall budget. The CLI permits at most 4,096 units and a
600-second overall budget. A rejected command, unavailable compiler, stale path, timeout, or budget
cutoff is represented as an unavailable analysis warning in v3.

For compiler-measured edges, the compiler `-H` trace decides `resolved` and the actual edge
relationship. BuildScope source-scans the parent to recover the directive line and delimiter, so
`evidence: "compiler-measured"` can appear with `location_evidence: "source-scan"`. Missing-header
diagnostics use `location_evidence: "compiler-diagnostic"`; an edge whose location cannot be
recovered reports `unavailable`. Estimated edges use `source-scan` location evidence and never
claim compiler measurement.

Each edge records its parent, requested header, delimiter, resolved path (or null), ordered search
records, alternatives, classification, line, and both evidence labels. Search order follows the
normalized include roots: for quoted includes, the parent directory then `quote` roots; then
`include`/`framework`, `system`, and `after` roots in recorded order. Angle includes skip the
current/quote phase. The first existing candidate is selected for estimates; measured traces mark
the compiler-selected candidate. Other existing candidates are retained in `alternatives`, making
same-basename collisions visible rather than silently losing them.

The strict v3 consumer cross-checks that a resolved edge equals exactly one search candidate marked
`selected`, and that `alternatives` contains the distinct existing candidates that were not selected.
If a search path is recorded more than once, its candidates retain recorded order but only the first
occurrence is marked `selected`.

Classification distinguishes `project`, `vendor`, `generated`, `system`, `missing`, and
`unresolved`: vendor path components use the known vendor directory names, generated files are
recognized below the compilation build roots (`build`, `out`, `.build`, or `cmake-build-*`), paths
outside the project root are system, a compiler diagnostic with no file is missing, and an estimate
with no existing candidate is unresolved.

### GUI edge navigation

The v3 **Include Edges** tab shows the analysis provenance, edge count, duration, requested/resolved
paths, classification, source location, and an expandable list of ordered search candidates. Click
an edge to inspect its directive, location evidence, collision alternatives, and search order. The
replay command is shown separately (and is empty for estimated evidence). Double-clicking an edge,
or using **Open Source Location**, opens the recorded parent source location; **Compilation Command**
jumps back to the structured/raw command view. v1/v2 snapshots continue to show that include
analysis is unavailable rather than being treated as measured data.

### B3 historical local candidate evidence (2026-09-01; before PR #34)

The implementation and local checks currently recorded for this branch are:

| Check | Result | Scope |
|---|---:|---|
| Python 3.10 pytest suite | 57/57 PASS | includes replay-policy, estimate/compiler, duplicate-search selection, v3 projection, and bounded-failure tests |
| Ruff check + format | 14 files PASS | local Python quality gate |
| mypy | 11 source files PASS | local Python type-check gate |
| Qt 6.10.2 Release CMake/CTest | 6/6 PASS | v3 contract parsing, GUI edge navigation, and hybrid include contract |
| Qt 5.15.18 Release CMake/CTest | 6/6 PASS | same six-test local candidate matrix |

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

This historical local evidence is separate from the B3 PR/remote Pages evidence below and does not
by itself claim B5 release integration or ici I3 completion.

### B3 remote integration and main evidence (2026-09-01)

[PR #34](https://github.com/jihoon22-lee/toy-projects/pull/34) carried feature head
`c3835cd4b0c859c38ae0f4afbdb20aae970515dc`. Its [CI run
`33459294092`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33459294092) completed all 16
checks successfully, including `Merge Gate` and `Publish Reports & Sticky Comment`. The [sticky
comment](https://github.com/jihoon22-lee/toy-projects/pull/34#issuecomment-5487386460) contains exactly
one marker and exactly three project links. The BuildScope report was `WARN` with `11 PASS / 2 WARN /
0 FAIL / 0 ERROR / 0 SKIP`, TEM `4.94`, tests `63/63`, and line/function/branch `92.7% / 98.9% /
79.5%`; diskmap and loglens were `PASS` with TEM `4.92` and `4.80` respectively.

The PR BuildScope benchmark used `100,000` entries / `25,000` sources and recorded model `118 ms`,
filter `1,424 ms`, a `10,000 ms` budget, and correctness `true`. Independent PR Pages checks for all
three reports found HTTP 200 `text/html`, the exact expected title, and zero external attributes/CSS
references.

| Project | Hosted report | Bytes | SHA-256 | Title |
|---|---|---:|---|---|
| BuildScope | [buildscope/pr/34](https://jihoon22-lee.github.io/toy-projects/buildscope/pr/34/) | 858,143 | `e0b9c9ece1fb7268aa519bd0a4c62fd3da7c44a52b2efe6121393474d3ad36d4` | `ici Verification Report — buildscope` |
| diskmap | [diskmap/pr/34](https://jihoon22-lee.github.io/toy-projects/diskmap/pr/34/) | 311,847 | `8a6b01544b99eee6f0c2b95758f81395032f2b67a7c1a600879447cf7fb5f3bf` | `ici Verification Report — diskmap` |
| loglens | [loglens/pr/34](https://jihoon22-lee.github.io/toy-projects/loglens/pr/34/) | 446,786 | `1144759ef7e1b83ef7bd23f7bcfe9d02b05430a37af372350ec3b6e26d6c7ac7` | `ici Verification Report — loglens` |

PR #34 was squash-merged to `main` as
`9cce2699606e58ed67c3dac46f60dc7bf113bb60`, and the feature branch was deleted. Exact-main [CI run
`33459591250`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33459591250) succeeded for all
applicable jobs, including `Merge Gate` (PR publish correctly skipped on push). [Dependency Graph run
`33459594605`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33459594605) also succeeded on
the same head.

B3 implementation and remote evidence are complete and the code is shipped to `main`; `0.4.0` remains
unreleased until B5 hybrid release integration. ici I3 cross-repository comparison is complete, and
B4 configuration diff is the `0.5.0` main candidate with implementation and
remote/merged-main evidence complete.

## B4 semantic configuration diff (`0.5.0` candidate, unreleased)

B4 compares two **raw** `compile_commands.json` arrays. The diff command does not accept
`buildscope.snapshot/v1`, `v2`, or `v3` documents as inputs and never executes a compiler, shell, or
response file. Snapshot compatibility remains separate: the producer defaults to v2, can explicitly
emit the v1 raw projection, and can opt into the v3 include-analysis contract. The diff output is the
strict `buildscope.diff/v1` contract.

### CLI and exit policy

```bash
PYTHONPATH="$repo_root/buildscope/python" \
  "$py310_bin" -m buildscope diff \
  "$repo_root/buildscope/fixtures/diff-before.compile_commands.json" \
  "$repo_root/buildscope/fixtures/diff-after.compile_commands.json" \
  --before-project-root /project \
  --after-project-root /project \
  --suppress "standard:src/**/*.cpp" --pretty \
  --output "$scratch_root/buildscope.diff.json"
```

The installed `buildscope-diff` entry point accepts the same arguments. Exit status is intentionally
small and scriptable:

| Status | Meaning |
|---:|---|
| `0` | No visible semantic units remain (identical inputs or all changes suppressed). |
| `1` | At least one unsuppressed added, removed, moved, or changed unit remains. |
| `2` | The comparison/export failed: malformed, oversized, duplicate-key, or untrusted JSON; invalid normalization or suppression; an opaque response file; a final symlink or input/output TOCTOU/alias violation; or an output size/write failure. |

### Semantic normalization and pairing

The semantic view retains compiler command style/family/name/path and wrappers, launcher tokens,
language, standard, target (build target and triple), sysroot, ordered define/undefine actions,
ordered include kind/path records, and residual flags. Project-relative path-bearing values are
lexically rebased against each side's project root; no filesystem existence or timestamp is used.
Windows separators are normalized and Windows identities/globs are case-insensitive. This reduces
noise from relocated build directories, absolute project paths, and output object names while still
reporting real toolchain or option drift.

The following are explicitly ignored by the policy: raw command spelling, compilation directory,
output path/filename, original entry index and duplicate annotation, filesystem existence/stale
status, and snapshot diagnostics/include-analysis observations. Define and include order is
semantic—reordering either is a change, and define versus undefine actions are not collapsed into a
last-wins map.

Source identity is the normalized path plus command style. Within one source, duplicate semantic
digests pair one-to-one in stable canonical order; remaining unique language/build-target/triple
roles pair when unambiguous, and a single remaining configuration may pair as changed. Extra
duplicates stay added/removed. Across source paths, the conservative move heuristic first pairs a
unique basename + role + semantic digest, then a unique basename + role. A move unit retains the
rename and any configuration drift together. Ambiguous candidates remain added/removed and emit a
warning; there is no source-content identity, so a unique same-basename replacement can still look
like a move and should be reviewed.

### Suppressions and deterministic export

Suppressions use `CATEGORY[:GLOB]` with repeatable `--suppress`; `*` as the category suppresses all
categories. The glob is slash-aware: `*` and `?` do not cross `/`, while `**` may cross path
segments. A pattern without `/` can match a basename at any depth. Backslashes and character
classes are rejected, duplicate rules are rejected, and rule count/length are bounded. Rules are
canonicalized before export, and suppression evidence remains attached to each affected change.

Reports are canonical JSON (stable semantic/unit ordering, sorted keys, one trailing newline, and a
256 MiB serialized bound). `schemas/buildscope-diff-v1.schema.json` describes the strict contract.
The C++ parser rejects duplicate keys, unknown fields, inconsistent summaries, tampered semantic
digests, omitted semantic changes, and invalid suppression evidence. `buildscope-cli --diff
DIFF.json` consumes the same contract, while the native Qt GUI opens it in an issues-first tree with
change details, filtering, and suppressed counts.

The B4 fixture and test coverage includes Python semantic/CLI tests, native C++ parser/model and
adversarial rejection tests, the Python-to-C++ byte-identical hybrid contract, and the GUI diff-mode
test. Python is `83/83`; Ruff check/format covers `19` files and mypy covers `15` source files. The
default Qt5 5.15.18 and Qt6 6.10.2 Release CMake/CTest matrices are each `9/9`; enabling
`BUILDSCOPE_BUILD_BENCHMARKS` makes each matrix `10/10`. The pure
`buildscope-0.5.0-py3-none-any` wheel contains the v1/v2/v3 snapshot and diff v1 schemas and no
native extension.

The checksum-validated public ici v0.9.0 release also passed an uncached deep verification as
historical local evidence: suite `WARN`, 14 engines = 11 PASS / 3 WARN / 0 FAIL / 0 ERROR / 0 SKIP,
`92/92` tests, line/function/branch coverage `93.5% / 99.0% / 76.7%`, sanitizer PASS, compile DB
`12/12` production units and `27` configurations, and TEM `4.95/5.0`. The standalone Zero-CDN HTML
was 1,235,505 bytes, SHA-256
`0c98a38b27e928df2c60dcadff9ecc3daa1072cb620354d9f4a9fe8d9b987f80`, with title
`ici Verification Report — buildscope`. This historical local result is separate from the current
B4 v0.9.1 remote evidence below.

### B4 PR #36 and merged-main remote integration evidence (2026-09-01)

B4 implementation and PR/remote/hosted evidence are complete on [PR #36](https://github.com/jihoon22-lee/toy-projects/pull/36),
head `ce64613263f0c4358579012aab135e0b23341a0e`. [Run `33485837830`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33485837830)
used ici `v0.9.1` and completed all `16/16` checks successfully. The BuildScope report was `WARN`
(`10 PASS / 3 WARN / 0 FAIL / 0 ERROR / 0 SKIP`) with lint WARN (49 warnings), `92/92` tests,
line/function/branch coverage `93.5% / 99.0% / 77.3%`, sanitizer PASS, compile DB `12/12`
production units and `27` configurations, and TEM `4.95/5.0`. The remote 100,000-entry /
25,000-source benchmark recorded model `65 ms`, filter `1,602 ms`, filtered sources `1`, budget
`10,000 ms`, and correctness `true`. [Sticky comment #5489976814](https://github.com/jihoon22-lee/toy-projects/pull/36#issuecomment-5489976814)
has exactly one marker and three hosted-report links.

The three hosted Pages reports were HTTP 200 with exact titles and zero external resource references:

| Project | Hosted report | Bytes | SHA-256 |
|---|---|---:|---|
| BuildScope | [buildscope/pr/36](https://jihoon22-lee.github.io/toy-projects/buildscope/pr/36/) | 1,319,378 | `5dc517d3ec8324cb1aedb6e611120ac9ae2951e27851cbc6b0e28303d02c5d43` |
| diskmap | [diskmap/pr/36](https://jihoon22-lee.github.io/toy-projects/diskmap/pr/36/) | 337,554 | `39a52d1e3d5b9eed6bbc1ec5253f1bf837deb4a7bb33ed2f2ef3b32df0f90e0e` |
| loglens | [loglens/pr/36](https://jihoon22-lee.github.io/toy-projects/loglens/pr/36/) | 492,746 | `0480f07aea266c0777403ac37ebf90d4acd10f84b04e49aa2a9ccf99a6153007` |

The `0.5.0` product release artifact and B5 hybrid integration remain pending/not started.

PR #36 was squash-merged to `main` as
`590899a0a9430e9ce35162b301bfef5d7dfc78a4`, and its feature branch was deleted. Exact-main
[CI run `33488169769`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33488169769) completed
with all 14 prerequisite jobs and `Merge Gate` successful; the PR-only publisher was skipped as expected.
[Dependency Graph run `33488174425`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33488174425)
also succeeded on the same head.

## B5 release candidate (`0.5.0`, local groundwork; unreleased)

B5 connects the B4 producer/consumer contract to a releasable, inspectable bundle. The current local
slice is intentionally bounded:

- `tools/build_standalone.py` validates the Python, pyproject, CMake, and ici version surfaces,
  writes a fixed-metadata zipapp containing the package and all four public schemas, and atomically
  installs it. `tests/python/test_standalone.py` covers version/output, schema inventory, execution,
  reproducibility, and symlink refusal.
- `CMakeLists.txt` installs `buildscope-cli` and `buildscope-gui` under `bin/`, documentation under
  `share/doc/buildscope/`, and schemas/examples under `share/buildscope/`. The checked-in
  `examples/cmake` and `examples/qmake` projects plus [quickstart.md](docs/quickstart.md) provide
  reproducible producer inputs and the pyz/wheel → JSON → native consumer flow.
- The release workflow builds a pure `buildscope-<version>-py3-none-any.whl`, sdist, deterministic
  Linux x86_64 native bundle, and standalone pyz; it compares wheel/pyz snapshots through the native
  CLI before publishing any asset.

### Local B5 ici candidate evidence (2026-09-01)

The local deep/no-cache run used public ici `v0.10.0`, pinned to literal
`6d5f8c008b3b5393a61b2c1a418124eb66393c9eaab0abbb7d1c7922162bed9b`, with
`ICI_PYTHON=/tmp/toy-b5-py310/bin/python`. It reported `Suite WARN`: 14 engines = `11 PASS / 3 WARN /
0 FAIL / 0 ERROR / 0 SKIP`, tests `96/96`, line/function/branch `93.4% / 99.0% / 76.7%`, and TEM
`4.95`. `compile_db` covered `12/12` production units and `27` configurations with `0` issues.
Qt code generation was exact for 3 inputs: MOC `1`, UIC `1`, and RCC `1`; the report recorded
12 Qt6 compile units. The JSON report is 2,873,207 bytes with SHA-256
`ea5fce118e6edad8fa5af24c821663e4290805570377e9b7c190de8da1029612`; the Zero-CDN HTML is
1,264,867 bytes with SHA-256
`4e12e77ee11f98b2d5bb146bd3d08252b088083726e6f11235e25a449471a565`, exact title
`ici Verification Report — buildscope`, and 0 external references. Local `clang-tidy` and `clazy`
were unavailable, so tool-backed static-analysis evidence remains a release-runner requirement.

The focused Python suite now contains 87 tests, and the standalone builder's two direct outputs are
byte-identical. Existing Qt5 5.15.18 and Qt6 6.10.2 Release CMake/CTest evidence remains the B4
historical/local baseline (`9/9`, or `10/10` with the opt-in benchmark); the B5 release workflow
defines fresh Python `3.10/3.14` and Qt `5/6` legs. The first PR deep run exposed a Qt5-only
LeakSanitizer failure after all 12 `test_main_window` test functions passed (14 QtTest lifecycle
result entries). Draining deferred-delete
and event work in the QtTest `cleanup()` hook removes the retained Qt5 offscreen/fontconfig/widget
state: a rebuilt instrumented binary using the same toolchain and configuration, plus the full
`9/9` CTest tree, pass with `detect_leaks=1` still
enabled. No sanitizer suppression was introduced; final B5 evidence requires the corrected PR rerun.

### B5 release contract (defined, not published)

`buildscope-release.yml` runs only for an annotated `buildscope-vX.Y.Z` tag, requires that it point
to exact `origin/main` with a successful `Merge Gate`, and checks all public version surfaces plus
one matching `CHANGELOG.md` heading. Its Qt legs inspect generated
`ui_main_window.h`, `qrc_buildscope.cpp`, and `moc_main_window.cpp`, then run CTest and a six-second
offscreen GUI smoke. The package/deep leg requires `clang-tidy` and `clazy`, validates the ici sidecar,
downloaded digest, and API digest against the literal pin, and checks wheel/sdist purity.

The eventual release publishes `buildscope.pyz`, `buildscope.pyz.sha256`,
`buildscope-<version>-py3-none-any.whl`, `buildscope-<version>.tar.gz`,
`buildscope-ici-deep.json`, `buildscope-ici-deep.html`, `buildscope-provenance.json`,
`buildscope-<version>-linux-x86_64.tar.gz`, and `SHA256SUMS`. Publication fails closed on a missing
path and audits the remote annotated-tag target plus the exact nine uploaded names, sizes, and API
digests after release creation. No BuildScope B5 PR/remote CI/Pages run, annotated tag, or GitHub
Release exists yet; `0.5.0` remains unreleased and B5 acceptance is pending those gates.

## Run without installing into the repository

All build and temporary output below stays under a scratch directory. The Python package is loaded
with `PYTHONPATH`; it is not installed into `buildscope`.

```bash
repo_root="$(git rev-parse --show-toplevel)"
scratch_root="$(mktemp -d /tmp/buildscope-b2.XXXXXX)"
py310_bin="$(command -v python3.10)"
release_root="$(mktemp -d /tmp/buildscope-ici-v0.9.0.XXXXXX)"  # historical local evidence
ici_bin="$release_root/ici.pyz"
ici_python="/tmp/buildscope-ici-py310/bin/python"  # external env with pytest+coverage+mypy

# Fetch and verify the public release asset under /tmp, never under the repository.
curl -fsSL -o "$ici_bin" \
  "https://github.com/jihoon22-lee/ici/releases/download/v0.9.0/ici.pyz"
curl -fsSL -o "$release_root/ici.pyz.sha256" \
  "https://github.com/jihoon22-lee/ici/releases/download/v0.9.0/ici.pyz.sha256"
(
  cd "$release_root"
  sha256sum --check ici.pyz.sha256
)
chmod +x "$ici_bin"

# Python producer/unit tests, with no package install.
PYTHONPATH="$repo_root/buildscope/python" \
  "$py310_bin" -m unittest discover \
  -s "$repo_root/buildscope/tests/python" -p 'test_*.py'

PYTHONPATH="$repo_root/buildscope/python" \
  "$py310_bin" -m buildscope \
  "$repo_root/buildscope/fixtures/compile_commands.json" \
  --project-root "$repo_root/buildscope" \
  --schema-version v2 \
  --output "$scratch_root/buildscope.snapshot.json" --pretty

# Explicit raw v1 compatibility projection.
PYTHONPATH="$repo_root/buildscope/python" \
  "$py310_bin" -m buildscope \
  "$repo_root/buildscope/fixtures/compile_commands.json" \
  --schema-version v1 \
  --output "$scratch_root/buildscope.snapshot.v1.json" --pretty

# Qt 6.10.2 leg.
cmake -S "$repo_root/buildscope" -B "$scratch_root/qt6" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=ON \
  -DBUILDSCOPE_BUILD_BENCHMARKS=ON \
  -DPython3_EXECUTABLE="$py310_bin"
cmake --build "$scratch_root/qt6" --parallel
QT_QPA_PLATFORM=offscreen \
  ctest --test-dir "$scratch_root/qt6" --output-on-failure

# Qt 5.15.18 leg.
cmake -S "$repo_root/buildscope" -B "$scratch_root/qt5" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON \
  -DBUILDSCOPE_BUILD_BENCHMARKS=ON \
  -DPython3_EXECUTABLE="$py310_bin"
cmake --build "$scratch_root/qt5" --parallel
QT_QPA_PLATFORM=offscreen \
  ctest --test-dir "$scratch_root/qt5" --output-on-failure

# The native consumer accepts the Python-produced v2 snapshot.
"$scratch_root/qt6/src/core/buildscope-cli" \
  "$scratch_root/buildscope.snapshot.json"

# The preserved fixture independently retains the v1 compatibility path.
"$scratch_root/qt6/src/core/buildscope-cli" \
  "$repo_root/buildscope/fixtures/sample.snapshot.json"

# Historical ici 0.9.0 public-release verification. Provision this interpreter outside the repo;
# the current B5 candidate workflow pins ici v0.10.1 with a literal SHA-256 and verifies it twice.
# beforehand with pytest, coverage, and mypy, then point ici at it.
(
  cd "$repo_root/buildscope"
  ICI_PYTHON="$ici_python" \
    "$ici_bin" verify --no-cache --report --html "$scratch_root/buildscope-ici.html"
)
```

For a fresh external verification environment, replace the fixed `ici_python` path above with a
scratch path and provision it under `/tmp` (never under the repository):

```bash
ici_python_root="$(mktemp -d /tmp/buildscope-ici-py310.XXXXXX)"
uv venv --python "$(command -v python3.10)" "$ici_python_root"
uv pip install --python "$ici_python_root/bin/python" \
  ruff==0.16.5 pytest==9.1.1 coverage==7.15.4 mypy==2.3.1
ici_python="$ici_python_root/bin/python"
```

## B1 historical local and public verification evidence (2026-09-01)

The B1 public `ici v0.7.1` cold verification passed the standard `sha256sum --check ici.pyz.sha256`
asset check and reported suite
`WARN`: `13 engines = 11 PASS / 2 WARN / 0 FAIL / 0 ERROR / 0 SKIP`, total duration `71.09s`
(raw `71.08794903755188s`), `45/45` tests, line/function/branch coverage
`95.2% / 100.0% / 84.3%`, and TEM `5.00`.
`compile_db` covered `7/7` production units and `16` configurations with `0` failures/warnings;
complexity was max `13` across `140` functions with `0` issues; exception analysis was PASS with
`0` exceptions. Duplication was WARN at `8.8%` (raw `8.77914951989026`), `25` groups, and
`56` findings. The only type
warning was unsupported analysis for `7` C++ sources; external dependency count was `0`.

The HTML report was `489,978` bytes, SHA-256
`538bdde8fae8cc769d212799e80ffeae1e39069662b214b19efb3d35a66f3257`, with title
`ici Verification Report — buildscope`. The line inventory is `2,798` total, `2,453` code, and
`345` blank lines across `19` files. Current source inventory is `contract.cpp` 111 lines,
`contract_json_guard.cpp` 125, `contract_parser.cpp` 382, and `contract_parser_v2.cpp` 333;
the Python suite contains `41` tests and the CTest aggregate is `45`.

These final public/local results are distinct from the hosted HTML and remote integration evidence
below. The local cold HTML above is `489,978` bytes with SHA-256
`538bdde8fae8cc769d212799e80ffeae1e39069662b214b19efb3d35a66f3257`; it must not be confused with
the hosted Pages artifact.

### PR #31 remote integration evidence

The initial implementation/docs head was `1ff08fe5d2accddc0e9107113eb83dd86bd6d50a`.
Workflow run [33439733990](https://github.com/jihoon22-lee/toy-projects/actions/runs/33439733990)
for [PR #31](https://github.com/jihoon22-lee/toy-projects/pull/31) completed all 15 dynamic-matrix
checks successfully: 3 ici verify checks, 6 Qt5/Qt6 GUI checks, manifest, 3 benchmark smokes,
`Publish Reports & Sticky Comment`, and `Merge Gate`. The
[sticky comment](https://github.com/jihoon22-lee/toy-projects/pull/31#issuecomment-5484640868)
contains one marker, exactly three project links, and the BuildScope result `11 PASS / 2 WARN`,
TEM `5.00`, `45/45` tests, `7/7` production units, `16` configurations, and complexity max
`13` across `140` functions.

An independent Pages audit found all three hosted reports at HTTP 200 `text/html` with zero
external dependencies:

| Project | Hosted report | Bytes | SHA-256 | Title |
|---|---|---:|---|---|
| BuildScope | [buildscope/pr/31](https://jihoon22-lee.github.io/toy-projects/buildscope/pr/31/) | 493,453 | `643a3e9e5c45a1512244cc90940146192399471621eac1a2dcb581cc534089c2` | `ici Verification Report — buildscope` |
| diskmap | [diskmap/pr/31](https://jihoon22-lee.github.io/toy-projects/diskmap/pr/31/) | 311,846 | `a8806808638c584312943d2551c1668a407c45830311de07cb0eed30d15e6924` | `ici Verification Report — diskmap` |
| loglens | [loglens/pr/31](https://jihoon22-lee.github.io/toy-projects/loglens/pr/31/) | 446,796 | `56f3b2d54ed2a05ebf100313b4d9447553e9c6fb9c85f7e7adce8eccc838dc4f` | `ici Verification Report — loglens` |

B1 implementation and PR #31 remote integration evidence are complete. B2 implementation and its
local/public verification are recorded below. B3 implementation and remote evidence are recorded
above; B4 implementation and PR/remote/hosted/merged-main evidence are recorded above, while the 0.5.0 release
artifact and B5 hybrid release integration remain pending; the ici I3 target-by-target comparison is
complete.

## B2 local, public, and remote verification evidence (2026-09-01)

The public `ici v0.8.0` verification for the 0.3.0 explorer completed with `46/46` tests, line /
function / branch coverage of `94.5% / 99.5% / 83.9%`, and TEM `4.98`. Its `compile_db` result
covered `8/8` production units and `19` configurations.

Local Qt 5.15.18 and Qt 6.10.2 builds each passed the complete CTest suite `6/6` (including the
normalized model, Qt shell, hybrid contract, and opt-in 100k benchmark checks). The Qt6 benchmark
result was `100,000` entries / `25,000` source groups, model build `45 ms`, recursive filter
`1,071 ms`, peak RSS `132,612 KiB`, and a `10,000 ms` budget; all correctness and budget checks
passed.

BuildScope B2 is implemented locally as version `0.3.0` and its remote integration evidence is
complete. PR #32 head `41472a66e69477fde7a71fe78c3ae9e47ba7f292` was squash-merged to main as
`51a3480677a740475857dd92dd5a5a9373a287a4`. [Run `33454143021`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33454143021)
passed all 16 checks; [sticky comment #5486637533](https://github.com/jihoon22-lee/toy-projects/pull/32#issuecomment-5486637533)
has one marker and three project links. The PR BuildScope report recorded `46/46` tests, branch
`84.2%`, TEM `4.98`, compile DB `8/8` production units across `19` configurations, and complexity
max `14` across `196` functions. The PR benchmark summary was model `53 ms`, filter `1,518 ms`,
with summary JSON SHA-256 `af7162b7603d558da6e7bc49d7bf5a80f546f412b7076992ded5e15739024db7`; exact-main run
`33454634202` succeeded with the report job expected skipped, and the main benchmark was model
`58 ms`, filter `1,527 ms`, summary JSON SHA-256
`247c0b33095e0a09e97a289af556eae30f47f4f5c4136c530e3d6ca0018ae2d2`.

The three hosted reports were independently confirmed as HTTP 200 `text/html`, with the expected
titles and zero external resource references:

| Project | Bytes | SHA-256 | Title |
|---|---:|---|---|
| [BuildScope](https://jihoon22-lee.github.io/toy-projects/buildscope/pr/32/) | 562,234 | `f15d18fe42ac172385e682ceb49e4b6d6f1d9bbfcc0ead301c11d1ee049c4c82` | `ici Verification Report — buildscope` |
| [diskmap](https://jihoon22-lee.github.io/toy-projects/diskmap/pr/32/) | 311,846 | `752f07251bc38285ea1633f5df879985131963e4b99f90532722eaedc9be1802` | `ici Verification Report — diskmap` |
| [loglens](https://jihoon22-lee.github.io/toy-projects/loglens/pr/32/) | 446,791 | `7b2669fb7de82ada30bfdf28a2d82533f5566ad92779ea08c90528e188ea582b` | `ici Verification Report — loglens` |

B3 implementation and remote evidence are recorded above. B4 configuration diff implementation and
PR/remote/hosted/merged-main evidence are recorded above; the 0.5.0 release artifact and B5 hybrid release
integration remain future work, while the ici I3 target-by-target same-basename comparison is complete.
