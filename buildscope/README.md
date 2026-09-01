# BuildScope

BuildScope 0.3.0 is a hybrid build explorer. B0 established the producer/consumer boundary:
Python 3.10+ reads a `compile_commands.json` without executing its commands and emits a
deterministic `buildscope.snapshot/v1` document; a C++20/Qt CLI and GUI validate and consume that
document. B1 adds the Python compile-database normalization core and emits the additive
`buildscope.snapshot/v2` contract. B2 completes the normalized C++/Qt explorer over that contract:
sources are grouped with their configurations, status/search/detail views are available, and raw
commands remain separate from structured JSON argv. The B1 public ici v0.7.1 cold verification and
the B2 public ici v0.8.0 evidence recorded here remain separate from the hosted BuildScope evidence for
[PR #31](https://github.com/jihoon22-lee/toy-projects/pull/31), whose initial verified
implementation/docs head is `1ff08fe5d2accddc0e9107113eb83dd86bd6d50a`.

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
B1 owns contract acceptance; B2 owns the normalized C++ model/UI transition and is complete.
Configuration diff (B4),
compiler-measured include explanation (B3), hybrid release integration (B5), and the ici I3
target-by-target comparison remain pending.

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

## Run without installing into the repository

All build and temporary output below stays under a scratch directory. The Python package is loaded
with `PYTHONPATH`; it is not installed into `buildscope`.

```bash
repo_root="$(git rev-parse --show-toplevel)"
scratch_root="$(mktemp -d /tmp/buildscope-b2.XXXXXX)"
py310_bin="$(command -v python3.10)"
release_root="$(mktemp -d /tmp/buildscope-ici-v0.8.0.XXXXXX)"
ici_bin="$release_root/ici.pyz"
ici_python="/tmp/buildscope-ici-py310/bin/python"  # external env with pytest+coverage+mypy

# Fetch and verify the public release asset under /tmp, never under the repository.
curl -fsSL -o "$ici_bin" \
  "https://github.com/jihoon22-lee/ici/releases/download/v0.8.0/ici.pyz"
curl -fsSL -o "$release_root/ici.pyz.sha256" \
  "https://github.com/jihoon22-lee/ici/releases/download/v0.8.0/ici.pyz.sha256"
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

# ici 0.8.0 public release verification. Provision this interpreter outside the repo
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
local/public verification are recorded below. B3/B4/B5 and the ici I3 target-by-target comparison
remain pending.

## B2 local and public verification evidence (2026-09-01)

The public `ici v0.8.0` verification for the 0.3.0 explorer completed with `46/46` tests, line /
function / branch coverage of `94.5% / 99.5% / 83.9%`, and TEM `4.98`. Its `compile_db` result
covered `8/8` production units and `19` configurations.

Local Qt 5.15.18 and Qt 6.10.2 builds each passed the complete CTest suite `6/6` (including the
normalized model, Qt shell, hybrid contract, and opt-in 100k benchmark checks). The Qt6 benchmark
result was `100,000` entries / `25,000` source groups, model build `45 ms`, recursive filter
`1,071 ms`, peak RSS `132,612 KiB`, and a `10,000 ms` budget; all correctness and budget checks
passed.

BuildScope B2 is implemented locally as version `0.3.0`. B3 compiler-measured include explanation,
B4 configuration diff, B5 hybrid release integration, and the ici I3 target-by-target comparison
remain future work.
