# ServiceLens

ServiceLens 0.1.0 explains Linux systemd configuration **on disk**, including unit selection,
drop-ins, setting provenance, environment files, explicit dependencies and offline differences.
It never starts services, executes unit commands, calls generators, contacts D-Bus, or claims that
the running daemon has reloaded the files it reads.

## Install and use

Python 3.10+ on Linux. There are no runtime package dependencies.

```bash
python -m pip install .
servicelens inspect worker@blue.service --root examples/rootfs
servicelens explain worker@blue.service --root examples/rootfs --key Service.ExecStart
servicelens graph worker@blue.service --root examples/rootfs --depth 3 --format dot
servicelens snapshot worker@blue.service --root examples/rootfs --output /tmp/worker-before.json
servicelens snapshot worker@blue.service --root examples/rootfs --output /tmp/worker-after.json
servicelens diff /tmp/worker-before.json /tmp/worker-after.json
servicelens check /tmp/worker-after.json --fail-on error,unknown
```

`--root /` (the default) inspects the local machine's files. `--unit-path /custom/units`
replaces the ordered system load path; repeat it from highest to lowest priority.
Paths inside the image remain image-absolute, never host absolute. `--depth`, `--max-files`,
`--max-bytes`, and `--max-nodes` bound capture. The Python API exposes all limits.
Depth means the number of dependency hops after the requested unit (minimum 1).

Text output leads with source, coverage and diagnostics. `--format json` preserves machine-readable
information. `explain --key Section.Directive` shows every assignment, its source line, and its
replace/append/reset/ignored action. `graph` emits Graphviz DOT with distinct ordering edges;
Graphviz is not needed to generate it. Missing referenced units produce partial evidence.

Inspect/explain/graph/snapshot return 0 when a report was produced, including a partial report.
`check` returns 1 for selected diagnostic categories (default: error). `diff --fail-on changed,unknown`
returns 1 for selected comparison categories. Invalid CLI, input or output errors return 2.
Unknown policy names and format names are rejected. Use `check --fail-on error,unknown` for strict CI.

## Supported semantics and explicit boundaries

The documented contract is `systemd-255-subset-v1`, based on the systemd unit, syntax, service
and exec interfaces. A higher systemd version is not automatically declared supported.

* System unit selection uses ordered control/transient/generator/etc/run/vendor paths. Generators
  are not run. Existing generated files are ordinary input evidence.
* Drop-ins support lexical filename order, same-name shadowing, unit/template/instance, dash-prefix
  and type scopes. Alias names discovered in the configured load paths contribute drop-ins.
  `/dev/null` and empty-file masks, aliases, symlink cycles and input failures are visible.
* The parser preserves physical line ranges, repeated assignments, sections, escapes and continuation.
  It is a systemd parser, not a general shell or INI evaluator.
* Typed settings include common service scalar settings, command lists with empty resets,
  environment lists/maps and explicit dependency unions. Empty dependency assignments do not erase
  earlier dependencies. Unknown directives retain their provenance and produce `unknown` diagnostics.
* Unit-derived `%n`, `%N`, `%p`, `%P`, `%i`, `%I` and `%%` specifiers are supported. Other specifiers
  require context and remain unknown. Templates may be instantiated by their requested name.
* Exec commands are tokenized without execution. Prefix flags are preserved; shell prefix `|`,
  `$VAR` word splitting and unknown environment expansions remain unknown. Known `${VAR}` is
  expanded from static pre-unset environment. The executable's presence is an observation at capture
  time, not a guarantee that service namespace/mount/security conditions allow execution.
* EnvironmentFile supports optional files, bounded wildcards, UTF-8, multiline quoting, escaping,
  variable overrides and resets. No command or shell variable expansion occurs. EnvironmentFile
  overrides Environment; UnsetEnvironment is applied last. Manager/PAM environments and files that
  might be generated immediately before execution cannot be reconstructed statically.
* Dependency graphs contain explicit declarations and `.wants`/`.requires` links. They deliberately
  exclude implicit/default/runtime dependencies by default. Opt in with
  `--include-default-dependencies` to add the supported system-service sysinit,
  basic, shutdown and Type=dbus edges. Synthetic origins identify the exact
  systemd-255 rule. `DefaultDependencies=no` disables the default edges;
  Type=dbus dependencies remain. Other implicit/runtime edges stay uncollected,
  so graphs do not claim complete startup ordering or health.
  Explicit ordering cycles are reported separately from legal requirement cycles.
  `[Install]` declarations describe installation intent, not active or enabled daemon state.

[Official service defaults](https://github.com/systemd/systemd/blob/v255/man/systemd.service.xml),
[official unit semantics](https://github.com/systemd/systemd/blob/v255/man/systemd.unit.xml) and
[official execution/environment semantics](https://github.com/systemd/systemd/blob/v255/man/systemd.exec.xml)
are the reference. Unsupported evidence is never converted into a successful guess.

## Privacy, snapshots, and filesystem safety

All outputs are redacted by default. Environment values, command arguments, unknown setting values,
non-structural scalar values and superseded assignments are masked. Names, paths, source locations,
executable paths and dependency names remain visible. They may themselves identify private systems:
review these identifiers before sharing. Credential file contents are never read.

`--show-values` explicitly produces an unredacted report or snapshot for local diagnosis. Both the
CLI and Python API are redacted by default. Masked values have no secret hash. Diff reports their
values as incomparable, even when both display `<redacted>`. Mixed redacted/unredacted comparisons
never export raw peer values. Thus identical redacted captures may still have unknown comparisons;
this is not a claim that secrets changed.

`servicelens.snapshot/v1` stores selected and shadowed sources, final typed settings, source ledger,
commands, environment provenance, explicit edges, limits and diagnostics. `servicelens.diff/v1`
distinguishes value, origin, structure and evidence changes. List order is preserved. JSON loading
rejects duplicate keys, unknown schemas/properties, invalid types, inconsistent partial flags and
oversized documents. Schemas are installed with the wheel and checked against the implementation.

All input files are read through a root directory descriptor. Absolute image links are resolved in
the image; traversal outside the root, link loops, special files, excessive bytes/entries/directives,
and inputs changing while read produce explicit diagnostics. Reads follow bounded symlinks then
open directory components without following newly inserted links. This is not an atomic filesystem
snapshot: changes between separate file reads can still affect the collected configuration. Use a
read-only filesystem snapshot for a consistent image.

Snapshot output uses a mode-0600 temporary file, fsync and atomic replacement. Output aliases of
collected input files, including hardlinks and symlinks, are refused by the CLI. Existing snapshot
files are not mutated by diff/check. No input file content hashes are published.

## Library

```python
from servicelens import Limits, inspect, diff, check, load, save

snapshot = inspect("worker@blue.service", root="examples/rootfs", limits=Limits(depth=2))
assert snapshot["redacted"]
print(check(snapshot))
```

`inspect` returns a plain JSON-compatible snapshot. `save` validates its schema before writing;
library callers should pass their input paths through `inputs=` to enforce output collision checks.

## Development and packaging

```bash
uv sync --group dev
uv run pytest
uv run ruff check .
uv run ruff format --check .
uv run mypy src
uv build
```

Wheel and sdist are independent of other repository products. Tests use temporary rootfs fixtures;
they do not need root, a running systemd daemon, network access or service manipulation. Optional
`systemd-analyze verify` tests run only against benign fixture commands and do not activate units;
  enable them with `SERVICELENS_SYSTEMD_VERIFY=1 uv run pytest tests/test_systemd_optional.py`.
Initial release tag: `servicelens/v0.1.0`. CI should run these commands from `servicelens/` and publish
`dist/servicelens-0.1.0-py3-none-any.whl` and `dist/servicelens-0.1.0.tar.gz` when explicitly released.
