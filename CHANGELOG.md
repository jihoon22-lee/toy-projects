# Changelog

All notable changes to each product are documented here. Products are
versioned independently and tagged as `{product}/vX.Y.Z`.

## 2026-10-03 — portfolio expansion published

- Add TraceLens: saved strace analysis, source evidence, snapshot/diff, Qt GUI.
- Add TestLens: JUnit/CTest collection, diff/history, CI policies and offline HTML.
- Add ServiceLens: offline systemd unit/drop-in provenance, graph, snapshot/diff.
- Correct BuildScope replay/source bounds, LogLens session input protection,
  DiskMap duplicate cleanup evidence, EnvLens runtime/PEP evaluation and
  AbiLens conservative compatibility.
- Extend existing products with versioned evidence/session/snapshot contracts,
  asynchronous desktop workflows and richer inspection controls.
- Gate all eight products independently; package installed layouts, connect
  release creation directly to verified draft assets, and repair Pages links.

Detailed behavior and release history live in each product's README and CHANGELOG.
The expansion was published as 0.2.1 for the five existing products and 0.1.0 for
TraceLens, TestLens and ServiceLens. The planned 0.2.0 tags were not published.

## 2026-10-03 — final review corrections

- Preserve EnvLens runtime input files and bounded pipe cleanup; incomplete
  snapshot collection remains unknown in check/diff.
- Resolve BuildScope vendor include roots correctly and auto-open saved diffs.
- Save/search with LogLens's applied filter and align AbiLens report I/O budgets.
- Validate ServiceLens scalar values and CTest observation completeness.
- Reconcile all product documentation, historical notes, examples and release
  links; add a required documentation consistency gate.

Product versions are owned by their package/build metadata. A local build or a
change recorded here does not itself publish a release.
