# BuildScope

BuildScope investigates C/C++ compilation databases, include evidence and affected translation units. It is one independent C++20/Qt6 product: `buildscope` produces snapshots and queries, `buildscope-cli` validates saved contracts, and `buildscope-gui` provides the desktop explorer. Use `buildscope --version` for the installed version.

Normalization and lexical include estimation do not execute commands. Compiler replay occurs only when explicitly selecting `compiler` or selected-unit `delayed` analysis. It runs an allowlisted GCC/Clang preprocessing command without a shell; BuildScope never invokes the recorded build command as a general build.

## Build, test and install

Dependencies: CMake 3.16+, a C++20 compiler, Qt6 Core, Widgets, Concurrent and Test. On Debian/Ubuntu, `build-essential cmake qt6-base-dev` supplies the usual build dependencies. The producer links Qt Core; the GUI additionally links Widgets/Concurrent. Python is not a runtime or test dependency.

```sh
# Run from the repository root.
cmake -S buildscope -B /tmp/buildscope-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/buildscope-build -j2
ctest --test-dir /tmp/buildscope-build --output-on-failure
cmake --install /tmp/buildscope-build --prefix /tmp/buildscope-install
/tmp/buildscope-install/bin/buildscope --version
export PATH="/tmp/buildscope-install/bin:$PATH"
```

All three executables install under `bin/`. Schemas and examples install under `share/buildscope/`, documentation under `share/doc/buildscope/`, and the desktop entry/icon under `share/applications/` and `share/icons/hicolor/scalable/apps/`. The installed tools work without the source checkout. Runtime Qt libraries must be available on the target Linux system. Version ownership is `CMakeLists.txt`; all executable versions derive from it. Independent release tags use `buildscope/vX.Y.Z`.

## CLI workflow

These examples use the installed commands above and the compilation database of
the project being inspected. Replace database/header paths with that project's
paths. The [quickstart](docs/quickstart.md) provides runnable repository fixtures.

```sh
# Default normalized v2 snapshot; no compiler execution.
buildscope build/compile_commands.json --project-root "$PWD" -o snapshot.json --pretty

# v4 lexical graph; no compiler execution.
buildscope build/compile_commands.json --project-root "$PWD" \
  --include-analysis estimate -o includes.json --pretty

# Actual compiler traces plus separate estimates when measurement is partial.
buildscope build/compile_commands.json --project-root "$PWD" \
  --include-analysis compiler --analysis-unit-ms 5000 \
  --analysis-time-budget 60 -o measured.json --pretty

# Estimate budgeted units; replay only matching normalized source paths.
buildscope build/compile_commands.json --project-root "$PWD" \
  --include-analysis delayed --analysis-unit 'src/**/*.cpp' -o selected.json

buildscope impact measured.json --header include/api.hpp --pretty -o impact.json
buildscope-cli measured.json
buildscope-gui build/compile_commands.json
```

`--analysis-unit` is repeatable. Globs are slash-aware (`*`/`?` do not cross `/`; `**` can), use normalized source paths, and honor Windows case folding. `exact:PATH` selects a literal normalized path, including filenames containing glob characters. `delayed` without a selector estimates only. Every attempted unit, including its estimate and possible replay, spends the same per-unit/global budgets.

`buildscope impact` reports direct including files/lines and affected translation-unit configurations with evidence chains. Compiler and fallback graphs are queried independently; a reported estimated chain never becomes compiler-measured through graph mixing. `partial` and unavailable-analysis counts explain coverage. No observed edge is **not** proof of no build impact. Traversal is bounded at 500,000 edges and a displayed chain at 128 links; truncation is explicit.

Snapshot/query commands return 0 on a successful export, including explicitly marked partial evidence; 2 indicates malformed input, invalid options, unsafe replay or output failure. SIGINT/SIGTERM cancellation during analysis retains a valid partial v4 snapshot and returns 130. Cancellation during database import stops before producing a snapshot. Exit 130 may have no output in that case. The semantic `diff` command has its own 0/1/2 policy below.

## Honest evidence and compatibility

With no analysis flag, the producer still emits `buildscope.snapshot/v2`; `--schema-version v1` emits the raw compatibility projection. Analysis now defaults to **v4**. Explicit `--schema-version v3` remains available for old consumers. `v3`/`v4` without a mode selects estimation; v1/v2 with an analysis mode is rejected.

The current reader accepts v1–v4. v4 extends the normalized contract with:

- `include_analysis.complete`, `stop_reason`, and `fallback` (null or a separately labeled, non-recursive estimated analysis);
- `analysis_run` with mode, cancellation state, units/files/bytes/edges consumed and all configured limits;
- existing command, duration, diagnostics and edges, with each edge's original evidence label.

A nonzero compiler exit, timeout, cancellation, malformed trace, missing source or budget cutoff does not erase an already validated trace prefix. An estimate is kept separately when replay is incomplete or unavailable. An estimated graph is lexical: it does not evaluate preprocessor conditions, macros or compiler builtin paths. Even a `complete` estimate means the bounded lexical traversal finished, not that the result proves actual compiler behavior. Legacy v3 exports cannot represent the separate fallback or completeness fields; diagnostics disclose omitted fallback information. Use v4 when those distinctions matter.

Snapshots reject duplicate JSON keys, unsupported versions, unknown contract fields, invalid ranges and inconsistent provenance. v4 readers also validate counters against declared limits and refuse a complete analysis with a stop reason or fallback. The schemas under `schemas/` are self-contained. v1 compatibility retains permissive legacy extension behavior; strict v2–v4 validate normalized/state cross-entry consistency.

Normalized records preserve compiler family/name/path, wrappers, ordered define/undefine actions, ordered include paths, language, standard, sysroot, target, directory/source/output path records, invocation form, configuration digest, duplicate annotation and source status. `arguments` is authoritative when both invocation forms exist. Tokenization does not expand environment variables, shell substitution, globs or response files. Configuration digests identify recorded effective invocations; they are not content hashes or relocation-invariant semantic identifiers.

Project-relative include paths remain relative to the project root even when
classified as `vendor`; classification does not change the search directory.

## Budgets and replay boundary

All budgets use monotonic elapsed time. Cancellation and budget checks occur while scanning source chunks, visiting includes and polling compiler output. The remaining global budget also bounds an in-progress translation unit; a per-unit timeout cannot silently consume the entire default replay timeout after the global deadline.

| Resource | Default | CLI option |
|---|---:|---|
| Attempted translation units | 512, maximum 4,096 | `--analysis-max-units` |
| Global analysis time | 120 s, maximum 600 s | `--analysis-time-budget` |
| Per-unit analysis time | 15,000 ms | `--analysis-unit-ms` |
| Total source bytes scanned | 256 MiB | `--analysis-source-bytes` |
| Per-unit source bytes scanned | 16 MiB | `--analysis-unit-bytes` |
| Total source-file visits | 65,536 | `--analysis-max-files` |
| Per-unit source-file visits | 4,096 | `--analysis-unit-files` |
| Total evidence edges | 500,000 | `--analysis-max-edges` |
| Per-unit evidence edges | 100,000 | `--analysis-unit-edges` |
| Compiler stderr per replay | 16 MiB maximum | `--analysis-trace-bytes` |

Repeated reads and separate fallback/measurement graphs consume actual work budgets. Source files are independently capped at 4 MiB and read in fixed 64 KiB chunks. Before/after inode, size, mtime and ctime checks detect growth, replacement and mutation. Retained evidence serialization and bounded trace-prefix decoding can continue briefly after a work deadline; these are cooperative limits, not hard real-time or RSS guarantees.

Compilation databases are capped at 64 MiB and 100,000 entries; native normalization parses array elements individually rather than constructing a whole database DOM. Snapshot inputs/outputs are capped at 256 MiB. Arguments are capped at 32,768 items and 1 MiB of text per invocation. Reader and writer safeguards reject non-regular/final-symlink inputs and source/output aliases. POSIX output uses a no-follow parent descriptor, exclusive temporary creation, fsync and descriptor-relative atomic rename.

Replay only accepts a direct executable GCC/Clang driver from the system toolchain policy. It uses a positive option allowlist, accepts safe optimization levels such as `-O2`, removes output/dependency-generation flags, and rejects response files even when supplied as an option value, extra source operands, stdin, plugins, driver escapes and wrapper scripts. The environment is fixed and minimal. Timeout/output-limit/cancellation termination kills the replay process group and collects only complete stderr lines from a capped tail.

Measured edges use compiler `-H` relationships. Recovering a directive line from a parent file is separately labeled `source-scan`; a missing-header compiler diagnostic uses `compiler-diagnostic`. If a location cannot be recovered, it remains unavailable. A synthetic compiler-selected search candidate is not used to claim that an unrelated inactive directive produced the edge. Candidate order, selected candidate, same-name alternatives and project/vendor/generated/system classification remain inspectable.

## Relocating a recorded build

```sh
buildscope moved/build/compile_commands.json --project-root "$PWD/moved" \
  --map-root "/old/worktree=$PWD/moved" \
  --include-analysis estimate -o relocated.json
```

Repeat `--map-root OLD=NEW` for up to 32 unique absolute roots. The longest matching path prefix wins, with path-component boundaries (`/old/tree-other` does not match `/old/tree`). Mapping rewrites explicit path fields and known path-bearing argument tokens; it never replaces arbitrary text inside a `-D` macro. Effective argv are stored as structured `arguments`, re-normalized and re-digested, with a mapping diagnostic. The original database remains the source of the pre-mapping invocation. Relative paths keep their relative meaning. GUI relocation configures source navigation and the next analysis; it does not silently relabel old evidence as a fresh scan.

## Desktop workflow

Open a snapshot or **Import compile DB…** directly. Command-line GUI input auto-detects a raw database, snapshot or diff. Database import estimates includes in a worker, using the database directory as the default project root (a conventional `build`/`cmake-build-*` directory uses its parent). For a relocated or unconventional layout, use **Relocate root…** before re-analysis, or produce a snapshot with an explicit CLI `--project-root`.

The 1280×800 palette-aware layout keeps source/status/short configuration identity in the tree; compiler, target and standard remain in Overview. Full digests remain available in tooltips. Tabs show structured argv, raw commands, definitions, search paths, include evidence, diagnostics, configuration diff and header impact. Partial evidence and the separate estimated fallback are visible labels, not implied by color.

Loading, direct import, analysis and impact queries run in background workers. Cancel interrupts bounded work; generation IDs prevent older jobs from overwriting a newer selection. Text filters debounce for 150 ms. The **Budgets…** dialog exposes primary analysis limits. Choosing compiler replay is explicit in the analysis-mode selector.

Select an include edge to inspect its parent/line, resolution and ordered candidates; double-click or **Open Source Location** navigates it. **Editor argv…** accepts a JSON array such as `["code","--goto","{file}:{line}"]`. Placeholders are substituted within individual argv tokens and launched without a shell. An empty array restores the desktop file opener, which does not guarantee line navigation. A missing source line is shown as unavailable; editors receive the first line as a file-opening fallback. Editor configuration is stored in local Qt settings.

Keyboard: Ctrl+O opens input, Ctrl+L focuses filtering, Escape cancels; Enter in the header-impact field runs its query. Saved snapshots retain the same strict native contracts used by the CLI.

## Semantic configuration diff

```sh
buildscope diff before/compile_commands.json after/compile_commands.json \
  --before-project-root /before/project --after-project-root /after/project \
  --suppress 'standard:src/**/*.cpp' --pretty -o configuration.diff.json
buildscope-cli --diff configuration.diff.json
buildscope-gui configuration.diff.json
```

Diff accepts raw compilation databases, not snapshots. It compares normalized toolchain/language/target/sysroot, ordered definitions/includes and residual flags. Each side's project root reduces relocation noise. Raw command spelling, output filenames, entry order, duplicate annotation and source timestamps are ignored. Exact semantic digests pair first, then unique configuration roles; ambiguous matches remain added/removed with diagnostics. Move inference is conservative and based on source names/configurations, not source-content identity.

Suppressions use repeatable `CATEGORY[:GLOB]`, retain evidence on every suppressed change and support `*` as the category. Exit 0 means no visible changes, 1 means visible changes, and 2 means invalid input or processing failure. The strict `buildscope.diff/v1` reader checks summaries, digests, suppression evidence and omitted changes.

## Verification and benchmarks

```sh
cmake -S buildscope -B /tmp/buildscope-asan \
  -DCMAKE_BUILD_TYPE=Debug -DBUILDSCOPE_SANITIZE=ON
cmake --build /tmp/buildscope-asan -j2
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir /tmp/buildscope-asan --output-on-failure

cmake -S buildscope -B /tmp/buildscope-bench -DCMAKE_BUILD_TYPE=Release \
  -DBUILDSCOPE_BUILD_BENCHMARKS=ON
cmake --build /tmp/buildscope-bench -j2
ctest --test-dir /tmp/buildscope-bench -L benchmark --output-on-failure
```

CTest sets `QT_QPA_PLATFORM=offscreen` for GUI tests. Coverage includes old contracts, strict v4 provenance, real compiler partial failure, timeout/output/cancellation prefixes, deterministic source growth, normalized glob selection, option-value response-file rejection, source relocation boundaries, impact chains, direct GUI import/cancellation/reopening, editor token boundaries and installed CLI flows. The opt-in model benchmark measures 100,000 entries and filtering with a configurable test budget; it reports measured timing/RSS rather than guaranteeing host performance.

See [quickstart](docs/quickstart.md) for independent CMake and qmake examples.
