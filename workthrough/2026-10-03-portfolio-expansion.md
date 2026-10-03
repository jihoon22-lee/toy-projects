# Portfolio implementation ledger

Baseline: `e40ae22`. The coordinator implemented EnvLens/AbiLens and shared integration.
After all three new products passed acceptance, their owners completed
BuildScope, LogLens and DiskMap as authorized.
Root CI, release configuration and documentation remain coordinator-owned.

## Existing products

- [x] BuildScope: bounded source reads, response-file option values, directive
  matching, normalized replay paths/globs and optimization flags; native tests.
- [x] BuildScope: analysis budgets/cancellation, partial replay, reverse includes,
  relocation, direct DB GUI import, asynchronous loading, editor navigation, UI.
- [x] LogLens: shared session input protection, strict format, persistent timeline
  selection; core and GUI regression tests.
- [x] LogLens: evidence-bound triage, full sessions, GUI plugins, structured
  correlations, whole-file search, layout/theme/timeline and layout persistence.
- [x] DiskMap: timestamp and duplicate keeper proofs, whole-plan protection,
  keeper policies, durable receipts, bounded scans, raw-byte snapshots, async UI.
- [x] DiskMap: comparison summaries, workbench tabs, treemap selection/colors,
  human-readable size input and truthful reclamation reporting.
- [x] EnvLens: requested interpreter, absolute runtime paths, package standards,
  installation origins, dependency paths, shadowing, batch compilation, policies.
- [x] AbiLens: conservative compatibility, unknown symbol evidence, ordered
  paths, richer ELF/symbol data, sysroot resolution, optional DWARF and policies.

## New products

- [x] TraceLens: saved trace parser/analysis/CLI/GUI, evidence, diff, bounds,
  fixtures, tests, benchmark, standalone install and documentation.
- [x] TestLens: runner XML collection, identity/attempt/cohort contracts,
  diff/history, offline HTML, policies, bounds and standalone distribution.
- [x] ServiceLens: rootfs unit/drop-in resolution, typed settings provenance,
  command/environment/dependency explanation, diff/check and distribution.

## Integration

- [x] Eight independent CI gates and test result artifacts.
- [x] Explicit release workflow chain, staged install packages, provenance.
- [x] Pages links/resources/triggers and benchmark directory creation.
- [x] Current README/ROADMAP/product docs, versions and compatibility notes.
- [x] Final source, schema, installed-product, sanitizer and GUI validation.

Only validated work is marked complete. Detailed commands/results are appended
as implementation checkpoints finish.


## Validated integration checkpoint

- EnvLens: 131 tests on Python 3.10 and 3.14; Ruff and strict mypy. Venv symlink,
  relative compilation, later-batch syntax errors, console return values,
  PEP/extras/direct URL, platform tags, source changes and shadowing regressions.
- AbiLens: native parser, integration, cleanup and real ELF evidence suites pass
  in default, optional libdw and ASan/UBSan builds. ELF32/64, exported data size,
  symbol removal, loader order, rootfs symlinks, v1/v2 and actual DWARF layouts.
- TraceLens: 3 CTest suites in normal and ASan/UBSan builds; installed CLI and
  CPack. 64 MiB benchmark: 671,088 calls, 0.807 s, 12,000 KiB RSS; cancellation
  11.418 ms on the local machine.
- TestLens: 47 pytest cases; Ruff, strict mypy, clean wheel command pipeline and
  offline HTML interaction at light/dark/mobile sizes. 100,000 case collection
  measured about 1.29 s and 300 MiB peak RSS locally.
- ServiceLens: 58 pytest cases after opt-in documented service-default edge
  support; Python 3.10/3.14, Ruff and strict mypy; clean wheel command pipeline.
- All workflow files pass actionlint. GitHub workflows and publication have not
  been executed by this local implementation task.


## Final existing-product acceptance

| Product | Normal checks | Instrumented / installed checks |
|---|---|---|
| BuildScope | Release 11/11 (includes 100k benchmark) | ASan+UBSan 10/10; installed compiler→snapshot→consumer→impact→GUI |
| LogLens | 20/20 | ASan+UBSan 20/20; installed CLI/GUI, session schema and whole-file search |
| DiskMap | Release 19/19 | ASan+UBSan+LSan 19/19; installed raw-byte snapshot/diff, keeper→Trash→restart→restore |
| EnvLens | 131 tests on Python 3.10 and 3.14 | Ruff/strict mypy; clean wheel snapshot/check |
| AbiLens | Parser, integration, clean, real ELF evidence suites | Default, libdw and ASan+UBSan; installed CLI; v2 schema validation |

BuildScope's local 100k-entry benchmark measured 67 ms model build and 1,199 ms
filtering. DiskMap's 10k-item normal/cancellation benchmark passed correctness.
Qt workbenches were also exercised and visually inspected in compact layouts.

Independent final reviews caught and fixed DWARF4 `.debug_types` traversal,
legacy bit-field offsets and unknown-kind symbol size changes. Real ELF tests
now prevent these cases from being reported as compatible.

Release review also fixed draft-without-tag checkout, explicit CI dispatch for
GITHUB_TOKEN-generated PRs, tag/artifact version equality and feature→minor
version selection. Six mocked-GitHub state-transition tests cover draft SHA
resolution, deferred tag/publish, mismatched tags, corrupt uploads, published
retries and PR CI dispatch. They perform no external mutations. Workflow lint,
embedded Python parsing, all schema metaschemas and generated documentation links
passed locally.

Versions: existing five products 0.2.0 development; new three products 0.1.0.
The release manifest keeps existing published baselines. Source changes remain
unstaged and uncommitted. No GitHub workflow, release or Pages deployment was
started by this task. Existing local historical tags were not modified.

Known supported boundaries are product-specific and documented: compiler/scan
budgets are cooperative, LogLens source fingerprints use identity and a 64KiB
prefix while notes hash whole records, AbiLens loader results are candidates and
DWARF public API reachability stays unknown, ServiceLens implements a documented
systemd subset, and TestLens reports observed failure frequencies without declaring
flakiness automatically.
