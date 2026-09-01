# Deliberate toy release cadence

## Overview

The portfolio documentation now separates roadmap progress from product versioning. The policy keeps
infrastructure and verification changes from creating accidental releases while preserving BuildScope
`0.5.0` as the planned first usable B3/B4/B5 boundary.

## Context

The repository records implementation stages, ici pin changes, CI evidence, candidates, and releases in
the same timeline. Without an explicit cadence, a B-stage or workflow-only change could be mistaken for
a product version bump. This update makes the stable-release evidence threshold and per-product version
ownership explicit without rewriting historical evidence. The independent operational dependency pin
can change without changing the toy product version.

## Changes Made

### README.md

- Added the user-facing release version policy.
- Clarified the independent versioning of each toy, the stable/candidate distinction, and the planned
  BuildScope `0.5.0` B3/B4/B5 boundary.

### ROADMAP.md

- Added the roadmap-level release discipline and the PR naming rule.
- Kept the B5 local candidate and ici evidence historical while stating that later work accumulates under
  `Unreleased` until a comparable checkpoint.

### Product portfolio master plan

- Added version cadence and stable-release criteria beside the common PR operating rules.
- Required product/technical outcomes in PR titles and summaries; plan codes remain secondary metadata.
- Recorded the BuildScope `0.5.0` boundary in the B5 plan without changing its pending status.

### CHANGELOG.md

- Added an `Unreleased` entry documenting the policy and independent ici dependency pinning.
- Left existing B-stage and release evidence in place.

## Policy snapshot

```text
portfolio/B-stage/ici-pin/CI-only change -> no automatic toy version bump
public stable defect/security/compat regression -> patch
cohesive usable checkpoint + required evidence -> minor
candidate/pre-release/unreleased -> not stable
```

BuildScope `0.5.0` remains the bundled B3/B4/B5 first usable release boundary. Work after that
boundary remains under `Unreleased` until the same evidence standard is met.

## Verification Results

- `git diff --check` — passed.
- Lightweight documentation search confirmed the cadence language, PR naming rule, BuildScope `0.5.0`
  boundary, preserved historical evidence, and current operational pin are present in the requested
  documents.
- No source, workflow, package, tag, or release artifacts were modified.

## Next Steps

The B5 release candidate still requires its remote evidence and release workflow acceptance before a
stable BuildScope `0.5.0` tag is created. The current ici pin update is an independent dependency
correction and does not alter the toy version cadence.
