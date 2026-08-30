# loglens poll 경계 스트림 상태 보존

## Overview

`loglens` CLI와 GUI가 서로 다른 익명 continuation folding 경로를 사용하던 문제를
해결했다. `RecordAssembler`가 newline, partial bytes, physical line 번호, pending
continuation과 source generation을 소유하고, 두 front end는 같은 `Append`/`Extend`
delta를 적용한다.

## Context

기존 `parseLines`와 CLI의 `appendLine`은 매 poll마다 line 번호를 1부터 다시 붙이고,
poll 사이에 stack trace continuation을 기억하지 못했다. `FileTailer`는 newline 없는
마지막 조각도 `std::getline`으로 소비해 다음 append와 합칠 수 없었다. 파일 truncation
뒤에는 예전 partial/continuation이 새 generation에 섞일 위험도 있었다.

## Changes Made

### 1. Stateful parser contract

- `loglens/include/loglens/log_parser.hpp` / `src/log_parser.cpp`
  - `RecordDelta`에 `Append`와 `Extend`, logical record index, physical line, generation을
    정의했다.
  - `RecordAssembler::consumeBytes()`는 LF가 없는 조각을 보류하고, `flush()`에서만
    명시적으로 완성한다.
  - `consumeLine()`과 `consumeLines()`도 같은 partial 상태를 사용한다.
  - continuation은 이미 발표한 record의 전체 갱신본을 `Extend`로 발행한다.
  - `reset(generation)`은 line/index/partial/pending 상태를 함께 초기화한다.

```cpp
auto deltas = assembler.consumeBytes(chunk.bytes);
// A normal follow poll never flushes a partial line.
auto eof = assembler.flush();  // one-shot CLI's explicit EOF policy
```

### 2. Raw byte tailing and generation

- `loglens/include/loglens/log_source.hpp` / `src/log_source.cpp`
  - `SourceChunk`와 `FileTailer::pollChunk()`을 추가해 raw bytes와 generation 변경을
    전달한다.
  - 파일이 이전 offset보다 작아지면 restart count와 generation을 증가시키고 offset을
    0으로 되돌린다.
  - 실제로 읽은 byte 수만 offset에 반영해 동시 append를 건너뛰지 않게 했다.
  - 기존 `poll(vector<string>&)` API는 호환용으로 유지하고 chunk에서 complete line을
    변환한다.

### 3. CLI/GUI and model integration

- `loglens/src/main.cpp`
  - 중복 `appendLine`을 삭제하고 assembler delta를 record vector에 적용한다.
  - one-shot 실행에서는 EOF `flush()`를 호출해 newline 없는 마지막 record를 보여준다.
- `loglens/src/gui/main_window.cpp` / `include/.../main_window.hpp`
  - 익명 `parseLines`를 삭제하고 open/follow 모두 같은 assembler를 사용한다.
  - generation 변경 시 assembler와 model을 함께 reset한다.
- `loglens/src/gui/log_model.cpp` / `include/.../log_model.hpp`
  - continuation `Extend`를 기존 row의 `dataChanged`로 반영한다.
  - filter 결과가 바뀌는 extension은 올바른 insert/remove model signal을 낸다.
- `README.md`
  - 공통 parser contract와 partial/flush 의미론을 기록했다.

### 4. Regression coverage

- parser: initial/next poll line numbering, continuation across polls, leading
  continuation, partial bytes, mixed line/byte input, explicit flush, generation reset
- source: partial chunk suffix, truncation generation and bytes
- model: visible update and filter membership transitions

## Verification Results

```text
cmake -S loglens -B loglens/build/stream-check -DCMAKE_BUILD_TYPE=Debug
cmake --build loglens/build/stream-check --parallel
100% tests passed, 0 tests failed out of 8

QT_QPA_PLATFORM=offscreen /home/jihoon/projects/ici/dist/ici.pyz verify --report
Suite: PASS — all applicable engines passed
TEM: 4.14 / 5.0, line 77.2%, func 85.8%, branch 63.0%
```

## Known limitations / Next Steps

- `FileTailer`의 restart detection은 현재 size 감소 기반이다. 같은 크기 이상으로
  교체되는 inode/device rotation과 retryable read 상태는 L1 reliable-tailing에서
  identity abstraction과 함께 확장한다.
- Qt shell 자체의 MainWindow/timer 테스트와 Qt 5/Qt 6 matrix는 T0-3/T0-5에서
  별도 milestone으로 진행한다.

Implementation commit: `81e9c2d fix(loglens): preserve parser state across file polls`
