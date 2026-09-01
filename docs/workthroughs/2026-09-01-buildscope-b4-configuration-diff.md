# BuildScope B4 configuration diff workthrough

> **Post-green update (2026-09-01):** This workthrough was originally captured before PR/remote
> verification. B4 implementation plus PR/remote/hosted evidence is now complete on [PR #36](https://github.com/jihoon22-lee/toy-projects/pull/36),
> head `ce64613263f0c4358579012aab135e0b23341a0e`. [Run `33485837830`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33485837830)
> used ici `v0.9.1` and completed all `16/16` checks successfully. BuildScope was `WARN`
> (`10 PASS / 3 WARN / 0 FAIL / 0 ERROR / 0 SKIP`), with lint WARN (49 warnings), `92/92` tests,
> line/function/branch `93.5% / 99.0% / 77.3%`, sanitizer PASS, compile DB `12/12` production
> units and `27` configurations, and TEM `4.95/5.0`. The 100,000-entry / 25,000-source benchmark
> recorded model `65 ms`, filter `1,602 ms`, filtered sources `1`, budget `10,000 ms`, and
> correctness `true`. [Sticky comment #5489976814](https://github.com/jihoon22-lee/toy-projects/pull/36#issuecomment-5489976814)
> has exactly one marker and three hosted-report links. Pages reports were HTTP 200 with exact
> titles and zero external resource references: BuildScope `1,319,378` bytes / SHA-256
> `5dc517d3ec8324cb1aedb6e611120ac9ae2951e27851cbc6b0e28303d02c5d43`, diskmap `337,554` bytes /
> `39a52d1e3d5b9eed6bbc1ec5253f1bf837deb4a7bb33ed2f2ef3b32df0f90e0e`, and loglens `492,746` bytes /
> `0480f07aea266c0777403ac37ebf90d4acd10f84b04e49aa2a9ccf99a6153007`. The `0.5.0` product
> release artifact and B5 hybrid integration remain pending/not started.

> **Post-merge update (2026-09-01):** PR #36 was squash-merged to `main` as
> `590899a0a9430e9ce35162b301bfef5d7dfc78a4`, and its feature branch was deleted. Exact-main
> [CI run `33488169769`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33488169769) completed
> with all 14 prerequisite jobs and `Merge Gate` successful; the PR-only publisher was skipped as expected.
> [Dependency Graph run `33488174425`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33488174425)
> also succeeded on the same head. B4 is now shipped on `main`; the `0.5.0` product release artifact
> and B5 hybrid integration remain pending/not started.

## Overview

BuildScope B4 documents the `0.5.0` semantic configuration-diff candidate originally developed on
`feat/buildscope-config-diff` and now shipped on `main` via PR #36. It compares two raw compile databases without executing build
commands, preserves meaningful compiler/configuration order, and exports a strict diff contract that
the Python CLI, native C++ consumer, and Qt UI share. This workthrough records the implementation,
adversarial hardening, documentation updates, and checksum-verified local release evidence available
before PR, remote CI, and release-artifact publication; the post-green and post-merge updates above
record the later remote and merged-main evidence.

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
- **Pre-PR capture:** B4 PR/remote CI/hosted report and release-artifact evidence remained pending.
  The public ici v0.9.0 uncached deep result is historical local evidence and is recorded below.

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
with B5 still the unreleased hybrid boundary. This synchronization list is a pre-PR record; the
post-green update above supersedes its pending remote-evidence state:

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
- Historical local ici v0.9.0 uncached deep suite: `WARN`, 11 PASS / 3 WARN with no FAIL/ERROR/SKIP,
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
historical ici v0.9.0 local     WARN; 11 PASS / 3 WARN; no FAIL/ERROR/SKIP
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

## Next Steps at pre-PR capture

- Historical TODO (superseded): open the B4 PR and capture remote CI/hosted report evidence.
- Remaining work: perform B5 Python → JSON → C++ hybrid release integration, Qt/Python matrix
  confirmation, and release-artifact publication.
- Preserve the raw-database input boundary and the strict diff v1/native-consumer contract while
  completing the remaining B5/release boundary.
