# BuildScope quickstart

BuildScope supports two workflows:

1. The `buildscope` producer executable reads an existing
   `compile_commands.json` and writes a versioned snapshot (or diff report).
2. `buildscope-cli` validates saved JSON; `buildscope-gui` opens snapshots, diffs or raw databases directly and performs cancellable include analysis.

The producer stage is offline and does not execute compiler commands unless
`--include-analysis compiler` or selected-unit `delayed` analysis explicitly opts into the bounded replay policy. The GUI defaults to lexical estimation when importing a raw database.

## Prerequisites

- A CMake build of this directory (Qt 6 required for the GUI; the producer
  links Qt Core only)
- A CMake-generated or otherwise captured `compile_commands.json`

The examples in this directory are deliberately small and independent of Qt:
`examples/cmake` can generate a database with CMake, while
`examples/qmake` demonstrates a qmake project and includes a deterministic
sample database for the producer.

## Build

```bash
repo_root="$(git rev-parse --show-toplevel)"
scratch_root="$(mktemp -d /tmp/buildscope-quickstart.XXXXXX)"

cmake -S "$repo_root/buildscope" -B "$scratch_root/build" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$scratch_root/build" --parallel 2
export PATH="$scratch_root/build/src/native:$scratch_root/build/src/core:$scratch_root/build/src/gui:$PATH"
```

The producer lands at `$scratch_root/build/src/native/buildscope`, the
consumers at `src/core/buildscope-cli` and `src/gui/buildscope-gui`.

## Produce a snapshot

```bash
"$scratch_root/build/src/native/buildscope" \
  "$repo_root/buildscope/examples/cmake/compile_commands.json" \
  --project-root "$repo_root/buildscope/examples/cmake" \
  --output "$scratch_root/cmake.snapshot.json" --pretty
```

For a CMake-generated database, configure and build the example first. CMake
writes `compile_commands.json` in the build tree because the example enables
`CMAKE_EXPORT_COMPILE_COMMANDS`:

```bash
cmake -S "$repo_root/buildscope/examples/cmake" \
  -B "$scratch_root/cmake-build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$scratch_root/cmake-build" --parallel 2
"$scratch_root/build/src/native/buildscope" \
  "$scratch_root/cmake-build/compile_commands.json" \
  --project-root "$repo_root/buildscope/examples/cmake" \
  --output "$scratch_root/cmake-generated.snapshot.json" --pretty
```

The checked-in example database is useful for a reproducible smoke test; the
generated database reflects the compiler and build directory on the current
machine.

## Inspect with the consumers

```bash
native_cli="$scratch_root/build/src/core/buildscope-cli"
native_gui="$scratch_root/build/src/gui/buildscope-gui"

"$native_cli" "$scratch_root/cmake.snapshot.json"
"$native_gui" "$scratch_root/cmake.snapshot.json"
```

The GUI also has Open Snapshot, Import compile DB, Open Diff, Analyze, Budgets and Cancel actions. Import runs in a background worker and produces v4 evidence. On a headless machine,
use `QT_QPA_PLATFORM=offscreen` only for smoke tests; a normal desktop launch
uses the platform's default Qt backend.

## Compare two configurations

The `diff` subcommand compares two raw compilation databases and returns exit
status 0 when there are no visible changes, 1 when visible changes exist, and
2 for invalid input or another processing error:

```bash
"$scratch_root/build/src/native/buildscope" diff \
  "$repo_root/buildscope/fixtures/diff-before.compile_commands.json" \
  "$repo_root/buildscope/fixtures/diff-after.compile_commands.json" \
  --project-root "$repo_root/buildscope" \
  --output "$scratch_root/buildscope.diff.json" --pretty

"$native_cli" --diff "$scratch_root/buildscope.diff.json"
"$native_gui" "$scratch_root/buildscope.diff.json"
```

The fixture intentionally contains a visible change, so the first command is
expected to return 1 while still writing the report. Suppressions and stable
before/after labels are available on the `buildscope diff` command when a CI
pipeline needs them.

## qmake capture limitations

qmake generates Makefiles; it does not promise to write a
`compile_commands.json`. BuildScope does not invoke qmake, `make`, or Bear and
does not expand shell commands. To capture a real qmake build, configure it and
run the full build through a command-interception tool such as Bear:

```bash
mkdir -p "$scratch_root/qmake-build"
qmake -o "$scratch_root/qmake-build/Makefile" \
  "$repo_root/buildscope/examples/qmake/example.pro"
(
  cd "$scratch_root/qmake-build"
  bear --output compile_commands.json -- make -j2
)
"$scratch_root/build/src/native/buildscope" \
  "$scratch_root/qmake-build/compile_commands.json" \
  --project-root "$repo_root/buildscope/examples/qmake" \
  --output "$scratch_root/qmake.snapshot.json" --pretty
```

Use `qmake6` instead of `qmake` for a Qt 6 toolchain. Capture after a clean or
full build so every translation unit is observed, and repeat the capture for
each Qt/compiler toolchain whose flags need to be compared. Generated moc or
wrapper invocations may appear in the captured database; that is expected and
depends on the build actually performed. If Bear is unavailable, use the
checked-in `examples/qmake/compile_commands.json` as a deterministic producer
input, or provide a database from another capture tool. The qmake example is
compiler-only on purpose, so its small source set can be built with both qmake
majors without requiring Qt modules.

## Include evidence, impact and relocated builds

```sh
buildscope build/compile_commands.json --project-root "$PWD" \
  --include-analysis compiler --analysis-unit-ms 5000 \
  --analysis-time-budget 30 -o includes.v4.json
buildscope impact includes.v4.json --header include/api.hpp --pretty
buildscope relocated/compile_commands.json --project-root "$PWD/relocated" \
  --map-root "/old/project=$PWD/relocated" --include-analysis estimate -o relocated.json
```

v4 records incomplete compiler traces and estimates separately. SIGINT/SIGTERM cancellation during analysis exports partial evidence and exits 130. Use explicit `--schema-version v3` only for older consumers that cannot retain the new fallback model. See the README for all per-unit/global budgets and evidence limits.

In the GUI, **Editor argv…** takes a JSON array such as `["code","--goto","{file}:{line}"]`; token boundaries are preserved and no shell is used. **Relocate root…** maps navigation and the next analysis to the chosen local checkout. **Header impact** shows direct includers and translation-unit evidence chains.
