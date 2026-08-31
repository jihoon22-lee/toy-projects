# loglens L2 — bounded storage foundation

## Overview

LogLens의 기존 `RingBuffer`를 실제 CLI와 Qt model에 연결하고, 큰 파일을 한 번에 읽던 source
경계를 bounded chunk로 바꿨다. 단순 record count 제한만으로는 충분하지 않아 검토에서 발견된
긴 line/continuation, 계속 증가하는 one-shot 입력, legacy line adapter의 chunk 분할도 같은
slice에서 보완했다.

## Invariants and design

- record ID는 ring slot이 아니라 source generation 안에서 증가하는 absolute ID다.
- 기본 32,768개, 최대 1,000,000개만 보존하며 eviction은 contiguous Qt row remove/insert로
  알린다.
- GUI/CLI 모두 같은 `RingBuffer`와 `RecordDelta::record_index` 의미론을 사용한다.
- source poll은 기본 1 MiB, 최대 16 MiB이며 `more_available`로 backlog를 알린다.
- GUI는 backlog를 zero-delay event로 나눠 처리해 Follow off와 독립적으로 초기 snapshot을
  끝까지 읽는다.
- one-shot CLI는 첫 `snapshot_end`를 고정해 동시 append를 무한히 추격하지 않는다.
- physical/logical record는 기본 64 KiB, 최대 1 MiB다. 초과 bytes는 조용히 숨기지 않고
  `input_bytes`와 `omitted_bytes`로 보존하며 UI와 CLI에 표시한다.
- absolute ID counter가 wrap되기 전에는 명시적 `overflow_error`를 내어 잘못된 ID를 재사용하지
  않는다.

## Main changes

- `ring_buffer.*`: stable lookup/replace, eviction result, retained window와 dropped count.
- `log_source.*`: bounded read, snapshot boundary, CRLF/긴 line adapter 호환.
- `log_parser.*`, `log_record.hpp`: bounded partial/continuation과 omission metadata.
- `log_model.*`: absolute visible IDs, generation guard, incremental row notifications.
- `main_window.*`: cooperative backlog, bounded status, polling 상태 helper 분리.
- `main.cpp`: `--capacity`, bounded output/stats, fixed one-shot snapshot.
- tests: repeated/batch eviction, filter after wrap, stale generation, backlog with Follow off,
  multi-chunk CLI, invalid capacity, long line/CRLF/snapshot, oversized record/continuation.

## Review findings closed

초기 구현은 Qt6 10/10을 통과했지만 독립 검토에서 세 경계를 발견했다. `FileTailer::poll()`이
chunk 사이의 line을 분리했고, one-shot CLI가 성장 속도가 빠른 파일을 끝없이 따라갈 수
있었으며, `RecordAssembler::partial_`와 continuation 한 개가 record count 제한을 우회할 수
있었다. snapshot-limited overload, line adapter drain, bounded assembler metadata로 각각
회귀 테스트와 함께 수정했다. ici 첫 full run의 유일한 WARN인 `pollSource()` complexity 16도
오류 처리/chunk 적용/backlog scheduling을 분리해 최대 15로 낮췄다.

## Verification

```text
Qt 5.15 Release CMake + CTest: 10/10 passed
Qt 6.10 Release CMake + CTest: 10/10 passed
Qt 6 strict -Wall -Wextra -Wpedantic -Werror + CTest: 10/10 passed
Qt5/Qt6 offscreen GUI smoke: expected timeout exit 124
```

ici 0.6.0 최종 local verify:

```text
Suite PASS — 10 pass / 0 warn / 0 fail / 0 error / 2 skip
Tests 10/10, TEM 4.85
Coverage line 93.9% / function 96.9% / branch 83.1%
Maximum complexity 15 / 0 issues, duplication 1.43%, sanitizer clean
HTML 283,077 bytes, external script/link/image references 0
```

## Remaining L2 work

이 결과는 bounded foundation만 완료한다. 초기 Tail N/streaming index 선택, 실제 background
worker parsing 중 filter/search 안전성, 1 GiB·100만 record benchmark와 first-paint/throughput/
peak RSS 측정, 실측 기반 default/budget 확정은 다음 slice이며 완료로 표시하지 않았다.

## Remote PR evidence

구현과 local evidence를 담은 head `fa4fd1a`의 PR
[#24](https://github.com/jihoon22-lee/toy-projects/pull/24)는 workflow
[`33348597272`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33348597272)에서
manifest, diskmap/loglens `ici verify`, 두 프로젝트의 Qt5·Qt6 GUI, `Publish Reports & Sticky
Comment`, `Merge Gate`를 모두 통과했다. 원격 loglens 결과는 10/10 tests, TEM 4.82,
line/function/branch 93.5%/96.3%/83.4%, complexity 15/0 issues였다.

[sticky comment](https://github.com/jihoon22-lee/toy-projects/pull/24#issuecomment-5472700934)는
diskmap/loglens PASS와 두 HTML 링크를 포함한다. 직접 내려받은 `diskmap/pr/24/`는 HTTP 200,
`text/html`, 180,160 bytes, external refs 0개였고 `loglens/pr/24/`는 HTTP 200, `text/html`,
279,484 bytes, external refs 0개였다. 이 수치는 앞 절의 local candidate와 구분한 원격
evidence다.
