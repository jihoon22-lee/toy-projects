# Changelog

## Unreleased

### BuildScope 0.4.0 — B3 local candidate

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
- The final checksum-verified `ici v0.8.0` no-cache local public-release validation completed with
  `Suite WARN` (verification passed): engines `11 PASS / 2 WARN / 0 FAIL / 0 ERROR / 0 SKIP`, line
  `PASS` (`5,151` total / `4,591` code / `3` comment / `557` blank across `25` files), lint `PASS`,
  `compile_db` `8/8` production units, `19` configurations, `0` failures/warnings, `63/63` tests,
  line/function/branch `92.6% / 98.9% / 79.1%`, TEM `4.94`, complexity `PASS` (max `14` across
  `251` functions, `0` issues), sanitize/security/resource/cycle/dead/exception `PASS`, duplication
  `11.65%` (raw display `11.7%`, `78` groups, `179` findings), and total `34.71s`. WARNs were only
  type (C++ unsupported) and duplication. The `/tmp` HTML was `851,656` bytes with SHA-256
  `07d25971e04ed6a4aece36724ce8cf5e3c0548b7c382941a810454d8521c3e34`, exact title
  `ici Verification Report — buildscope`, and `0` external references. This is local public-release
  validation, separate from B3 PR/remote Pages evidence.
- The final benchmark used `100,000` entries / `25,000` sources and recorded model `61 ms`, filter
  `1,126 ms`, budget `10,000 ms`, and correctness `true`. The pure
  `buildscope-0.4.0-py3-none-any.whl` packaged `compiler_replay.py` and the v3 schema; schema
  validation passed.
- Local evidence currently recorded: Python 3.10 pytest `57/57` PASS, Ruff check + format `14 files`
  PASS, mypy `11 source files` PASS, and Qt 5.15.18/6.10.2 Release CMake/CTest `6/6` PASS each.
  B3 PR/remote CI, hosted reports, B5 release integration, and ici I3 cross-repository comparison
  remain pending; this is not a release or a completed B3 status.
