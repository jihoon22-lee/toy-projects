# AbiLens

AbiLens is a small, dependency-free C++20 command-line inspector for Linux
ELF build artifacts.  It validates the ELF identification and table bounds
itself, then asks the system `readelf` for bounded, C-locale evidence.  The
input artifact is never loaded or executed.

The product is intentionally narrow:

- report ELF class, endian, type, machine, dynamic/static state, and stripped
  state where section evidence permits it;
- collect `DT_NEEDED`, `DT_RPATH`, `DT_RUNPATH`, and typed GLIBC/GLIBCXX/CXXABI
  version requirements;
- compare a report with another report or compare two binaries;
- apply a small, documented ABI/dependency policy and emit deterministic JSON.

## Build and use

```sh
make all
make check
build/bin/abilens inspect build/bin/abilens
build/bin/abilens inspect --json build/lib/libabilens-fixture.so
build/bin/abilens diff --json first-report.json second-report.json
```

`make OUT=/tmp/abilens-build check` keeps all outputs in the selected tree.
The release tree contains `bin/abilens`, `lib/libabilens.a`, and the
`lib/libabilens-fixture.so` shared fixture.  Coverage, ASan/UBSan, and TSan
use separate `OUT` subtrees and never replace release files:

```sh
make OUT=build coverage
make OUT=build sanitize
make OUT=build thread-sanitize
make clean
```

### The release tree is byte-reproducible

Two clean builds at the same `OUT` produce identical files — the executable, both
libraries, every object, and even the generated `.d` dependency files:

```sh
out=/tmp/abilens-repro
rm -rf "$out" && make --jobs 2 all OUT="$out" && cp -a "$out" /tmp/abilens-a
rm -rf "$out" && make --jobs 2 all OUT="$out" && cp -a "$out" /tmp/abilens-b
diff -rq /tmp/abilens-a /tmp/abilens-b   # no output
```

Measured 2026-09-06 with GCC on Linux x86-64. Building into two *different* `OUT`
trees still yields identical `bin/abilens`, `lib/libabilens.a`,
`lib/libabilens-fixture.so` and every `.o`; only the `.d` files differ, because
they record the absolute output path they were generated for. The `.d` files are
build bookkeeping, not release artifacts.

Ordinary repository CI runs the checksummed public ici release against
`ici.toml`. That gate covers the stable release's static source engines, while
the native CI job owns `make test` and the sanitizer variants. To verify the
newer Make/ELF/integration engine contract with an exact, separately attested
candidate artifact, apply the candidate-only overlay explicitly:

```sh
ICI_CONFIG=ici-candidate.toml /path/to/candidate/ici.pyz verify --profile deep \
  --report --html verify_report.html
```

The overlay does not change the repository's stable ici pin and must not be
used with the public `v0.10.2` artifact, which predates those configuration
keys.

The policy file is a bounded UTF-8 text file with one `key=value` per line.
Supported keys are `expected_class`, `expected_machine`, `max_glibc`,
`max_glibcxx`, `max_cxxabi`, `forbid_absolute_rpath`, and
`forbidden_needed` (a comma-separated list).  Version values are numeric,
for example `max_glibc=2.31`.

```sh
build/bin/abilens inspect --policy policy.conf --json build/bin/abilens
```

An inspection returns 0 for valid evidence and a passing policy, 2 for a
valid ELF that violates policy, 3 for non-ELF/corrupt/unreadable/tool-error
input, and 64 for a command-line or policy-file error.  A diff returns 0
when both inputs can be inspected (even when they differ), and 3 when either
input is not a valid report/ELF.

## Safety and support boundary

The ELF header is read directly with bounded integer arithmetic before
`readelf` is started.  `readelf` is invoked using `fork`/`execvp` with fixed
arguments, no shell, a C locale, a 30-second deadline, and independent 8 MiB
bounds for stdout and stderr.  A timeout, signal, or truncation is incomplete
evidence and is reported as a tool error.

AbiLens currently targets GNU `readelf` output from ELF32/ELF64 Linux files.
Before parsing an artifact, it runs the bounded, shell-free
`readelf --version` capability check and requires a parseable GNU Binutils
version.  The report's `tool` object records the stable name (`GNU readelf`)
and numeric version used for the evidence; non-GNU or unparseable tools fail
closed as `tool-error`.  Extended ELF table counts and unknown byte
orders/classes are reported as unsupported.  ABI names outside the numeric
GLIBC/GLIBCXX/CXXABI forms are not interpreted as floors.  The report schema
is deliberately versioned and self-contained; see
`schemas/abilens-report-v1.schema.json` and
`schemas/abilens-diff-v1.schema.json`.

Each inspection opens the target once and reads the ELF header and program
headers from that descriptor. The same descriptor is passed to `readelf` as a
`/proc/self/fd/<n>` path, so the structural and tool evidence refer to one
opened file rather than independently reopened path names. AbiLens records the
descriptor's device, inode, mode, size, mtime, and ctime and also rechecks the
original path identity; an ordinary path replacement or in-place metadata
change during evidence collection fails closed as a tool error. This requires
Linux `/proc/self/fd`. It is an input-identity guard, not a cryptographic
content snapshot: an adversary that changes bytes and restores every observed
metadata value before the final check is outside this guarantee.

Output directories are protected by an ownership marker.  A non-empty
unowned `OUT`, a symlink, the project root, and `/` are refused by the Make
adapter; `make clean` removes only an explicitly marked output tree.

## Release

`0.1.0` is the first public release. It is published as a native bundle,
`abilens-0.1.0-linux-x86_64.tar.gz`, containing `bin/abilens`,
`lib/libabilens.a` and this README. The `lib/libabilens-fixture.so` that
`make check` builds is an integration fixture and is not shipped.

Every release also publishes the deep ici report it was gated on
(`abilens-ici-deep.json` / `.html`), a provenance record naming the exact
`main` commit it was built from (`abilens-provenance.json`), and `SHA256SUMS`
covering the other four assets:

```sh
sha256sum --check SHA256SUMS
```

Releases are cut by pushing an annotated `abilens-v<version>` tag at an exact
`main` commit whose Merge Gate is green. The workflow refuses before building
if the version in `ici.toml`, the compiled constant reported by
`abilens --version`, and the CHANGELOG entry do not agree.
