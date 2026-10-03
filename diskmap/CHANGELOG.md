# Changelog

## 0.2.0 — development checkpoint (unreleased)

- Preserve duplicate survivors with explicit keeper policies, ancestor protection and fresh content proofs.
- Revalidate mtime/ctime before cleanup; persist Trash receipts and reconstruct recovery after restart.
- Bound production scans by retained nodes and estimated memory, with partial-evidence reporting.
- Add snapshot v2 with lossless filename bytes and ctime; retain strict v1 loading and comparison.
- Move snapshot and Trash work off the GUI thread with safe cancellation boundaries.
- Separate workbench tabs, link selection, add meaningful treemap colors, human size input and diff review filters.
- Distinguish potential disposal savings from bytes moved to same-filesystem Trash.
- Ship independent CMake install rules, schemas, desktop/icon and regression coverage.

## [0.1.1](https://github.com/jihoon22-lee/toy-projects/compare/diskmap/v0.1.0...diskmap/v0.1.1) (2026-10-03)


### Features

* **cli:** let the diskmap and loglens binaries state their version ([#71](https://github.com/jihoon22-lee/toy-projects/issues/71)) ([a31ec70](https://github.com/jihoon22-lee/toy-projects/commit/a31ec70c708965768488d10adbff37404609b3c3))
* **diskmap:** --cleanup-plan dry run for duplicate copies ([#111](https://github.com/jihoon22-lee/toy-projects/issues/111)) ([2805a2d](https://github.com/jihoon22-lee/toy-projects/commit/2805a2d1932119614538f51b16f368e89affc1f9))
* **diskmap:** add a deterministic uncertainty-aware explorer core ([#45](https://github.com/jihoon22-lee/toy-projects/issues/45)) ([0688e44](https://github.com/jihoon22-lee/toy-projects/commit/0688e44fa99d1ec69aba0c9bf9995a4a857fea9e))
* **diskmap:** add cancellable latest-generation scans ([#28](https://github.com/jihoon22-lee/toy-projects/issues/28)) ([ec075e5](https://github.com/jihoon22-lee/toy-projects/commit/ec075e57874d20654f7cbfbc604ad8aaee8401a6))
* **diskmap:** compare two saved snapshots offline ([#104](https://github.com/jihoon22-lee/toy-projects/issues/104)) ([a767330](https://github.com/jihoon22-lee/toy-projects/commit/a767330217febdfb55ac311cccf9b1455d23f01f))
* **diskmap:** complete the uncertainty-aware storage explorer ([#46](https://github.com/jihoon22-lee/toy-projects/issues/46)) ([0cdd639](https://github.com/jihoon22-lee/toy-projects/commit/0cdd63953179a1dc885ed660e955b399d54243b7))
* **diskmap:** disk usage treemap core, CLI and test suite ([#1](https://github.com/jihoon22-lee/toy-projects/issues/1)) ([75dccc2](https://github.com/jihoon22-lee/toy-projects/commit/75dccc20e71c2d938c338936aae69eaea812f76b))
* **diskmap:** filter snapshot diff reports by kind, delta, certainty ([#99](https://github.com/jihoon22-lee/toy-projects/issues/99)) ([881b168](https://github.com/jihoon22-lee/toy-projects/commit/881b16834de477edc07b0488f61ad29c767c8635))
* **diskmap:** Qt6 treemap GUI ([#3](https://github.com/jihoon22-lee/toy-projects/issues/3)) ([4938664](https://github.com/jihoon22-lee/toy-projects/commit/49386642a2b0e0eebbc41c9a07b517adefdb04dd))
* loglens on CMake, diskmap on qmake, with Qt tests that pass ([#10](https://github.com/jihoon22-lee/toy-projects/issues/10)) ([3919ba4](https://github.com/jihoon22-lee/toy-projects/commit/3919ba430c166961330d458fb7b4cf8c61847cf0))
* **loglens:** log viewer core, CLI and test suite ([#2](https://github.com/jihoon22-lee/toy-projects/issues/2)) ([06843dc](https://github.com/jihoon22-lee/toy-projects/commit/06843dc2206601fede7589d980fe2cfe883fba0c))
* **tools:** make runtime, log, and storage evidence trustworthy ([#57](https://github.com/jihoon22-lee/toy-projects/issues/57)) ([6376bfc](https://github.com/jihoon22-lee/toy-projects/commit/6376bfc6c18e5dd74fc17387d4373b3688377d9b))


### Bug Fixes

* remove DiskMap's swappable read-range params and pin AbiLens thresholds ([#64](https://github.com/jihoon22-lee/toy-projects/issues/64)) ([bd949c1](https://github.com/jihoon22-lee/toy-projects/commit/bd949c156df29fd6850604364a2126b0b2917df3))

## [0.1.0](https://github.com/jihoon22-lee/toy-projects/releases/tag/diskmap/v0.1.0) (2026-10-03)

Baseline: native build, test, and release pipeline established for diskmap.
