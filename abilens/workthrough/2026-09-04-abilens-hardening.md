# AbiLens bounded ELF inspection and parser hardening — 2026-09-04

## Overview

AbiLens now has a bounded GNU `readelf` capability contract, deterministic
report evidence, fail-closed report parsing, and protected Make output trees.
The implementation remains a C++20/Linux product with no Qt, Python runtime,
or native third-party dependency.

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
- Product metadata remains `0.1.0` / Unreleased.

## Verification Results

All commands were run from the AbiLens project root:

```text
make --no-print-directory --jobs=4 OUT=build check coverage sanitize thread-sanitize
exit 0

test_parser: PASS
test_integration: PASS
test_clean: PASS

jsonschema Draft 2020-12 validation:
  build/reports/abilens.json: 0 errors
  build/reports/diff.json: 0 errors

Release reproducibility:
  abilens: b181ab4d272ad16ca928dd03bebfacffaf4bf975a1721c788dc5bd4362349b34
  libabilens.a: 66c25c117d6ab9b60ee307becb711b896052db37ecb07576d01e5c15c54397b0
  libabilens-fixture.so: 12c2095daa03661171267319e51434a34483928cfabfdd02afd7e2c4034fa968
  identical after clean and rebuild
```

The requested local deep ici run completed the required build, binary
compatibility, and integration checks. Its sanitizer adapters reached the
tests but the current ici Make adapter does not expose its recorded
`make test` process under the `ctest`/`make check` evidence name expected
by the sanitizer engine, so those two ici engine results remain an upstream
adapter evidence limitation. Direct AbiLens sanitizer and TSan Make targets
pass independently.

## Follow-up

- Align ici's Make adapter process-evidence name with its sanitizer lookup
  (`make test` versus `make check`) in the ici project; this is outside the
  AbiLens-only change boundary.
