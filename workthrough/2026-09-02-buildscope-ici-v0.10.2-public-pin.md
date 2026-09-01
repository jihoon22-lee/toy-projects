# BuildScope public ici v0.10.2 deep-verification pin

## Overview

The BuildScope PR #38 deep matrix now consumes the public ici `v0.10.2` release
for both Qt5 and Qt6. The temporary cross-repository source checkout and
source-built candidate pyz were removed so the deep jobs exercise the same
checksummed release path as ordinary CI and the tag-only release workflow.

The BuildScope product remains the unreleased `0.5.0` candidate. This is an
independent ici dependency and CI-path correction, not a product version bump.

## Release pin

- ici tag: `v0.10.2`
- public `ici.pyz` SHA-256:
  `8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4`

The same literal is used by `.github/workflows/ci.yml` as `ICI_SHA256` and by
`.github/workflows/buildscope-release.yml` as `ICI_PYZ_SHA256`. The B5 report
checker expects producer version `0.10.2` without the release tag's leading
`v`.

## Changes Made

- Removed the deep-job checkout of ici commit
  `27f4e5cf820ceb36b24711be927f19076472c822` and its `build-pyz.sh` step.
- Changed each deep matrix leg to download `ici.pyz` and its sidecar below
  `$RUNNER_TEMP/buildscope-b5-ici`.
- Added sidecar filename/hash validation, the sidecar's own `sha256sum --check`,
  a literal expected-hash check against the absolute file path, and a direct
  downloaded-byte digest check.
- Persisted the validated executable as absolute `ICI_DEEP_BIN`; the verify
  step checks both executability and absolute-path shape before running either
  Qt branch.
- Updated the release workflow and current operational documentation. Existing
  v0.10.0/v0.10.1 measurements, candidate commits, and historical workthrough
  records remain unchanged.

## Key workflow path

```bash
ici_root="$RUNNER_TEMP/buildscope-b5-ici"
ici_pyz="$ici_root/ici.pyz"
# download and validate sidecar/literal/direct digest
printf 'ICI_DEEP_BIN=%s\n' "$ici_pyz" >> "$GITHUB_ENV"
```

The Qt5 LSAN suppression/control fixture and the Qt5/Qt6 matrix build remain
unchanged; only the ici executable provenance path changed.

## Verification Results

- `actionlint .github/workflows/ci.yml .github/workflows/buildscope-release.yml`: passed.
- `git diff --check`: passed.
- Python 3.10 B5 checker tests: 14 passed.
- Python 3.14 B5 checker tests: 14 passed.
- Ruff check and format check for the checker files: passed; both files were already
  formatted.
- The public v0.10.2 sidecar filename/hash, sidecar check, literal digest,
  downloaded-byte digest, absolute executable path, executable bit, `ici 0.10.2`
  version, and GitHub API asset digest were all verified against the pinned SHA.

The full PR matrix must still provide the final remote evidence: both deep Qt
legs, the Qt5 leak-control test, release contract, report publication, Pages,
and Merge Gate.
