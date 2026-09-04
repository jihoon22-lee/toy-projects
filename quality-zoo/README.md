# Quality Zoo

Quality Zoo is a known-answer corpus for ici. It keeps intentionally defective,
small scenarios separate from the user-facing toy products and checks that ici
reports the expected finding at the expected location. A scenario is therefore a
test asset, not an application to install or a product release.

The released-artifact corpus in `manifest.json` intentionally remains six
scenario entries: the Python `python.dead-private-function` case, four C++
sanitizer cases, and a Qt 6 C++ clazy lifetime case. The candidate-only
`candidate-manifest.json` has ten entries: those six released scenarios plus
two CMake/CTest ThreadSanitizer cases, a Python security/resource/correctness
case, and a Make/ELF/integration case. The candidate registry never changes the
released ici `v0.10.2` validation path. These six released scenarios form the
stable known-answer slice. The Python scenario gives
the `dead` engine one deliberately unused private function in
`src/bad.py`, while `src/clean.py` provides the nearby positive counterpart that
must not produce the same finding. The C++ scenarios keep AddressSanitizer,
LeakSanitizer, and UndefinedBehaviorSanitizer defects, plus a sanitizer-clean
fixture, isolated from the user-facing products. The Qt scenario pairs a
missing-parent-constructor fixture with a clean `QObject` parent-forwarding
counterpart.

## Current status

The scenario contract, dependency-free runner, candidate archive intake, and local
candidate consumer are complete. The C++ sanitizer subset is locally complete for
both the released and candidate ici digests documented below. Released-artifact Q0
acceptance is complete through the toy repository's remote PR/exact-main path, and
the earlier six-scenario candidate cross-repository acceptance is complete through
the exact-revision dispatch of the ici-hosted workflow recorded below.

The currently accepted candidate is explicitly non-stable: it reports package
version `0.10.2`, has `stable=false`, and is never the released artifact. Its
authenticated producer is [run `33840314232`](https://github.com/jihoon22-lee/ici/actions/runs/33840314232)
with [artifact `9924697353`](https://github.com/jihoon22-lee/ici/actions/artifacts/9924697353),
raw ZIP SHA-256
`2583e7a69b5a14316968d67c08d1510c13a9e32a9f7810bf0a4800df61004126`, executable
SHA-256
`50d41d36775394f66f6620091f42a7a0333ee90758e19449a848d7ee0875a93c`, and exact
source target `0b7e620bdb80423c3532d3ce3979a8dbdfaf6b11`. All ten candidate
registry scenarios now have an exact selector for this executable; the released
six-scenario `manifest.json` remains unchanged.

Using the accepted executable locally, nine of ten candidate contracts pass. The
only local failure is the Qt scenario because `clazy` is unavailable on this host;
the other nine scenarios have no runner errors. The two new candidate scenarios
are `WARN`/`PASS` with four matched findings for
`python.security-resource-correctness`, and `PASS`/`PASS` with no findings for
`cpp.make-elf-integration`. No version or release changed.
PR #49 head
`f6ad1dc4e745d3d1a2703000a00a5d7c4eed61a0` ran as
[`33693241255`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33693241255):
22 jobs succeeded and the main publisher was expectedly skipped. Its Quality Zoo
artifact [`9870829400`](https://github.com/jihoon22-lee/toy-projects/actions/artifacts/9870829400)
recorded contract `PASS` with the released ici `v0.10.2` executable
(`8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4`). The PR
comment [`5517587341`](https://github.com/jihoon22-lee/toy-projects/pull/49#issuecomment-5517587341)
contains exactly one `<!-- ici-report -->` marker and exactly three ordinary
product report links. Merge commit
`ed5fea2e881da77ac95482cf665e4e40bfe172f1` passed exact-main run
[`33694452357`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33694452357);
the exact-main Quality Zoo artifact
[`9871249913`](https://github.com/jihoon22-lee/toy-projects/actions/artifacts/9871249913)
recorded stable contract `PASS`, observed suite `WARN`, and no errors, while the
main product Pages were byte-identical to their artifacts. The broader Python
and C++ families and hybrid scenarios remain future work; the candidate-only
local corpus now includes representative security/resource and Make-to-ELF
process-contract coverage.

After EnvLens was merged by [PR #50](https://github.com/jihoon22-lee/toy-projects/pull/50) as
`c307ac1ab01e12e4ac81a34623eb669da0e43641`, exact-main [run `33698248293`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33698248293)
also succeeded across all jobs, the main publisher, and `Merge Gate`; the PR-only publisher was
skipped as expected for a push. The exact-main Quality Zoo artifact
[`9872561713`](https://github.com/jihoon22-lee/toy-projects/actions/artifacts/9872561713) recorded
contract `PASS`, one stable scenario with the expected observed `WARN`, and zero errors. The current
manifest produced four project Pages, all byte-identical to their artifacts and passing exact-title /
Zero-CDN checks; their sizes and hashes are centralized in the [EnvLens workthrough](../workthrough/2026-09-03-envlens-snapshot.md).

The local candidate evidence is retained here alongside the released-artifact remote
acceptance so
the released and candidate channels remain explicit and comparable:

| Item | Evidence |
|---|---|
| Candidate workflow run | [`33689056008`](https://github.com/jihoon22-lee/ici/actions/runs/33689056008) |
| Candidate source / target SHA | `7872a7b80899cbd3d40d92d18e7920cd7e2283e7` |
| Candidate artifact | `9869395069` |
| Candidate ZIP SHA-256 | `640e50ecf5b099174c16f1ef5d2b5b87945329711e96f926d94c3cc04109081e` |
| Candidate `ici.pyz` SHA-256 | `53fc75f0a073a74689babfe9ef8a4b2378995002d7d563bdc52da548fdbb9ee8` |
| Candidate version | `0.10.2` |
| Authenticated GitHub API evidence | validation passed |
| First scenario | contract `PASS`; observed suite `WARN`; one matched finding; no clean-counterpart false positive |

That earlier candidate was a candidate-channel artifact even though its package
version was `0.10.2`; it did not change the toy repository version or create a
release. The accepted current candidate and its provenance are recorded in the
status section above.

### C++ sanitizer scenarios (local digest-bound evidence)

The C++ sanitizer subset contains one intentionally defective fixture for each
diagnostic family and a clean counterpart or clean scenario. The expected observed
status is part of the known answer: defect fixtures are expected to make ici report
`FAIL`, while the clean fixture is expected to report `PASS`.

| Scenario | Intended result | Clean constraint |
|---|---|---|
| `cpp.asan-use-after-free` | AddressSanitizer heap use-after-free | no sanitizer finding for `src/clean.cpp` |
| `cpp.lsan-memory-leak` | LeakSanitizer memory leak | no sanitizer finding for `src/clean.cpp` |
| `cpp.ubsan-signed-overflow` | UndefinedBehaviorSanitizer signed-integer overflow | no sanitizer finding for `src/clean.cpp` |
| `cpp.sanitizer-clean` | sanitizer completes with zero issues and an informational completion record | no active non-info sanitizer defect finding is allowed |

Both exact executable channels were run against the complete five-scenario
manifest locally. In each channel all five contract verdicts were `PASS` with no
runner errors; the three defect scenarios observed `FAIL`, the clean scenario
observed `PASS`, and `python.dead-private-function` observed its expected `WARN`.

| ici channel | Version | Executable SHA-256 | Contract result |
|---|---|---|---|
| released ici | `0.10.2` | `8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4` | `5/5 PASS`, 0 errors |
| ici candidate | `0.10.2` | `e7f1a2ce7147057538873a802715c7bf2b12e530a85070af862e02e378caceb8` | `5/5 PASS`, 0 errors |

This is local known-answer evidence for the sanitizer subset. The later remote
candidate acceptance is recorded separately below; this local run itself did not
claim PR CI, a merge, or a release, and it does not close the broader C++ or other
corpus areas.

### Candidate-only ThreadSanitizer scenarios

The candidate manifest adds two isolated CMake/CTest projects for the deep-only
`thread_sanitize` engine. `cpp.tsan-data-race` is a red scenario: two threads
release an atomic start gate and then update one project-owned non-atomic integer,
so TSan reports a real `data-race` at `src/race.cpp:15`. The bounded fixture uses
two threads, a short atomic readiness loop, and one update per thread; it does not
depend on timing sleeps or unbounded work. `cpp.tsan-synchronized` runs the same
shape of two-thread update under a `std::mutex` and must produce no non-info TSan
finding.

Both projects require `g++` and `cmake`, run only
`verify --profile deep --no-cache`, enable only the required `thread_sanitize`
engine, and forbid capability skips. The earlier TSan candidate was bound to
exact ici main `6ee08b14fa598a19074af7afed4368fd79b19b2b`, whose all-green main CI run is
[`33732817172`](https://github.com/jihoon22-lee/ici/actions/runs/33732817172).
Candidate producer run
[`33733780877`](https://github.com/jihoon22-lee/ici/actions/runs/33733780877)
published artifact
[`9884927798`](https://github.com/jihoon22-lee/ici/actions/artifacts/9884927798).
The raw ZIP is `2,293,522` bytes with SHA-256
`9a50972a5cb4ad96b2b0cf912e27c17a600fc19d6d899c6e33028d4449b1122d`; its contained
`ici.pyz` is `2,292,199` bytes with SHA-256
`424108397858470b1209bc2749b580a858fb06c8b09aaa2e4772c94e43690bb5`. The
candidate reports version `0.10.2` with `stable=false`, and authenticated five-file
intake is `PASS`.

The local native CMake checks already establish the fixture behavior: the red
binary reports one TSan data race and exits nonzero under the TSan flags, while the
synchronized binary passes. All 58 Quality Zoo unit tests pass. Running the current
candidate with Qt excluded returned `7/7 PASS`: `cpp.tsan-data-race` observed
suite/engine `FAIL`, one `tsan.data-race` at `src/race.cpp:15`, and ici exit `1`;
`cpp.tsan-synchronized` observed `PASS`, zero TSan issues, and ici exit `0`. A full
local eight-scenario aggregate had only the Qt contract failure because `clazy` was
unavailable locally; CI installs `clazy`. This is historical pre-expansion evidence;
the accepted current candidate's ten-scenario local result is summarized above.
No PR, merge, Pages, release, or version change is claimed by that earlier run.

### Candidate-only Python security/resource/correctness scenario

`python.security-resource-correctness` is a bounded deep-profile Python fixture
with a deliberately embedded secret and dynamic `eval` in `src/bad.py`, plus an
unmanaged `open()` and mutable default argument. Its nearby `src/clean.py`
counterparts use no dynamic execution, a context manager, and a `None` default;
the expectation forbids security/resource findings there. The accepted
non-stable candidate (package version `0.10.2`) reports `WARN` for both required
engines with measured
evidence and four exact findings: security at `src/bad.py:3:1` and `:7:12`,
resource at `:11:14`, and correctness at `:15:30`. Secret evidence is redacted
by ici and is not retained by this corpus.

This is a syntax-level, intraprocedural known-answer case. It does not prove
runtime taint safety or interprocedural ownership. Its strict schema-1 expectation
is stored under `expectations/candidate-0b7e620.json` and is selected only by the
accepted candidate executable's exact SHA-256; the scenario remains outside the
stable released-artifact manifest.

### Candidate-only Make-to-ELF integration scenario

`cpp.make-elf-integration` uses a root Makefile to build an executable and a
shared library under the adapter-owned shadow tree. The deep-profile run enables
only required `build`, `binary_compat`, and `integration` engines, requires
`make`, `g++`, and `readelf`, and records `PASS`/`MEASURED` for all three. The
binary engine checks both ELF artifacts with no compatibility finding; the
integration engine resolves `{artifact:app}`, sets the contained shared-library
search path, and runs the executable self-test with exit `0` and the expected
`make-elf-integration-ok` output. The expectation also forbids compatibility
findings for either artifact and integration findings at `ici.toml`.

The fixture validates the build-manifest handoff and one bounded process
contract, not arbitrary deployment behavior or an operating-system sandbox.

### Qt lifetime scenario (candidate contract; completed remote evidence)

`cpp.qt-missing-parent-constructor` is a Qt 6 Core CMake fixture with a bad
`MissingParent` constructor at `src/bad.cpp:3` and a clean `ParentAware`
parent-forwarding counterpart in `src/clean.cpp`. Its `ici.toml` requires `g++`,
`cmake`, and `clazy`, and selects the exact `ctor-missing-parent-argument`
check.

The released ici `v0.10.2` expectation and the new candidate expectation share
the exact rule, tool rule, `WARN` suite/engine status, `MEASURED` evidence,
`exact` confidence, and `src/bad.cpp:3` location. They intentionally differ in
category: the released answer is `maintainability`, while the candidate answer
uses `cpp_diagnostic_category_policy = tool-rule-v1` and is `resource`. Both
forbid a lint finding in `src/clean.cpp`.

| Channel | Executable SHA-256 | Expected / observed | Rule / tool rule | Category | Location |
|---|---|---|---|---|---|
| released ici `v0.10.2` | `8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4` | `WARN` / `WARN`; `MEASURED`; `exact` | `ici.legacy.lint.target` / `clazy-ctor-missing-parent-argument` | `maintainability` | `src/bad.cpp:3` |
| candidate target `e7a9f55be8893d91497a6e1d0bff6e2e5f4af5f3` | `985c81a63363356619207870cddb0d8cd9854a46925a3e0a745e54bd543d5b51` | `WARN` / `WARN`; `MEASURED`; `exact` | `ici.legacy.lint.target` / `clazy-ctor-missing-parent-argument` | `resource` (`tool-rule-v1`) | `src/bad.cpp:3` |

The candidate producer [run `33715173073`](https://github.com/jihoon22-lee/ici/actions/runs/33715173073)
published [artifact `9878317009`](https://github.com/jihoon22-lee/ici/actions/artifacts/9878317009).
The raw candidate ZIP is `2,288,897` bytes with SHA-256
`1165312e36344244fe0591e4fbcf869d126a0a7160a21099ee13c34ae8144d5e`; its
contained `ici.pyz` is `2,287,574` bytes with SHA-256
`985c81a63363356619207870cddb0d8cd9854a46925a3e0a745e54bd543d5b51`. The
candidate provenance binds exact ici target `e7a9f55be8893d91497a6e1d0bff6e2e5f4af5f3`
to successful main `Merge Gate` [run `33714515219`](https://github.com/jihoon22-lee/ici/actions/runs/33714515219).

Local Quality Zoo unit validation is `57/57` passed. Running the new candidate
with the Qt scenario excluded against the existing five scenarios returned
contract `5/5 PASS`. This is historical local candidate evidence for the pre-acceptance
five-scenario subset. The Qt candidate was then accepted end-to-end: [toy PR #55](https://github.com/jihoon22-lee/toy-projects/pull/55)
passed [run `33716728288`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33716728288)
and published [artifact `9878794296`](https://github.com/jihoon22-lee/toy-projects/actions/artifacts/9878794296);
sticky report comment [#5520667737](https://github.com/jihoon22-lee/toy-projects/pull/55#issuecomment-5520667737)
recorded the PR evidence. The PR squash-merged to `main` as
`a59461acaf0f2e967e6ba51e07e56ac7e73acbc6`; exact-main [run `33717415609`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33717415609)
published [artifact `9879023706`](https://github.com/jihoon22-lee/toy-projects/actions/artifacts/9879023706).
The ici-hosted candidate acceptance [run `33718024450`](https://github.com/jihoon22-lee/ici/actions/runs/33718024450)
published [artifact `9879217928`](https://github.com/jihoon22-lee/ici/actions/artifacts/9879217928)
and recorded the six-scenario contract as `6/6 PASS`. The candidate is non-stable and no version or
release changed.

### Sanitizer-normalization candidate selector (local authenticated evidence)

The scenario selector now includes the candidate executable produced from ici target
`9d470edca7ab037a24dcd6594531a822f116548b`. The producer workflow run
[`33706057540`](https://github.com/jihoon22-lee/ici/actions/runs/33706057540) succeeded and
published artifact [`9875319095`](https://github.com/jihoon22-lee/ici/actions/artifacts/9875319095).
The raw candidate archive SHA-256 is
`4aec084b3a30ac01a1df5124fa3b42b7f51d23f66c12b490194a84549be9db27`, and the executable inside
it has SHA-256
`e7f1a2ce7147057538873a802715c7bf2b12e530a85070af862e02e378caceb8`. The exact digest selects
`expectations/candidate-9d470ed.json`; package version alone is not used as a selector.

Authenticated local intake evidence succeeded for this archive. Running the Q0 scenario with the
selected candidate expectation returned contract `PASS`, observed suite `WARN`, one matched finding,
and no runner errors. This particular evidence is still for the Python dead-code known-answer case;
the separate C++ sanitizer all-scenario evidence above uses the same candidate executable digest and
its sanitizer-specific expectations. The exact-revision remote acceptance of that candidate is
recorded in the next section.

### Remote candidate acceptance (exact-revision dispatch)

The ici-hosted candidate-to-Quality-Zoo workflow completed an independently audited, read-only
acceptance run against exact revisions. The workflow checks out toy main at
`2d0d7c0b2dcc137a782d6042438fc287bffdf570`, consumes the candidate for ici target
`9d470edca7ab037a24dcd6594531a822f116548b`, and runs the complete five-scenario manifest without
passing credentials to Quality Zoo. The workflow head is ici main
`6df011f98be1a19092b112cb56c596dc35bcae4d`.

| Evidence | Exact value |
|---|---|
| Acceptance workflow / job | [`33710695336`](https://github.com/jihoon22-lee/ici/actions/runs/33710695336) / [`100509326331`](https://github.com/jihoon22-lee/ici/actions/runs/33710695336/job/100509326331) |
| Candidate producer run / artifact | [`33706057540`](https://github.com/jihoon22-lee/ici/actions/runs/33706057540) / [`9875319095`](https://github.com/jihoon22-lee/ici/actions/artifacts/9875319095) |
| Candidate ZIP | `2,285,368` bytes; SHA-256 `4aec084b3a30ac01a1df5124fa3b42b7f51d23f66c12b490194a84549be9db27` |
| Candidate `ici.pyz` | `2,284,045` bytes; SHA-256 `e7f1a2ce7147057538873a802715c7bf2b12e530a85070af862e02e378caceb8` |
| Acceptance evidence artifact | [`9876797536`](https://github.com/jihoon22-lee/ici/actions/artifacts/9876797536); `1,104,307` bytes; SHA-256 `e66ae2b65988abe10fc5ddb92a5c3bb6fc238ec2f77b7fd27ccfe75c24194a5f` |
| Provenance/API evidence | Exact five authenticated snapshots reverified; candidate target, producer run, artifact identity, archive digest, executable digest, and merge-gate evidence matched |

The acceptance artifact records suite contract `PASS` for all five scenarios, zero runner errors,
and the following exact expected/observed statuses and finding predicates:

| Scenario | Expected / observed status | Rule / tool rule | Category | Location |
|---|---|---|---|---|
| `cpp.asan-use-after-free` | `FAIL` / `FAIL` | `ici.legacy.sanitize.target` / `asan.heap-use-after-free` | correctness | `src/fault.cpp:5` |
| `cpp.lsan-memory-leak` | `FAIL` / `FAIL` | `ici.legacy.sanitize.target` / `lsan.memory-leak` | resource | `src/fault.cpp:3` |
| `cpp.sanitizer-clean` | `PASS` / `PASS` | `ici.legacy.sanitize.target` / no tool rule | correctness | `tests/test_clean.cpp:1` |
| `cpp.ubsan-signed-overflow` | `FAIL` / `FAIL` | `ici.legacy.sanitize.target` / `ubsan.signed-integer-overflow` | correctness | `src/fault.cpp:3:18` |
| `python.dead-private-function` | `WARN` / `WARN` | `ici.legacy.dead.target` / no tool rule | maintainability | `src/bad.py:1` |

The Python scenario still forbids a finding in `src/clean.py`; all five scenario contract results
have `errors: []`. This is the earlier sanitizer-subset acceptance; the later Qt-complete,
six-scenario acceptance is recorded in the Qt section above. The workflow uses read-only API
evidence, stages a separate acceptance artifact, and does not publish Pages, a PR comment, a
release, a tag, or a branch mutation. Ordinary toy CI remains pinned to released ici `v0.10.2`;
the candidate is still non-stable and no version or product release changed.

## Exact executable expectations

Package version alone is not a sufficient expectation selector. The released
ici `v0.10.2` executable with SHA-256
`8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4` reports
the legacy `MEASURED` evidence and `high` confidence for the dead finding. The
earlier candidate executable with SHA-256
`53fc75f0a073a74689babfe9ef8a4b2378995002d7d563bdc52da548fdbb9ee8` reports
provenance-aware `ESTIMATED` evidence and `medium` confidence, even though both
executables report package version `0.10.2`. The sanitizer-normalization candidate has its own
strict expectation even though it reports the same package version. The accepted current
candidate also reports `0.10.2`, but is selected by its distinct executable digest and
remains non-stable.

The scenario's schema-2 `scenario.json` maps each exact executable SHA-256 to a
contained, full strict schema-1 expectation:

| Executable SHA-256 | Channel | Selected expectation |
|---|---|---|
| `8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4` | released ici `v0.10.2` | `expectations/released-v0.10.2.json` |
| `53fc75f0a073a74689babfe9ef8a4b2378995002d7d563bdc52da548fdbb9ee8` | ici candidate | `expectations/candidate-7872a7b.json` |
| `e7f1a2ce7147057538873a802715c7bf2b12e530a85070af862e02e378caceb8` | ici sanitizer candidate | `expectations/candidate-9d470ed.json` |
| `50d41d36775394f66f6620091f42a7a0333ee90758e19449a848d7ee0875a93c` | accepted non-stable candidate `0.10.2` | `expectations/candidate-9d470ed.json` (legacy sanitizer/dead cases) |

The runner selects one expectation by exact digest before checking the report;
an unknown digest has no fallback and fails closed. Ordinary CI uses the
released executable and released expectation, while candidate validation uses
the candidate executable and candidate expectation. Thus the two channels do
not share one expectation merely because their package versions match.

## Reproduce a local run

The runner requires an explicit local executable. Set `ICI_BIN` to an absolute
path, or pass the same path with `--ici-bin`:

```bash
cd quality-zoo
ICI_BIN=/path/to/ici.pyz \
  python3.10 -m runner.run \
  --manifest manifest.json \
  --output-dir /tmp/quality-zoo-results
```

The output directory must not already exist. To run only the current scenario,
add `--scenario python.dead-private-function`. The runner records the exact
candidate path, SHA-256, and version in `suite.json`, and writes each scenario's
`report.json`, `report.html`, and `run.json` below the output directory.

Candidate-only validation opts in explicitly with
`--manifest candidate-manifest.json`. Digest-bound legacy/TSan entries still
require their matching candidate executable. All ten candidate entries now select
expectations by exact SHA-256, including the two new schema-2 selectors whose full
schema-1 expectations live under `candidate-0b7e620.json`. With the accepted
`ici 0.10.2` executable, the two new scenarios are locally `2/2 PASS` at the
runner-contract level (`WARN` is the intentional Python suite status); the full
ten-scenario local run is `9/10 PASS` because this host lacks required `clazy` for
the Qt scenario.

`ICI_BIN` is deliberately a local path. The Quality Zoo runner does not resolve
URLs, download an executable, or silently substitute the released tool. This
makes the tool identity explicit for local candidate testing and keeps ordinary
CI's released-tool path separate.

## Registry and scenario format

`manifest.json` is the released-artifact registry of scenario IDs and paths, while
`candidate-manifest.json` is an explicit candidate-only registry. The latter
contains the released six plus the two TSan scenarios, the Python
security/resource/correctness scenario, and the Make/ELF/integration scenario (ten
entries total); ordinary CI must continue to select `manifest.json` while it
consumes released ici `v0.10.2`; candidate validation alone selects
`candidate-manifest.json`. The accepted non-stable candidate artifact and its
exact expectations have passed authenticated intake; the candidate package still
reports version `0.10.2` and is never the released artifact. Both files are
intentionally JSON rather than TOML: the runner is dependency-free and must run on Python
3.10, where JSON parsing is provided by the standard library. Using a TOML
parser would add a dependency or create a Python-version compatibility question
for a file that only maps an ID to a directory. The scenario's `ici.toml` is
still present because ici consumes it; the Quality Zoo runner does not parse
that TOML file.

The released manifest has schema `1` and six entries. The candidate manifest has
the same schema and ten entries, adding four candidate-only entries:

```json
{
  "schema": 1,
  "scenarios": [
    {
      "id": "cpp.asan-use-after-free",
      "path": "scenarios/cpp/asan-use-after-free"
    },
    {
      "id": "cpp.lsan-memory-leak",
      "path": "scenarios/cpp/lsan-memory-leak"
    },
    {
      "id": "cpp.qt-missing-parent-constructor",
      "path": "scenarios/cpp/qt-missing-parent-constructor"
    },
    {
      "id": "cpp.sanitizer-clean",
      "path": "scenarios/cpp/sanitizer-clean"
    },
    {
      "id": "cpp.ubsan-signed-overflow",
      "path": "scenarios/cpp/ubsan-signed-overflow"
    },
    {
      "id": "python.dead-private-function",
      "path": "scenarios/python/dead-private-function"
    },
    {
      "id": "cpp.tsan-data-race",
      "path": "scenarios/cpp/tsan-data-race"
    },
    {
      "id": "cpp.tsan-synchronized",
      "path": "scenarios/cpp/tsan-synchronized"
    },
    {
      "id": "python.security-resource-correctness",
      "path": "scenarios/python/security-resource-correctness"
    },
    {
      "id": "cpp.make-elf-integration",
      "path": "scenarios/cpp/make-elf-integration"
    }
  ]
}
```

Each scenario directory contains `scenario.json`, an ici project directory with
`ici.toml`, and the source fixture. Existing schema-2 `scenario.json` selectors
declare the scenario identity and map exact ici executable SHA-256 values to
contained expectation paths. The two candidate-only additions also use schema-2
selectors; their strict schema-1 expectations are kept under
`expectations/candidate-0b7e620.json` and are selected only for the accepted
candidate digest. Each schema-1 expectation declares the class, project root,
profile, fixed verify command, expected suite status, expected producer version,
expected engine state, expected findings, and forbidden findings. Stable
scenarios should include a nearby clean counterpart when the rule could
overmatch ordinary code.

Commands are intentionally narrow. The runner accepts only an argv array of
`verify --profile fast|standard|deep`, optionally followed by `--no-cache`; it
does not invoke a shell. Scenario and project paths must remain inside the
checked-in scenario directory, and symlinks are rejected.

## Contract verdict and observed result

Quality Zoo reports two different concepts:

- `observed_suite_status` is what ici actually reported (`PASS`, `WARN`, `FAIL`,
  `ERROR`, or `SKIP`). It is evidence about the analyzer run.
- `contract_verdict` is whether the complete known answer matched. It is `PASS`
  only when the v3 report schema, count fields, producer version, expected
  engine status/evidence, expected finding predicates, forbidden-finding
  absences, and ici exit code all agree with the selected full schema-1
  expectation for the executable's exact SHA-256.

Consequently, an observed `WARN` is not automatically a failed Quality Zoo
scenario. The first scenario expects the dead-code engine to be `WARN` because
the Python analysis is heuristic, and it expects exactly one finding. Its
contract verdict is `PASS` even though its observed suite status is `WARN`.
The C++ sanitizer defect scenarios similarly expect an observed `FAIL`, while
the sanitizer-clean scenario expects `PASS`; each is a contract pass only when
the exact status, evidence, diagnostic location, and clean-finding constraints
match the selected executable expectation.
Conversely, an observed `FAIL` or an unexpected `WARN` is not accepted merely
because a finding happened to appear: status, evidence, location, and exit
semantics remain part of the contract. A scenario may explicitly expect a
known `FAIL`/`ERROR`/`SKIP` case, but only an exact match is a contract pass.

The runner keeps schema/contract errors, timeout or execution errors, and ici's
own observed engine status distinct. A successful contract run exits zero; a
contract mismatch exits one; malformed runner input or an unsafe candidate is a
runner error.

## Candidate archive intake

Candidate consumption is a separate step from scenario execution. The intake
command accepts a local candidate ZIP, an expected archive SHA-256, the expected
ici repository and target SHA, and a destination that must not already exist:

```bash
cd quality-zoo
python3.10 -m runner.candidate_intake \
  --archive /path/to/ici-candidate.zip \
  --archive-sha256 <archive-sha256> \
  --repository jihoon22-lee/ici \
  --target-sha <full-commit-sha> \
  --destination /tmp/ici-candidate \
  --json
```

The archive must contain exactly these regular files, with the stated modes:

| Member | Mode | Purpose |
|---|---:|---|
| `candidate-provenance.json` | `0644` | provenance-bound repository, target, workflow, run, and package identity |
| `ici.pyz.sha256` | `0644` | canonical sidecar for the executable digest |
| `ici.pyz` | `0755` | candidate executable |

The intake checks the out-of-band ZIP digest before extraction, rejects unsafe
paths, duplicates, symlinks and special files, encrypted or compressed members,
and bounded-size violations, then validates exact provenance fields. It also
recomputes the executable digest and size, checks the sidecar, and runs the
candidate's bounded `--version` probe. Extraction is into a fresh destination
and an existing destination is never replaced.

An optional `--github-evidence` directory may contain exactly five authenticated
GitHub API snapshots:

```text
artifact.json
candidate-run.json
gate-check.json
gate-job.json
gate-run.json
```

The intake does not call GitHub itself. The caller supplies snapshots obtained
through an authenticated API request, and intake compares their exact IDs,
repository, head SHA, workflow identity, run/job/check status, URLs, artifact
name, artifact size, and archive digest against the in-bundle provenance and
the local ZIP. Extra files, missing files, or an identity mismatch fail closed.
The five-file evidence set is optional for a local smoke run, but it is required
when recording authenticated candidate provenance.

## Threat model and safety boundary

The candidate archive, provenance JSON, scenario metadata, ici report, and tool
output are treated as untrusted inputs. The contract protects the following
boundaries:

- ZIP path traversal, absolute paths, backslashes, duplicate members, symlink or
  device entries, encryption, compression bombs, oversized members, and
  destination replacement are rejected before a candidate is used.
- The expected archive digest is checked before extraction. Provenance is exact
  and binds the executable to `jihoon22-lee/ici`, a full target SHA, the candidate
  workflow, candidate run, package version, and `stable: false`. The executable
  digest, size, sidecar, and `--version` output must agree.
- Manifest and scenario paths are canonical and contained within the repository
  fixture. Symlinks are rejected. The command is a fixed argv list, not shell
  text. Each run has a timeout, receives an isolated temporary `HOME`, XDG
  config directory, and ici cache directory, and retains at most 1 MiB from
  each stdout/stderr stream in its result.
- Report JSON must be `ici.result/v3`, have internally consistent counts and
  unique engines, stay within the 16 MiB parser limit, use project-relative
  canonical locations, and satisfy both positive finding predicates and
  negative clean-counterpart predicates.
- Optional GitHub snapshots are local evidence files, not trusted merely because
  they contain a URL. Exactly five files and exact authenticated API identities
  are required when that evidence is enabled.

This is a process and input-integrity boundary, not an operating-system sandbox.
The runner intentionally executes the selected ici binary and analysis tools as
the invoking user. It does not grant root privileges, and it does not promise to
block network access or contain a malicious executable. Run unknown candidates
with no secrets and, when the source is not trusted, in a disposable container,
VM, or other externally enforced sandbox. Stdout/stderr are captured in temporary
files before the retained excerpts are capped, so the runner does not impose an
overall disk quota. The runner itself performs no network download.

## CI and release policy

Ordinary toy-projects CI remains pinned to the released ici `v0.10.2` artifact
and its existing checksum. A candidate archive is an explicit local/manual
consumer input and must not replace that stable CI pin. Candidate evidence can
be reviewed independently before a pin update is proposed. The released CI
digest selects the released expectation; candidate validation selects the
candidate expectation. An identical package version does not make those
expectations interchangeable.

Quality Zoo is not a user-facing application and has no product release in this
change. Released-artifact Q0 (the scenario contract, local runner, and toy
PR/exact-main acceptance) and the earlier exact-revision candidate
cross-repository acceptance are complete, as is the current candidate's
authenticated local intake/selector evidence. The Qt 6 lifetime fixture and
digest-specific expectations are checked in, and its earlier toy PR/exact-main
and ici candidate acceptance are complete; that six-scenario candidate contract
is `6/6 PASS` (see the linked evidence above). The current TSan candidate is
bound to exact ici main `6ee08b14fa598a19074af7afed4368fd79b19b2b`, with local
seven-scenario contract `7/7 PASS`; its full local eight-scenario aggregate has
only the Qt contract failure because local `clazy` is unavailable, while CI
installs `clazy`. The new digest-bound Python and Make/ELF/integration scenarios
have local contract evidence against the current `ici.pyz`, but their exact toy
PR/main acceptance and remote candidate workflow remain pending. Broader Q2 and
Q1–Q5 (the remaining Python, C++, Qt, and hybrid corpus expansions) remain
future work. No version or release changed.
