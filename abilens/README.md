# AbiLens

AbiLens is a small, dependency-free C++20 command-line inspector for Linux
ELF build artifacts.  It validates the ELF identification and table bounds
itself, then parses program headers, dynamic tags, version requirements and
string tables directly from the input bytes.  The artifact is never loaded or
executed, and no external tool is invoked.

The product is intentionally narrow:

- report ELF class, endian, type, machine, dynamic/static state, and stripped
  state where section evidence permits it;
- collect `DT_NEEDED`, `DT_RPATH`, `DT_RUNPATH`, typed GLIBC/GLIBCXX/CXXABI
  version requirements, and the defined dynamic-symbol export surface;
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

The policy file is a bounded UTF-8 text file with one `key=value` per line.
Supported keys are `expected_class`, `expected_machine`, `max_glibc`,
`max_glibcxx`, `max_cxxabi`, `forbid_absolute_rpath`, `forbid_rpath`,
`forbid_runpath`, `forbid_stripped`, `forbidden_needed`,
`forbidden_symbols`, and `required_symbols` (the three `forbidden_*`/
`required_*` keys take comma-separated lists).  Symbol rules compare against
the report's exported identities: a rule containing `@`, such as
`required_symbols=init@MYAPP_1.0`, pins one version definition exactly, while
a bare name such as `forbidden_symbols=debug_dump` matches that symbol under
any version (or none).  Rules fail closed when the evidence is missing:
`forbid_stripped=true` is a violation when strippedness is unknown (no
section headers), and symbol rules are a violation against a saved report
that predates exported-symbol evidence.  Version values are numeric, for
example `max_glibc=2.31`.

```sh
build/bin/abilens inspect --policy policy.conf --json build/bin/abilens
```

An inspection returns 0 for valid evidence and a passing policy, 2 for a
valid ELF that violates policy, 3 for non-ELF/corrupt/unreadable/tool-error
input, and 64 for a command-line or policy-file error.  A diff returns 0
when both inputs can be inspected (even when they differ), and 3 when either
input is not a valid report/ELF.

## Safety and support boundary

The ELF header is read directly with bounded integer arithmetic.  Evidence
then comes from the file's own structures: `PT_DYNAMIC` dynamic entries, the
dynamic string table translated through `PT_LOAD` segments, and version
requirement records.  Because segment data is authoritative, `DT_NEEDED`,
`DT_RPATH`, `DT_RUNPATH`, and GLIBC/GLIBCXX/CXXABI requirements are still
recovered when section headers are stripped, as is the exported dynamic
symbol set (from `DT_SYMTAB` bounded by `DT_HASH`/`DT_GNU_HASH`); sections are
only consulted for the `.symtab` strippedness check.  When the binary carries
`DT_VERDEF`/`DT_VERSYM` version definitions, exported symbols are reported
with their version identity (`name@version`) — a renamed or rebased version
node therefore shows as a removed/added symbol in the diff.  Dynamic symbols
whose mangled name starts with `_ZTV` additionally populate the report's
`vtables` list and the diff's `vtables` set: Itanium-ABI vtables are the
runtime contract downstream subclasses bind to, so their gain or loss is
surfaced separately (as `+/- VTABLE:` lines in text diffs) rather than
buried in the flat symbol set.  Version names are decoration on symbols the
report already has: a malformed `DT_VERDEF`/`DT_VERSYM` table keeps the
input valid, reports unqualified symbol names, and records a
`symbol versions unavailable` diagnostic.  A saved report from before
version identities existed (no `vtables` field) can still be diffed: symbols
are compared by name, the vtable axis is skipped, and a diagnostic says so,
instead of every versioned symbol reading as removed and re-added.  All offsets,
counts,
sizes, and string
indices are bounds-checked; out-of-file tables and oversized structures fail
closed.

AbiLens targets ELF32/ELF64 little- and big-endian Linux files.  Inputs larger
than 256 MiB are refused as a tool error rather than parsed partially.  The
report's `tool` object records the analyzer identity (`abilens`) and its
version.  Extended ELF table counts and unknown byte orders/classes are
reported as unsupported.  ABI names outside the numeric GLIBC/GLIBCXX/CXXABI
forms are not interpreted as floors.  The report schema is deliberately
versioned and self-contained; see `schemas/abilens-report-v1.schema.json` and
`schemas/abilens-diff-v1.schema.json`.

Each inspection opens the target once and reads every byte through that
single descriptor, so the evidence refers to one opened file rather than an
independently reopened path name.  AbiLens records the descriptor's device,
inode, mode, size, mtime, and ctime and rechecks them after the read; an
ordinary path replacement or in-place change during evidence collection fails
closed as a tool error.  This is an input-identity guard, not a cryptographic
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

Every release publishes a `SHA256SUMS` covering its assets:

```sh
sha256sum --check SHA256SUMS
```

Releases are cut by pushing an annotated `abilens/v<version>` tag at a `main`
commit whose CI is green.
