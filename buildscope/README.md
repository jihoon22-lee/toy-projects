# BuildScope

BuildScope is an offline explorer for C and C++ compile databases. A bounded
native analyzer reads `compile_commands.json` without executing its commands
and emits a deterministic, versioned snapshot document; the Qt CLI and GUI
validate and consume it. The whole product is a single C++20/Qt6 codebase:
the `buildscope` producer executable plus the `buildscope-cli` contract
consumer and the `buildscope-gui` explorer.

What it does:

- **normalize** raw compile databases into `buildscope.snapshot/v2`, keeping
  every raw field while adding deterministic derived data;
- **explain includes** — lexical/source-scan explanations offline, or
  compiler-measured ones through a bounded replay policy;
- **diff** two raw `compile_commands.json` arrays semantically;
- **explore** the snapshot in a Qt GUI: sources grouped with their
  configurations, status/search/detail views, and include-graph navigation.

## B0 scope

The native producer in `src/native/` is dependency-free and bounded:

- it rejects databases larger than 64 MiB or with more than 100,000 entries;
- it performs JSON parsing and validation only—no shell or compiler process is started;
- it preserves the raw `arguments` array or `command` string, plus `directory`, `file`, and optional
  `output` fields;
- it emits the `schema_version`, producer, source-count, and sorted-entry fields in stable JSON.

The C++ consumer in `include/` and `src/` validates the v1 core contract and declared
entry count. The producer bounds the input compile database at 64 MiB/100,000 entries;
serialized snapshots and native reads are bounded separately at 256 MiB. The CLI prints a compact
summary:

```text
buildscope-cli SNAPSHOT.json
```

The Qt window accepts an optional snapshot path or opens one through its file chooser, then shows
source, working directory, and raw compiler invocation rows. CMake enables C++20,
`CMAKE_AUTOMOC`, `CMAKE_AUTOUIC`, `CMAKE_AUTORCC`, and compile-command export. The CTest set
covers the C++ v1 contract, the normalized model, the diff parser/validation, native-producer
unit tests, the Qt window, and three producer → consumer integration contracts (snapshot,
include trace, and diff). B1 extends native acceptance with legacy v1 core validation and
bounded/core/cross-entry v2 validation; B2 completes the normalized C++ model/UI transition.

The B0 `v1` snapshot remains the raw compatibility boundary. Normalization
is implemented in the native producer, while B2 presents the normalized view
and retains the raw compatibility fields.

## B1 compile-database normalization (`buildscope.snapshot/v2`)

The normalization core keeps every B0 raw entry field and adds deterministic derived data. At the
top level, `source` now includes `project_root`; each entry has:

The machine-readable contracts `buildscope-snapshot-v1.schema.json` and
`buildscope-snapshot-v2.schema.json` are published under `schemas/`. In v2,
`producer.version` uses the public schema's bounded `maxLength` of 1 MiB and
the same limit in the native reader.

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

`--project-root` controls classification (the CLI default is the current working
directory). Paths under that root are `project`; known vendor
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

## Include explanation (historical `0.4.0` candidate; included in `0.5.0`)

B3 adds an optional include graph to the normalized snapshot. The input is still a bounded
`compile_commands.json`; no external context is required. The producer can either explain
the include paths lexically or ask the compiler for its actual include trace. This slice was
developed as the historical `0.4.0` candidate, was implemented and remote-verified on `main`, and
is included in the `0.5.0` boundary rather than being published as a separate stable version.

### CLI modes and compatibility

The CLI remains backward-compatible by default. With no analysis flag it emits normalized v2, and
`--schema-version v1` still emits the raw compatibility projection. `--include-analysis` accepts
`estimate`, `compiler`, or `delayed` and implies v3; it may also be written explicitly as:

The examples use the `repo_root` and `scratch_root` variables initialized in the
run section below; `buildscope` is the producer executable built there. Choose a
scratch directory outside the repository.

```bash
# v2 remains the default and does not execute a compiler.
buildscope "$repo_root/buildscope/fixtures/compile_commands.json" \
  --project-root "$repo_root/buildscope" \
  --output "$scratch_root/buildscope.snapshot.v2.json" --pretty

# Lexical/source-scan explanation; no subprocess is started.
buildscope "$repo_root/buildscope/fixtures/compile_commands.json" \
  --project-root "$repo_root/buildscope" \
  --schema-version v3 --include-analysis estimate \
  --output "$scratch_root/buildscope.snapshot.estimate.json" --pretty

# Compiler-measured explanation through the bounded replay policy.
buildscope "$repo_root/buildscope/fixtures/compile_commands.json" \
  --project-root "$repo_root/buildscope" \
  --schema-version v3 --include-analysis compiler \
  --analysis-max-units 512 --analysis-time-budget 120 \
  --output "$scratch_root/buildscope.snapshot.compiler.json" --pretty

# Delayed replay: every unit is estimated; only units whose normalized file
# path matches a repeatable --analysis-unit glob are upgraded to a compiler
# replay within the same unit/time budget.
buildscope "$repo_root/buildscope/fixtures/compile_commands.json" \
  --project-root "$repo_root/buildscope" \
  --schema-version v3 --include-analysis delayed \
  --analysis-unit 'src/*.cpp' \
  --output "$scratch_root/buildscope.snapshot.delayed.json" --pretty
```

`--schema-version v3` without an explicit mode selects `estimate`. Supplying
`--include-analysis` with v1 or v2 is rejected, so a caller cannot silently drop the analysis
fields. In `delayed` mode every unit is estimated first and only `--analysis-unit` glob matches
spend replay budget; a matched unit whose replay is cut by the unit/time budget or fails keeps its
estimate plus a warning diagnostic rather than becoming unavailable, and a replay attempt counts
against `--analysis-max-units` whether it succeeds or not. The published `schemas/buildscope-snapshot-v3.schema.json` is self-contained and strict:
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
`src/native/native_replay.cpp`; `src/native/native_include.cpp` retains source scanning,
edge assembly, and compiler-trace interpretation.

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

## Semantic configuration diff (`0.5.0` release boundary)

B4 compares two **raw** `compile_commands.json` arrays. The diff command does not accept
`buildscope.snapshot/v1`, `v2`, or `v3` documents as inputs and never executes a compiler, shell, or
response file. Snapshot compatibility remains separate: the producer defaults to v2, can explicitly
emit the v1 raw projection, and can opt into the v3 include-analysis contract. The diff output is the
strict `buildscope.diff/v1` contract.

### CLI and exit policy

```bash
buildscope diff \
  "$repo_root/buildscope/fixtures/diff-before.compile_commands.json" \
  "$repo_root/buildscope/fixtures/diff-after.compile_commands.json" \
  --before-project-root /project \
  --after-project-root /project \
  --suppress "standard:src/**/*.cpp" --pretty \
  --output "$scratch_root/buildscope.diff.json"
```

The `diff` subcommand accepts the same arguments as the snapshot command's shared
options. Exit status is intentionally
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

The fixture and test coverage includes native producer unit tests, C++ parser/model and
adversarial rejection tests, the producer → consumer byte-identical integration contracts
(snapshot, include trace, and diff), and the GUI diff-mode test. The Qt6 Release CMake/CTest
matrix is `9/9`; enabling `BUILDSCOPE_BUILD_BENCHMARKS` makes it `10/10`.

## Run without installing into the repository

All build and temporary output below stays under a scratch directory.

```bash
repo_root="$(git rev-parse --show-toplevel)"
scratch_root="$(mktemp -d /tmp/buildscope-b2.XXXXXX)"

cmake -S "$repo_root/buildscope" -B "$scratch_root/qt6" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILDSCOPE_BUILD_BENCHMARKS=ON
cmake --build "$scratch_root/qt6" --parallel 2
QT_QPA_PLATFORM=offscreen \
  ctest --test-dir "$scratch_root/qt6" --output-on-failure

# The native producer emits a v2 snapshot; the consumer validates it.
"$scratch_root/qt6/src/native/buildscope" \
  "$repo_root/buildscope/fixtures/compile_commands.json" \
  --project-root "$repo_root/buildscope" \
  --schema-version v2 \
  --output "$scratch_root/buildscope.snapshot.json" --pretty
"$scratch_root/qt6/src/core/buildscope-cli" \
  "$scratch_root/buildscope.snapshot.json"

# Explicit raw v1 compatibility projection.
"$scratch_root/qt6/src/native/buildscope" \
  "$repo_root/buildscope/fixtures/compile_commands.json" \
  --schema-version v1 \
  --output "$scratch_root/buildscope.snapshot.v1.json" --pretty

# The preserved fixture independently retains the v1 compatibility path.
"$scratch_root/qt6/src/core/buildscope-cli" \
  "$repo_root/buildscope/fixtures/sample.snapshot.json"
```
