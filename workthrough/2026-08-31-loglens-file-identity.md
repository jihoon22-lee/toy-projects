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

- `loglens/tests/test_log_source.cpp`
  - Replaced shared `/tmp` fixture names with per-test temporary directories.
  - Added contract checks for identity equality, structured chunk fields,
    append/truncation/rename/unlink transitions, unsupported directories, and
    adapter compatibility.
- `workthrough/2026-08-31-loglens-file-identity.md`
  - Recorded the test contract and the source-versus-GUI L1 boundary.

## Verification status

Focused `test_log_source` builds/tests completed successfully in isolated
directories for both available Qt majors:

```text
/tmp/loglens-identity-qt6 — Qt 6.10.2 — 1/1 test passed
/tmp/loglens-identity-qt5 — Qt 5.15.18 — 1/1 test passed
```

This is focused evidence only; no full-suite or packaging verification claim is
made here.
