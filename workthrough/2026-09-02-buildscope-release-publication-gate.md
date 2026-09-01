# BuildScope private-draft publication gate

## Overview

BuildScope `0.5.0` remains the same unreleased product checkpoint. This change does not increment
any version surface. It closes the final-publication gap found during an independent pre-tag review:
the release workflow previously made a final GitHub Release visible before it had independently
downloaded and audited the uploaded bytes.

The new boundary is:

```text
exact green main + annotated tag
        ↓
build/test/package/deep-report gates
        ↓
private GitHub draft + explicit nine-file upload
        ↓
metadata, manifest, pyz, HTML, and independent API-ID download audit
        ↓
publish the already-audited draft
        ↓
final release ID/state + immutable tag re-audit
```

No public `buildscope-v0.5.0` tag or Release is claimed by this workthrough. Those remain blocked on
this change's PR CI, its exact-main CI, the tag-only workflow, and a separate public-download audit.

## Why the previous boundary was incomplete

The accepted release pipeline already required exact main, a green Merge Gate, public ici
`v0.10.2`, Python 3.10/3.14, Qt 5/6, pure Python distributions, a reproducible zipapp, a native
bundle, exactly nine API asset names, and size/digest agreement with the local upload directory.
Four details were still weaker than the intended release contract:

1. `softprops/action-gh-release` used `draft: false`, so a failing post-upload audit could leave a
   visible but rejected Release.
2. `SHA256SUMS` was passed directly to `sha256sum --check` without first proving its exact names,
   order, separators, and lack of traversal entries.
3. The release deep HTML was copied but did not pass the same exact-title and Zero-CDN checker used
   by PR/main Pages.
4. The standalone artifact had no database-free `--version` probe.

## Changed surfaces

The release boundary is synchronized across these full paths:

| Path | Change |
|---|---|
| `/home/jihoon/projects/toy-projects/buildscope/python/buildscope/__main__.py` | Adds the standard `buildscope --version` action using the package version. |
| `/home/jihoon/projects/toy-projects/buildscope/tests/python/test_standalone.py` | Executes a built pyz without a database and requires exact `buildscope 0.5.0` output. |
| `/home/jihoon/projects/toy-projects/ci/check_buildscope_release_assets.py` | Separates `draft` and `final` metadata contracts and requires release/asset IDs, exact name, publication state, and unique asset IDs. |
| `/home/jihoon/projects/toy-projects/ci/test_check_buildscope_release_assets.py` | Covers both release stages plus release identity, publication metadata, and duplicate IDs. |
| `/home/jihoon/projects/toy-projects/ci/check_buildscope_release_manifest.py` | Safely parses the exact ordered eight-entry manifest, streams every digest, and validates the pyz sidecar. |
| `/home/jihoon/projects/toy-projects/ci/test_check_buildscope_release_manifest.py` | Covers malformed, missing, reordered, traversal, digest, control-byte, bound, symlink, directory, sidecar, and CLI cases. |
| `/home/jihoon/projects/toy-projects/ci/download_buildscope_release_assets.py` | Downloads the nine draft assets by numeric API ID with direct argv, closed stdin, per-process/overall time bounds, fresh output, and immediate digest verification. |
| `/home/jihoon/projects/toy-projects/ci/test_download_buildscope_release_assets.py` | Covers exact argv/IDs/bytes, unsafe repository input, existing destinations, failed/time-limited downloads, corrupt bytes, and CLI errors. |
| `/home/jihoon/projects/toy-projects/.github/workflows/buildscope-release.yml` | Stages a private draft, audits local and independently downloaded bytes, publishes only after success, then re-audits final state. |
| `/home/jihoon/projects/toy-projects/.github/workflows/ci.yml` | Runs every release-audit regression suite during manifest discovery. |
| `/home/jihoon/projects/toy-projects/ci/test_ci_workflow_contract.py` | Locks the ordering `draft → audit → publish → final audit` and all helper invocations. |
| `/home/jihoon/projects/toy-projects/CHANGELOG.md` | Records the user-visible version probe and fail-closed publication policy in the existing 0.5.0 entry. |
| `/home/jihoon/projects/toy-projects/README.md` | Updates the portfolio release contract without claiming publication. |
| `/home/jihoon/projects/toy-projects/buildscope/README.md` | Documents the private-draft and independent-download boundary. |
| `/home/jihoon/projects/toy-projects/ROADMAP.md` | Keeps the last public-release checkbox pending while describing its strengthened gate. |

## Implementation details

### Standalone identity

The package and built zipapp now share one conventional probe:

```bash
buildscope --version
# buildscope 0.5.0

python3 buildscope.pyz --version
# buildscope 0.5.0
```

The positional compilation database remains required for every snapshot-producing invocation. The
version action exits before argparse requests that positional path.

### Exact checksum contract

`SHA256SUMS` covers exactly these eight files, in this order; it does not hash itself:

```text
buildscope.pyz
buildscope.pyz.sha256
buildscope-0.5.0-py3-none-any.whl
buildscope-0.5.0.tar.gz
buildscope-ici-deep.json
buildscope-ici-deep.html
buildscope-provenance.json
buildscope-0.5.0-linux-x86_64.tar.gz
```

Before any `sha256sum --check`, the Python checker requires bounded regular UTF-8 input, lowercase
64-hex digests, the exact two-space `sha256sum` separator, exact ordered basenames, a terminal
newline, and no CR/NUL data. It refuses symlinked directories/files and streams each asset. The
sidecar must be one exact line containing the measured `buildscope.pyz` digest.

### Draft asset download boundary

The upload action is pinned and runs with `draft: true`. Its numeric release ID is used to fetch the
authenticated draft response. The metadata checker requires:

- `id > 0`, exact tag, and exact `BuildScope 0.5.0` name;
- `draft=true`, `prerelease=false`, and no `published_at` before publication;
- exactly nine unique names and nine unique positive asset IDs;
- `state=uploaded`, integer size, and exact `sha256:` API digest.

The downloader accepts only a bounded `owner/repository` identifier and uses direct subprocess argv:

```text
gh api -H "Accept: application/octet-stream" \
  repos/jihoon22-lee/toy-projects/releases/assets/<numeric-id>
```

It never invokes a shell, closes stdin, writes each response to an exclusive partial file under a
new directory, applies a 120-second per-call and 600-second overall budget, atomically finalizes a
successful file, and then reuses the asset checker against all downloaded bytes.

### Publication ordering

The workflow checks the downloaded draft's manifest, HTML, and pyz identity before issuing the only
publication mutation:

```json
{"draft": false, "make_latest": "true"}
```

It then requires the tag endpoint to return the same release ID with `draft=false`,
`prerelease=false`, a nonempty `published_at`, the same exact nine assets, and bytes that still match
the draft download. A previously final release for the tag is never overwritten.

## Verification

Local focused verification:

```bash
(cd buildscope && PYTHONPATH=python:. python3.10 -m unittest tests/python/test_standalone.py -v)
(cd buildscope && PYTHONPATH=python:. python3.14 -m unittest tests/python/test_standalone.py -v)

python3.10 -m unittest \
  ci/test_check_manifest.py \
  ci/test_check_published_html.py \
  ci/test_check_buildscope_release_assets.py \
  ci/test_check_buildscope_release_manifest.py \
  ci/test_download_buildscope_release_assets.py \
  ci/test_ci_workflow_contract.py -v
python3.14 -m unittest \
  ci/test_check_manifest.py \
  ci/test_check_published_html.py \
  ci/test_check_buildscope_release_assets.py \
  ci/test_check_buildscope_release_manifest.py \
  ci/test_download_buildscope_release_assets.py \
  ci/test_ci_workflow_contract.py -v

uvx ruff check buildscope/python buildscope/tests/python ci
uvx ruff format --check buildscope/python buildscope/tests/python ci
uvx --from mypy==2.3.1 mypy --python-version 3.10 \
  ci/check_buildscope_release_assets.py \
  ci/check_buildscope_release_manifest.py \
  ci/download_buildscope_release_assets.py
actionlint .github/workflows/ci.yml .github/workflows/buildscope-release.yml
git diff --check
```

At the implementation checkpoint, both interpreters passed the five standalone tests and 53
selected CI contract tests; Ruff, mypy, actionlint, and diff checks passed. Full BuildScope Python,
native, ici, Qt 5/6, release-contract, sticky-comment, Pages, and Merge Gate evidence must be supplied
by this change's PR and exact-main runs before the annotated tag is created.

## Accepted baseline before this hardening PR

The immediately preceding release-prep PR #40 merged as
`8b7e7db14f6de1da4e2df2aee249dacf8d35e577`. Its exact-main run `33554222019` completed with 21
successful jobs and the event-inapplicable PR publisher skipped. The three trusted main reports
were byte-identical to that run's artifacts and passed HTTP 200, exact-title, and Zero-CDN checks:

| Project | Bytes | SHA-256 |
|---|---:|---|
| BuildScope | 1,349,080 | `7ff4c92372caee47b6cd5a86d7f471471ca0ab9186d8804207c2f93948ab2b77` |
| DiskMap | 340,040 | `08efac0313532c1019bc3bcd56cadca73e9a823a94e5c7c0cd19db1c2e5ebda9` |
| LogLens | 495,232 | `4b14031b1dc327410f266e3527ba699b9198143be9dd07b104c4237cfcfc9b9a` |

That run proves the release-prep source baseline, not the new private-draft behavior. The latter
must receive its own PR/main evidence.

## Remaining release gates

1. Merge this hardening change only after its public ici-backed PR CI, single sticky comment, and
   three Pages reports pass.
2. Require the squash merge commit's exact-main run, trusted Pages publication, and Merge Gate.
3. Create annotated `buildscope-v0.5.0` only at that exact green main commit.
4. Require the tag workflow to keep the Release private until all draft checks pass, then publish
   and finish its final metadata audit.
5. Independently download the final nine public assets into a fresh temporary directory and verify
   API digests, manifest/sidecar, pyz producer, deep JSON/HTML, provenance, wheel, sdist, and bundle.
6. Update current-state documentation in a separate descriptive PR with immutable release evidence.
