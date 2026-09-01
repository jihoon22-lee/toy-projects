# BuildScope private-draft publication gate

## Overview

BuildScope `0.5.0` remains the same unreleased product checkpoint. This change does not increment
any version surface. It closes the final-publication gap found during an independent pre-tag review:
the release workflow previously made a final GitHub Release visible before it had independently
downloaded and audited the uploaded bytes.

The new boundary is:

```text
exact green main + fixed annotated tag target
        ↓
newest exact GitHub Actions Merge Gate identity
        ↓
build/test/package/deep-report gates
        ↓
paginated release-slot audit
        ├─ empty slot → direct private draft creation → fixed release ID
        └─ existing final → no mutation; audit-only path
        ↓
exact nine-file fixed-ID upload without clobber + bounded metadata, manifest,
payload/archive, provenance, B5/HTML, and API-ID download audit
        ↓
prepublish re-audit of the same fixed private draft
        ↓
publish with ambiguous-PATCH reconciliation
        ↓
final release ID/state + fixed annotated-tag peel + fresh public-byte download audit
```

No public `buildscope-v0.5.0` tag, Release, or completed nine-asset audit is claimed by this
workthrough. Those remain blocked on this change's PR CI, its exact-main CI, the tag-only workflow,
and the independent final public-byte audit.

## Why the previous boundary was incomplete

The accepted release pipeline already required exact main, a green Merge Gate, public ici
`v0.10.2`, Python 3.10/3.14, Qt 5/6, pure Python distributions, a reproducible zipapp, a native
bundle, exactly nine API asset names, and size/digest agreement with the local upload directory.
The newest `ci/check_buildscope_merge_gate.py` boundary selects the highest-ID exact `Merge Gate`
check-run, requires the GitHub Actions app and completed success, then independently verifies the
referenced Actions run's ID, repository/head repository, SHA, workflow name/path/event/status/conclusion,
and canonical URLs. The annotated tag's peeled SHA is authoritative; the Release API's
`target_commitish` is deliberately not compared.
The earlier design also allowed a release action to discover and update an existing release by tag,
which left a race between “no final release found” and upload. It did not have a paginated slot
decision, an explicit fixed release ID, or a safe recovery rule for an ambiguous publication request.
The redesign removes `softprops/action-gh-release` from this boundary: only an empty, fully validated
slot may create a draft; an existing final release is never mutated and is handled by an audit-only
path. `SHA256SUMS`, the archive payloads, provenance, B5 JSON, and Zero-CDN HTML are now checked
before publication and again from fresh final downloads. A current-run owner marker makes ambiguous
creation recoverable only when exactly one matching private draft has zero assets and the expected body
digest; an existing draft is
never guessed at or overwritten.

## Changed surfaces

The release boundary is synchronized across these full paths:

| Path | Change |
|---|---|
| `/home/jihoon/projects/toy-projects/buildscope/python/buildscope/__main__.py` | Adds the standard `buildscope --version` action using the package version. |
| `/home/jihoon/projects/toy-projects/buildscope/tests/python/test_standalone.py` | Executes a built pyz without a database and requires exact `buildscope 0.5.0` output. |
| `/home/jihoon/projects/toy-projects/ci/check_buildscope_merge_gate.py` | Safely audits bounded paginated check-runs, selects the newest exact GitHub Actions `Merge Gate` check-run, and independently validates the referenced workflow-run identity. |
| `/home/jihoon/projects/toy-projects/ci/test_check_buildscope_merge_gate.py` | Covers newest-ID selection, exact SHA/app/status, canonical check/run URLs, repository identity, workflow identity, bounds, malformed JSON, and CLI failures. |
| `/home/jihoon/projects/toy-projects/ci/check_buildscope_release_assets.py` | Separates `draft` and `final` metadata contracts and requires release/asset IDs, exact name, publication state, and unique asset IDs. |
| `/home/jihoon/projects/toy-projects/ci/test_check_buildscope_release_assets.py` | Covers both release stages plus release identity, publication metadata, and duplicate IDs. |
| `/home/jihoon/projects/toy-projects/ci/check_buildscope_release_state.py` | Audits bounded paginated release slots, rejects duplicate/pre-existing drafts, validates owner markers/body SHA and exact release state, and carries the fixed numeric release ID. |
| `/home/jihoon/projects/toy-projects/ci/test_check_buildscope_release_state.py` | Covers empty/final/draft/duplicate slots, pagination bounds, exact state, IDs, malformed JSON, and CLI failures. |
| `/home/jihoon/projects/toy-projects/ci/check_buildscope_release_payload.py` | Audits release-directory inventory, exact-shebang/version pyz, pure wheel, sdist, Linux bundle, schema-byte agreement, provenance, B5 JSON, and Zero-CDN HTML; preflights bounded ZIP EOCD/central-directory bytes before `ZipFile`. |
| `/home/jihoon/projects/toy-projects/ci/test_check_buildscope_release_payload.py` | Builds in-memory ZIP/TAR fixtures in temporary directories and covers traversal, duplicates, links/special files, native ZIP members, metadata/schema/version, ELF/embedded bytes, both valid self-extracting ZIP offset layouts, provenance, and CLI failures. |
| `/home/jihoon/projects/toy-projects/ci/check_buildscope_release_manifest.py` | Safely parses the exact ordered eight-entry manifest, streams every digest, and validates the pyz sidecar. |
| `/home/jihoon/projects/toy-projects/ci/test_check_buildscope_release_manifest.py` | Covers malformed, missing, reordered, traversal, digest, control-byte, bound, symlink, directory, sidecar, and CLI cases. |
| `/home/jihoon/projects/toy-projects/ci/download_buildscope_release_assets.py` | Downloads the nine draft assets by numeric API ID with direct argv, closed stdin, per-process/overall time bounds, fresh output, and immediate digest verification. |
| `/home/jihoon/projects/toy-projects/ci/test_download_buildscope_release_assets.py` | Covers exact argv/IDs/bytes, unsafe repository input, existing destinations, failed/time-limited downloads, corrupt bytes, and CLI errors. |
| `/home/jihoon/projects/toy-projects/.github/workflows/buildscope-release.yml` | Audits a paginated release slot, creates/uploads only to a new fixed-ID private draft with a current-run owner marker and expected body digest, re-audits before publication, reconciles an ambiguous PATCH, and independently downloads final public bytes without executing remote pyz in write-token steps. |
| `/home/jihoon/projects/toy-projects/.github/workflows/ci.yml` | Runs every release-audit regression suite during manifest discovery. |
| `/home/jihoon/projects/toy-projects/ci/test_ci_workflow_contract.py` | Locks the slot decision, bounded fixed-ID upload, `draft → audit → prepublish → publish/reconcile → final download audit` ordering, no remote-pyz execution, and helper invocations. |
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

### Release slot and draft asset boundary

The workflow no longer delegates release discovery or upload to
`softprops/action-gh-release`. It first requests all release pages with authenticated
`gh api --paginate --slurp` and validates the slot with `check_buildscope_release_state.py`:

- an empty slot is the only state allowed to create a new draft;
- a pre-existing private draft fails closed and is never guessed at or overwritten;
- exactly one final release is accepted for a mutation-free audit-only path;
- duplicate tag entries, malformed pages, and metadata mismatches fail closed.

For an empty slot, the workflow directly creates a private draft with the exact tag, name, notes, and
`prerelease=false`, appending one terminal current-run owner marker containing the repository, run ID,
and target SHA. It normalizes `RELEASE_NOTES.md` once and materializes separate expected final and
owner-marked draft body files, computing an exact UTF-8 SHA-256 for each. The draft digest is checked at
creation, before upload, prepublish, and failure reporting; the final digest is required for publication
and final audit. If the POST response is ambiguous, recovery accepts
only one exact owner-marked private draft with zero assets and the expected body digest; a pre-existing draft is not recoverable by
guesswork. The annotated tag's peeled SHA is authoritative, and the Release API's `target_commitish` is
intentionally not compared. The created response is validated and its positive numeric release ID is
carried through every subsequent API call. It verifies that fixed ID is still the exact empty draft
before uploading the nine explicit paths.
Upload uses the numeric release-ID endpoint, a binary body, HTTPS/TLS, 20-second connect and 300-second
transfer bounds, and an exact HTTP 201/uploaded/id/size/digest response contract; `--clobber` is not used.
A missing or unexpected asset path fails the job.

The metadata checker requires:

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
successful file, and then reuses the asset checker against all downloaded bytes. The same bounded
downloader is used again for the final public release.

### Release payload and archive contract

The payload checker audits the exact nine-file release directory and requires the artifact formats to
agree before a release can proceed. Before calling `zipfile.ZipFile`, it reads a bounded tail to locate
the EOCD, rejects ZIP64/multi-volume and inconsistent offsets, bounds the central-directory size and
member count, and scans each central-directory record. This prevents a hostile archive from creating an
unbounded `ZipInfo` inventory before the normal parser's checks.

- exact-shebang/version pyz, pure `py3-none-any` wheel, and the required BuildScope package/schema inventory;
- sdist and Linux x86_64 bundle roots, source/example/docs layout, and no native extension or unsafe
  archive member;
- matching schema bytes across pyz, wheel, sdist, and bundle, plus matching bundle-embedded assets;
- exact provenance fields for the tag, exact main commit, Merge Gate, workflow run, repository,
  runner, ici version, and ici digest;
- B5 deep JSON contract (`ici.result/v3`, uncached deep/tool-backed evidence) and exact-title,
  Zero-CDN HTML.

`ci/test_check_buildscope_release_payload.py` constructs the complete valid payload with `BytesIO`
ZIP/TAR writers and `TemporaryDirectory`, including both valid self-extracting ZIP offset layouts, then
exercises representative rejection paths for traversal, duplicate entries, ZIP symlinks/native names,
TAR symlinks/hardlinks/special files, wrong metadata/version/schema, embedded-artifact and ELF mismatch,
provenance extra/wrong fields, and CLI failure. This coverage is absorbed here so the publication gate
has one source of truth.

### Publication ordering and recovery

The workflow validates the draft metadata and independently downloaded bytes, then repeats the
manifest, payload/archive, provenance, B5/HTML, pyz, and peeled-tag checks immediately before issuing
the only public-state mutation. The publication request keeps the same tag and release name,
`prerelease=false`, and `make_latest=true` while changing only the already-audited draft to final. It
does not send or compare `target_commitish`; the remote annotated tag peel is the target proof:

```json
{"tag_name":"buildscope-v0.5.0","name":"BuildScope 0.5.0","body":"<exact normalized release notes>","draft":false,"prerelease":false,"make_latest":"true"}
```

If the PATCH response is missing or ambiguous, reconciliation fetches the same fixed release ID. A
final exact release counts as success; an exact private draft is retried; any other identity or state
fails closed. The final release body is also checked against the expected body digest. No alternate
release is selected and no final release is overwritten.

After publication, the workflow checks both the fixed release ID and the tag endpoint, then downloads
all nine final public assets into a fresh directory and reruns API digest, manifest/sidecar,
payload/archive, provenance, B5/HTML, and pyz checks. Every final asset is byte-compared with the
current local `dist`, including existing-final audit-only mode; a newly created release must also
byte-match the previously audited draft download. The write-token publish steps never execute a
downloaded remote BuildScope pyz. After the download, release metadata by ID and by tag, all asset
records, and the peeled tag are fetched again and revalidated. On failure, the preserve/report step runs
for the empty-slot path. If the create step lost its output ID, it paginates the release listing and
recovers an exact current-run-owned, zero-asset private draft with the expected body digest solely to
report and preserve it; it
performs no remote mutation. With an ID in hand it re-fetches that fixed release and preserves an owned
draft (or leaves an already-final release untouched). No automatic remote draft deletion is attempted.

## Verification

Local focused verification:

```bash
(cd buildscope && PYTHONPATH=python:. python3.10 -m unittest tests/python/test_standalone.py -v)
(cd buildscope && PYTHONPATH=python:. python3.14 -m unittest tests/python/test_standalone.py -v)

python3.10 -m unittest discover -s ci -p 'test_*.py' -v
python3.14 -m unittest discover -s ci -p 'test_*.py' -v

(cd buildscope && PYTHONPATH=python:. uvx --python 3.10 --from pytest==9.1.1 pytest -q tests/python)
(cd buildscope && PYTHONPATH=python:. uvx --python 3.14 --from pytest==9.1.1 pytest -q tests/python)

uvx ruff check buildscope/python buildscope/tests/python buildscope/tools ci
uvx ruff format --check buildscope/python buildscope/tests/python buildscope/tools ci
uvx --from mypy==2.3.1 mypy --python-version 3.10 \
  ci/check_buildscope_merge_gate.py \
  ci/check_buildscope_release_assets.py \
  ci/check_buildscope_release_state.py \
  ci/check_buildscope_release_manifest.py \
  ci/check_buildscope_release_payload.py \
  ci/download_buildscope_release_assets.py
actionlint .github/workflows/ci.yml .github/workflows/buildscope-release.yml
git diff --check
```

At the current implementation checkpoint, the complete CI helper discovery suite passed `145/145` on
both Python 3.10 and 3.14, including the two valid self-extracting ZIP offset-layout regressions.
BuildScope's `88/88` Python tests passed on both interpreters, and fresh local Qt 5.15.18 and Qt 6.10.2
Release builds each passed CTest `9/9`; their temporary build directories were removed immediately.
Ruff check/format, mypy, actionlint, and `git diff --check` passed. These are local implementation
checks; this workthrough makes no current PR, tag, or release claim. Public ici-backed PR CI, its
sticky/Pages evidence, exact-main `Merge Gate`, the tag-only workflow, and the final public-byte audit
remain required before creating the annotated tag.

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
4. Require the tag workflow to keep the Release private until all draft and prepublish checks pass,
   then publish through the fixed-ID PATCH reconciliation path.
5. Independently download the final nine public assets into a fresh temporary directory and verify
   API digests, manifest/sidecar, pyz producer, deep JSON/HTML, provenance, wheel, sdist, and bundle;
   for a newly created release, compare those bytes with the audited draft.
6. Update current-state documentation in a separate descriptive PR with fixed annotated-tag and
   release-ID evidence only after the public audit succeeds.
