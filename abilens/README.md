# AbiLens

AbiLens is a C++20 command-line inspector for Linux
ELF build artifacts.  It validates the ELF identification and table bounds
itself, then parses program headers, dynamic tags, version requirements and
string tables directly from the input bytes.  The artifact is never loaded or
executed, and no external tool is invoked. The default build has no third-party
runtime dependency; optional DWARF analysis links elfutils libdw/libelf.

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
versioned and self-contained; see [`report v2`](schemas/abilens-report-v2.schema.json) and
[`diff v2`](schemas/abilens-diff-v2.schema.json). Saved report v1 inputs remain readable
with missing evidence represented as unknown.

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

## Evidence and compatibility

Report v2 preserves symbol binding, visibility, type, size and default-version
status alongside the familiar `name@version` identities. Local/hidden/internal
symbols are excluded from the exported surface. SONAME, program interpreter and
GNU build ID are recorded. Loader path order, repetitions and empty components
are preserved. A missing symbol-count table leaves symbols unknown.

Diff v2 emits `compatibility: compatible|incompatible|unknown` plus the legacy
boolean, which is true only for `compatible`. Export removal, data/TLS size or
symbol type changes, lost default versions, SONAME changes and raised ABI floors
are incompatible. Binding/visibility changes, missing evidence and unresolved
loader changes are unknown. Function code-size
changes and build ID changes alone do not establish an ABI break. Compatibility
covers the observed axes; it does not prove source-level C++ compatibility.

`--fail-on incompatible`, `unknown`, or `changed` select CI failure criteria
(exit 2); `never` reports differences without failing. The default preserves the
previous diff exit behavior. Invalid inputs still exit 3 and usage errors 64.

## Offline loader candidates

```sh
build/bin/abilens inspect --json --sysroot /images/rootfs \
  --origin /opt/app/lib --library-path /opt/dependencies/lib app.so
```

Resolution reads only a supplied rootfs. Linux `openat2(RESOLVE_IN_ROOT)` confines
absolute symlinks and path traversal; an unsupported kernel leaves candidates
unresolved. `$ORIGIN` requires the explicit target directory. RUNPATH (or RPATH
when absent), explicit library directories and standard target directories are
checked in order. Candidates must match ELF class, byte order and machine. No `ldd`, loader,
or target binary is executed. This is candidate evidence: `ld.so.cache`, hwcaps,
environment overrides and transitive RPATH are not simulated.

## Optional DWARF layout analysis

```sh
# Debian/Ubuntu build dependency: libdw-dev, pkg-config
make OUT=build/dwarf WITH_DWARF=1 check
build/dwarf/bin/abilens inspect --dwarf --json library.so
build/dwarf/bin/abilens diff --dwarf --fail-on unknown before.so after.so
```

The libdw reader records named aggregate sizes, members, referenced type names,
member offsets and DWARF4/5 bit fields, including separate DWARF4 type units. Analysis stops at 100,000 DIE visits, 10,000
layouts, 4 MiB of layout text, depth 128 or five seconds; input is limited to 128 MiB. Compressed debug
sections are refused. Unsupported location expressions, missing debug data and
limits remain explicit. Changed type layouts are reported as unknown impact
because public API reachability is not inferred. The basic build reports
`unavailable` when `--dwarf` is requested. No debuginfod or external debug lookup
is performed.

## Install and release

```sh
make PREFIX=/tmp/abilens-install install
/tmp/abilens-install/bin/abilens --version
```

The current development version is 0.2.0. Releases use `abilens/v<version>` and
contain the installed `bin`, `lib`, public headers, schemas and documentation.
The test fixture library is not shipped. Native bundles use system runtime
libraries; `RUNTIME-DEPENDENCIES.txt` describes those required by the build.
Checksums and provenance are attached before a draft release is published.
