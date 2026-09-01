# BuildScope B5 release-candidate documentation

## Overview

Documented the local BuildScope 0.5.0 B5 release-candidate groundwork on
`feat/buildscope-b5-release`. The documentation records what is implemented and
locally verified, while keeping the product release boundary explicit: no B5 PR,
remote run, Pages publication, annotated tag, or GitHub Release exists yet.

## Context

B4 implementation and its PR, hosted-report, and merged-main evidence remain
historical shipped evidence on `main`. B5 adds the packaging and hand-off pieces
needed to turn the Python producer output into a distributable native product:

- `buildscope/tools/build_standalone.py` validates the public version surfaces,
  schema inventory, deterministic archive metadata, and atomic installation.
- CMake install rules provide native CLI/GUI binaries together with docs,
  schemas, and examples under the release bundle layout.
- The CMake and qmake examples, plus `buildscope/docs/quickstart.md`, show the
  Python `buildscope.pyz`/wheel → JSON → native CLI/GUI flow and explain why
  qmake capture needs an explicit compile-database producer such as Bear.

## Local evidence

The focused Python suite is `87/87` on Python 3.10 and 3.14; the 3.10 interpreter
is `/tmp/toy-b5-py310/bin/python`. Standalone tests cover execution, schema
inventory, reproducibility, and symlink refusal. The CMake/qmake examples were
validated with their checked-in compile databases, and qmake builds were run
with Qt 5.15.18 and Qt 6.10.2. The local ici v0.10.0 deep/no-cache report is
`Suite WARN` with 14 engines (`11 PASS / 3 WARN / 0 FAIL / 0 ERROR / 0 SKIP`),
`96/96` tests, coverage `93.4% / 99.0% / 76.7%` (line/function/branch), TEM
`4.95`, compile DB `12/12` production units across `27` configurations, and
zero compile-database issues. Qt code generation was exact for 3 inputs:
MOC 1, UIC 1, RCC 1, with 12 Qt6 compile units.

The report identity records source commit
`190fa58e27ee34bfbfda9488311a526f4462487d`. An initial verification attempt
found an unrelated `buildscope/.venv` left by a Python 3.14 tool run; ici
correctly reported missing pytest/coverage from that incomplete environment.
Removing the generated environment and rerunning with the explicit Python 3.10
tool environment produced the successful uncached evidence above.

The report JSON is 2,873,207 bytes with SHA-256
`ea5fce118e6edad8fa5af24c821663e4290805570377e9b7c190de8da1029612`. The
Zero-CDN HTML is 1,264,867 bytes with SHA-256
`4e12e77ee11f98b2d5bb146bd3d08252b088083726e6f11235e25a449471a565`, exact
title `ici Verification Report — buildscope`, and zero external references.
The public ici asset is pinned by the literal SHA-256
`6d5f8c008b3b5393a61b2c1a418124eb66393c9eaab0abbb7d1c7922162bed9b`; local
`clang-tidy` and `clazy` are unavailable, so their tool-backed gate remains
pending.

## Release boundary

`.github/workflows/buildscope-release.yml` defines, but has not executed, the
release contract: an annotated `buildscope-vX.Y.Z` tag must point at exact
`origin/main` with a green `Merge Gate`; Python 3.10/3.14 and Qt 5/6 matrix
legs must pass generated MOC/UIC/RCC path checks and headless smoke; the wheel,
pyz, and native CLI must exchange byte-identical snapshots; and the Qt6 Linux
x86_64 bundle must contain its runtime docs, schemas, and examples. The asset
set is `buildscope.pyz`, its sidecar checksum, the pure wheel, sdist, deep JSON
and HTML reports, provenance JSON, the Linux bundle, and `SHA256SUMS`.
The workflow is intentionally tag-only: it revalidates the remote annotated
tag immediately before and after publication, fails on unmatched paths, and
audits that the final release exposes exactly those nine names with matching
sizes and GitHub API SHA-256 digests.
The later operational CI/release contract pins corrective public ici `v0.10.2`
at SHA-256 `8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4`;
the v0.10.0 values above remain the dated local measurement rather than the
current dependency contract.
Both the exact-main check-run poll and the final public-release audit treat a
transient GitHub API failure as a bounded retry, while a missing successful
Merge Gate or incomplete final asset set still fails closed after the deadline.

Until that workflow runs successfully and the PR/remote/tag/release artifacts
exist, B5 acceptance and the 0.5.0 product release remain pending.

## Documentation scope

This documentation-only follow-up changes the root and package README files,
CHANGELOG, ROADMAP, the master plan, the handover, and this workthrough. No
BuildScope source, tests, CI workflow, or release workflow was changed by this
commit.
