# Changelog

## Unreleased

### EnvLens deterministic environment snapshot core

- Added the pure-Python `envlens` CLI/library on the Python 3.10 floor. The current package and
  `--version` identity is `0.1.0`; it remains unreleased, with no tag, GitHub Release, or stable
  artifact. This slice captures one explicitly selected interpreter and does not change the version
  cadence of the other portfolio products.
- Added a fixed `python -c` probe launched with an explicit executable path, `shell=False`, and no
  `PATH` or shell lookup. The probe records interpreter identity, prefixes and platform/compiler
  data, `sysconfig`, environment variables, and installed distribution metadata including normalized
  names, versions, `Requires-Python`, `Requires-Dist`, entry points, locations, and per-distribution
  errors. Metadata failures remain attached to the affected distribution and produce a `partial`
  collection instead of discarding healthy results.
- Added the strict `envlens.snapshot/v1` schema and canonical serialization. Object keys and
  unordered collections are sorted; `captured_at` is a UTC, second-precision timestamp kept separate
  from source identity; compact output is ASCII-safe and newline-terminated, while `--pretty` only
  changes indentation. Collection limits are 10,000 distributions, 4,096 environment/sysconfig
  fields, 100,000 nested collection items, and 65,536 characters per string field.
- Added default-safe privacy handling. Host and target home paths become `<USER_HOME>`; secret-bearing
  environment names retain their names but receive `<REDACTED>`, including token/password/API/access/
  private-key/auth/cookie/credential/secret/registry/repository families and `_URL`/`_URI` suffixes.
  URL userinfo and common secret query values are scrubbed from all captured strings, including
  requirements, entry points, locations, and metadata errors. The CLI has no unredacted switch;
  library callers can opt out only explicitly with `redact=False` in a controlled context.
- Added bounded process and output handling: a 10-second default timeout, 8 MiB probe stdout limit,
  64 KiB retained stderr limit, concurrent pipe draining, and process-group/session cleanup. POSIX
  timeout or inherited-pipe cleanup uses `SIGTERM` followed by bounded `SIGKILL`; Windows uses
  `taskkill /T /F`. Missing/invalid interpreters, nonzero exits, timeout/size/protocol failures, and
  output errors return exit status `2` without a traceback. envlens remains a current-user process
  boundary, not an operating-system sandbox.
- Added atomic same-directory JSON replacement with POSIX mode `0600`, refusing symlink/special-file
  destinations and replacement of the selected interpreter, including an existing hardlink alias.
- Local Python 3.10 validation is `50/50` tests (CLI 7, I/O 6, probe/process 12, redaction 7,
  snapshot normalization/schema 18), with Ruff check/format and strict mypy for six source modules
  also passing. Released ici `v0.10.2` local deep verification is `PASS` across 14 total engines:
  `13 PASS / 0 WARN / 0 FAIL / 0 ERROR / 1 compile_db SKIP`, with line/function/branch
  `93.0% / 100.0% / 84.6%`, TEM `5.00`, complexity max `13`, and cycle/sanitize also `PASS`.
  The path-aware manifest now includes envlens, while a Python 3.10/latest matrix validates tests,
  schema, strict typing, reproducible pure wheel/sdist builds, package metadata, and a clean-wheel
  smoke. PR #50 merged as `c307ac1ab01e12e4ac81a34623eb669da0e43641`; exact-main run
  [`33698248293`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33698248293) passed all jobs,
  the main publisher, and `Merge Gate`, with the PR-only publisher skipped as expected. Stable
  release evidence remains pending; snapshot diff, dependency/wheel compatibility, project import,
  and runtime smoke stay in future E2/E3 work and E4 remains the release boundary.
- Exact-main artifacts include EnvLens ici `9872574260`, Python 3.10 `9872561889`, latest Python
  `9872564898`, and Quality Zoo `9872561713`. Python 3.10/latest emitted the same wheel SHA-256
  `906c86270b2cad5c693816d43fa4d143e50be0cc3f3a852fdd538a453f51e3df` and sdist SHA-256
  `d8bf786c6bb6569371bc27092f3ade01e03315abca4fb862b66d22cd6ac9e63e`. The exact-main EnvLens
  report was `PASS` with 13 total engines, 12 `PASS`, one C++ `SKIP`, test `50/50`, line/function/
  branch coverage `93.0% / 100.0% / 84.6%`, and TEM `5.0`. The four main Pages were byte-identical
  to their artifacts and passed exact-title/Zero-CDN checks. The complete page sizes and hashes are
  recorded in the [EnvLens workthrough](workthrough/2026-09-03-envlens-snapshot.md).

### Quality Zoo known-answer contract

- Added the Quality Zoo scenario contract and local candidate consumer. The registry is the
  dependency-free `quality-zoo/manifest.json`, intentionally JSON rather than a TOML parser so the
  runner remains standard-library-only and compatible with Python 3.10. Scenario `ici.toml` files
  remain ici inputs; the runner validates their location and executes only its fixed verify argv.
- Documented the explicit local `ICI_BIN` boundary, candidate ZIP intake, and threat model. Intake
  checks the expected archive SHA-256, exact archive members/modes, safe extraction, provenance,
  executable digest/size, sidecar, and `--version`; an optional authenticated evidence directory
  must contain exactly `artifact.json`, `candidate-run.json`, `gate-check.json`, `gate-job.json`, and
  `gate-run.json`.
- Separated the Quality Zoo `contract_verdict` from ici's `observed_suite_status`. A known `WARN`
  or `FAIL` can be part of an expected answer, but schema, status, evidence, location, positive
  finding, forbidden-finding, exit-code, and version checks must all match for contract `PASS`.
- Corrected expectation selection for schema-2 `scenario.json`: the released ici `v0.10.2` digest
  `8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4` reports legacy
  `MEASURED`/`high`, while candidate digest
  `53fc75f0a073a74689babfe9ef8a4b2378995002d7d563bdc52da548fdbb9ee8` reports provenance-aware
  `ESTIMATED`/`medium`, despite the same package version. The selector maps each exact executable
  SHA-256 to a full strict schema-1 expectation and fails closed for unknown digests. Ordinary CI
  uses the released expectation; candidate validation uses the candidate expectation.
- Missing-report failures now retain a bounded `quality-zoo.runner-error/v1` `run.json`, including
  exit status, report presence, and truncated stdout/stderr, so the always-upload CI step carries
  actionable diagnostics instead of an empty artifact path. Executable symlinks are rejected
  before resolution, and the selected ici binary is rehashed after its version probe and scenarios.
- Recorded the first local candidate consumer evidence: candidate run `33689056008`, source
  `7872a7b80899cbd3d40d92d18e7920cd7e2283e7`, artifact `9869395069`, ZIP SHA-256
  `640e50ecf5b099174c16f1ef5d2b5b87945329711e96f926d94c3cc04109081e`, candidate `ici.pyz`
  SHA-256 `53fc75f0a073a74689babfe9ef8a4b2378995002d7d563bdc52da548fdbb9ee8`, and version `0.10.2`.
  Authenticated API evidence validation passed. The first `python.dead-private-function` scenario
  was contract `PASS` with observed suite `WARN`, exactly one matched finding, and no clean
  counterpart false positive.
- Added an exact-SHA expectation for the sanitizer-normalization candidate target
  `9d470edca7ab037a24dcd6594531a822f116548b`. Its producer run
  `33706057540` succeeded and artifact `9875319095` supplied a raw ZIP SHA-256 of
  `4aec084b3a30ac01a1df5124fa3b42b7f51d23f66c12b490194a84549be9db27`; the contained executable
  SHA-256 is `e7f1a2ce7147057538873a802715c7bf2b12e530a85070af862e02e378caceb8`. Authenticated
  local intake evidence succeeded, and the Q0 runner returned contract `PASS`, observed suite
  `WARN`, one matched finding, and no errors. This is local candidate evidence for the Python
  dead-code known-answer scenario; that local run did not itself claim sanitizer coverage or remote
  acceptance, and the separately audited exact-revision workflow below records the later acceptance.
- Remote Q0 acceptance is complete. [PR #49](https://github.com/jihoon22-lee/toy-projects/pull/49)
  passed run [`33693241255`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33693241255),
  published exactly one sticky comment containing exactly one marker and three product HTML links
  at that time, and merged as
  `ed5fea2e881da77ac95482cf665e4e40bfe172f1`. Exact-main run
  [`33694452357`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33694452357) also passed,
  including the released-ici known-answer artifact and byte-identical trusted main Pages. Ordinary
  CI remains pinned to released ici `v0.10.2`. The later exact-main run
  [`33698248293`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33698248293) after EnvLens
  merge also passed the Quality Zoo contract: artifact `9872561713` recorded one stable scenario
  with the expected observed `WARN` and zero errors. No toy product version or release changed;
  Q1~Q5 Python/C++/Qt/build/binary/hybrid corpus expansion remains open.

- Added four stable C++ sanitizer scenarios: `cpp.asan-use-after-free`,
  `cpp.lsan-memory-leak`, `cpp.ubsan-signed-overflow`, and `cpp.sanitizer-clean`. Their strict
  expectations cover AddressSanitizer use-after-free, LeakSanitizer memory leak,
  UndefinedBehaviorSanitizer signed-integer overflow, and a clean sanitizer run with clean
  counterparts where applicable. Each scenario binds both the released ici `v0.10.2` executable
  SHA-256 `8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4` and the candidate
  executable SHA-256 `e7f1a2ce7147057538873a802715c7bf2b12e530a85070af862e02e378caceb8` to
  separate digest-selected expectations. Local all-scenario runs passed the contract for both
  digests (`5/5` scenarios, zero errors); the three defect scenarios intentionally observed
  `FAIL`, the clean scenario observed `PASS`, and the existing Python scenario observed its
  expected `WARN`. This closes only the C++ sanitizer ASan/UBSan/LSan-plus-clean subitem; the
  broader corpus remains open. That local run did not itself claim candidate remote acceptance, PR
  CI, merge, or release; the separate remote evidence is recorded below. There is no version change.
- Completed the ici-hosted exact-revision candidate-to-Quality-Zoo acceptance for candidate target
  `9d470edca7ab037a24dcd6594531a822f116548b` at exact ici workflow head
  `6df011f98be1a19092b112cb56c596dc35bcae4d`: [workflow run `33710695336`](https://github.com/jihoon22-lee/ici/actions/runs/33710695336)
  and job [`100509326331`](https://github.com/jihoon22-lee/ici/actions/runs/33710695336/job/100509326331)
  succeeded against toy main `2d0d7c0b2dcc137a782d6042438fc287bffdf570`. The workflow reverified
  authenticated provenance/API evidence for producer run `33706057540` and artifact `9875319095`
  (raw candidate ZIP `2,285,368` bytes,
  SHA-256 `4aec084b3a30ac01a1df5124fa3b42b7f51d23f66c12b490194a84549be9db27`; contained
  `ici.pyz` `2,284,045` bytes,
  SHA-256 `e7f1a2ce7147057538873a802715c7bf2b12e530a85070af862e02e378caceb8`) and uploaded the
  separate acceptance artifact [`9876797536`](https://github.com/jihoon22-lee/ici/actions/artifacts/9876797536)
  (`1,104,307` bytes, SHA-256
  `e66ae2b65988abe10fc5ddb92a5c3bb6fc238ec2f77b7fd27ccfe75c24194a5f`).
- The accepted suite was contract `PASS` for all five scenarios with zero errors. Observed status /
  rule / category / location matched exactly: `cpp.asan-use-after-free` = `FAIL` /
  `ici.legacy.sanitize.target` + `asan.heap-use-after-free` / correctness / `src/fault.cpp:5`;
  `cpp.lsan-memory-leak` = `FAIL` / `ici.legacy.sanitize.target` + `lsan.memory-leak` /
  resource / `src/fault.cpp:3`; `cpp.sanitizer-clean` = `PASS` /
  `ici.legacy.sanitize.target` + no tool rule / correctness / `tests/test_clean.cpp:1`;
  `cpp.ubsan-signed-overflow` = `FAIL` / `ici.legacy.sanitize.target` +
  `ubsan.signed-integer-overflow` / correctness / `src/fault.cpp:3:18`; and
  `python.dead-private-function` = `WARN` / `ici.legacy.dead.target` + no tool rule /
  maintainability / `src/bad.py:1` (with `src/clean.py` remaining forbidden). This closes only
  candidate remote acceptance and the Q2 runtime ASan/LSan/UBSan-plus-clean sub-scope. The
  workflow used read-only evidence and a separate artifact and caused no Pages, PR comment,
  release, tag, or branch mutation; broader Q2, Qt lifetime, Q1/Q3~Q5, I4, and product release
  remain open, and the stable toy pin/version remains ici `v0.10.2` with no version change.
- Added the Qt 6 Core `cpp.qt-missing-parent-constructor` bad/clean fixture. The bad
  `MissingParent` omits the `QObject *parent` constructor argument at `src/bad.cpp:3`, while the
  clean `ParentAware` counterpart in `src/clean.cpp` accepts and forwards it. The released ici
  `v0.10.2` expectation is `WARN` / `MEASURED` / `exact` for
  `ici.legacy.lint.target` + `clazy-ctor-missing-parent-argument`, category `maintainability`,
  at `src/bad.cpp:3`; the candidate expectation keeps the same rule/status/evidence/confidence/
  path/line but records category `resource` under `cpp_diagnostic_category_policy = tool-rule-v1`.
  Both expectations forbid a lint finding in `src/clean.cpp`.
- The candidate target is `e7a9f55be8893d91497a6e1d0bff6e2e5f4af5f3`; producer
  [run `33715173073`](https://github.com/jihoon22-lee/ici/actions/runs/33715173073) published
  [artifact `9878317009`](https://github.com/jihoon22-lee/ici/actions/artifacts/9878317009). The raw
  candidate ZIP is `2,288,897` bytes with SHA-256
  `1165312e36344244fe0591e4fbcf869d126a0a7160a21099ee13c34ae8144d5e`; its contained `ici.pyz`
  is `2,287,574` bytes with SHA-256
  `985c81a63363356619207870cddb0d8cd9854a46925a3e0a745e54bd543d5b51`. Its provenance binds
  successful main `Merge Gate` [run `33714515219`](https://github.com/jihoon22-lee/ici/actions/runs/33714515219).
  Local Quality Zoo unit validation is `57/57` passed, and the new candidate run with Qt excluded
  returned contract `5/5 PASS` for the existing scenarios. Qt candidate remote acceptance, PR CI,
  exact-main CI, and Pages remain pending. The candidate is non-stable; no version or release
  changed.

- Added the candidate-only CMake/CTest ThreadSanitizer pair
  `cpp.tsan-data-race` and `cpp.tsan-synchronized`. The red fixture uses two
  threads released by an atomic readiness gate to perform one unsynchronized
  update of project-owned `src/race.cpp:15`; the clean counterpart protects the
  same two-thread update with `std::mutex`. Both fixtures require `g++` and
  `cmake`, select only the required deep `thread_sanitize` engine, forbid skips,
  and use the fixed `verify --profile deep --no-cache` command. The native CMake
  smoke confirms the red binary emits one real TSan data race and fails, while
  the synchronized binary passes. `quality-zoo/manifest.json` is unchanged so
  released ici `v0.10.2` CI remains compatible; the new
  `quality-zoo/candidate-manifest.json` contains the existing six plus these two
  candidate-only entries. Their schema-2 selectors intentionally use an all-zero
  SHA-256 placeholder until the merged ici TSan candidate artifact is produced;
  candidate runner acceptance, remote acceptance, PR CI, exact-main CI, Pages,
  merge, and release evidence remain pending. No version or release changed.

### Sticky PR report comment cardinality

- The pull-request report verifier now enumerates every paginated PR comment and fails closed unless
  the exact `<!-- ici-report -->` marker occurs once in exactly one comment. Duplicate comments or
  repeated markers are reported as a contract failure and are never deleted automatically. The
  ordinary CI gate remains pinned to released ici `v0.10.2` with its existing SHA-256.

### LogLens filter diagnostics and bounds

- Hardened the recursive-descent filter parser with deterministic byte-based diagnostics. `ParseError::position`
  remains the start offset for existing callers, and the new `ParseError::end` completes a half-open
  `[position, end)` range; the GUI and CLI render the range in filter errors. The appended field keeps
  existing `position`/`message` aggregate initialization source-compatible.
- Added bounded filter queries (4,096 input bytes, 256 AST nodes, 1,024 decoded bytes per literal) while
  retaining the 64-level nesting cap. Oversized queries, literals, and ASTs now fail before unbounded
  parser state can grow, with stable messages and ranges.
- Quoted filter values now decode only `\\"` and `\\\\`. Unsupported escapes and unterminated quotes are
  rejected, while UTF-8 input bytes remain byte-preserving. `~` and `!~` continue to perform literal,
  case-insensitive substring matching rather than regular-expression matching.
- CLI `--level` and `--filter` are parsed independently so diagnostics map back to the user-supplied
  argument instead of a synthesized conjunction. The GUI parses the untrimmed UTF-8 input, preserves
  the previously applied filter after a failed apply, points depth failures at the extra nesting token,
  and spans an unsupported UTF-8 escape through its complete scalar. Structured parser token metadata
  also removes the new clang-tidy swapped-parameter warnings. Final local validation used the public
  ici `v0.10.2` `ici.pyz`, SHA-256
  `2af5198d1348a64c39f4f37d12657aa9a2c4bf3ddf034a9099909c41e86e30e7`. Its uncached deep suite is
  `WARN` only because clazy is unavailable and pre-existing lint findings remain. The changed
  `filter_expr.cpp`, `main.cpp`, and `main_window.cpp` have zero actionable lint targets; the overall
  lint result still contains 26 targets because ici currently counts 16 clang-tidy `note:` lines
  separately, which is recorded as a known ici engine follow-up. `compile_db` is `PASS` for 14/14
  production units across 40 configurations; `test` is `PASS` for 12/12 with line/function/branch
  coverage `93.3% / 96.7% / 82.4%`; `complexity` is `PASS` with max 15 across 218; `sanitize` is
  `PASS`. The HTML artifact is 484,899 bytes with exact title `ici Verification Report — loglens`
  and Zero-CDN. LogLens version/release remains pending, and this evidence does not claim broader
  L3 completion.

### DiskMap explorer workbench (merged GUI milestone)

- Completed the historical `feat/diskmap-explorer-ui` GUI slice on top of the D3 core projection.
  `MainWindow` owns one shared immutable scan document, and `NodeKey` is the navigation
  identity shared by the treemap, sortable table, and breadcrumb trail. Both views share the
  current root, filters, and metric; the table can additionally switch to a recursive
  largest-files projection. Accessible breadcrumb actions and table activation provide equivalent
  directory navigation.
- Added search, type/size/age/state filters, selectable logical/allocated/reclaimable metrics,
  and a bounded largest-files projection. The GUI explains the three size meanings and keeps
  unknown, incomplete, cycle, depth, mount, and scanner-filtered facts visible instead of
  presenting uncertain physical totals as exact values.
- Rescans restore the deepest valid `NodeKey` trail and selection, fall back safely when an entry
  disappears or changes identity, and reject stale generations and mismatched result metadata.
  While a scan is running, explorer interaction is frozen; atomic progress state remains alive
  until late worker callbacks are harmlessly drained, including after window destruction.
- The local native qmake matrix is clean on Qt 5.15.18 and Qt 6.10.2 with `-Werror`: all `11/11`
  targets pass. Focused QtTest counts are MainWindow `29`, TreemapWidget `12`, and
  NodeTableModel `11`.
- Deep no-cache verification with the public ici `v0.10.2` asset (SHA-256
  `8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4`) is `WARN` with
  `10 PASS / 2 WARN / 0 FAIL / 0 ERROR / 2 SKIP`, TEM `4.95`, `16/16` production units across
  `30` configurations, line/function/branch coverage `96.1% / 99.1% / 83.4%`, complexity max
  `14`, and sanitizer `PASS`. The HTML artifact is exactly `499,265` bytes, SHA-256
  `9b624303b6191c6ead73079aa42636f318b495e807699601cd403a960cf059c3`, and its exact-title /
  Zero-CDN checker passes.
- The local limitation is that `clang-tidy` and `clazy` are unavailable, ici does not yet provide
  C++ type analysis, and exact C++ dead-symbol analysis remains pending. The heuristic duplicate
  warning is `6.42%` across `34` groups; it is linked
  to ici I4-3's robust duplicate backlog, so product code is not contorted around false-positive
  clone shapes. The implementation was subsequently merged by [PR #46](https://github.com/jihoon22-lee/toy-projects/pull/46)
  as `main` commit `0cdd63953179a1dc885ed660e955b399d54243b7`. PR workflow
  [`33627322683`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33627322683) and exact-main
  workflow [`33628585439`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33628585439) were
  all green; the PR sticky comment had exactly one marker and exactly three report links, and PR/main
  artifact-to-Pages byte comparisons passed. The feature branch and local worktree were deleted
  after merge. DiskMap remains `0.1.0` under `Unreleased` with no product release; D4~D7
  cleanup/trash/snapshot/release remain pending. The exact PR/main artifact and Pages table is
  centralized in the [D3 explorer workbench workthrough](workthrough/2026-09-02-diskmap-explorer-workbench.md).

### DiskMap explorer projection (first core slice)

- Added a Qt-free view projection for logical, allocated, and reclaimable metrics,
  deterministic node keys/issues, conjunctive search/type/size/age filters, and
  stable visible-child/largest-regular-file ordering. This entry preserves the historical
  core-only slice; its GUI and cleanup work were separate follow-up scopes.

### Roadmap status reconciliation

- Marked the master-plan T0 checkpoint complete after the merged Qt shell, native/ici, Qt5/Qt6,
  PR, and exact-main evidence already recorded in the handover.
- Reconciled the historical ROADMAP/ICI-GAPS entries: the hand-written-Makefile A-2 limitation
  remains open; BuildScope B-2 comparison is complete; B-3 language-scope disclosure is explicit
  while C++ dead/resource/maintainability coverage remains owned by ici I4-3/I4-4. C-7's original
  evidence-taxonomy ambiguity is documented as ici-owned without claiming future gate policy closed.
- Marked the old Qt-shell document as a completed-T0 historical recipe without mechanically
  rewriting its detailed task checkboxes. No product version or release was changed.

### Trusted main report publication

- Added a separate exact-`main` publisher for the BuildScope, DiskMap, and LogLens ici HTML reports.
  It runs only after every quality job succeeds, rejects a stale checkout or superseded `main` SHA,
  downloads the checksum-pinned public ici `v0.10.2` artifact, and publishes explicit
  `<project>/main/index.html` destinations without relying on the PR-only sticky-comment job.
- Added a bounded standard-library HTML contract checker and Python 3.10-compatible unit coverage.
  Each public Pages response must be byte-identical to its local artifact, have the exact project
  title, and have no external script, stylesheet, image, frame, media, form, SVG-use, or CSS resource
  dependency.
- Extended `Merge Gate` so PR runs require the sticky-comment publisher while `main` pushes require
  the trusted main publisher. A skipped event-specific publisher can no longer leave a green push
  with missing stable report URLs. Static workflow contract tests prevent the event split, exact-SHA
  checks, checksum pin, explicit destination labels, or byte-level public audit from regressing.

### Release discipline — deliberate per-project version cadence

- Documented that portfolio direction, B-stage progress, ici pin changes, and CI/runner-only changes
  do not automatically bump a toy product version. Each toy product versions independently.
- Reserved `patch` for defect/security/compatibility regressions in an already-public stable product,
  and `minor` for a cohesive user-usable checkpoint backed by native tests, released-ici verification,
  PR and exact-main CI/Pages, docs/limitations, and reproducible release assets.
- Clarified that candidate, pre-release, and unreleased states are not stable releases. BuildScope
  `0.5.0` is the bundled B3/B4/B5 first usable stable release boundary; later work stays under
  `Unreleased` until a comparable checkpoint is complete.
- Added the PR naming rule that product/technical outcomes are primary and plan codes such as `T0`/`B1`/
  `D2` are secondary body or label metadata only. Historical evidence is preserved; an operational
  ici pin change remains independent from the toy product version.

### Publication closeout for BuildScope 0.5.0

- Stable publication completed on 2026-09-02 KST (`published_at` `2026-09-01T22:36:42Z`) as the
  [BuildScope `0.5.0` GitHub Release](https://github.com/jihoon22-lee/toy-projects/releases/tag/buildscope-v0.5.0),
  release ID `380863869`, with `immutable=true`, `draft=false`, and `prerelease=false`. The
  successful tag workflow [run `33566464110`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33566464110)
  ran at exact `main` `fda8b5fb068b68c04c8c40e297812fbe79cee3da`.
- Provenance is the annotated tag object `dcaaf83a5842f6d7fc6c47e3b212e26b9528c342`, peeled to
  that exact `main` SHA. The successful `Merge Gate` is [job `100050176790`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33565542193/job/100050176790)
  in [run `33565542193`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33565542193).
- The final release body SHA-256 is
  `9e58639c280655bf50b510ef676bb3e5f458cf2021c3c6c6b24c3b625945dd3b`; exactly `9` fresh-downloaded
  assets were independently audited. The public ici `v0.10.2` pin is
  `8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4`.
- The final deep report is `WARN` with `11 PASS / 3 WARN / 0 FAIL / 0 ERROR / 0 SKIP`, tests
  `97/97`, line/function/branch coverage `93.5% / 99.0% / 77.2%`, compile DB `12/12` units across
  `27` configurations, exact Qt6 codegen (`MOC 1`, `UIC 1`, `RCC 1`), and exact `12`-source/config
  coverage for both `clang-tidy` and `clazy`; TEM was `4.95`.
- The final HTML is `1,344,843` bytes, SHA-256
  `0a0b50f8e056ad561427fd2141dbd8649dd43fdf111b2d6e187c220b0a610ee9`, with
  exact title `ici Verification Report — buildscope` and Zero-CDN; see the canonical
  [BuildScope README](buildscope/README.md) for the implementation and contract detail.

## [0.5.0] - 2026-09-02

- Added a shell-free semantic diff for two raw `compile_commands.json` arrays. Snapshot v1/v2/v3
  compatibility remains on the producer boundary; the diff output is the separate strict
  `buildscope.diff/v1` contract.
- Added relocation-aware normalization for compiler family/name/path/style and wrappers, launcher,
  language/standard, target/sysroot, ordered define/undefine actions, ordered include kind/path, and
  residual flags. Raw command spelling, build directory, output path/name, entry indexes/duplicate
  annotations, filesystem status, and snapshot diagnostics are ignored by policy.
- Added conservative added/removed/changed/moved TU pairing, deterministic duplicate pairing, and
  structured drift categories. Moves require a unique basename/role match and retain configuration
  drift; ambiguous matches remain visible as additions/removals with a warning.
- Added bounded slash-aware suppression globs (`*`/`?` do not cross `/`; `**` does), canonical
  deterministic export, exit statuses `0` (no visible drift), `1` (visible drift), and `2` (input,
  policy, security, or export failure), including strict malformed/oversized/duplicate-key and
  symlink/TOCTOU boundaries.
- Added native C++ strict diff parser/model/UI consumption, Python-to-C++ byte-identical hybrid
  coverage, and adversarial envelope/policy/semantic rejection tests. Python is `83/83`, Ruff
  check/format covers `19` files, mypy covers `15` source files, and the default Qt5 5.15.18 plus
  Qt6 6.10.2 Release CMake/CTest matrices are each `9/9` (`10/10` with the opt-in benchmark).
  The pure `buildscope-0.5.0-py3-none-any` wheel contains the v1/v2/v3 snapshot and diff v1
  schemas and no native extension.
- Historical local validation used the checksum-validated public ici v0.9.0 release under the
  uncached deep profile: suite `WARN`, 14 engines = 11 PASS / 3 WARN / 0 FAIL / 0 ERROR / 0 SKIP,
  `92/92` tests, line/function/branch coverage `93.5% / 99.0% / 76.7%`, sanitizer PASS,
  compile DB `12/12` production units and `27` configurations, and TEM `4.95/5.0`. The
  Zero-CDN HTML is 1,235,505 bytes with SHA-256
  `0c98a38b27e928df2c60dcadff9ecc3daa1072cb620354d9f4a9fe8d9b987f80`; this remains separate
  from the B4 remote CI evidence.
- B4 implementation and PR/remote/hosted evidence are complete on [PR #36](https://github.com/jihoon22-lee/toy-projects/pull/36),
  head `ce64613263f0c4358579012aab135e0b23341a0e`. [Run `33485837830`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33485837830)
  used ici `v0.9.1` and completed all `16/16` checks successfully. BuildScope was `WARN`
  (`10 PASS / 3 WARN / 0 FAIL / 0 ERROR / 0 SKIP`) with lint WARN (49 warnings), `92/92` tests,
  line/function/branch `93.5% / 99.0% / 77.3%`, compile DB `12/12` production units and `27`
  configurations, sanitizer PASS, and TEM `4.95/5.0`. The remote 100,000-entry / 25,000-source
  benchmark recorded model `65 ms`, filter `1,602 ms`, filtered sources `1`, budget `10,000 ms`,
  and correctness `true`. [Sticky comment #5489976814](https://github.com/jihoon22-lee/toy-projects/pull/36#issuecomment-5489976814)
  has exactly one marker and three hosted-report links. Pages reports were HTTP 200 with exact titles
  and zero external resource references: BuildScope `1,319,378` bytes / SHA-256
  `5dc517d3ec8324cb1aedb6e611120ac9ae2951e27851cbc6b0e28303d02c5d43`, diskmap `337,554` bytes /
  `39a52d1e3d5b9eed6bbc1ec5253f1bf837deb4a7bb33ed2f2ef3b32df0f90e0e`, and loglens `492,746` bytes /
  `0480f07aea266c0777403ac37ebf90d4acd10f84b04e49aa2a9ccf99a6153007`.
- PR #36 was squash-merged to `main` as `590899a0a9430e9ce35162b301bfef5d7dfc78a4`, and its
  feature branch was deleted. Exact-main [CI run `33488169769`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33488169769)
  completed with all 14 prerequisite jobs and `Merge Gate` successful; the PR-only publisher was
  skipped as expected. [Dependency Graph run `33488174425`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33488174425)
  also succeeded on the same head.
- Historical boundary note: at the B4 merge boundary, the `0.5.0` product release artifact and B5
  hybrid integration were pending/not started. The following entries preserve the later local
  candidate work; current remote evidence is recorded below.
- Historical local release-candidate work added the `tools/build_standalone.py` surface, which validates all public
  version surfaces and creates an atomic, fixed-metadata standalone `buildscope.pyz`; direct tests
  verify reproducibility, execution, schema inventory, and symlink refusal. CMake install rules now
  lay out native CLI/GUI binaries, docs, schemas, and the CMake/qmake examples for a bundle.
- Added `examples/cmake`, `examples/qmake`, and [docs/quickstart.md](buildscope/docs/quickstart.md)
  covering CMake/qmake compile-database inputs, Python artifact → JSON → native CLI/GUI handoff,
  diff inspection, and qmake capture limitations.
- Historical local B5 deep/no-cache evidence used public ici `v0.10.0` with the literal SHA-256 pin
  `6d5f8c008b3b5393a61b2c1a418124eb66393c9eaab0abbb7d1c7922162bed9b` and
  `ICI_PYTHON=/tmp/toy-b5-py310/bin/python`: `Suite WARN`, 14 engines = `11 PASS / 3 WARN / 0 FAIL /
  0 ERROR / 0 SKIP`, tests `96/96`, line/function/branch `93.4% / 99.0% / 76.7%`, TEM `4.95`, and
  compile DB `12/12` production units across `27` configurations with `0` issues. Qt codegen was
  exact for 3 inputs (MOC `1`, UIC `1`, RCC `1`) with 12 Qt6 compile units. The JSON report is
  2,873,207 bytes / SHA-256 `ea5fce118e6edad8fa5af24c821663e4290805570377e9b7c190de8da1029612`;
  the Zero-CDN HTML is 1,264,867 bytes / SHA-256
  `4e12e77ee11f98b2d5bb146bd3d08252b088083726e6f11235e25a449471a565`, exact title
  `ici Verification Report — buildscope`, with 0 external references. `clang-tidy` and `clazy`
  were unavailable locally; the limitation was subsequently covered by the remote PR gate recorded below.
- Added the `buildscope-release.yml` contract: a fixed annotated `buildscope-vX.Y.Z` tag target at
  exact `origin/main` with a green `Merge Gate`, Python `3.10/3.14` and Qt `5/6` release matrices,
  generated MOC/UIC/RCC checks, pure wheel/sdist checks, wheel/pyz-to-native handoff, and deterministic
  bundle plus `SHA256SUMS` assets. The workflow is tag-only, allows up to 20 minutes for the
  exact-main gate, revalidates the fixed tag target before publication and final audit, fails on
  unmatched asset paths, and audits the exact nine names, sizes, states, and API digests.
- Added the newest exact Merge Gate identity boundary: `ci/check_buildscope_merge_gate.py` selects
  the newest exact `Merge Gate` check-run by positive check-run ID, requires the GitHub Actions app and
  completed success, then independently verifies the referenced Actions run's ID, repository/head
  repository, SHA, workflow name/path/event/status/conclusion, and canonical URLs. The annotated tag's
  peeled commit remains authoritative; the release API's `target_commitish` is not compared as proof.
- Hardened the final public release audit to require exactly nine API assets, reject malformed or
  duplicate asset names, and then verify the exact name set, uploaded states, sizes, and SHA-256
  digests through a tested Python 3.10 helper. Bounded regular JSON input, nonempty regular local
  files, symlink refusal, and streaming hashes avoid both hidden duplicates and full-bundle reads.
- Added `buildscope --version` and standalone-pyz coverage so wheel and zipapp consumers can prove
  the exact `0.5.0` producer before supplying a compilation database.
- Reworked release publication to remove softprops' existing-release update behavior. An authenticated
  paginated release-slot audit is fail-closed: an empty slot alone may create a direct private draft;
  an existing final release is never mutated and follows an audit-only path, while an existing draft
  or ambiguous slot stops the workflow. A newly created draft carries a terminal current-run owner
  marker; ambiguous creation recovery accepts only one exact owner-marked private draft with zero
  assets and the expected body digest. Its fixed numeric release ID is checked before uploading exactly nine paths without
  `--clobber`.
- The workflow normalizes `RELEASE_NOTES.md` once and materializes separate expected final and
  owner-marked draft body files. It computes the exact UTF-8 SHA-256 of each: the draft digest is
  rechecked at creation, before upload, prepublish, and failure reporting, while the final digest is
  required for publication and final audit.
- Direct asset uploads use the fixed numeric release-ID endpoint with a binary body, HTTPS/TLS, a
  20-second connect bound, a 300-second transfer bound, and an exact HTTP 201/uploaded response
  contract. No release is selected by tag after the fixed ID has been assigned.
- The prepublish gate now validates the ordered eight-entry `SHA256SUMS`, pyz sidecar, exact release
  payload/archive contracts, schema-byte agreement, provenance, B5 deep JSON, and Zero-CDN HTML. ZIP
  archives receive a bounded EOCD/central-directory preflight before `ZipFile` constructs its
  inventory. The workflow then downloads all nine draft assets through a bounded numeric-API-ID
  downloader into a fresh directory. The same fixed draft is re-audited immediately before publication.
- Publication uses an explicit fixed-ID PATCH and reconciles an ambiguous response: an exact final
  release succeeds, an exact private draft is retried, and any other state fails closed. The write-token
  publish steps never execute a downloaded remote BuildScope pyz; remote pyz bytes are inspected as
  payload data. The final public release is independently downloaded into a fresh directory, every
  asset is byte-compared with the current `dist` (including existing-final audit-only mode), and release
  metadata by ID, metadata by tag, asset records, and the peeled tag are re-fetched after download.
  Failed current-run-owned drafts are preserved for explicit manual review; no remote draft is deleted
  automatically. On an empty-slot failure, the report step can recover a lost-ID draft only by paginating
  and matching the exact current-run owner marker, zero-asset state, and expected body digest, solely
  to report and preserve it.
- The current dependency-free CI helper discovery suite passes `145/145` on Python `3.10` and `3.14`;
  `actionlint`, Ruff check/format, and mypy also pass. This is implementation evidence only and does
  not claim a new PR, tag, or release.
- Hardened both release API polling loops so transient GitHub 404/5xx/API failures are retried
  inside their existing bounded windows instead of being terminated early by shell `errexit`.
- Updated the current CI, release workflow, and B5 report contract to the corrective public ici
  `v0.10.1` artifact, pinned by literal SHA-256
  `9e262730b49420c59ee115cf389881dbfbb944b6a96ca1d397a1ecb247ec17ca`. The v0.10.0
  local measurements below remain historical evidence; this dependency correction does not change
  the BuildScope `0.5.0` product version.
- Historical candidate evidence: the Qt5/Qt6 deep legs temporarily built the exact final ici diagnostic-correction candidate
  `27f4e5cf820ceb36b24711be927f19076472c822` while the ordinary portfolio jobs continue to
  exercise the public v0.10.1 checksum pin. Candidate
  `e5096e10e9ce0069d5cea951dbdb28f87ee60e14` passed all 21 jobs in run `33536526972`, including
  both deep legs and the release contract; the single sticky comment pointed to three independently
  audited HTTP 200, exact-title, Zero-CDN Pages reports. The earlier candidate
  `040e61e64df20d64923df80a6c7ea29993e5c3ac` remains recorded by run `33531285208`: both deep
  legs exposed Clang tooling selecting GCC 14 libstdc++ headers for the GCC 13 compile database.
  This was cross-repository candidate evidence, not a stable ici release or a BuildScope version
  change; the final branch subsequently returned to a released ici artifact.
- The first B5 PR deep matrix exposed a Qt5-only sanitizer failure in `test_main_window`: all 12
  test functions passed (14 QtTest lifecycle result entries), then LeakSanitizer found 1,288 bytes
  retained by deferred Qt5 offscreen/fontconfig/widget work at process shutdown. The test now drains
  deferred-delete and event work after every case, removing the test-owned portion. A rebuilt local
  Qt5 ASan/UBSan tree passes `9/9` with the original `detect_leaks=1` policy, while the hosted
  Ubuntu 24.04 image still reports smaller process-global offscreen-plugin allocations. The Qt5 deep
  workflow suppresses only residual stacks through the known `libqoffscreen.so` platform plugin
  using `LSAN_OPTIONS` with `detect_leaks=1`; Qt6 remains unsuppressed. The control fixture
  `ci/fixtures/outside/project_leak.cpp` runs with the same options and must still produce a
  nonzero LeakSanitizer result, proving project-owned leaks remain visible.
- Replaced the temporary source-pinned ici diagnostic candidate in the Qt5/Qt6 deep legs with the
  checksummed public ici `v0.10.2`, pinned by literal SHA-256
  `8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4`. Both deep legs validate
  the sidecar, literal digest, and downloaded bytes before executing the pyz from an absolute
  runner-temp path; the B5 checker now requires producer version `0.10.2`, while BuildScope remains
  product version `0.5.0`.
- The BuildScope release boundary is now remotely validated. [PR #38](https://github.com/jihoon22-lee/toy-projects/pull/38)
  head `3ba645eae5181698e1272729dddaa8a72189b067` passed [run `33545168957`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33545168957);
  [sticky comment #5494648837](https://github.com/jihoon22-lee/toy-projects/pull/38#issuecomment-5494648837)
  contains the three audited HTML report links. The PR was squash-merged to `main` as
  `069a3a86c0164a1d2a88710f9c3c48a398c8087e`, and exact-main [run `33546046566`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33546046566)
  passed with the PR-only publisher skipped as expected.
- The trusted exact-main Pages publisher is also validated. [PR #39](https://github.com/jihoon22-lee/toy-projects/pull/39)
  head `b861ff5b4cc0314aae5ec9f6dab905648233216d` passed [run `33548626203`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33548626203);
  [sticky comment #5499184834](https://github.com/jihoon22-lee/toy-projects/pull/39#issuecomment-5499184834)
  records the PR evidence. The PR was squash-merged to `main` as
  `c80e922f0d0911019cfa8b5c67a8b654c556a68c`, and exact-main [run `33549475034`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33549475034)
  passed.
- At exact-main run `33549475034`, stable `main` Pages responses were HTTP 200 with the exact project
  title, Zero-CDN resources, and byte-identical content to that run's audited artifacts:

  | Project | Hosted report | Bytes | SHA-256 |
  |---|---|---:|---|
  | BuildScope | [buildscope/main](https://jihoon22-lee.github.io/toy-projects/buildscope/main/) | 1,349,088 | `dd7ec9b49281875f812b9a9c6b4e18a028051936a37441f19d907098c05dcc65` |
  | diskmap | [diskmap/main](https://jihoon22-lee.github.io/toy-projects/diskmap/main/) | 339,929 | `1bd6ebdce2206e4538fb28c20c17f2d504f1a160e15f5d8d4e2923b15b399e65` |
  | loglens | [loglens/main](https://jihoon22-lee.github.io/toy-projects/loglens/main/) | 495,237 | `11e4c78eed957e237bcea8ec5ea64c3894751eafbe639c454e47ab0acd70df96` |

- BuildScope `0.5.0` combines the public ici `v0.10.2` pin
  (`8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4`) with the completed
  PR/main verification boundary. Its publication boundary requires the exact-main fixed annotated-
  tag check, fail-closed slot policy, prepublish audit, and final nine-asset public-byte audit.

## BuildScope 0.4.0 — historical B3 implementation and remote evidence

- Added optional include explanation modes: `--include-analysis estimate` performs bounded
  source-scan estimation, while `--include-analysis compiler` records compiler-measured edges
  through the shell-free replay boundary.
- Added strict, self-contained `buildscope.snapshot/v3` with ordered search candidates, selected
  paths, same-basename alternatives, project/vendor/generated/system/missing/unresolved
  classification, explicit `estimated`/`compiler-measured` evidence, and source-scan versus
  compiler-diagnostic location evidence.
- The strict v3 consumer cross-checks `resolved` against the single selected search candidate and
  requires `alternatives` to equal distinct existing unselected candidates; duplicate search-path
  records retain order but select only the first candidate.
- Kept v2 as the default and preserved the explicit v1 raw compatibility projection. v3 without a
  mode selects `estimate`; analysis cannot be combined with v1 or v2.
- Added bounded allowlisted GCC/Clang replay and failure records for unavailable compilers, unsafe
  options, stale paths, trace failures, unit limits, and time budgets.
- Split compiler execution, argument sanitization, and process/trace bounds into the new
  `buildscope/python/buildscope/compiler_replay.py`; `include_analysis.py` retains source scanning,
  edge assembly, and trace interpretation. The current modules are 483 lines for
  `include_analysis.py` and 255 lines for `compiler_replay.py`.
- Added Qt Include Edges inspection, ordered candidate expansion, collision details, replay command
  display, and parent source-location navigation.
- Added the v3 schema, native contract/UI coverage, Python replay/analysis tests, and hybrid
  producer-to-consumer coverage.
- The historical (pre-PR #34) checksum-verified `ici v0.8.0` no-cache local public-release
  validation completed with
  `Suite WARN` (verification passed): engines `11 PASS / 2 WARN / 0 FAIL / 0 ERROR / 0 SKIP`, line
  `PASS` (`5,151` total / `4,591` code / `3` comment / `557` blank across `25` files), lint `PASS`,
  `compile_db` `8/8` production units, `19` configurations, `0` failures/warnings, `63/63` tests,
  line/function/branch `92.6% / 98.9% / 79.1%`, TEM `4.94`, complexity `PASS` (max `14` across
  `251` functions, `0` issues), sanitize/security/resource/cycle/dead/exception `PASS`, duplication
  `11.65%` (raw display `11.7%`, `78` groups, `179` findings), and total `34.71s`. WARNs were only
  type (C++ unsupported) and duplication. The `/tmp` HTML was `851,656` bytes with SHA-256
  `07d25971e04ed6a4aece36724ce8cf5e3c0548b7c382941a810454d8521c3e34`, exact title
  `ici Verification Report — buildscope`, and `0` external references. This is local public-release
  validation, separate from the B3 PR/remote Pages evidence below.
- The final benchmark used `100,000` entries / `25,000` sources and recorded model `61 ms`, filter
  `1,126 ms`, budget `10,000 ms`, and correctness `true`. The pure
  `buildscope-0.4.0-py3-none-any.whl` packaged `compiler_replay.py` and the v3 schema; schema
  validation passed.
- Historical local candidate evidence recorded before PR #34: Python 3.10 pytest `57/57` PASS, Ruff
  check + format `14 files` PASS, mypy `11 source files` PASS, and Qt 5.15.18/6.10.2 Release
  CMake/CTest `6/6` PASS each.
- B3 remote evidence is complete. [PR #34](https://github.com/jihoon22-lee/toy-projects/pull/34)
  head `c3835cd4b0c859c38ae0f4afbdb20aae970515dc` passed all 16 CI checks, including `Merge Gate`
  and `Publish Reports & Sticky Comment`; its sticky comment
  ([#5487386460](https://github.com/jihoon22-lee/toy-projects/pull/34#issuecomment-5487386460)) has
  exactly one marker and exactly three project links. The BuildScope report was `WARN` with
  `11 PASS / 2 WARN / 0 FAIL / 0 ERROR / 0 SKIP`, TEM `4.94`, tests `63/63`, and
  line/function/branch `92.7% / 98.9% / 79.5%`; diskmap and loglens were `PASS` with TEM `4.92`
  and `4.80` respectively.
- The PR BuildScope benchmark used `100,000` entries / `25,000` sources and recorded model `118 ms`,
  filter `1,424 ms`, budget `10,000 ms`, and correctness `true`. Independent PR Pages checks for
  BuildScope, diskmap, and loglens were all HTTP 200 `text/html`, had the exact expected title, and
  had zero external attributes/CSS references. The reports were `858,143` bytes with SHA-256
  `e0b9c9ece1fb7268aa519bd0a4c62fd3da7c44a52b2efe6121393474d3ad36d4`, `311,847` bytes with
  SHA-256 `8a6b01544b99eee6f0c2b95758f81395032f2b67a7c1a600879447cf7fb5f3bf`, and `446,786` bytes
  with SHA-256 `1144759ef7e1b83ef7bd23f7bcfe9d02b05430a37af372350ec3b6e26d6c7ac7`, respectively.
- PR #34 was squash-merged to `main` as `9cce2699606e58ed67c3dac46f60dc7bf113bb60` and its
  branch was deleted. Exact-main CI run
  [33459591250](https://github.com/jihoon22-lee/toy-projects/actions/runs/33459591250) succeeded
  for all applicable jobs, including `Merge Gate` (PR publish correctly skipped on push), and
  Dependency Graph run
  [33459594605](https://github.com/jihoon22-lee/toy-projects/actions/runs/33459594605) succeeded
  on the same head.
- Historical note: the B3 implementation was shipped on `main`, but `0.4.0` remained unreleased
  until B5 hybrid release integration. ici I3 cross-repository comparison was complete at that
  point; the next toy-project stage was B4 configuration diff.
