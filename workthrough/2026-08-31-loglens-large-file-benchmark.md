# LogLens 1 GiB/100만 record 대용량 benchmark

## Overview

LogLens의 bounded storage와 background loader가 실제 대용량 입력에서 유지해야 할 응답성,
처리량, 메모리 상한을 고정하기 위해 canonical 1 GiB benchmark를 추가하고 Qt5/Qt6 전체
capacity sweep을 완료했다. 측정 결과 GUI/CLI 기본 record capacity는 `8192`로 결정했다.

## Context

기존 L2 slice는 `RingBuffer`, chunk/record 상한, Tail N/From start 초기 로드와 ACK
backpressure를 구현했지만, synthetic log를 실제 제품 경로로 읽었을 때의 first result,
first paint, 전체 load 시간, throughput과 peak RSS를 함께 비교하는 기준이 없었다. 작은
fixture와 단일 capacity만으로는 capacity가 커질 때의 RSS 탈락과 Qt major별 차이를 발견할 수
없으므로 deterministic generator, production-path benchmark binary, aggregation runner와
budget decision을 한 계약으로 묶었다.

## Changes Made

### 1. Deterministic input과 production-path benchmark

- `loglens/benchmarks/generate.cpp`가 plain ISO record를 deterministic하게 생성한다.
- canonical input은 정확히 1,073,741,824 bytes, 1,000,000 records이며 SHA-256은
  `11186d3021e558c8ed5e33473198a6f9f281ca0605ae79739a928a87156435bb`다.
- `loglens/benchmarks/core_benchmark.cpp`는 production `FileTailer`, `RecordAssembler`,
  `RingBuffer`를 사용해 core load를 측정한다.
- `loglens/benchmarks/gui_benchmark.cpp`는 production `MainWindow`와
  `loadProgress` 경계를 사용해 GUI load와 first paint를 측정한다.
- 공통 입력·결과 계약은 `loglens/benchmarks/benchmark_common.hpp`와
  `loglens/benchmarks/benchmark_common.cpp`에 둔다.

### 2. CMake와 runner 계약

- `loglens/CMakeLists.txt`의 `LOGLENS_BUILD_BENCHMARKS` 기본값은 `OFF`다. opt-in 시
  `loglens-bench-generate`, `loglens-bench-core`, `loglens-bench-gui` target을 만든다.
- `loglens/benchmarks/run_benchmark.py`는 Python 3.10 표준 라이브러리만 사용하며 generator
  summary, exact size/record/newline 수와 SHA-256을 먼저 확인한다.
- 기본 sweep capacity는 `8192, 16384, 32768, 65536, 131072, 262144`, 각 capacity
  repetition은 3회, process timeout은 180초다. 결과는 min/median/p95/max로 집계하고
  decision 표에는 median을 사용한다.
- canonical 입력에서는 first result `≤ 5000 ms`, first paint `≤ 5000 ms`, load
  `≤ 60000 ms`, throughput `≥ 25 MiB/s`, records `≥ 25000 records/s`, core peak RSS
  `≤ 256 MiB`, GUI peak RSS `≤ 512 MiB`를 강제한다.
- recommendation rule은 best median load time 대비 10% 이내인 적격 capacity 중 가장 작은
  값을 고르는 것이다.

### 3. GUI progress telemetry와 회귀 테스트

`loglens/include/loglens/gui/main_window.hpp`와 `loglens/src/gui/main_window.cpp`에
초기 로드의 seen/retained/완료/error 진행 신호를 노출하고, `loglens/tests/test_main_window.cpp`
에서 처음부터 읽는 경로의 완료와 missing source error를 검증한다. 이 신호는 benchmark가
GUI event loop에서 첫 결과와 load 완료를 측정할 수 있게 하면서도 기존 GUI thread 소유권을
바꾸지 않는다.

### 4. Workflow와 artifact 경계

`.github/workflows/loglens-benchmark.yml`은 Qt5/Qt6 matrix에서 반대 Qt major를 disable한
Release build를 수행한다. `workflow_dispatch`와 주간 schedule에서만 실행하며 일반 PR이나
merge gate의 required check로 연결하지 않는다. runner가 보관하는 것은
`summary.json`, `summary.md`, `toolchain.json`, `toolchain.txt`, `samples/*.json`뿐이다.
1 GiB input과 process log는 scratch에 남고 artifact에 들어가지 않으며, workflow는 허용된
suffix와 파일 크기, 다운로드된 결과에 log input이 없는지를 다시 확인한다.

일반 PR의 `.github/workflows/ci.yml`에는 별도의 `benchmark-smoke`가 있다. 이 job은
1 MiB/1,000 records, capacity `64,256`, 1회, 30초 timeout, budget skip으로 harness의
correctness만 확인하고, `Merge Gate`가 성공을 요구한다. 따라서 작은 smoke와 비용이 큰
canonical 1 GiB budget sweep의 실행 정책을 혼동하지 않는다.

## Code Examples

Qt6 local benchmark는 다음처럼 실행한다. Qt5는 build 경로와 guard, `--qt-major`만 각각
`benchmark-qt5`, `CMAKE_DISABLE_FIND_PACKAGE_Qt6=ON`, `5`로 바꾼다.

```bash
cd loglens
cmake -S . -B build/benchmark-qt6 -DCMAKE_BUILD_TYPE=Release \
  -DLOGLENS_BUILD_BENCHMARKS=ON -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=ON
cmake --build build/benchmark-qt6 --parallel \
  --target loglens-bench-generate loglens-bench-core loglens-bench-gui
QT_QPA_PLATFORM=offscreen python3.10 benchmarks/run_benchmark.py \
  --build-dir build/benchmark-qt6 --scratch /tmp/loglens-benchmark-qt6 \
  --artifact-dir /tmp/loglens-benchmark-artifacts/qt6 --qt-major 6 \
  --bytes 1073741824 --records 1000000 \
  --capacities 8192,16384,32768,65536,131072,262144 \
  --repetitions 3 --timeout-seconds 180
```

## Verification Results

### Native tests and input contract

- Qt 5.15.18 CMake/CTest: 12/12 PASS
- Qt 6.10.2 CMake/CTest: 12/12 PASS
- canonical generator output: exact size, 1,000,000 records/newlines and the SHA-256 above
- Qt5/Qt6 benchmark sweep: capacities 8192, 16384, 32768, 65536, 131072, 262144 × 3 reps,
  180-second per-process timeout
- both Qt majors: capacities `8192..65536` eligible under the correctness/performance/RSS policy
- `131072` core and `262144` core+GUI fail their component RSS budgets; no threshold was lowered
- ici 0.6.0 deep/no-cache: Suite PASS, 12/12 tests, TEM 4.83,
  line/function/branch 93.6%/96.6%/81.8%, maximum complexity 15, duplication 1.71%,
  sanitizer PASS, HTML 433,351 bytes with zero external references
- background/Tail N parent slice는 구현 head `ce2a7cd91ff0a47c4f153b60f7fb7984de406ce9`의
  PR #25, workflow `33351033448`, sticky comment와 Pages HTML까지 원격 검증·병합됐다.

### Capacity 8192 median

| Qt | component | first result | first paint | load | throughput | records/s | peak RSS |
|---|---|---:|---:|---:|---:|---:|---:|
| 5 | core | 2.931 ms | — | 1212.313 ms | 844.666 MiB/s | 824869.622 | 25.699 MiB |
| 5 | GUI | 20.099 ms | 20.668 ms | 13374.639 ms | 76.563 MiB/s | 74768.375 | 55.848 MiB |
| 6 | core | 3.352 ms | — | 1349.536 ms | 758.779 MiB/s | 740995.486 | 25.719 MiB |
| 6 | GUI | 21.982 ms | 22.426 ms | 17949.170 ms | 57.050 MiB/s | 55712.882 | 58.648 MiB |

`8192`는 두 Qt 결과에서 best median load time 대비 10% 이내인 가장 작은 적격 capacity이므로
GUI/CLI의 추천 및 기본값으로 고정했다. 이 수치는 benchmark candidate의 local evidence이며,
원격 PR 검증 결과는 다음 절에 기록한다.

## Remote PR Evidence

benchmark candidate의 [PR #26](https://github.com/jihoon22-lee/toy-projects/pull/26)은 verified
head `564b782b93cfabed14db31f92e47619d5c17df2c`에서 검증됐다. [green workflow run](https://github.com/jihoon22-lee/toy-projects/actions/runs/33354504610)은
benchmark smoke 42초를 포함한 모든 checks가 SUCCESS였다. [sticky comment](https://github.com/jihoon22-lee/toy-projects/pull/26#issuecomment-5473343910)는
`diskmap: PASS · TEM 4.90`, `loglens: PASS · TEM 4.80`, warn 0과 HTML 링크를 담았다.
Pages `diskmap/pr/26/`와 `loglens/pr/26/`는 각각 HTTP 200·`text/html`·external refs 0개
(180160/334215 bytes)였다. PR26은 아직 병합하지 않았고, 현재 상태는 remote verified,
squash merge pending이다.

## Next Steps

- PR26의 green run과 sticky/Pages evidence 확인은 완료됐으므로, 아직 병합 전인 PR26을 squash
  merge한다.
