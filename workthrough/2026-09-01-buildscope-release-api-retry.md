# BuildScope release API retry hardening

## Overview

The BuildScope tag-only release workflow already bounded its exact-main Merge
Gate poll and final public-asset audit. Under shell `errexit`, however, a
transient GitHub API 404 or 5xx response could terminate either loop before its
documented retry window was used.

## Changes Made

- Wrapped the check-run API request in a conditional. Failed requests now log
  the attempt, wait ten seconds, and continue within the existing 120-attempt
  gate window.
- Wrapped the public-release API request in the same pattern with the existing
  five-second, 12-attempt window.
- The Merge Gate lookup now uses bounded `gh api --paginate --slurp` input and the dependency-free
  `ci/check_buildscope_merge_gate.py` helper: it selects the newest exact check-run by ID and verifies
  the independent GitHub Actions workflow-run identity. The annotated tag's peeled SHA is authoritative;
  Release API `target_commitish` is not compared.
- Kept all provenance and publication failures fail-closed: only transport/API
  request failures retry, while the exact tag, exact main commit, successful
  Merge Gate, fixed annotated-tag target revalidation, and exact nine-asset audit remain
  mandatory.

## Verification

- `actionlint .github/workflows/buildscope-release.yml`
- `git diff --check`

The release has not been triggered by this change. Remote acceptance still
requires a green PR and exact-main workflow before the annotated product tag is
created; no PR, tag, or release is claimed by this workthrough.
