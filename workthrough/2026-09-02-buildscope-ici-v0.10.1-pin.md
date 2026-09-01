# BuildScope corrective ici release pin

## Overview

BuildScope's current CI, tag-only release workflow, and strict deep-report
checker now consume the corrective public ici `v0.10.1` artifact. The change
updates a dependency contract only; BuildScope remains the `0.5.0` release
candidate under the deliberate toy version policy.

## Release provenance

- ici tag: `v0.10.1`
- exact ici main commit: `326a12abd4ac56cd88949c15c7748877e713531c`
- ici release workflow: `33521155513` (`success`)
- public release state: non-draft, non-prerelease, exact nine assets
- public `ici.pyz` SHA-256:
  `9e262730b49420c59ee115cf389881dbfbb944b6a96ca1d397a1ecb247ec17ca`

The published sidecar, downloaded bytes, and GitHub release API digest all
matched this literal value, and the executable reported `ici 0.10.1`.

## Changes Made

- Updated the repository CI and BuildScope release workflow version/digest
  pins and their user-facing step labels.
- Updated the B5 report checker and its valid fixture to require producer
  version `0.10.1`.
- Updated only current operational documentation. The earlier v0.10.0 local
  report metrics and hashes remain explicitly historical evidence.
- Recorded that an ici dependency correction does not itself create a new toy
  product version.

## Verification Results

```text
Python 3.10 B5 checker tests: 14 passed
Python 3.14 B5 checker tests: 14 passed
Ruff check: passed
Ruff format check: 2 files already formatted
actionlint (CI + release workflows): passed
git diff --check: passed
public sidecar/download/API SHA-256: matched
public executable version: ici 0.10.1
```

The temporary download directory was created under `/tmp` and removed after
verification.

## Next Steps

- Run the corrected PR matrix with released ici v0.10.1.
- Require successful Qt5/Qt6 tool-backed deep evidence, sanitizer checks,
  sticky comment, and all three Zero-CDN Pages reports before merge.
- Create the annotated BuildScope product tag only from a green exact-main
  commit and audit the exact nine public assets.
