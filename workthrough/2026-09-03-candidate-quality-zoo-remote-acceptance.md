# Exact-Revision Candidate-to-Quality-Zoo Acceptance

## Overview

This workthrough records the independently audited remote acceptance of an ici
candidate against the toy-projects Quality Zoo. The acceptance used exact toy and
ici revisions, revalidated the candidate archive and authenticated provenance/API
evidence, and ran all five checked-in scenarios without exposing credentials to
the Quality Zoo process. It is acceptance evidence for the candidate channel, not
a stable release or a claim that the whole Q1–Q5 corpus is complete.

## Context

Before this run, Quality Zoo had a complete local candidate intake/selector and
local C++ sanitizer known-answer evidence, but the merged ici-hosted
candidate-to-Quality-Zoo workflow had not yet been proven by an exact-revision
remote dispatch. Ordinary toy CI intentionally remained pinned to the released
ici `v0.10.2` artifact.

The audited workflow was:

- Acceptance run [`33710695336`](https://github.com/jihoon22-lee/ici/actions/runs/33710695336),
  job [`100509326331`](https://github.com/jihoon22-lee/ici/actions/runs/33710695336/job/100509326331),
  successful at ici workflow head `6df011f98be1a19092b112cb56c596dc35bcae4d`.
- Exact toy-projects main: `2d0d7c0b2dcc137a782d6042438fc287bffdf570`.
- Candidate ici target: `9d470edca7ab037a24dcd6594531a822f116548b`.

## Changes Made

The documentation-only update touched these files:

- `CHANGELOG.md`
- `quality-zoo/README.md`
- `docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md`
- `docs/superpowers/2026-08-30-handover.md`
- `workthrough/2026-09-03-candidate-quality-zoo-remote-acceptance.md`

### 1. Recorded candidate and acceptance artifacts

- Candidate producer run [`33706057540`](https://github.com/jihoon22-lee/ici/actions/runs/33706057540)
  succeeded and produced artifact [`9875319095`](https://github.com/jihoon22-lee/ici/actions/artifacts/9875319095).
- The candidate artifact's raw ZIP is `2,285,368` bytes with SHA-256
  `4aec084b3a30ac01a1df5124fa3b42b7f51d23f66c12b490194a84549be9db27`.
- Its contained `ici.pyz` is `2,284,045` bytes with SHA-256
  `e7f1a2ce7147057538873a802715c7bf2b12e530a85070af862e02e378caceb8`.
- The acceptance output is the separate artifact
  [`9876797536`](https://github.com/jihoon22-lee/ici/actions/artifacts/9876797536),
  `1,104,307` bytes with SHA-256
  `e66ae2b65988abe10fc5ddb92a5c3bb6fc238ec2f77b7fd27ccfe75c24194a5f`.
- The candidate is package version `0.10.2` with `stable: false`; no stable
  version or toy product version was changed.

### 2. Revalidated provenance and API identity

The workflow rechecked the candidate archive against the candidate target and
producer identity, including the exact candidate artifact/run and archive and
executable digests. The five authenticated snapshots were present and matched:

```text
artifact.json
candidate-run.json
gate-check.json
gate-job.json
gate-run.json
```

The candidate provenance also bound the producer merge gate to run
`33705500603` / check-job `100495054979`, both successful. The Quality Zoo
execution ran after the no-credentials preflight; the workflow retained the
evidence in the acceptance artifact instead of publishing a product report.

### 3. Captured the five-scenario known-answer result

Every scenario had contract verdict `PASS` and `errors: []`. The expected and
observed suite statuses, rule predicates, categories, and locations were:

| Scenario | Expected / observed | Rule / tool rule | Category | Location |
|---|---|---|---|---|
| `cpp.asan-use-after-free` | `FAIL` / `FAIL` | `ici.legacy.sanitize.target` / `asan.heap-use-after-free` | correctness | `src/fault.cpp:5` |
| `cpp.lsan-memory-leak` | `FAIL` / `FAIL` | `ici.legacy.sanitize.target` / `lsan.memory-leak` | resource | `src/fault.cpp:3` |
| `cpp.sanitizer-clean` | `PASS` / `PASS` | `ici.legacy.sanitize.target` / no tool rule | correctness | `tests/test_clean.cpp:1` |
| `cpp.ubsan-signed-overflow` | `FAIL` / `FAIL` | `ici.legacy.sanitize.target` / `ubsan.signed-integer-overflow` | correctness | `src/fault.cpp:3:18` |
| `python.dead-private-function` | `WARN` / `WARN` | `ici.legacy.dead.target` / no tool rule | maintainability | `src/bad.py:1` |

The Python scenario also retained its negative assertion: `src/clean.py` must
not produce the same finding. The three sanitizer defect scenarios intentionally
observe `FAIL`; the clean sanitizer scenario observes `PASS`; and the Python
heuristic scenario observes its expected `WARN`. Contract success therefore
means that the complete known answer matched, not that every observed engine
status was `PASS`.

## Code Examples

No source, workflow, or runner code was modified in this documentation-only
acceptance record. The evidence contract can be summarized as:

```text
exact toy SHA + exact candidate target
  -> authenticated candidate provenance and archive digest
  -> five Quality Zoo contracts PASS with zero errors
  -> separate acceptance artifact
```

## Verification Results

### Remote workflow

The acceptance job and all of its verification/staging steps succeeded. It
checked out the exact toy commit, verified the exact candidate inputs, rechecked
the authenticated GitHub evidence, ran Quality Zoo, and uploaded the acceptance
artifact. Local documentation validation also completed with:

```text
git diff --check: PASS
workthrough trailing-whitespace check: PASS
```

### Side-effect audit

The dispatch was `workflow_dispatch` with no pull-request context. The audited
run produced no Pages deployment/build, PR or issue comment, release, tag, or
branch mutation. The workflow's permissions and steps were read-only apart from
uploading its dedicated acceptance artifact; no product Pages or PR report was
published.

### Scope decision

This evidence closes only:

- candidate cross-repository acceptance for the exact revisions above; and
- the Q2 runtime ASan/LSan/UBSan-plus-clean sub-scope represented by these four
  C++ scenarios.

It does not close Qt lifetime analysis, broader Q2, Q1/Q3–Q5 corpus expansion,
I4, or any product/stable release. Ordinary toy CI remains pinned to released
ici `v0.10.2`.

## Next Steps

- Keep the released ici `v0.10.2` pin and candidate/stable channels separate.
- Expand the remaining Quality Zoo Python, broader C++, Qt, build/binary, and
  hybrid scenarios with their own exact expectations and acceptance evidence.
- Treat this candidate artifact as non-stable evidence; make no version bump from
  this acceptance alone.
