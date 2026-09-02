# Quality Zoo

Quality Zoo is a known-answer corpus for ici. It keeps intentionally defective,
small scenarios separate from the user-facing toy products and checks that ici
reports the expected finding at the expected location. A scenario is therefore a
test asset, not an application to install or a product release.

The current checked-in corpus contains one stable Python scenario:
`python.dead-private-function`. It gives the `dead` engine one deliberately
unused private function in `src/bad.py`, while `src/clean.py` provides the
nearby positive counterpart that must not produce the same finding.

## Current status

The scenario contract, dependency-free runner, candidate archive intake, and
local candidate consumer are implemented and have passed local validation. The
remote PR CI, sticky report publication, and exact-main evidence for this
repository are still pending. The other corpus areas (C++, Qt, build/binary,
and hybrid scenarios) have not been completed.

The local candidate evidence is recorded here so that a future remote run can be
compared with a known boundary:

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

The candidate is a candidate-channel artifact even though its package version is
`0.10.2`. It does not change the toy repository version and does not create a
release.

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

`ICI_BIN` is deliberately a local path. The Quality Zoo runner does not resolve
URLs, download an executable, or silently substitute the released tool. This
makes the tool identity explicit for local candidate testing and keeps ordinary
CI's released-tool path separate.

## Registry and scenario format

`manifest.json` is the registry of scenario IDs and paths. It is intentionally
JSON rather than TOML: the runner is dependency-free and must run on Python
3.10, where JSON parsing is provided by the standard library. Using a TOML
parser would add a dependency or create a Python-version compatibility question
for a file that only maps an ID to a directory. The scenario's `ici.toml` is
still present because ici consumes it; the Quality Zoo runner does not parse
that TOML file.

The manifest currently has schema `1` and one entry:

```json
{
  "schema": 1,
  "scenarios": [
    {
      "id": "python.dead-private-function",
      "path": "scenarios/python/dead-private-function"
    }
  ]
}
```

Each scenario directory contains `scenario.json`, an ici project directory with
`ici.toml`, and the source fixture. `scenario.json` declares the scenario
identity and class, project root, profile, fixed verify command, expected suite
status, expected producer version, expected engine state, expected findings, and
forbidden findings. Stable scenarios should include a nearby clean counterpart
when the rule could overmatch ordinary code.

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
  absences, and ici exit code all agree with `scenario.json`.

Consequently, an observed `WARN` is not automatically a failed Quality Zoo
scenario. The first scenario expects the dead-code engine to be `WARN` because
the Python analysis is heuristic, and it expects exactly one finding. Its
contract verdict is `PASS` even though its observed suite status is `WARN`.
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
be reviewed independently before a pin update is proposed.

Quality Zoo is not a user-facing application and has no product release in this
change. The local Q0 implementation and candidate consumer are complete, while
remote PR CI, the sticky report, and exact-main verification remain follow-up
work. The Python, C++, Qt, build/binary, and hybrid corpus expansions remain
pending.
