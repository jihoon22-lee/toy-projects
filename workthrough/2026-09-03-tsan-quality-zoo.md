# Candidate-only ThreadSanitizer Quality Zoo scenarios

## Overview

This slice adds two bounded CMake/CTest fixtures for validating ici's merged
deep-only `thread_sanitize` engine. The released-artifact registry remains
unchanged, while an explicit candidate registry includes the race and clean
counterpart. The current candidate is bound to exact ici main
`6ee08b14fa598a19074af7afed4368fd79b19b2b`; its all-green main CI run is
`33732817172`, and its authenticated five-file intake is `PASS`. The released
six-scenario path remains separate. Exact toy PR/main acceptance and the remote
`8/8` candidate workflow remain pending.

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
  binding their expectations to the current candidate executable digest
  `424108397858470b1209bc2749b580a858fb06c8b09aaa2e4772c94e43690bb5`.

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
test covers all eight candidate entries. All 58 unit tests pass. The current
candidate producer run
[`33733780877`](https://github.com/jihoon22-lee/ici/actions/runs/33733780877)
published artifact
[`9884927798`](https://github.com/jihoon22-lee/ici/actions/artifacts/9884927798)
for exact ici main `6ee08b14fa598a19074af7afed4368fd79b19b2b`; main CI run
[`33732817172`](https://github.com/jihoon22-lee/ici/actions/runs/33732817172)
was all green. The raw ZIP is `2,293,522` bytes with SHA-256
`9a50972a5cb4ad96b2b0cf912e27c17a600fc19d6d899c6e33028d4449b1122d`; the contained
`ici.pyz` is `2,292,199` bytes with SHA-256
`424108397858470b1209bc2749b580a858fb06c8b09aaa2e4772c94e43690bb5`. The
candidate reports version `0.10.2` with `stable=false`, and authenticated five-file
intake is `PASS`. The completed Qt six-scenario remote evidence is recorded below;
the exact toy PR/main and remote `8/8` candidate workflow remain pending.

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
The current candidate run excluding Qt returned `7/7 PASS`: the race scenario
observed suite/engine `FAIL`, one `tsan.data-race` at `src/race.cpp:15`, and ici
exit `1`; the synchronized scenario observed `PASS`, zero TSan issues, and ici
exit `0`. A full local eight-scenario aggregate has only the Qt contract failure
because `clazy` is unavailable locally, while CI installs `clazy`. The
Qt-complete six-scenario candidate acceptance is recorded above. This workthrough
does not claim exact toy PR/main acceptance or the remote `8/8` candidate workflow;
both remain pending. No version or release changed.

## Follow-up

- Complete exact toy PR/main acceptance for this candidate-only TSan slice.
- Independently audit the toy PR's single sticky ici comment, its published HTML,
  exact-main CI, and Pages evidence.
- Run and independently audit the remote `8/8` candidate workflow against exact toy
  main and its immutable acceptance artifact.
- Keep the TSan pair candidate-only and the released six-scenario manifest unchanged;
  no version or release change is planned.
