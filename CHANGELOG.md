# Changelog

## Unreleased

### BuildScope 0.5.0 — B4 semantic configuration diff + B5 release candidate groundwork (unreleased)

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
- At the B4 merge boundary, the `0.5.0` product release artifact and B5 hybrid integration were
  pending/not started; the following entries record the later B5 local candidate work.
- Added the B5 local release-candidate slice: `tools/build_standalone.py` validates all public
  version surfaces and creates an atomic, fixed-metadata standalone `buildscope.pyz`; direct tests
  verify reproducibility, execution, schema inventory, and symlink refusal. CMake install rules now
  lay out native CLI/GUI binaries, docs, schemas, and the CMake/qmake examples for a bundle.
- Added `examples/cmake`, `examples/qmake`, and [docs/quickstart.md](buildscope/docs/quickstart.md)
  covering CMake/qmake compile-database inputs, Python artifact → JSON → native CLI/GUI handoff,
  diff inspection, and qmake capture limitations.
- Local B5 deep/no-cache evidence used public ici `v0.10.0` with the literal SHA-256 pin
  `6d5f8c008b3b5393a61b2c1a418124eb66393c9eaab0abbb7d1c7922162bed9b` and
  `ICI_PYTHON=/tmp/toy-b5-py310/bin/python`: `Suite WARN`, 14 engines = `11 PASS / 3 WARN / 0 FAIL /
  0 ERROR / 0 SKIP`, tests `96/96`, line/function/branch `93.4% / 99.0% / 76.7%`, TEM `4.95`, and
  compile DB `12/12` production units across `27` configurations with `0` issues. Qt codegen was
  exact for 3 inputs (MOC `1`, UIC `1`, RCC `1`) with 12 Qt6 compile units. The JSON report is
  2,873,207 bytes / SHA-256 `ea5fce118e6edad8fa5af24c821663e4290805570377e9b7c190de8da1029612`;
  the Zero-CDN HTML is 1,264,867 bytes / SHA-256
  `4e12e77ee11f98b2d5bb146bd3d08252b088083726e6f11235e25a449471a565`, exact title
  `ici Verification Report — buildscope`, with 0 external references. `clang-tidy` and `clazy`
  were unavailable locally, so tool-backed deep evidence remains pending on the release runner.
- Added the `buildscope-release.yml` contract: annotated `buildscope-vX.Y.Z` tag at exact `origin/main`
  with a green `Merge Gate`, Python `3.10/3.14` and Qt `5/6` release matrices, generated MOC/UIC/RCC
  checks, pure wheel/sdist checks, wheel/pyz-to-native handoff, and deterministic bundle plus
  `SHA256SUMS` assets. The workflow is tag-only, allows up to 20 minutes for the exact-main gate,
  revalidates the remote annotated tag before and after publication, fails on unmatched asset paths,
  and audits the exact nine public names, sizes, states, and API digests. No B5 PR/remote CI/Pages
  run, annotated tag, or GitHub Release exists yet; the `0.5.0` product release remains pending.
- Hardened both release API polling loops so transient GitHub 404/5xx/API failures are retried
  inside their existing bounded windows instead of being terminated early by shell `errexit`.
- The first B5 PR deep matrix exposed a Qt5-only sanitizer failure in `test_main_window`: all 12
  test functions passed (14 QtTest lifecycle result entries), then LeakSanitizer found 1,288 bytes
  retained by deferred Qt5 offscreen/fontconfig/widget work at process shutdown. The test now drains
  deferred-delete and event work after every case. A rebuilt Qt5 ASan/UBSan binary using the same
  toolchain and configuration passes with the original `detect_leaks=1` policy, and the full
  sanitizer CTest tree is `9/9`; no leak suppression or sanitizer weakening was added.

### BuildScope 0.4.0 — B3 implementation + remote evidence (main candidate, unreleased)

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
- The B3 implementation is shipped on `main`, but `0.4.0` remains unreleased until B5 hybrid
  release integration. ici I3 cross-repository comparison is complete; the next toy-project stage
  is B4 configuration diff.
