# BuildScope 0.5.0 pre-tag release readiness

## Overview

This work records the release-readiness boundary for BuildScope `0.5.0`. The checkpoint combines
include explanation, semantic compile-configuration diff, and reproducible Python/C++/Qt hybrid
packaging. The implementation, remote acceptance, exact-main CI, trusted `main` Pages publication,
and documentation synchronization are complete; the annotated tag, GitHub Release, and final
public-asset audit are deliberately still pending.

This document is pre-tag release-ready documentation. It does not claim that
`buildscope-v0.5.0` exists, that a GitHub Release has been published, or that stable release assets
are already available.

## Context

BuildScope had accumulated a historical `0.4.0` include-explanation candidate and a `0.5.0`
configuration-diff/release-candidate implementation. The release decision was intentionally held
until the hybrid boundary had evidence from the actual remote workflow: Python `3.10/3.14`, Qt
`5/6`, generated Qt code, offscreen GUI tests, native handoff, deep ici verification, report
publication, and the release-contract checks.

The release boundary is now the first usable BuildScope checkpoint containing:

- historical include explanation (not a separate stable `0.4.0` release);
- relocation-aware semantic comparison of two raw `compile_commands.json` inputs;
- deterministic standalone `buildscope.pyz`, pure wheel/sdist, native CLI/GUI installation, and
  CMake/qmake examples;
- Python artifact -> JSON -> C++/Qt consumer handoff;
- public ici `v0.10.2`, pinned by literal SHA-256
  `8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4`.

The version decision is deliberately conservative. All BuildScope version surfaces already report
`0.5.0`, and this release-prep documentation does not bump them. An ici dependency pin, a CI/Pages
repair, or a documentation-only change is not a BuildScope product release and must not produce a
new product version.

## Documentation and evidence synchronization

The release state is synchronized across the following full paths:

| Full path | Synchronized information |
|---|---|
| `/home/jihoon/projects/toy-projects/CHANGELOG.md` | Promotes the completed BuildScope checkpoint to the dated `[0.5.0]` entry, preserves historical candidate notes, records PR #38/#39 evidence, and keeps the top-level `Unreleased` section for future work. |
| `/home/jihoon/projects/toy-projects/README.md` | Describes BuildScope as release-ready, records the public ici pin and remote validation boundary, and distinguishes the remaining public tag/release from completed implementation evidence. |
| `/home/jihoon/projects/toy-projects/ROADMAP.md` | Changes the release-boundary checklist from local groundwork to remotely accepted readiness, records the exact-main and Pages evidence, and leaves only tag/release/post-release audit unchecked. |
| `/home/jihoon/projects/toy-projects/buildscope/README.md` | Documents the product boundary, supported matrix, release contract, exact PR/main evidence, stable Pages bytes/digests, and the fact that no public stable release is claimed yet. |
| `/home/jihoon/projects/toy-projects/docs/superpowers/2026-08-30-handover.md` | Updates the handoff state so a new session sees implementation, remote acceptance, exact-main CI, and trusted main Pages as complete, with the public release audit pending. |
| `/home/jihoon/projects/toy-projects/docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md` | Keeps the portfolio release cadence and checkpoint policy aligned: B-stage progress, ici pins, and CI-only changes do not automatically bump a product version; `0.5.0` is the single usable BuildScope boundary. |
| `/home/jihoon/projects/toy-projects/workthrough/2026-09-02-buildscope-0.5.0-release-prep.md` | This pre-tag record of context, changes, evidence, verification, and remaining release gates. |

Supporting workflow and checker surfaces referenced by this documentation are:

- `/home/jihoon/projects/toy-projects/.github/workflows/buildscope-release.yml`
- `/home/jihoon/projects/toy-projects/ci/check_buildscope_b5_report.py`
- `/home/jihoon/projects/toy-projects/.github/workflows/ci.yml`

## Changes made

### Release status and historical records

- Historical B3 include explanation remains documented as part of the `0.5.0` boundary rather than
  being published as a separate stable `0.4.0` product.
- Semantic configuration diff remains the user-visible configuration-drift feature in this
  checkpoint.
- The earlier local ici `v0.10.0`/candidate measurements remain explicitly historical. The
  accepted remote boundary uses the public ici `v0.10.2` artifact and its literal checksum.
- `Unreleased` remains available for post-`0.5.0` work; the current release-prep docs do not absorb
  future features into the stable entry.

### Release-contract boundary

The tag-only release workflow already enforces the following before publication:

- annotated `buildscope-vX.Y.Z` tag resolves to the exact remote `main` commit;
- the exact target commit has a successful `Merge Gate`;
- Python `3.10` and `3.14` test/lint/type-quality legs;
- Qt `5` and `6` Release CMake/CTest legs, generated MOC/UIC/RCC path checks, and offscreen GUI
  execution;
- pure `py3-none-any` wheel and sdist checks;
- reproducible standalone pyz generation;
- byte-identical wheel/pyz snapshots consumed by the native CLI;
- Qt6 Linux `x86_64` installed bundle checks;
- checksummed ici `v0.10.2` download, sidecar, literal, downloaded-byte, and API digest checks;
- B5 deep JSON/HTML report contract and deterministic `SHA256SUMS` generation;
- exact asset-name matching and immutable tag/release re-audit after publication.

The release boundary does not change the BuildScope version surfaces:

~~~text
/home/jihoon/projects/toy-projects/buildscope/python/buildscope/__init__.py  -> 0.5.0
/home/jihoon/projects/toy-projects/buildscope/pyproject.toml                -> 0.5.0
/home/jihoon/projects/toy-projects/buildscope/CMakeLists.txt                -> 0.5.0
/home/jihoon/projects/toy-projects/buildscope/ici.toml                      -> 0.5.0
/home/jihoon/projects/toy-projects/CHANGELOG.md                             -> [0.5.0] - 2026-09-02
~~~

## Remote acceptance evidence

### PR #38 — reproducible hybrid release pipeline

[PR #38](https://github.com/jihoon22-lee/toy-projects/pull/38) final head was
`3ba645eae5181698e1272729dddaa8a72189b067`. [CI run
`33545168957`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33545168957) passed all 21
checks, including:

- Python/native handoff;
- Python `3.10/3.14` and Qt `5/6` Release/CTest matrices;
- generated Qt codegen and offscreen GUI smoke;
- hosted deep legs using public ici `v0.10.2`;
- the release contract;
- `Publish Reports & Sticky Comment` and `Merge Gate`.

[Sticky comment #5494648837](https://github.com/jihoon22-lee/toy-projects/pull/38#issuecomment-5494648837)
records the ici verification and three HTML report links. The PR was squash-merged to `main` as
`069a3a86c0164a1d2a88710f9c3c48a398c8087e`, and its branch was deleted immediately. The same exact
head passed applicable checks and `Merge Gate` in [main run
`33546046566`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33546046566); the PR-only
publisher was skipped as expected on the push event.

### PR #39 — trusted `main` Pages publisher

[PR #39](https://github.com/jihoon22-lee/toy-projects/pull/39) final head was
`b861ff5b4cc0314aae5ec9f6dab905648233216d`. [CI run
`33548626203`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33548626203) passed, and
[sticky comment #5499184834](https://github.com/jihoon22-lee/toy-projects/pull/39#issuecomment-5499184834)
records its report links. The PR was squash-merged to `main` as
`c80e922f0d0911019cfa8b5c67a8b654c556a68c`, and its branch was deleted immediately.

The resulting [exact-main run
`33549475034`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33549475034) passed. Its
trusted publisher checks out the exact main SHA, runs the quality-dependent main report, publishes
explicit `<project>/main/index.html` destinations, and audits the public response against the local
artifact at byte level.

### Trusted main Pages audit

The three stable Pages reports from exact-main run `33549475034` returned HTTP 200 with
`text/html`, the exact `ici Verification Report — <project>` title, and Zero-CDN resources. Each
response byte-matched the corresponding locally audited artifact:

| Project | Trusted `main` report | Bytes | SHA-256 |
|---|---|---:|---|
| BuildScope | [buildscope/main](https://jihoon22-lee.github.io/toy-projects/buildscope/main/) | 1,349,088 | `dd7ec9b49281875f812b9a9c6b4e18a028051936a37441f19d907098c05dcc65` |
| diskmap | [diskmap/main](https://jihoon22-lee.github.io/toy-projects/diskmap/main/) | 339,929 | `1bd6ebdce2206e4538fb28c20c17f2d504f1a160e15f5d8d4e2923b15b399e65` |
| loglens | [loglens/main](https://jihoon22-lee.github.io/toy-projects/loglens/main/) | 495,237 | `11e4c78eed957e237bcea8ec5ea64c3894751eafbe639c454e47ab0acd70df96` |

These are validation artifacts, not BuildScope release assets. They establish that the trusted
main publication boundary is usable before creating a product tag.

## Key commands and implementation evidence

The relevant release workflow uses absolute runner-temporary paths for generated artifacts and
performs reproducibility and digest checks before an upload:

~~~bash
"$ICI_PYTHON" buildscope/tools/build_standalone.py \
  --project-root "$GITHUB_WORKSPACE/buildscope" \
  --output "$artifact/.buildscope-first.pyz"
"$ICI_PYTHON" buildscope/tools/build_standalone.py \
  --project-root "$GITHUB_WORKSPACE/buildscope" \
  --output "$artifact/.buildscope-second.pyz"
test "$(sha256sum "$artifact/.buildscope-first.pyz" | awk '{print $1}')" = \
     "$(sha256sum "$artifact/.buildscope-second.pyz" | awk '{print $1}')"
~~~

The wheel and standalone pyz must produce byte-identical snapshots consumed by both native CLI
invocations:

~~~bash
"$wheel_env/bin/buildscope" "$database" --output "$wheel_snapshot"
"$artifact/buildscope.pyz" "$database" --output "$pyz_snapshot"
cmp "$wheel_snapshot" "$pyz_snapshot"
"$native_cli" "$wheel_snapshot"
"$native_cli" "$pyz_snapshot"
~~~

The release workflow creates and checks the final manifest before publication:

~~~bash
(cd "$artifact" && sha256sum \
  buildscope.pyz buildscope.pyz.sha256 \
  "buildscope-${VERSION}-py3-none-any.whl" \
  "buildscope-${VERSION}.tar.gz" \
  buildscope-ici-deep.json buildscope-ici-deep.html \
  buildscope-provenance.json \
  "buildscope-${VERSION}-linux-x86_64.tar.gz") > "$artifact/SHA256SUMS"
(cd "$artifact" && sha256sum --check SHA256SUMS)
~~~

Local documentation-only verification for this workthrough is:

~~~bash
git diff --check -- workthrough/2026-09-02-buildscope-0.5.0-release-prep.md
~~~

The full implementation gates already passed in the accepted PR #38 workflow and must be repeated
by the tag workflow for the exact release commit; this document does not replace those gates.

## Intended release and remaining gates

The intended annotated tag is:

~~~text
buildscope-v0.5.0 -> the release-prep PR's future exact green main commit (resolved after merge)
~~~

The current `c80e922f0d0911019cfa8b5c67a8b654c556a68c` is PR #39 evidence, not the eventual tag target:
this documentation PR necessarily creates a newer main commit. Before creating the tag, resolve that
merge commit, wait for its exact-main CI and `Merge Gate`, and confirm remote `main` still points to
it. After the tag is created, the release workflow must publish exactly these nine top-level assets:

1. `buildscope.pyz`
2. `buildscope.pyz.sha256`
3. `buildscope-0.5.0-py3-none-any.whl`
4. `buildscope-0.5.0.tar.gz`
5. `buildscope-ici-deep.json`
6. `buildscope-ici-deep.html`
7. `buildscope-provenance.json`
8. `buildscope-0.5.0-linux-x86_64.tar.gz`
9. `SHA256SUMS`

Post-tag, post-release audit gates are mandatory:

- verify the tag is annotated and its peeled commit is exactly the intended green `main` SHA;
- verify the GitHub Release is non-draft and non-prerelease with exactly the nine expected names and
  no extras;
- download every public asset independently, recompute SHA-256, and verify `SHA256SUMS` plus the
  `buildscope.pyz.sha256` sidecar;
- validate the provenance JSON's product/version/tag/target-main/Merge-Gate/run/ici fields;
- validate the deep JSON producer/version and B5 contract, and the deep HTML exact title and
  Zero-CDN policy;
- inspect the pure wheel (`py3-none-any`, no native extension) and sdist contents;
- confirm the Linux bundle contains the expected native binaries, docs, examples, schemas, and
  embedded Python artifacts;
- re-check the GitHub API asset states/digests and immutable tag target after propagation;
- update the release notes, roadmap, README, handover, and this workthrough to state that the public
  release audit actually passed. Until those checks succeed, retain the wording “release-ready” and
  “public release pending.”

## Explicit version decision

No version bump is part of this work. BuildScope is already `0.5.0` on every product version surface,
and the planned tag is exactly `buildscope-v0.5.0`. The ici `v0.10.2` checksum is a verified runtime
dependency boundary, not a reason to bump BuildScope. The next BuildScope version must wait for a new
cohesive user-facing checkpoint with its own full native, ici, CI/Pages, documentation, and
reproducible-asset evidence.
