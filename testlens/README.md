# TestLens

TestLens 0.1.0 is a local CLI and Python library for test result collection, comparison,
observed failure frequency and offline interactive reports. Original test names,
parameter IDs, observations and source digests remain available behind each conclusion.
It does not execute tests, contact a service, or diagnose a test as definitely flaky.

## Install and quick start

Requires Python 3.10+. Runtime dependencies are `defusedxml` and `jsonschema`.

```bash
python -m pip install ./testlens
# From this project directory:
testlens collect examples/baseline.xml --project demo --run-id baseline \
  --dialect pytest --complete --executed-at 2026-01-01T00:00:00Z --output baseline.json
testlens collect examples/current.xml --project demo --run-id current \
  --dialect pytest --complete --executed-at 2026-01-02T00:00:00Z --output current.json
testlens diff baseline.json current.json --fail-on new-failure
testlens history baseline.json current.json --last 20
testlens report current.json --baseline baseline.json \
  --history baseline.json current.json --output report.html
testlens validate current.json
```

The example diff intentionally returns exit code 1 under `--fail-on new-failure`.
Open `report.html` in any modern browser. Results, changes, history and input quality
are searchable; rows are paginated; expandable details include original XML locators
and source verification status. Reports work without a server or network. Input-quality
verification occurs at report generation, not automatically after the HTML is opened.

## Collect existing runners

Use unique output paths for concurrent runner invocations.

```bash
# Existing CMake projects: CMake 3.21+; one result per CTest registered target.
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-junit results.xml
testlens collect results.xml --dialect ctest-junit --project diskmap \
  --run-id BUILD_ID --scope ctest --output run.json
# Finer-grained Qt case results are a separate collection scope.
QT_QPA_PLATFORM=offscreen ./build/tests/test_example -o qt-results.xml,junitxml
testlens collect qt-results.xml --dialect qt --project diskmap \
  --scope qt-cases --run-id BUILD_ID --output qt-run.json
# pytest default xunit2 and xunit1/legacy JUnit reports are supported.
python -m pytest --junitxml=pytest-results.xml
testlens collect pytest-results.xml --dialect pytest --project envlens \
  --run-id BUILD_ID --output pytest-run.json
# Existing dashboard artifacts, no execution required:
testlens collect build/Testing/TAG/Test.xml --dialect ctest \
  --project demo --run-id BUILD_ID --output dashboard-run.json
```

`auto` recognizes CTest dashboard roots and generic JUnit roots. It deliberately does
not infer CTest JUnit/pytest/Qt from arbitrary suite names: specify `--dialect ctest-junit`
to retain runner-target granularity. Qt and pytest reports use test-case granularity.
CTest outer results and Qt inner cases are never automatically merged or counted as
the same tests. Qt's `xml` and `lightxml` formats are not JUnit and are not supported.
Generic JUnit parsing supports `testsuite(s)`, nested suites, testcase, failure, error,
skipped, properties and captured output. Unsupported retry extensions produce unknown
rather than an inferred final success. Unknown XML roots are rejected.

## Identity and evidence contract

Identity is SHA-256 of an unambiguous JSON tuple: project, granularity, suite, classname,
and exact test name (including parameter ID). Input filename, timestamp and checkout
root are excluded. Suite/class/name changes are identity changes; no fuzzy rename
inference is performed. Source paths are display metadata, normalized relative to
`--source-root` when possible. Original source values are retained. For intentionally
renamed tests, `diff --aliases aliases.json` takes an explicit one-to-one old-ID/new-ID
map and records the applied mapping.

Each XML source has a digest, path, size and dialect. Each observation has an XML
element locator, messages, duration, source ID, properties and original source location
when supplied. Output truncation is explicit. XML line numbers are not fabricated.
CTest compressed/base64 output is not decoded in v0.1: an `encoded-output` diagnostic
preserves that limitation; test status and other measurements remain available.

Repeated identical input content is deduplicated. Repeated test identities are ambiguous
and become unknown unless every observation has a unique contiguous positive integer
`testlens.attempt` property (1..N). This is an explicit user/exporter contract, not an
inference from XML order. The highest attempt supplies final status/duration; any-attempt
failure is separately tracked. `testlens.shard` identifies shards, but equal identities
in different shards are still ambiguous unless the explicit attempt contract is met.
Expected shards can be declared by repeating `--expected-shard`.

## Coverage, comparison and history

Valid XML does not prove the entire run was collected. `collect` defaults to unconfirmed
coverage. `--complete` is the caller's declaration that all results in the named `--scope`
were supplied. Parser errors, counts inconsistent with actual cases, ambiguous identities,
suite errors, missing expected shards and manifest mismatches override that declaration.
An optional `--manifest manifest.json` contains `{"test_ids": ["normalized-id", ...]}`
and records its digest. Missing results are always reported as absent observations,
never silently relabeled passed or deleted.

States: passed, failed, error, skipped, not-run, unknown. Explicit xfail/xpass evidence
is preserved when present; a generic skip does not prove expected failure. Suite setup
errors remain diagnostics. Skipping a previously failed test is not recovery.

Diff categories: new-failure (passed to failed/error), new-test-failure, persistent-failure,
recovered, missing, added, status-changed, unchanged. Durations retain runner measurement
scope: pytest may include setup/teardown, CTest measures its registered executable.
A slowdown requires both the relative threshold (default 0.2) and absolute threshold
(default 0.1 seconds); zero baselines have no percentage, missing/non-finite durations
remain unknown. Use `--relative-threshold` and `--absolute-threshold` to choose policy.

Comparisons require matching project, scope, metadata branch/platform/environment/runner
and granularity. Supply these with repeated `--metadata KEY=VALUE`; absent metadata is
not evidence that machines were identical. Choose scopes/cohorts explicitly.
History requires `--executed-at` RFC3339 timestamps, never file mtime. Equal timestamps
have a deterministic run-ID tie-break. Conflicting duplicate run IDs are rejected.

History reports failures / observed executed results among the latest N runs. Skips,
not-run and unknown do not enter that denominator; excluded and missing counts remain
visible. Final-result and any-attempt failure rates are separate. These are observational
frequencies, not statistical proof of flakiness or causal analysis.

## CLI policy and output

All analysis commands accept `--format json|text`, `--output`, `--verbose` and `--fail-on`.
No policy is enabled by default. Collect supports failure,error,incomplete; diff also
supports new-failure,new-test-failure,missing,slowdown; history supports incomplete.
Unknown or inapplicable policies are errors. Exit codes: **0** analysis completed and
selected policy passed; **1** selected policy violated; **2** invalid input/configuration.

`testlens.run/v1`, `testlens.diff/v1`, `testlens.history/v1` schemas are bundled in wheels.
`validate` checks schema versions and run cross-field invariants. JSON output is stable
apart from collection timestamps, with finite numeric values and explicit null unknowns.

## Resource and safety boundaries

Default collection limits: 1,024 files, 64 MiB per file, 256 MiB total XML, 64 element
levels, 1,000,000 XML elements per file, 100,000 testcase observations per run and 64 Ki
characters per output/message. CLI `--max-*` flags permit explicit changes. XML is parsed
one bounded file at a time; the current implementation retains that file's bounded tree
and normalized run in memory. Node and byte limits are both needed; this is not an
unbounded streaming database.

DTD, entity declarations and external references are rejected. No archive extraction,
XInclude, network access or test execution is performed. Directory discovery skips
symlink inputs. Malformed XML is rejected by default. `--allow-partial` retains previous
valid files, marks the run incomplete and stops at the first invalid file/budget failure.
JSON reads are bounded, reject duplicate keys and non-finite numbers. HTML uses text nodes
for untrusted evidence and escaped embedded JSON; terminal controls are stripped in text
reports. Reports contain captured output, which may be sensitive; choose sharing scope.
Writes are atomic and refuse symlink outputs and same-file input collisions. Concurrent
hostile filesystem mutation is not a supported security boundary.

## Development and CI handoff

```bash
cd testlens
uv sync --locked
uv run pytest
uv run ruff check .
uv run ruff format --check .
uv run mypy src
uv build
uv run python scripts/clean_install_e2e.py
uv run python scripts/benchmark.py --cases 10000
```

Artifacts: `dist/testlens-0.1.0-py3-none-any.whl`, `dist/testlens-0.1.0.tar.gz`.
`testlens/v0.1.0` is the independent product tag convention. Product tests require no
existing sibling build and emit standard pytest JUnit with `--junitxml=...` if requested.
Use runner output collection even when the runner fails; retain the original exit status
in CI rather than replacing it with the collection command's status. Root CI, release
and portfolio documentation are owned by the repository integrator.

Official format references: [CTest](https://cmake.org/cmake/help/latest/manual/ctest.1.html),
[Qt Test](https://doc.qt.io/qt-6/qtest-overview.html),
[pytest](https://docs.pytest.org/en/stable/reference/reference.html),
[Python XML security](https://docs.python.org/3/library/xml.html#xml-vulnerabilities).

## Verification record

The initial implementation was validated on 2026-10-03: 47 pytest cases, Ruff check and
format, strict mypy, wheel/sdist build, and a clean installed-wheel command sequence all
passed. Fixtures include actual Qt 6.10.2 results from DiskMap, BuildScope and LogLens,
actual EnvLens pytest output, and CTest JUnit/dashboard output for those executables.
An optional Playwright smoke covered tabs, search, filters, expandable failure evidence,
light/dark/mobile layouts and absence of network requests.

A generated 100,000-case collection took approximately 1.29 seconds with peak RSS about
300 MiB on the development host; the case-budget rejection was verified. This measures
collection, not schema validation, JSON serialization or HTML startup, and is an observed
baseline rather than a performance guarantee.
