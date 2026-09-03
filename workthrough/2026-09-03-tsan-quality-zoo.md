# Candidate-only ThreadSanitizer Quality Zoo scenarios

## Overview

This slice adds two bounded CMake/CTest fixtures for validating ici's merged
deep-only `thread_sanitize` engine. The released-artifact registry remains
unchanged, while an explicit candidate registry includes the race and clean
counterpart. The two candidate-only TSan scenarios remain awaiting the
post-hermetic exact candidate digest/run; the released six-scenario path remains
separate.

## Context

The ici TSan engine needs a real C++ runtime race and a synchronized control case.
The fixtures must be deterministic without sleeps or high iteration counts, and
the released ici `v0.10.2` binary must not be asked to parse the new engine. The
candidate manifest therefore opts in to the additional scenarios while ordinary
`manifest.json` CI remains six scenarios.

## Changes made

### Candidate registry

- Added `quality-zoo/candidate-manifest.json` with the six released scenarios plus
  `cpp.tsan-data-race` and `cpp.tsan-synchronized`.
- Added a registry contract test in `quality-zoo/tests/test_run.py` asserting the
  two registries remain distinct and the TSan entries remain candidate-only while
  their post-hermetic exact candidate digest/run is pending.

### Race fixture

- Added `quality-zoo/scenarios/cpp/tsan-data-race/CMakeLists.txt`, `ici.toml`,
  `scenario.json`, and `expectations/candidate-thread-sanitize.json`.
- `src/race.cpp` starts two threads, waits for both to reach an atomic readiness
  counter, opens one atomic start gate, and updates a shared non-atomic integer.
  The expected answer is one `ThreadSanitizer` `tsan.data-race` finding at
  `src/race.cpp:15`, observed suite/engine status `FAIL`, and `MEASURED`/`exact`
  project-owned evidence.
- The scenario is class `red`, candidate-only, and requires `g++` and `cmake`.

### Synchronized fixture

- Added `quality-zoo/scenarios/cpp/tsan-synchronized/CMakeLists.txt`, `ici.toml`,
  `scenario.json`, and `expectations/candidate-thread-sanitize.json`.
- `src/clean.cpp` protects the same two-thread update with `std::mutex`. The
  expected answer is one informational CMake test target, zero TSan issues, and
  no non-info `thread_sanitize` finding.

## Verification results

### Repository contract tests

```text
python3 -m unittest discover -s quality-zoo/tests -v
Ran 58 tests in 0.922s
OK
```

The checked-in released manifest remains six entries; the candidate registry
test covers all eight candidate entries. No candidate digest gate is claimed by
this workthrough. The completed Qt six-scenario remote evidence is recorded below;
the TSan pair remains candidate-only pending its post-hermetic exact candidate
digest/run.

### Qt-complete remote evidence

The Qt candidate was accepted through the toy repository and the ici-hosted
candidate workflow. [Toy PR #55](https://github.com/jihoon22-lee/toy-projects/pull/55)
passed [run `33716728288`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33716728288)
and published [artifact `9878794296`](https://github.com/jihoon22-lee/toy-projects/actions/artifacts/9878794296);
sticky report comment [#5520667737](https://github.com/jihoon22-lee/toy-projects/pull/55#issuecomment-5520667737)
recorded the PR evidence. The PR squash-merged to `main` as
`a59461acaf0f2e967e6ba51e07e56ac7e73acbc6`, and exact-main [run `33717415609`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33717415609)
published [artifact `9879023706`](https://github.com/jihoon22-lee/toy-projects/actions/artifacts/9879023706).
The ici candidate acceptance [run `33718024450`](https://github.com/jihoon22-lee/ici/actions/runs/33718024450)
published [artifact `9879217928`](https://github.com/jihoon22-lee/ici/actions/artifacts/9879217928)
and recorded a six-scenario contract of `6/6 PASS`.

### Native CMake/TSan smoke

Both projects configured with CMake and built with
`-fsanitize=thread -fno-omit-frame-pointer -g` plus the matching linker flag.
The red `tsan_data_race` CTest run emitted one real TSan data race at
`src/race.cpp:15` and exited with the expected failing CTest status. The
`tsan_synchronized` CTest run passed in `0.02 sec` with no diagnostic.

### Merged ici source execution

Running the merged ici source from commit `cfd7066` with Python 3.10 produced:

- `cpp.tsan-data-race`: suite/engine `FAIL`, `MEASURED`, one
  `tsan.data-race`, `ThreadSanitizer`, exact location `src/race.cpp:15`, and
  ici exit `1`.
- `cpp.tsan-synchronized`: suite/engine `PASS`, `MEASURED`, zero TSan issues,
  one informational target at `CMakeLists.txt:1`, and ici exit `0`.

The local `/home/jihoon/projects/ici/dist/ici.pyz` was an older pre-TSan build
and rejected `engines.thread_sanitize`; it was not used as candidate evidence.
The Qt-complete six-scenario candidate acceptance is recorded above. The two TSan
scenarios remain awaiting the post-hermetic exact candidate digest/run, so this
workthrough does not claim a full eight-scenario candidate runner result.

## Follow-up

- Complete the post-hermetic exact candidate digest/run for the two TSan scenarios.
- Add that final digest to expectations for the existing six scenarios and update
  the candidate workflow to use `candidate-manifest.json`.
- Run and independently audit candidate intake, the full eight-scenario contract,
  cross-repository acceptance, PR comment/Pages evidence, and exact-main evidence.
