# Quality Zoo known-answer contract workthrough

## Outcome

The Quality Zoo scenario contract, dependency-free runner, candidate archive
intake, local candidate consumer, and remote Q0 acceptance are complete. PR #49
head `f6ad1dc4e745d3d1a2703000a00a5d7c4eed61a0` ran as
[`33693241255`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33693241255):
22 jobs succeeded and the main publisher was expectedly skipped. Quality Zoo
artifact [`9870829400`](https://github.com/jihoon22-lee/toy-projects/actions/artifacts/9870829400)
recorded contract `PASS` with released ici `v0.10.2` SHA-256
`8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4`. The PR
comment [`5517587341`](https://github.com/jihoon22-lee/toy-projects/pull/49#issuecomment-5517587341)
contains exactly one `<!-- ici-report -->` marker and exactly three ordinary
product report links. Merge commit
`ed5fea2e881da77ac95482cf665e4e40bfe172f1` passed exact-main run
[`33694452357`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33694452357).
Exact-main Quality Zoo artifact
[`9871249913`](https://github.com/jihoon22-lee/toy-projects/actions/artifacts/9871249913)
recorded stable contract `PASS`, observed suite `WARN`, and an empty error list;
the main product Pages were byte-identical to their artifacts. Q1–Q5 (the
Python/C++/Qt/build/binary/hybrid corpus expansion) remain future work.

The later EnvLens merge, [PR #50](https://github.com/jihoon22-lee/toy-projects/pull/50) at
`c307ac1ab01e12e4ac81a34623eb669da0e43641`, passed exact-main [run `33698248293`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33698248293)
with all jobs, the main publisher, and `Merge Gate` successful; the PR-only publisher was skipped as
expected for a push. Its Quality Zoo artifact [`9872561713`](https://github.com/jihoon22-lee/toy-projects/actions/artifacts/9872561713)
still recorded contract `PASS`, one stable scenario with the expected observed `WARN`, and zero
errors. The current four-project Page artifact/hash table is centralized in the [EnvLens snapshot
workthrough](../../workthrough/2026-09-03-envlens-snapshot.md); Q1–Q5 remain pending.

This is a test-asset milestone, not a user-facing product milestone. No toy
product version was changed and no release was created.

## Why this corpus exists

The user-facing toy projects should remain useful, clean products. They are not
the right place to add deliberately dead functions, malformed build inputs, or
other code whose purpose is to exercise an analyzer finding. Quality Zoo keeps
those known-bad examples isolated and gives each one an expected report answer,
including a nearby clean counterpart where false positives are plausible.

The first stable scenario is `python.dead-private-function`. It contains an
unused module-level private function in `src/bad.py` and a used private helper in
`src/clean.py`. The expected answer is one dead-code finding for the bad file and
no corresponding finding for the clean file. The finding location is shared by
the released and candidate channels, but their report evidence and confidence
are not interchangeable.

## Contract and format decisions

### JSON registry and Python 3.10 boundary

The registry is [`quality-zoo/manifest.json`](../../quality-zoo/manifest.json),
not a TOML file. It only maps a scenario ID to a scenario directory, so JSON is
the smallest sufficient format. The runner deliberately uses only the Python
3.10 standard library; adding a TOML parser would add a dependency or force a
runtime compatibility decision for no additional contract value. Scenario
`ici.toml` files remain part of each ici project and are consumed by ici. The
Quality Zoo runner validates their presence and location but does not parse
TOML.

### Explicit local tool identity

Scenario execution requires `ICI_BIN` or `--ici-bin` and rejects anything that
is not an executable regular local file. The runner hashes and records the
selected executable, probes its version, and passes it to every scenario. It
does not resolve a URL or silently replace a candidate with the ordinary CI
tool. A reproducible local invocation is:

```bash
cd quality-zoo
ICI_BIN=/path/to/ici.pyz \
  python3.10 -m runner.run \
  --manifest manifest.json \
  --output-dir /tmp/quality-zoo-results
```

### Observed status versus contract verdict

The runner preserves ici's `observed_suite_status` and separately emits a
`contract_verdict`. The observed value describes what ici reported. The verdict
answers whether the report is the expected known answer, including v3 schema and
counts, producer version, engine status/evidence, expected finding predicates,
forbidden-finding absence, and exit code.

This distinction matters for heuristic engines. The first scenario deliberately
expects the dead engine and suite to be `WARN`, so a `WARN` report can have a
contract `PASS`. A different `WARN`, an unexpected `FAIL`, or a missing/extra
finding is a contract failure. A future scenario may expect a known `FAIL`,
`ERROR`, or `SKIP`, but only the exact declared status and evidence pass.

### Exact executable-SHA expectation selector

The package version is not a sufficient identity for a strict expected answer.
The released ici `v0.10.2` executable with SHA-256
`8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4` reports
legacy `MEASURED` evidence and `high` confidence. The candidate executable with
SHA-256 `53fc75f0a073a74689babfe9ef8a4b2378995002d7d563bdc52da548fdbb9ee8`
reports provenance-aware `ESTIMATED` evidence and `medium` confidence, despite
reporting the same package version `0.10.2`.

The scenario therefore uses schema-2 `scenario.json` as an exact SHA-256
selector. Each known digest maps to one contained, full strict schema-1
expectation (`expectations/released-v0.10.2.json` or
`expectations/candidate-7872a7b.json`); the runner selects it before validating
the report. An unknown digest has no default expectation and fails closed.
Ordinary CI uses the released executable and released expectation; candidate
validation uses the candidate executable and candidate expectation.

The first PR run exposed this distinction rather than being treated as a flaky
gate. Job `100453860515` in run `33692399796` used the public release and first
rejected the candidate-only `engines.dead.cpp_unused` configuration key. After
that key was removed from the shared fixture, both executables produced reports,
but the release reported `MEASURED`/`high` while the candidate reported
`ESTIMATED`/`medium`. That is why the correction binds expectations to executable
digests instead of weakening evidence/confidence predicates. The obsolete run
was cancelled after diagnosis; the full rerun and exact-main closeout are
recorded in the Outcome above.

The same failure also showed that a missing report left the always-upload step
with an empty directory. The runner now writes a bounded
`quality-zoo.runner-error/v1` `run.json` before raising that error, rejects an
executable symlink before path resolution, and rehashes the selected ici binary
after its version probe and after scenario execution.

## Candidate archive and provenance boundary

Candidate consumption is separate from scenario execution. The consumer accepts
a local archive plus an expected archive SHA-256, expected ici repository, target
SHA, and fresh extraction destination. The archive has exactly three regular
members: `candidate-provenance.json` (`0644`), `ici.pyz.sha256` (`0644`), and
`ici.pyz` (`0755`). It checks the archive digest before extraction, rejects
traversal/absolute/backslash paths, duplicates, symlinks and special files,
encrypted or compressed entries, and bounded-size violations. It then verifies
exact provenance fields, executable digest and size, canonical sidecar, and a
bounded `ici.pyz --version` probe.

An optional `--github-evidence` directory carries exactly five authenticated API
snapshots:

```text
artifact.json
candidate-run.json
gate-check.json
gate-job.json
gate-run.json
```

The consumer does not perform the API requests. A caller obtains the snapshots
through authenticated GitHub API access and supplies them locally. When present,
the consumer compares their exact artifact/run/job/check identities, repository,
head SHA, workflow, status/conclusion, canonical URLs, artifact name and size,
and archive digest with the in-bundle provenance and local ZIP. Extra or missing
files fail closed. This keeps API provenance optional for a local smoke run but
strict when evidence is claimed.

## Threat model

The archive, provenance, scenario metadata, report JSON, and tool output are
untrusted at the input boundary. The implemented protections cover:

- archive path traversal, duplicate members, unsafe file types/modes, encrypted
  or compressed members, oversized members, and replacement of an existing
  extraction destination;
- digest, sidecar, version, repository, target SHA, workflow, run, and
  `stable: false` provenance mismatches;
- scenario symlinks and paths escaping the fixture, arbitrary shell text, and
  unsupported command arguments;
- report JSON larger than 16 MiB, retained stdout/stderr excerpts larger than
  1 MiB per stream, hung analysis, malformed v3 reports, inconsistent counts,
  duplicate engines, non-canonical locations, and positive/negative finding
  mismatches; and
- forged-looking GitHub evidence that is not the exact five-file, exact-identity
  snapshot set expected by the candidate provenance.

The runner executes the selected candidate and analysis tools as the invoking
user. It does not grant root, block network traffic, impose an overall disk
quota on the temporary stdout/stderr capture files, or provide an OS sandbox;
the isolated HOME/XDG/cache directories, timeout, parser limit, and retained
excerpt limits are not a security boundary against a malicious executable.
Unknown candidates should be run without secrets and inside an externally
enforced disposable container or VM. The runner itself does not download code
or make network requests.

## Local candidate evidence

The local candidate consumer was exercised with the following identity:

| Item | Value |
|---|---|
| Candidate workflow run | `33689056008` |
| Source / target SHA | `7872a7b80899cbd3d40d92d18e7920cd7e2283e7` |
| Artifact ID | `9869395069` |
| Candidate ZIP SHA-256 | `640e50ecf5b099174c16f1ef5d2b5b87945329711e96f926d94c3cc04109081e` |
| Candidate `ici.pyz` SHA-256 | `53fc75f0a073a74689babfe9ef8a4b2378995002d7d563bdc52da548fdbb9ee8` |
| Candidate version | `0.10.2` |
| Authenticated API evidence | validation passed |

The first scenario result was:

```text
scenario:              python.dead-private-function
contract_verdict:      PASS
observed_suite_status:  WARN
matched_findings:      1
clean counterpart:     no false positive in src/clean.py
```

The candidate remains a candidate-channel artifact even though it reports
version `0.10.2`. Ordinary CI continues to use the released ici `v0.10.2` pin
and its existing checksum and therefore selects the released expectation.
Candidate validation selects the candidate expectation. Candidate validation
does not update that pin, bump a toy version, or create a release.

## Verification results

The implementation and its trust boundaries were checked on Python 3.10:

```bash
uv run --python 3.10 --no-project python -m unittest discover -s tests -v
# 56 tests passed

uvx ruff check quality-zoo
uvx ruff format --check quality-zoo
# all checks passed; 13 files already formatted

uvx --python 3.10 mypy --strict quality-zoo/runner
# success: no issues in 5 source files

actionlint .github/workflows/ci.yml
python3 -m unittest ci.test_ci_workflow_contract -v
# actionlint passed; 24 workflow tests passed
```

The authenticated candidate archive intake also passed with the five GitHub API
snapshots listed above. Running that verified candidate on Python 3.10 produced
contract `PASS`, observed suite `WARN`, exactly one matched finding, and no
runner errors for `python.dead-private-function`.

## Documentation and follow-up

The documentation synchronization covers the canonical Quality Zoo README,
repository README, changelog, handover, and portfolio master plan. The local
documentation quality check for this change is:

```bash
git diff --check
```

Q0 acceptance is closed: the PR CI rerun, exactly one ordinary sticky comment
with the three released-ici product HTML links, the separate Quality Zoo result
artifacts, and exact-main evidence are all recorded above. The later merged-main
run and four-project Page evidence are linked above as the current portfolio
record. The remaining Python, C++, Qt, build/binary, and hybrid scenario
families (Q1–Q5) stay future until their known answers and clean counterparts
are implemented and verified.
