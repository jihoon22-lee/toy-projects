# Changelog

## [0.1.1](https://github.com/jihoon22-lee/toy-projects/compare/testlens/v0.1.0...testlens/v0.1.1) (2026-10-03)


### Bug Fixes

- Diagnose unsupported/missing CTest statuses and missing/empty test names, mark those
  observations unknown and incomplete, and make `--fail-on incomplete` return 1.
- Reject stored completion claims for unknown or unnamed results while preserving
  legitimate named CTest `notrun` observations as complete coverage.
- Align runner examples, CLI option documentation and retry/shard support descriptions
  with current behavior; remove duplicate release headings and fixed-version usage examples.

* resolve review findings and reconcile product documentation ([#124](https://github.com/jihoon22-lee/toy-projects/issues/124)) ([6918cfb](https://github.com/jihoon22-lee/toy-projects/commit/6918cfb99ebe6e614ca6809ba94bb99bc9cd5c62))

## 0.1.0 (2026-10-03)

### Features

* expand eight independent diagnostic tools and verified releases ([#120](https://github.com/jihoon22-lee/toy-projects/issues/120)) ([3a01bb9](https://github.com/jihoon22-lee/toy-projects/commit/3a01bb93c3fee936a335bb9445400b5f469d6a87))

- Collect pytest/Qt JUnit and CTest JUnit/dashboard reports into evidence-preserving runs.
- Track explicit retry attempts, duplicate inputs, ambiguous identities, coverage and shards.
- Compare result transitions and execution durations; summarize observed failure frequency.
- Export standalone searchable HTML with input-quality and original observation details.
- Provide strict public schemas, bounded XML, atomic output, CI policies and independent packages.
