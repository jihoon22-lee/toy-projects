# BuildScope

BuildScope is a hybrid build explorer. B0 defines the producer/consumer boundary:
Python 3.10+ reads a `compile_commands.json` without executing its commands and emits a
deterministic `buildscope.snapshot/v1` document; a C++20/Qt CLI and GUI validate and consume that
document. The B0 implementation is locally complete on `feat/buildscope-skeleton`. Remote evidence
is complete: [ici PR #109](https://github.com/jihoon22-lee/ici/pull/109) has its sticky report and
two Pages checks, and the exact `main` commit `b87afba` passed CI run
`33419851128`.

## B0 scope

The Python producer in `python/buildscope/` is dependency-free and bounded:

- it rejects databases larger than 64 MiB or with more than 100,000 entries;
- it performs JSON parsing and validation only—no shell or compiler process is started;
- it preserves the raw `arguments` array or `command` string, plus `directory`, `file`, and optional
  `output` fields;
- it emits the `schema_version`, producer, source-count, and sorted-entry fields in stable JSON.
- its package metadata includes this README and builds as a pure `py3-none-any` wheel plus sdist.

The C++ consumer in `include/` and `src/` validates the same versioned contract, including the
64 MiB/100,000-entry bounds and the declared entry count. The CLI prints a compact summary:

```text
buildscope-cli SNAPSHOT.json
```

The Qt window accepts an optional snapshot path or opens one through its file chooser, then shows
source, working directory, and raw compiler invocation rows. CMake enables C++20,
`CMAKE_AUTOMOC`, `CMAKE_AUTOUIC`, `CMAKE_AUTORCC`, and compile-command export. The four CTest
entries cover Python unit tests, the C++ contract, the Qt window, and the Python-producer → C++
consumer hybrid contract.

B0 intentionally does not claim compiler/language/standard/define/include/sysroot/target
normalization, configuration diff, or include explanation. Those are B1/B3 work. The ici I3
target-by-target external comparison also remains pending.

## Run without installing into the repository

All build and temporary output below stays under a scratch directory. The Python package is loaded
with `PYTHONPATH`; it is not installed into `buildscope`.

```bash
repo_root="$(git rev-parse --show-toplevel)"
scratch_root="$(mktemp -d /tmp/buildscope-b0.XXXXXX)"
py310_bin="$(command -v python3.10)"
release_root="$(mktemp -d /tmp/buildscope-ici-v0.7.1.XXXXXX)"
ici_bin="$release_root/ici.pyz"
ici_python="/tmp/buildscope-ici-py310/bin/python"  # external env with pytest+coverage+mypy

# Fetch and verify the public release asset under /tmp, never under the repository.
curl -fsSL -o "$ici_bin" \
  "https://github.com/jihoon22-lee/ici/releases/download/v0.7.1/ici.pyz"
curl -fsSL -o "$release_root/ici.pyz.sha256" \
  "https://github.com/jihoon22-lee/ici/releases/download/v0.7.1/ici.pyz.sha256"
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
  --output "$scratch_root/buildscope.snapshot.json" --pretty

# Qt 6.10.2 leg.
cmake -S "$repo_root/buildscope" -B "$scratch_root/qt6" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=ON \
  -DPython3_EXECUTABLE="$py310_bin"
cmake --build "$scratch_root/qt6" --parallel
QT_QPA_PLATFORM=offscreen \
  ctest --test-dir "$scratch_root/qt6" --output-on-failure

# Qt 5.15.18 leg.
cmake -S "$repo_root/buildscope" -B "$scratch_root/qt5" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON \
  -DPython3_EXECUTABLE="$py310_bin"
cmake --build "$scratch_root/qt5" --parallel
QT_QPA_PLATFORM=offscreen \
  ctest --test-dir "$scratch_root/qt5" --output-on-failure

# The C++ consumer reads the Python-produced snapshot.
"$scratch_root/qt6/src/core/buildscope-cli" \
  "$scratch_root/buildscope.snapshot.json"

# ici 0.7.1 public release verification. Provision this interpreter outside the repo
# beforehand with pytest, coverage, and mypy, then point ici at it.
(
  cd "$repo_root/buildscope"
  ICI_PYTHON="$ici_python" \
    "$ici_bin" verify --report --html "$scratch_root/buildscope-ici.html"
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

## Local evidence (2026-09-01)

- Qt 5.15.18 and Qt 6.10.2 each configured and built the C++20 project; each reported `4/4`
  CTest tests passed, including the hybrid contract.
- The cold isolated verification of the public `ici v0.7.1` release asset used `ICI_PYTHON`
  with a Python 3.10 interpreter whose `python -m` probes reported `pytest`, `coverage`, and
  `mypy` capability `READY`. It produced suite `WARN` with
  `12 PASS / 1 WARN / 0 FAIL / 0 ERROR / 0 SKIP`.
- The mypy invocation received only the `python` source root (C++ roots were excluded) and
  returned `rc=0`. The sole warning is the documented C++ type-checking limitation; C++ type
  analysis is unsupported.
- The same report recorded `9/9` tests, TEM `5.00`, line/function/branch coverage
  `96.3% / 100.0% / 86.8%`, complexity `14 PASS`, compilation DB `4/4` production units and
  `13` configurations, and `63.37s` total duration.

The public v0.7.1 release revalidated and fixed the D11/I5 interpreter/tool capability path. The
release run `33420348698` succeeded, published [ici v0.7.1](https://github.com/jihoon22-lee/ici/releases/tag/v0.7.1)
with nine assets, and the standard `sha256sum --check ici.pyz.sha256` passed. B1 normalization and
the ici I3 target-by-target comparison remain incomplete.
