# envlens

envlens captures a deterministic, offline inventory of one Python interpreter
and its installed distributions. It is a library and CLI for answering “what is
this environment?” before later compatibility and diff analysis is attempted.

The current `0.1.0` metadata is an unreleased development identity. No envlens
release or tag exists yet.

## Capture a snapshot

Pass an explicit interpreter path. The default is the interpreter running
envlens; command names are not resolved through a shell or `PATH`.

```bash
envlens snapshot --interpreter /path/to/python --output snapshot.json --pretty
python -m envlens snapshot --output -
```

The target runs one fixed `python -c` probe with `shell=False`. It records:

- implementation, version, executable/prefix, platform, machine, and compiler;
- `sysconfig` paths and variables;
- installed distribution name/version, raw `Requires-Python` and
  `Requires-Dist`, entry points, and location; and
- per-distribution metadata errors without discarding the rest of the snapshot.

Snapshots use `envlens.snapshot/v1`; the checked-in schema is
[`schemas/envlens-snapshot-v1.schema.json`](schemas/envlens-snapshot-v1.schema.json).
Keys, distributions, requirements, entry points, and errors have stable ordering.
`captured_at` is deliberately separate from source identity. Tests and reproducible
automation can pass an explicit instant:

```bash
envlens snapshot \
  --captured-at 2026-09-03T00:00:00Z \
  --output snapshot.json
```

## Privacy and failure semantics

Default snapshots replace host and target home-directory paths with
`<USER_HOME>`. Environment variables whose case-insensitive names look
secret-bearing—including tokens, passwords, API/access/private keys,
credentials, authorization, and cookies—retain their names but use
`<REDACTED>` as the value. The CLI intentionally has no unredacted switch.

Missing or non-executable interpreters, timeouts, nonzero probe exits, output
larger than 8 MiB, malformed/duplicate-key/non-finite JSON, invalid protocol
shapes, and write failures exit `2` without a traceback. Distribution-specific
metadata problems remain in that distribution's `errors`; the command succeeds
with `collection.status = "partial"` so healthy packages are still inspectable.

File output is an atomic same-directory replacement with mode `0600`. Symlink or
special-file destinations and replacement of the selected interpreter (including
an existing hardlink alias) are rejected. Stdout is available with `--output -`.

envlens is not an operating-system sandbox. It executes the selected interpreter
as the current user and does not block its network or filesystem access. Inspect
unknown interpreters without secrets and inside an externally enforced disposable
container or VM.

## Deliberate current boundary

This snapshot slice does not import project modules, run entry points, resolve
dependencies, parse wheel tags, compare environments, or decide compatibility.
Those operations require separate policies and evidence and remain future work.
