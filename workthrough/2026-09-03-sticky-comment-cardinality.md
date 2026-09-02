# Enforce exactly one sticky PR report comment

## Overview

Hardened the toy-projects pull-request report verification boundary so it enumerates every paginated
GitHub issue comment and accepts the published report only when the exact `<!-- ici-report -->`
marker occurs once in exactly one comment. Duplicate marker comments and repeated markers now fail
closed without attempting to delete or repair remote comments. The ordinary CI gate continues to
use the released ici `v0.10.2` asset and its existing checksum.

## Context

The report job already downloaded all PR comments with `gh api --paginate --slurp`, but it selected
the most recently updated marker comment. That allowed an older duplicate marker to remain on the PR
without failing the merge gate. The required contract is one sticky HTML report comment for the
whole run, so cardinality must be validated before checking the current-run link and Pages links.

## Changes Made

### 1. Fail closed on sticky-comment cardinality

- File: `.github/workflows/ci.yml`
- Flattened all paginated comment pages and identified comments containing the exact marker string.
- Counted both marker-bearing comments and total marker occurrences.
- Rejected every result except one marker in one comment, including zero comments, duplicate comments,
  and multiple markers in one body.
- Removed newest-comment selection; the only accepted body is `marked[0]` after the cardinality gate.
- No delete, patch, or duplicate cleanup operation was added.
- Preserved `ICI_VERSION: v0.10.2` and the existing literal `ICI_SHA256`.

### 2. Added focused contract and regression tests

- File: `ci/test_ci_workflow_contract.py`
- Added a static contract assertion for exact marker cardinality, removal of newest-marker selection,
  and the absence of delete calls.
- Extracted and executed the workflow's embedded verifier against synthetic paginated API responses.
- Covered one valid marker across pages, two marker comments, and two marker occurrences in one comment.
- Added a missing-marker regression with unrelated comments on separate API pages; zero marker
  comments now fails with the same cardinality mismatch.

### 3. Recorded the user-visible CI contract

- File: `CHANGELOG.md`
- Added an `Unreleased` entry documenting fail-closed cardinality behavior and the unchanged stable
  ici pin.

## Code Examples

### Cardinality gate

```python
marker = "<!-- ici-report -->"
marked = [
    comment
    for comment in comments
    if marker in (comment.get("body") or "")
]
marker_count = sum(
    (comment.get("body") or "").count(marker) for comment in comments
)
if len(marked) != 1 or marker_count != 1:
    raise SystemExit("sticky comment cardinality mismatch")
body = marked[0].get("body") or ""
```

## Verification Results

```text
python3 -m pytest -q ci/test_ci_workflow_contract.py
23 passed, 8 subtests passed in 0.15s

uvx ruff check ci/test_ci_workflow_contract.py
All checks passed!

uvx ruff format --check ci/test_ci_workflow_contract.py
1 file already formatted

actionlint .github/workflows/ci.yml
exit 0

git diff --check
exit 0
```

No full native build or GitHub Actions run was performed locally; this branch was intentionally kept
limited to the workflow verifier and its focused tests. No ici files or worktrees were changed.

## Next Steps

- Run the PR workflow and confirm the single-comment contract on a real pull request.
- Keep quality-zoo output in the existing single sticky comment when that consumer is added; do not
  create a second marker comment.
