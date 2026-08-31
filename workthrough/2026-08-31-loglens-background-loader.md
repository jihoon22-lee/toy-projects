# LogLens L2 background loader와 Tail N

## Overview

LogLens의 초기 로드와 follow I/O·parsing을 GUI event loop에서 분리하고, 큰 파일을
예측 가능한 메모리와 queued signal 비용으로 탐색할 수 있는 L2 background loading slice를
완성했다. 초기 화면은 최신 logical record의 Tail N 또는 파일 처음부터 읽는 모드를 선택하며,
두 모드 모두 source snapshot과 physical line 위치를 보존한다.

## Context

bounded record store와 chunk/record byte limit만으로는 큰 파일을 안전하게 읽을 수 있어도,
초기 parsing이 GUI thread에서 오래 실행되거나 늦은 batch가 새 파일 선택 뒤 적용될 수 있었다.
또한 Tail N이 단순히 마지막 N개의 physical line을 자르면 stack-trace continuation과 원본
line number를 잃는다. 이번 slice는 I/O, parser, GUI model의 소유권과 취소 경계를 명시해 이
공백을 닫는다.

## Changes Made

### 1. Tail N과 From start 초기 선택

- `Latest records`(Tail N)와 `From start`를 GUI에서 선택할 수 있게 했다.
- `locateTailWindow()`가 bounded byte scan으로 logical record root의 offset을 찾는다.
- continuation line은 root count에서 제외하고, 선택된 suffix의 첫 physical line number를
  `RecordAssembler::reset()`에 전달한다.
- `FileTailer`는 caller가 검증한 initial offset에서 시작하고, 첫 poll의 file identity와
  snapshot boundary를 다시 확인한다.
- 선택 또는 초기 로드 중 rotation/replacement가 발생하면 두 파일의 bytes를 합치지 않고
  retryable source error로 알린다.

관련 핵심 파일:

- `loglens/include/loglens/initial_load.hpp`, `loglens/src/initial_load.cpp`
- `loglens/include/loglens/log_source.hpp`, `loglens/src/log_source.cpp`
- `loglens/include/loglens/log_parser.hpp`, `loglens/src/log_parser.cpp`

### 2. 전용 QThread와 ACK backpressure

- `LogLoadWorker`가 `FileTailer`와 `RecordAssembler`를 전용 `QThread`에서 소유한다.
- `LogModel`, timeline과 위젯 변경은 GUI thread에서만 일어나며 worker는 `LoadBatch`만
  queued signal로 전달한다.
- batch당 `RecordDelta`는 최대 512개다. GUI의 동일한 `job_id`/`sequence` ACK 전에는
  worker가 다음 source chunk를 읽거나 batch를 발행하지 않는다.
- pending delta와 initial backlog는 zero-delay step으로 순차 처리해 Qt queued event가
  무한히 쌓이지 않게 한다.
- 새 파일 선택은 thread-safe job selector로 이전 job을 취소한다. GUI는 stale job을
  무시하고 sequence mismatch가 보이면 현재 load를 중지한다.
- `Follow` 중지·재개와 MainWindow 소멸 시 cancellation gate를 통해 pending follow poll과
  background 작업을 안전하게 끝낸다.

관련 핵심 파일:

- `loglens/include/loglens/gui/log_load_worker.hpp`
- `loglens/src/gui/log_load_worker.cpp`
- `loglens/include/loglens/gui/main_window.hpp`
- `loglens/src/gui/main_window.cpp`

### 3. Background 중 검색·필터 UX

- 구조화된 `Filter`와 대소문자 구분 없는 raw-text search를 `LogModel`에 보존한다.
- worker가 batch를 보내는 동안에도 predicates는 GUI thread에서 즉시 바꿀 수 있고, 새로
  도착하는 record에 동일한 조건이 적용된다.
- timeline 재계산은 짧은 single-shot debounce timer로 묶어 batch마다 전체 visible set을
  다시 순회하는 비용을 줄인다.

관련 핵심 파일:

- `loglens/include/loglens/gui/log_model.hpp`, `loglens/src/gui/log_model.cpp`
- `loglens/tests/test_main_window.cpp`

### 4. 회귀 검증

`test_log_load_worker`는 실제 전용 QThread와 queued signal을 사용하고, 고정 sleep 대신
`QSignalSpy`/`QTRY`와 blocking queued meta-object 경계를 사용한다. 다음 경계를 검증한다.

- 512-record upper bound 및 ACK 전 추가 batch 미발행
- ACK 이후 전체 initial load drain과 sequence 순서
- 새 job 선택 뒤 stale job/batch/ack 억제
- Tail N의 continuation과 physical line number 보존
- 잘못된 request의 non-retryable error
- initial batch ACK 중 Follow disable의 poll 억제
- initial load 중 source rotation의 retryable rejection

`test_main_window`는 Tail N/From start controls, search/filter during load, stale sequence,
follow recovery와 기존 bounded model 상태 전이를 함께 확인한다.

## Verification Results

- Qt 5.15.18 CMake/CTest: 12/12 PASS
- Qt 6.10.2 CMake/CTest: 12/12 PASS
- Qt 6 strict `-Wall -Wextra -Wpedantic -Werror` build/CTest: 12/12 PASS
- worker 테스트는 Qt event loop와 실제 thread 경계를 사용하며 고정 sleep을 사용하지 않는다.
- 구현 head `e19fea9`의 ici deep no-cache local verify: Suite PASS, 11 pass / 0 warn / 0 fail /
  0 error / 2 skip, TEM 4.83, line/function/branch 93.4%/96.6%/81.6%, maximum complexity
  15(0 issues), duplication 1.72%, sanitizer PASS, HTML 428,025 bytes·external refs 0개.
- 이 slice에 대한 새 remote PR, sticky comment와 Pages HTML은 아직 만들지 않았다. 기존 PR #24의
  수치는 bounded foundation의 historical evidence로만 남긴다.

## Remaining Work

- 1 GiB synthetic log와 1,000,000 record benchmark를 추가하고 first-paint, throughput,
  peak RSS를 실측한다.
- benchmark 결과로 성능 budget과 default capacity를 결정한다.
- 그 전까지 GUI/CLI 기본 capacity 32,768은 provisional 값으로 유지한다.
