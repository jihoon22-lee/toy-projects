# Trusted main portfolio report publication

## Overview

The portfolio CI previously published ici HTML only for pull requests. `Merge Gate` accepted that
publisher being skipped on a `main` push, so the stable BuildScope, DiskMap, and LogLens Pages URLs
returned HTTP 404 even though the exact-main quality run was green. This change makes the public
main reports part of the gate itself; it does not change any toy product version.

## Context and failure boundary

- PR reports and the sticky comment worked at `<project>/pr/<number>/index.html`.
- No push job invoked `ici publish`, so `<project>/main/index.html` did not exist on `gh-pages`.
- The old gate checked only that the PR publisher was skipped on pushes. It could not distinguish an
  intentionally skipped PR job from a missing main publication path.
- Publication must describe the same exact `main` commit whose quality jobs succeeded. A stale run
  must stop if a newer commit has already replaced it.

## Changes

### `/home/jihoon/projects/toy-projects/.github/workflows/ci.yml`

- Adds `Publish Main Portfolio Reports`, serialized with PR publishing through the repository-wide
  `gh-pages` concurrency group.
- Requires success from manifest discovery, ici verification, both GUI matrices, all three benchmark
  gates, BuildScope deep Qt5/Qt6 verification, Python quality, and the release contract.
- Checks out `${{ github.sha }}`, compares it to the local `HEAD`, the push ref, and the current
  remote `main` ref before publishing.
- Fetches public ici `v0.10.2` into an absolute runner-temp path and checks its sidecar, literal
  pinned digest, and downloaded bytes.
- Uses explicit labels so destinations cannot collide:

```bash
args+=(--report-dir "$project=$GITHUB_WORKSPACE/reports/$project")
"$ICI_MAIN_BIN" publish "${args[@]}"
```

- Polls every stable Pages URL and requires a byte-identical response before applying the
  exact-title/Zero-CDN contract, so an older cached report cannot satisfy the publication gate.
- Makes `Merge Gate` require PR publication on pull requests and main publication on pushes.

### `/home/jihoon/projects/toy-projects/ci/check_published_html.py`

Adds a dependency-free, Python 3.10-compatible validator with a 20 MB input bound, regular-file and
UTF-8 checks, an exact `ici Verification Report — <project>` title, and external resource rejection
for HTML attributes, inline styles, and style blocks. External navigation links remain allowed.

### `/home/jihoon/projects/toy-projects/ci/test_check_published_html.py`

Covers valid inline reports and rejects duplicate/wrong titles, external resource attributes,
external CSS resources, non-regular inputs, and unsafe project labels.

### `/home/jihoon/projects/toy-projects/ci/test_ci_workflow_contract.py`

Locks the PR/main event split, shared publication concurrency, complete upstream dependency set,
exact-main identity checks, ici digest validation, collision-free labels, byte comparison, public
HTML checker, and event-specific `Merge Gate` assertions into the discovery job.

### `/home/jihoon/projects/toy-projects/CHANGELOG.md`

Records the trusted publication boundary under `Unreleased`; no product version is advanced for a
CI/Pages repair.

## Verification

Local verification commands:

```bash
python3.10 -m unittest ci/test_check_manifest.py ci/test_check_published_html.py ci/test_ci_workflow_contract.py -v
python3.14 -m unittest ci/test_check_manifest.py ci/test_check_published_html.py ci/test_ci_workflow_contract.py -v
ruff check ci/check_published_html.py ci/test_check_published_html.py ci/test_ci_workflow_contract.py
ruff format --check ci/check_published_html.py ci/test_check_published_html.py ci/test_ci_workflow_contract.py
actionlint .github/workflows/ci.yml
git diff --check
```

The PR run must additionally prove that all quality jobs and the sticky-comment publisher pass,
that the main publisher is skipped, and that all three PR Pages URLs serve exact-title Zero-CDN
HTML. After merge, the exact-main run must prove the inverse event contract and all three stable
`/main/` URLs must return HTTP 200 byte-identical HTML.
