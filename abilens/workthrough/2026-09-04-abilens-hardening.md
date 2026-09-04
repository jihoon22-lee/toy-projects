# AbiLens bounded ELF inspection and parser hardening — 2026-09-04

## Overview

AbiLens now has a bounded GNU `readelf` capability contract, deterministic
report evidence, fail-closed report parsing, protected Make output trees, and
single-open input identity checks. The implementation remains a C++20/Linux
product with no Qt, Python runtime, or native third-party dependency.

## Changes Made

### Tool capability evidence

- Added a shell-free `readelf --version` probe before parsing a valid ELF.
- Only a parseable GNU readelf banner and numeric version are accepted.
- Non-GNU or malformed capability output becomes `tool-error`.
- Added a stable `tool.name`/`tool.version` object to the report schema,
  JSON serializer, text renderer, and round-trip tests.
- Kept the fixed C-locale `readelf -h -S -d -V -W` invocation bounded to an
  8 MiB limit per stream and a 30-second deadline.

### Process safety

- Refactored the readelf runner around one poll-based fork/exec adapter used
  by both capability and inspection calls.
- A timeout or output-bound violation sends `SIGKILL`, closes both pipes, and
  reaps the child; the output-bound regression uses a private fake executable
  and confirms it returns promptly.
- Temporary parser fixtures now use `mkstemp`; the fake tool is placed in a
  private `mkdtemp` directory.

### Single-open input identity

- Each inspection opens the target once and reads the ELF header and program
  headers from that descriptor.
- The same descriptor is passed to `readelf` as `/proc/self/fd/<n>`, preventing
  structural and tool evidence from independently reopening a mutable path.
- AbiLens compares device, inode, mode, size, mtime, and ctime on the open
  descriptor and rechecks the original path identity. Ordinary path replacement
  and in-place metadata changes during evidence collection fail closed as a
  `tool-error`.
- This requires Linux `/proc/self/fd` and is not a cryptographic content
  snapshot. An adversary that changes bytes and restores every observed
  metadata value before the final check is outside this guarantee.

### Strict report input

- Added a 64-container nesting bound to the handwritten JSON reader.
- Enforced exact v1 root and nested object keys, known status/namespace
  enumerations, numeric ABI version syntax, unique arrays and ABI tuples, and
  `abi.maximum` consistency with the version list.
- Duplicate object keys remain rejected.
- Added regressions for unknown keys, duplicates, invalid namespace/tool,
  inconsistent maximums, and deeply nested input.

### Make output ownership

- Added `tools/guard-out.sh`, which rejects dangerous paths, symlink outputs,
  and non-empty unowned directories.
- Owned output trees carry a canonical-path marker and are the only trees
  eligible for `make clean`; a shared Make shadow with owned variant
  children is also recognized.
- Added `tests/test_clean.sh` for unowned-directory preservation, root
  refusal, marker creation, and safe cleanup.
- Coverage, ASan/UBSan, and TSan variants remain isolated below `OUT`.

### Documentation and schema

- Updated `README.md` with GNU Binutils support, capability evidence, bounds,
  and output ownership behavior.
- Updated `schemas/abilens-report-v1.schema.json` for the tool object,
  conditional GNU readelf requirement, numeric maximums, and unique ABI
  requirements.
- Documented the single-open descriptor and path-identity contract, including
  its `/proc/self/fd` requirement and residual adversarial content limitation.
- Product metadata remains `0.1.0` / Unreleased.

## Verification Results

All commands were run from the AbiLens project root:

```text
make --no-print-directory --jobs=4 OUT=build check coverage sanitize thread-sanitize
exit 0

test_parser: PASS
test_integration: PASS
test_clean: PASS
native test checks: 2/2 PASS
coverage evidence: ESTIMATED

jsonschema Draft 2020-12 validation:
  build/reports/abilens.json: 0 errors
  build/reports/diff.json: 0 errors

deterministic rebuild: PASS after clean and rebuild
```

The exact ici candidate used for the deep verification is non-stable
`0.10.2` (`stable=false`). Its source target is
`ccb2067c656492c549dae8f4abc198a69ea013c2`, with all-green main CI
[run `33851206764`](https://github.com/jihoon22-lee/ici/actions/runs/33851206764).
The candidate producer is [run `33852539205`](https://github.com/jihoon22-lee/ici/actions/runs/33852539205),
which published [artifact `9928909508`](https://github.com/jihoon22-lee/ici/actions/artifacts/9928909508).
The candidate ZIP SHA-256 is
`c21437a7abb1b9016d351584b97a8ffc813ae17a699a3153262b31e9fa53af4b`,
and the contained `ici.pyz` SHA-256 is
`23d9922b94b2ba34ab8884cd2d39c8eda358ccb32d0925af5c0a3d52a7ddc893`.

The exact local deep ici report exited `0` with suite `WARN`: 19 total
engines, comprising 11 `PASS`, 3 `WARN`, 0 `FAIL`, 0 `ERROR`, and 5 `SKIP`.
It reported TEM `4.75`, maximum complexity `14` across `194` functions, and
duplication `3.71%`. The `sanitize`, `thread_sanitize`, `build`,
`binary_compat`, and `integration` engines all passed. Native tests were `2/2`
and coverage evidence was `ESTIMATED`.

The ici Make adapter evidence gap noted in the earlier draft was resolved by
[ici PR #154](https://github.com/jihoon22-lee/ici/pull/154): its recorded
`make test` process is now recognized by the sanitizer lookup. The current
candidate's remote toy PR/Pages verification and ici-hosted candidate
acceptance remain pending; the local Quality Zoo result is `15/16 PASS`, with
the only failure caused by unavailable host `clazy` and zero errors in the
other scenarios.

## Follow-up

- Complete the remote toy PR CI, sticky-comment/Pages verification, and
  ici-hosted candidate-to-Quality-Zoo acceptance for the current candidate.
- Keep the residual input-integrity limitation explicit: a cryptographic
  content snapshot would be needed to cover an adversarial change-and-restore
  attack that restores every observed metadata value before the final check.
