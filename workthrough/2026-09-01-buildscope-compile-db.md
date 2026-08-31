# BuildScope B1 compile-database normalization

## Overview

BuildScope B1 adds the Python compile-database normalization core on
`feat/buildscope-compile-db`. The package metadata is now `0.2.0`; the producer keeps the B0 raw
entry view and emits deterministic `buildscope.snapshot/v2` data. This workthrough records the
implementation, compatibility reader, tests, schema, packaging, and documentation/version
synchronization.

The final public ici v0.7.1 cold verification and local inventory are recorded below. BuildScope PR,
remote CI, sticky-report, and Pages evidence has not run and remains pending.

## Context

B0 established a bounded, shell-free Python producer and a C++20/Qt consumer around the raw
`buildscope.snapshot/v1` contract. B1 needs the same safe ingestion boundary to answer which
compiler configuration was recorded for each translation unit without executing a command or
assuming that a path exists. Field-level v1 compatibility is preserved by the raw keys. B1's native
reader scope is legacy v1 core validation plus v2 bounded/core/cross-entry validation; it is not a
full semantic attestation layer. B2 owns the normalized native model and UI, not basic v2 contract
acceptance.

## Changes made

### Normalized Python contract

The existing B1 Python implementation now:

- accepts `arguments` and `command`, preferring `arguments` when both are present while retaining
  the original raw values;
- tokenizes command-only entries with POSIX or Windows quoting, without a shell, environment/glob/
  command-substitution expansion, compiler execution, or response-file expansion;
- bounds the input compile database at 64 MiB/100,000 entries and bounds command/argv sizes; the
  serialized snapshot output is capped separately at 256 MiB, as is the native reader's serialized
  input;
- emits `normalized` invocation source, compiler, language, standard, ordered define/include,
  sysroot, target, canonical path, and sha256 configuration data;
- preserves duplicate configurations and records `duplicate`, source configuration count, and
  `present`/`missing`/`stale`/`unknown` source status; duplicate scope is source plus configuration,
  while `source_configuration_count` is the unique-configuration count for each source. Source
  aggregation keys combine `command_style` and normalized source path, matching the C++ reader; and
- classifies paths relative to `--project-root` as `project`, known vendor components as `vendor`,
  and other paths as `system`. Native-host existence and mtime checks are best effort; foreign
  platform paths remain unknown rather than being probed through host filesystem semantics. A foreign
  Windows `project_root` is kept in lexical Windows form without host probing, and a dedicated scope
  test covers project/vendor/system classification.

The configuration digest is a deterministic identity for a source's recorded invocation (canonical
argv plus normalized directory/output when present), not a relocation-stable semantic-equivalence
or diff key. Semantic configuration comparison belongs to B4. Command-only normalization is bounded
tokenization; it does not claim that a later consumer can semantically attest the raw command by
re-tokenizing it.

The input path hardening uses final-name `lstat`, a regular-file descriptor opened with no-follow
where supported, and read before/after checks for descriptor/name device/inode/type plus size, mtime,
and ctime. Final input symlinks are rejected. `--output` rejects the database itself and
self/hardlink/symlink aliases.
On POSIX, output uses a no-follow parent-directory fd, exclusive mode-0600 temporary creation,
flush/fsync, and fd-relative rename, which is the path with the atomic race guarantee. The portable
fallback pins the resolved real parent and performs temp/cleanup/replace plus parent-identity and
alias checks there. Before replacement it re-checks the newly created temporary with `lstat` for its
creation-time identity and regular-file type, without resolving a temporary symlink; it does not
claim the same dir-fd atomic race guarantee.

The CLI defaults to v2 but accepts explicit `--schema-version v1|v2`; v1 is a raw compatibility
projection. Metadata and output scanning stops at `--`; POSIX `-o` is recognized only in separated
form, while Windows `/Fo` accepts separated and joined forms. Drive, UNC, and backslash compiler
paths identify Windows style, including GCC paths such as `C:\\MinGW\\bin\\g++.exe`. MSVC option
matching is case-sensitive; `/Fo` and `/Fo:` separated/joined forms are recognized to avoid false
positives from similarly named switches.

The v2 entry retains the B0 raw `arguments`, `command`, `directory`, `file`, and `output` keys.
Consumers that tolerate additive fields can continue using that raw view. The machine-readable
`buildscope-snapshot-v1.schema.json` and `buildscope-snapshot-v2.schema.json` contracts are recorded
in `buildscope/schemas/` and shipped inside the pure wheel under `buildscope/schemas/`. In v2,
`producer.version` uses the public schema's bounded `maxLength` of 1 MiB and the same limit in the
native reader. A strict external v1 consumer still needs a v1 document, so
`fixtures/sample.snapshot.json` remains the v1 consumer smoke input. The v2 normalized model/UI work
is B2, not basic contract acceptance.

### Native compatibility reader

The B1 C++20 reader retains legacy v1 core exactly-one-invocation handling, including compatibility
with empty argv elements and legacy extension keys. Its v2 bounded/core/cross-entry validation
rejects duplicate JSON keys, checks required/unknown fields, field/item bounds, enums, normalized/
state/diagnostics core shapes, `invocation_source`, normalized argv equality when raw `arguments`
are authoritative, include array order, and `entry_index`/duplicate/
`source_configuration_count` consistency across entries. The native reader also rejects a final
snapshot symlink before reading. The word “strict” is intentionally limited
to those v1-core and v2-bounded/core/cross-entry guarantees: command-only normalized argv is not
re-tokenized against the raw command for full semantic attestation. The parser is split across
111-line `contract.cpp`, 125-line `contract_json_guard.cpp`, 382-line `contract_parser.cpp`, and
333-line `contract_parser_v2.cpp` production sources, with no source over 500 lines. The GUI still
displays the raw fields; normalized C++ model/UI presentation remains B2.

### Version and documentation synchronization

- `buildscope/pyproject.toml`, `python/buildscope/__init__.py`, `CMakeLists.txt`, and `ici.toml`
  now consistently identify BuildScope as `0.2.0`.
- `buildscope/README.md` documents the v2 shape, precedence, non-expansion rules, root/scope/status
  caveats, bound and I/O hardening, explicit schema projection, no-install commands, and the B2
  consumer boundary.
- `ROADMAP.md` and the B stream in the master plan mark B1 complete by implemented scope only;
  B2/B3/B4/B5 and ici I3 target-by-target comparison remain pending.
- The prior B0 workthrough remains intact; this file is the separate B1 record.

## Reproduction commands

These commands use an external Python 3.10 interpreter and write only to `/tmp`; BuildScope is
loaded through `PYTHONPATH` and is not installed into the repository.

```bash
repo_root="$(git rev-parse --show-toplevel)"
scratch_root="$(mktemp -d /tmp/buildscope-b1.XXXXXX)"
py310_bin="$(command -v python3.10)"

PYTHONDONTWRITEBYTECODE=1 \
PYTHONPATH="$repo_root/buildscope/python" \
  "$py310_bin" -m buildscope \
  "$repo_root/buildscope/fixtures/compile_commands.json" \
  --project-root "$repo_root/buildscope" \
  --schema-version v2 \
  --output "$scratch_root/buildscope.snapshot.v2.json" --pretty

# Explicit v1 raw compatibility projection.
PYTHONDONTWRITEBYTECODE=1 \
PYTHONPATH="$repo_root/buildscope/python" \
  "$py310_bin" -m buildscope \
  "$repo_root/buildscope/fixtures/compile_commands.json" \
  --schema-version v1 \
  --output "$scratch_root/buildscope.snapshot.v1.json" --pretty

```

The native consumer should be run against both the generated v2 snapshot and the preserved
`fixtures/sample.snapshot.json` v1 compatibility input.

## Verification results (2026-09-01)

The final public `ici v0.7.1` cold verification passed the standard
`sha256sum --check ici.pyz.sha256` asset check and reported suite `WARN`: `13 engines = 11 PASS /
2 WARN / 0 FAIL / 0 ERROR / 0 SKIP`. Total duration was `71.09s` (raw
`71.08794903755188s`); tests were `45/45`, line/function/branch coverage was
`95.2% / 100.0% / 84.3%`, and TEM was `5.00`.
`compile_db` covered `7/7` production units and `16` configurations with `0` failures/warnings;
complexity was max `13` across `140` functions with `0` issues; exception analysis was PASS with
`0` exceptions. Duplication was WARN at `8.8%` (raw `8.77914951989026`), `25` groups, and
`56` findings. The only type
warning was unsupported analysis for `7` C++ sources. External dependency count was `0`.

The HTML report was `489,978` bytes with SHA-256
`538bdde8fae8cc769d212799e80ffeae1e39069662b214b19efb3d35a66f3257` and title
`ici Verification Report — buildscope`. Line inventory is `2,798` total, `2,453` code, and
`345` blank lines across `19` files. Current source line counts are `contract.cpp` 111,
`contract_json_guard.cpp` 125, `contract_parser.cpp` 382, and `contract_parser_v2.cpp` 333.
The Python suite contains `41` tests, and the CTest aggregate is `45`.

These local/public results do not claim BuildScope PR, remote CI, sticky-report, or Pages
completion; those external integration results remain pending.

## Next steps

1. B2 should complete the normalized C++ model and Qt UI around the accepted v2 contract while
   retaining the explicit v1 compatibility path.
2. B3 include explanation, B4 configuration diff, and B5 hybrid integration/release remain future
   work.
3. The ici I3 target-by-target external comparison remains pending.
