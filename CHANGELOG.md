# Changelog

All notable changes to each product are documented here. Products are
versioned independently and tagged as `{product}/vX.Y.Z`.

## Unreleased

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

Detailed behavior and validation live in each product's README and CHANGELOG.
Existing products move to a 0.2.0 development checkpoint; new products begin at
0.1.0. The release manifest retains the last published versions until release. No release is published by a local build.
