# BuildScope quickstart

BuildScope has two stages:

1. A released `buildscope.pyz` or pure Python wheel reads an existing
   `compile_commands.json` and writes a versioned snapshot (or diff report).
2. The native `buildscope-cli` and `buildscope-gui` load that JSON for terminal
   or desktop inspection.

The Python stage is offline and does not execute compiler commands. The native
programs consume snapshots and diff reports, not a raw compilation database.

## Prerequisites

- Python 3.10 or newer
- A CMake-generated or otherwise captured `compile_commands.json`
- A release build of the native CLI/GUI (or a local CMake build of them)

The examples in this directory are deliberately small and independent of Qt:
`examples/cmake` can generate a database with CMake, while
`examples/qmake` demonstrates a qmake project and includes a deterministic
sample database for the producer.

## Produce a snapshot

Set the repository and release paths once. Replace `release_root` with the
directory containing the B5 release assets.

```bash
repo_root="$(git rev-parse --show-toplevel)"
release_root="/path/to/buildscope-0.5.0"
scratch_root="$(mktemp -d /tmp/buildscope-quickstart.XXXXXX)"
```

### Standalone zipapp

The standalone artifact is a self-contained `buildscope.pyz`:

```bash
chmod +x "$release_root/buildscope.pyz"
"$release_root/buildscope.pyz" \
  "$repo_root/buildscope/examples/cmake/compile_commands.json" \
  --project-root "$repo_root/buildscope/examples/cmake" \
  --output "$scratch_root/cmake.snapshot.json" --pretty
```

### Wheel

The wheel is pure Python (`buildscope-0.5.0-py3-none-any.whl`) and exposes the
same `buildscope` and `buildscope-diff` commands:

```bash
python3.10 -m venv "$scratch_root/venv"
"$scratch_root/venv/bin/python" -m pip install --no-deps \
  "$release_root/buildscope-0.5.0-py3-none-any.whl"
"$scratch_root/venv/bin/buildscope" \
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
cmake --build "$scratch_root/cmake-build" --parallel
"$scratch_root/venv/bin/buildscope" \
  "$scratch_root/cmake-build/compile_commands.json" \
  --project-root "$repo_root/buildscope/examples/cmake" \
  --output "$scratch_root/cmake-generated.snapshot.json" --pretty
```

The checked-in example database is useful for a reproducible smoke test; the
generated database reflects the compiler and build directory on the current
machine.

## Inspect with the native consumers

Point these variables at the binaries from the release bundle or an out-of-tree
CMake build of `buildscope`:

```bash
native_cli="/path/to/buildscope-cli"
native_gui="/path/to/buildscope-gui"

"$native_cli" "$scratch_root/cmake.snapshot.json"
"$native_gui" "$scratch_root/cmake.snapshot.json"
```

The GUI also has Open Snapshot and Open Diff actions. On a headless machine,
use `QT_QPA_PLATFORM=offscreen` only for smoke tests; a normal desktop launch
uses the platform's default Qt backend.

## Compare two configurations

The `diff` subcommand compares two raw compilation databases and returns exit
status 0 when there are no visible changes, 1 when visible changes exist, and
2 for invalid input or another processing error:

```bash
"$scratch_root/venv/bin/buildscope-diff" \
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
"$scratch_root/venv/bin/buildscope" \
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
