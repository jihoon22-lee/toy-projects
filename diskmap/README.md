# DiskMap

DiskMap is a disk usage explorer for Linux. A Qt-free core does the scanning and
analysis, a small console tool exposes it, and a Qt Widgets shell adds the
treemap and the review workflows. It is built with qmake and verified against
both Qt 5.15 and Qt 6.

The scan is deliberately conservative about what it will claim. Directory
entries carry a `FileIdentity` of device and file id rather than a path, symlinked
directories are not followed unless asked, and filesystem boundaries are not
crossed unless asked. Anything the scan could not resolve is reported as
uncertain instead of being folded into a total.

## Binaries

| Target | Kind | What it is |
|---|---|---|
| `diskmap` | console | Qt-free CLI over the scanner, snapshots, and duplicate evidence |
| `diskmap-gui` | Qt Widgets app | treemap explorer with the cleanup, trash, and storage review workflows |
| `diskmap_core` | static library | scanner, treemap, snapshot, duplicate, cleanup, and trash logic |
| `diskmap_gui` | static library | widget layer, so the GUI is unit-tested like the core |

Cleanup and trash are GUI-only on purpose. They act on real files, so they stay
behind an interface that shows what will be touched before anything moves.

## Build and test

```sh
mkdir -p build/gui-qt6 && cd build/gui-qt6
/usr/bin/qmake6 ../../diskmap.pro && make -j"$(nproc)"
QT_QPA_PLATFORM=offscreen make check
```

Substitute `/usr/bin/qmake` for the Qt 5.15 leg. `make check` runs all 17 test
targets; each test binary has its own `main()`, which is why `tests/tests.pro`
lists one project per file.

The whole project is also an ici verification target:

```sh
QT_QPA_PLATFORM=offscreen ../../ici/dist/ici.pyz verify --report
```

`ici.toml` pins the thresholds explicitly rather than inheriting a developer's
`~/.config/ici/ici.toml`, so a local run and a CI run compare the same numbers.

## CLI

```text
Usage: diskmap <path> [options]
   or: diskmap --load-snapshot FILE [options]
  --max-depth N       limit scan traversal depth
  --follow-symlinks   follow symlinked directories
  --min-size BYTES    skip files smaller than BYTES
  --one-file-system   do not cross filesystem boundaries
  --exclude GLOB      skip matching entries (repeatable)
  --depth N           limit printed tree depth
  --top N             show the N largest files (default 10)
  --json              emit the tree as JSON instead of text
  --save-snapshot FILE save the scan as a bounded snapshot
  --load-snapshot FILE inspect a saved snapshot without scanning
  --compare-snapshot FILE compare the scan with a saved snapshot
  --duplicates        inspect duplicate evidence (review-only)
  --help              show this message
```

A snapshot comparison classifies each entry as added, removed, grown, shrunk,
moved, or uncertain, and reports whether the comparison as a whole was
uncertain. Duplicate inspection is review-only: it reports evidence and never
deletes anything.

Malformed filesystem names cannot corrupt a report. Invalid UTF-8 bytes in a
path are escaped as `\u00XX`, so `--json` output stays valid JSON for any name
POSIX permits.

## Status and history

DiskMap is `0.1.0`/`Unreleased`. What has been implemented, in what order, and
on what evidence is recorded in [ROADMAP.md](../ROADMAP.md) and
[CHANGELOG.md](../CHANGELOG.md); individual work is written up under
[workthrough/](../workthrough/).
