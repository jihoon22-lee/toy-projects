# loglens file identity test contract

## Overview

The `loglens` source contract now carries stable file identity and structured
poll outcomes. This work adds filesystem-backed tests for the source layer so
ordinary appends, in-place truncation, atomic replacement, temporary absence,
and unsupported inputs remain distinguishable.

## Test contract

- `FileIdentity{device, file, valid}` has value equality and inequality.
- `SourceChange` distinguishes `None`, `Truncated`, and `Replaced`.
- `SourceErrorKind` exposes the structured error vocabulary, and
  `SourceChunk::ok()` reflects `SourceErrorKind::None`.
- The no-argument `FileTailer::pollChunk()` returns bytes, generation, change,
  identity, and the post-read position. The existing bool adapters remain
  usable, including their compatible `cannot stat` error text.
- A unique RAII temporary directory isolates every test. Appending preserves
  identity and generation while advancing position; truncation increments
  generation and reads from zero; same-directory rename replacement is
  detected for smaller, equal-size, and larger files; unlink reports retryable
  `Missing` without moving the cursor, then recreation is a `Replaced` source
  and is read from the beginning.
- Directories are rejected as fatal `UnsupportedFileType` inputs.

## L1 split

This milestone is source identity only: the tailer reports enough information
for its caller to make a safe generation decision. Follow recovery in the GUI
(retryable missing state, stopped/follow resume interaction, and user-facing
status) remains a later L1 follow-up and is intentionally not covered by these
core tests.

## Changes made

- `loglens/include/loglens/log_source.hpp`
  - Added `FileIdentity`, structured source change/error enums, and the rich
    `SourceChunk` result while retaining the bool adapter API.
- `loglens/src/log_source.cpp`
  - Implemented POSIX open-handle identity/stat checks, bounded reads, and
    platform-separated fallback behavior for Windows.
- `loglens/tests/test_log_source.cpp`
  - Replaced shared `/tmp` fixture names with per-test temporary directories.
  - Added contract checks for identity equality, structured chunk fields,
    append/truncation/rename/unlink transitions, unsupported directories, and
    adapter compatibility.
- `README.md`
  - Documented the file identity contract and the follow-up boundary.
- `docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md`
  - Recorded the completed loglens L1 source-identity slice.
- `workthrough/2026-08-31-loglens-file-identity.md`
  - Recorded the source-versus-GUI L1 boundary and the complete verification
    evidence.

## Verification status

The complete Release CMake build and CTest suite passed in isolated
directories for both available Qt majors:

```text
/tmp/loglens-identity-full-qt6 — Qt 6.10.2 — CTest 10/10 passed
/tmp/loglens-identity-full-qt5 — Qt 5.15.18 — CTest 10/10 passed
```

The real GUI binary also stayed alive for the full eight-second headless smoke
interval against `tests/data/sample.log` in both builds:

```text
QT_QPA_PLATFORM=offscreen /tmp/loglens-identity-full-qt6/src/gui/loglens-gui \
  /home/jihoon/projects/.worktrees/toy-loglens-identity/loglens/tests/data/sample.log
  — PASS (alive after 8 seconds; Qt 6.10.2)
QT_QPA_PLATFORM=offscreen /tmp/loglens-identity-full-qt5/src/gui/loglens-gui \
  /home/jihoon/projects/.worktrees/toy-loglens-identity/loglens/tests/data/sample.log
  — PASS (alive after 8 seconds; Qt 5.15.18)
```

The public `ici 0.6.0` release asset (`/home/jihoon/projects/ici/dist/ici.pyz`)
verified the project with `Suite: PASS`, `TEM: 4.86 / 5.0`, and maximum
complexity `12` (limit `15`). The 12-engine summary was `10 PASS, 0 WARN,
0 FAIL, 0 ERROR, 2 SKIP` (the C++ type-check and Python dead-code engines are
not applicable to this C++ project). Regenerating the standalone HTML report at
`/tmp/loglens-identity-ici-rerun.html` produced `265742` bytes; a direct scan
found `0` external `src`/`href` references.
