# BuildScope B4 configuration diff workthrough

## Overview

BuildScope B4 documents the `0.5.0` semantic configuration-diff candidate on
`feat/buildscope-config-diff`. It compares two raw compile databases without executing build
commands, preserves meaningful compiler/configuration order, and exports a strict diff contract that
the Python CLI, native C++ consumer, and Qt UI share. This workthrough records the implementation,
adversarial hardening, documentation updates, and checksum-verified local release evidence available
before PR, remote CI, and release-artifact publication.

## Context

- B3 shipped the snapshot producer boundary: v2 is the default, v1 remains an explicit raw
  projection, and v3 is the optional include-analysis contract.
- B4 intentionally accepts raw `compile_commands.json` arrays, not snapshot v1/v2/v3 files. The
  separate output contract is `buildscope.diff/v1`.
- The comparison must distinguish added, removed, changed, and moved translation units while
  avoiding false drift from relocated project/build paths and output names.
- The implementation was developed in small Conventional Commit units. The final hardening split
  glob matching, native semantic validation, and diff-window rendering into focused modules, then
  added a dedicated native rejection-test target.
- B4 PR/remote CI/hosted report and release-artifact evidence remain pending. The public ici v0.9.0
  uncached deep result is complete and recorded below.

## Changes Made

### 1. Semantic diff implementation and native boundary

The Python producer in `python/buildscope/diff.py`, `diff_policy.py`, and `diff_glob.py` owns raw
compile-database comparison, normalization, deterministic pairing, suppression, and canonical
serialization. The C++ consumer keeps parsing and envelope/policy checks in `diff_parser.cpp`, while
`diff_validation.cpp/.hpp` independently validates semantic objects, digests, lifecycle shapes, and
the exact canonical change set. `diff_model.cpp` provides the issues-first model, and
`diff_window.cpp` keeps diff-specific Qt rendering separate from general snapshot-window behavior.

The native contract coverage is split between `test_diff.cpp` and `test_diff_validation.cpp`.
Malformed envelope, policy, semantic, lifecycle, suppression-evidence, summary, and model-boundary
cases are rejected without weakening the public schema or coverage threshold. CMake registers the
new target in both Qt5 and Qt6 matrices.

### 2. B4 product and policy documentation

The implementation described in `/home/jihoon/projects/toy-projects/buildscope/README.md` now
covers:

- the raw compile-database-only input boundary and the `0/1/2` exit policy;
- relocation-aware lexical normalization for compiler family/name/path/style, wrappers, launcher,
  language, standard, target/sysroot, ordered define/undefine actions, ordered include kind/path,
  and residual flags;
- ignored command spelling, build directory, output path/name, entry-index/duplicate annotation,
  filesystem status, and snapshot diagnostic/include-analysis fields;
- conservative move pairing, one-to-one duplicate pairing, ambiguity warnings, and the limitation
  that a unique same-basename replacement can resemble a move;
- slash-aware suppression globs and canonical deterministic JSON export;
- strict schema/native parser validation and Qt issues-first diff consumption; and
- v1/v2/v3 snapshot compatibility versus the separate diff v1 contract.

### 3. Repository status synchronization

The following files were updated to describe B4 as the current `0.5.0` feature-branch candidate,
with B5 still the unreleased hybrid boundary:

- `/home/jihoon/projects/toy-projects/CHANGELOG.md` — added the top `Unreleased` B4 entry.
- `/home/jihoon/projects/toy-projects/docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md`
  — checked the B4 implementation items and recorded B5 as pending.
- `/home/jihoon/projects/toy-projects/docs/superpowers/2026-08-30-handover.md` — refreshed the
  project table, current B4 state, local evidence, and pending handoff items.
- `/home/jihoon/projects/toy-projects/README.md` — added a concise portfolio-level B4 summary.

### 4. Local evidence recorded

The synchronized status records the current local candidate gate:

- Python semantic/CLI suite: `83/83` PASS.
- Ruff check/format: `19` files PASS.
- mypy: `15` source files PASS.
- Qt5 5.15.18 default Release CMake/CTest: `9/9` PASS (`10/10` with benchmark).
- Qt6 6.10.2 default Release CMake/CTest: `9/9` PASS (`10/10` with benchmark).
- Pure `buildscope-0.5.0-py3-none-any` wheel: snapshot v1/v2/v3 and diff v1 schemas included;
  no native extension.
- Public ici v0.9.0 uncached deep suite: `WARN`, 11 PASS / 3 WARN with no FAIL/ERROR/SKIP,
  `92/92` tests, line/function/branch `93.5% / 99.0% / 76.7%`, sanitizer PASS, compile DB
  `12/12` production units and `27` configurations, TEM `4.95/5.0`.

## Code Examples

### Compare raw compile databases

```bash
before_root=/absolute/path/to/before
after_root=/absolute/path/to/after
PYTHONPATH=buildscope/python \
  python3 -m buildscope diff \
  before/build/compile_commands.json after/build/compile_commands.json \
  --before-project-root "$before_root" \
  --after-project-root "$after_root" \
  --suppress 'standard:src/**/*.cpp' \
  --pretty --output buildscope.diff.json
```

The installed `buildscope-diff` entry point accepts the same options. `0` means no visible units,
`1` means at least one unsuppressed unit, and `2` means the comparison or export failed.

### Native consumer boundary

```bash
buildscope-cli --diff buildscope.diff.json
```

The native parser consumes strict `buildscope.diff/v1`, checks bounded fields and duplicate/unknown
keys, recomputes semantic digests and change sets, and exposes the resulting units through the
issues-first Qt model/UI.

### Semantic identity sketch

```json
{
  "schema_version": "buildscope.diff/v1",
  "policy": {"version": "buildscope.diff-policy/v1"},
  "summary": {"visible_units": 1, "visible_changes": 1},
  "units": [{"kind": "changed", "changes": [{"category": "standard"}]}]
}
```

## Verification Results

```text
Python unittest discovery       83/83 PASS
Ruff check + format             19 files PASS
mypy                            15 source files PASS
Qt 5.15.18 default Release      9/9 CTest PASS
Qt 6.10.2 default Release       9/9 CTest PASS
ici v0.9.0 uncached deep        WARN; 11 PASS / 3 WARN; no FAIL/ERROR/SKIP
ici combined test evidence      92/92; line 93.5%; function 99.0%; branch 76.7%
ici sanitizer / compile DB      PASS; 12/12 production units; 27 configurations
pure wheel/schema inspection    PASS; no native extension
```

The checked-in hybrid fixture verifies Python export byte identity and native diff loading. The
sample diff SHA-256 is
`5722f45fa1408decb02838bec4bb5feb77d2bb49a73a02656e40c800d3e16fbf`. The deep standalone
HTML has title `ici Verification Report — buildscope`, zero external resource references,
1,235,505 bytes, and SHA-256
`0c98a38b27e928df2c60dcadff9ecc3daa1072cb620354d9f4a9fe8d9b987f80`.

An intermediate deep run exposed combined C++/Python branch coverage below the configured 75%
minimum even though direct Python coverage was already high. The fix was additional native strict
contract coverage, not a threshold change; the combined result rose to 76.7%.

## Next Steps

- TODO: open the B4 PR and capture remote CI/hosted report evidence.
- TODO: perform B5 Python → JSON → C++ hybrid release integration, Qt/Python matrix confirmation,
  and release-artifact publication.
- Preserve the raw-database input boundary and the strict diff v1/native-consumer contract while
  closing those evidence gates.
