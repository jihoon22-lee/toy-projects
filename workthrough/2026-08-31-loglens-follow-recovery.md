# loglens L1 Slice 2 — follow recovery 상태 머신

## Overview

`loglens`가 follow 중인 파일이 잠시 사라지거나 읽을 수 없게 되어도 사용자가 다시
열지 않고 복구할 수 있도록 GUI 상태를 보강했다. 마지막으로 정상적으로 읽은 행은
retryable source 오류 동안 유지하고, 새 file identity가 확인된 뒤에만 parser와 모델을
새 generation으로 교체한다. PR #22의 첫 원격 검증에서 일부 파일시스템의 즉시 inode
재사용 경계가 추가로 드러났고, 후속 수정 `d419d2f`로 identity가 재사용돼도 복구를
새 generation으로 강제하도록 보완했다.

이 문서는 구현 커밋 `4094ff5`, 테스트 커밋 `ca56d2e`, 복구 경계 보완 커밋 `d419d2f`의
설계·검증을 기록한다. local
ici 0.6.0 검증과 Qt5/Qt6 headless smoke도 완료했지만, GitHub Actions CI와 report-pr sticky
HTML/Pages 게시 검증은 아직 원격 PR 게이트로 남아 있다.

## Context

L1 Slice 1에서 `FileTailer`는 device/inode identity, generation, position, typed source
error를 반환할 수 있게 됐다. 그러나 GUI가 모든 source 오류를 즉시 follow 중지로 처리하면
원자적 log rotation의 짧은 pathname 공백을 복구하지 못하고, 기존 화면도 사용자가 다시
파일을 열기 전까지 갱신되지 않는다.

이번 slice의 핵심 불변식은 다음과 같다.

- retryable 오류는 일시적인 source 부재로 취급하고 기존 화면과 retry timer를 보존한다.
- replacement가 성공적으로 읽히기 전에는 기존 generation과 새 source의 bytes를 섞지 않는다.
- fatal unsupported source는 follow를 중지하지만 마지막으로 유효했던 화면은 보존한다.
- 사용자가 Follow를 끄면 timer와 retry를 멈추고, 다시 켜야 polling을 재개한다.
- 최초 open 실패는 기존 계약대로 source/model을 비우고 Follow를 끈다.

## CI에서 발견된 복구 경계와 후속 수정

PR #22의 첫 GitHub Actions 실행
[`33335607699`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33335607699)은
`disablingFollowWhileWaitingStopsPolling`에서 Qt5·Qt6 native test와 `ici verify`
모두 실패했다. missing 상태에서 파일을 제거한 직후 다시 만들면 일부 파일시스템이
이전 파일의 device/inode을 즉시 재사용할 수 있는데, 기존 구현은 이를 동일 source로
오인했다. 특히 더 큰 replacement가 Follow 재개 시 이전 offset에서 읽혀 stale/new
bytes가 섞였고, 해당 테스트의 실제 `rowCount`가 2가 됐다.

`d419d2f` (`fix(loglens): force generation after source recovery`)는
`FileTailer`에 `recovery_pending_`와 `recovery_restart_started_`를 추가했다. unavailable
interval을 한 번이라도 관측하면 identity가 재사용돼도 다음 성공 read를 새
`Replaced` generation으로 처리하고 offset을 0으로 되돌린다. 같은 대기 구간에서
poll이 여러 번 발생해도 restart/generation은 한 번만 증가하도록 하여 double increment도
막는다. 회귀 테스트도 실제로 unlink 후 replacement를 생성하는 순서로 바꿔 inode 재사용
경계를 재현한다.

## Design

### Follow 상태

`MainWindow`에 private `FollowState`와 retry 시도 횟수를 추가했다.

| 상태 | 진입 조건 | 화면/타이머 의미 |
|---|---|---|
| `Stopped` | 사용자 중지 또는 fatal 오류 | polling 중지 |
| `Following` | 정상 open, 정상 poll, 사용자 재개 | tail을 읽고 timer 활성 |
| `WaitingRetry` | retryable missing/permission/open/stat/read 오류 | 기존 행 유지, timer 활성, `Follow waiting (attempt N)` 표시 |

성공 poll에서 `chunk.generation`이 assembler generation과 다르거나 tailer restart가
감지되면 assembler와 model을 reset한 뒤 새 bytes를 적용한다. 이 비교가 있어야 missing
상태에서 행을 보존하다가 replacement의 첫 bytes를 읽는 순간에만 stale 행을 제거할 수
있다.

### 사용자 경험

- 같은 경로의 파일이 unlink된 동안에는 기존 행을 그대로 보며 retry 상태를 확인한다.
- staging 파일을 원자적으로 rename해 새 identity가 생기면 새 파일의 첫 line부터 다시
  표시한다.
- 대기 중 Follow를 끄면 파일이 재생성되어도 화면을 자동으로 바꾸지 않는다.
- Follow를 다시 켜면 pending replacement를 읽고 새 generation으로 화면을 갱신한다.
- 디렉터리 등 unsupported file type이 경로에 놓이면 `Follow stopped: ...`를 표시하고
  polling을 멈춘다. 마지막 정상 화면은 남긴다.

## Changes Made

### Implementation

- `loglens/include/loglens/gui/main_window.hpp`
  - `Stopped`, `Following`, `WaitingRetry` 상태와 retry 시도 횟수를 선언했다.
- `loglens/src/gui/main_window.cpp`
  - retryable `SourceChunk` 오류에서 기존 model을 지우지 않고 대기 상태를 표시한다.
  - generation/restart 경계를 성공 poll에서 확인해 stale parser/model을 함께 reset한다.
  - Follow 토글이 timer와 상태 머신을 명시적으로 제어하도록 했다.
- `loglens/include/loglens/log_source.hpp`, `loglens/src/log_source.cpp`
  - unavailable interval 이후 identity가 재사용돼도 새 generation과 offset reset을
    보장하는 `recovery_pending_`/`recovery_restart_started_` 상태를 추가했다.
  - 한 recovery interval에서 restart/generation이 중복 증가하지 않도록 경계를 분리했다.

### Deterministic tests

- `loglens/tests/test_main_window.cpp`
  - `retryableSourceErrorKeepsFollowingAndVisibleRows`
  - `sourceReplacementRecoversWithCleanRows`
  - `disablingFollowWhileWaitingStopsPolling`
  - `unsupportedSourceStopsFollowing`
  - `QTemporaryDir`와 실제 파일을 사용하고, poll slot은
    `QMetaObject::invokeMethod(..., Qt::DirectConnection)`으로 직접 호출해 고정 sleep
    없이 상태 전이를 검증한다.

### Synchronized documentation

- `README.md`: follow 오류 의미론과 `test_main_window` 검증 범위를 갱신했다.
- `docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md`: L1 Slice 2와
  stopped/retryable/fatal 상태 계약을 완료로 표시하고 native evidence와 남은 검증 경계를
  기록했다.
- `docs/superpowers/2026-08-30-handover.md`: Slice 2의 현재 branch/commit과 다음 PR 검증
  순서를 반영했다.
- `workthrough/2026-08-31-loglens-follow-recovery.md`: 본 구현과 검증을 재현할 수 있는
  작업 기록을 추가했다.

## Code Example

```cpp
if (!tailer_->pollChunk(chunk, error)) {
    if (chunk.error.retryable) {
        followState_ = FollowState::WaitingRetry;
        ++retryAttempts_;
        status_->setText(tr("Follow waiting (attempt %1): %2")
                             .arg(retryAttempts_)
                             .arg(QString::fromStdString(error)));
        return;
    }

    status_->setText(tr("Follow stopped: %1").arg(QString::fromStdString(error)));
    followBox_->setChecked(false);
    return;
}
```

The error branch deliberately returns without resetting the assembler or model. The reset is
performed only after a successful poll proves that the source generation changed.

## Verification Results

### Native Qt matrix

첫 PR 검증 전의 구현·테스트 커밋은 설치된 두 Qt 환경에서 complete Release CMake/CTest
suite를 통과했지만, 원격에서 즉시 inode 재사용 경계가 발견됐다. `d419d2f` 수정 후
Qt5와 Qt6에서 전체 suite를 다시 실행해 다음을 확인했다.

```text
Qt 6 — CMake build + QT_QPA_PLATFORM=offscreen ctest: 10/10 passed
Qt 5 — CMake build + QT_QPA_PLATFORM=offscreen ctest: 10/10 passed
```

The four recovery cases, including `disablingFollowWhileWaitingStopsPolling`, ran as part of
`test_main_window`; the existing CLI, core, model, and GUI tests remained green in both runs.
The first remote run reported `loglens` 9/10 with one sanitize defect and therefore also
failed the `ici verify` and Merge Gate jobs; `diskmap` remained green.

### Local ici verification

The primary agent ran `/home/jihoon/projects/ici/dist/ici.pyz` version `0.6.0` against this
branch. The result was:

```text
Suite: PASS
Engine summary: 10 pass / 0 warn / 0 fail / 0 error / 2 skip
Tests: 10/10
Coverage: line 92.5% / function 97.1% / branch 80.8%
TEM: 4.86 / 5.0
Maximum complexity: 12
Duplication: 1.3%
Elapsed: 81.28 s
```

The standalone HTML report `/tmp/loglens-recovery-fixed.html` was `265789` bytes and had
`0` external HTTP `src`/`href` references.

### Headless GUI smoke

The Qt5 and Qt6 GUI binaries each remained alive for the complete eight-second smoke timeout
with `QT_QPA_PLATFORM=offscreen`. The expected timeout exit was `124` in both runs; this is
the success condition for the long-running GUI process.

### Deliberately pending evidence

- d419d2f를 포함한 PR #22의 원격 CI 재실행과 Merge Gate
- 재실행 결과의 `report-pr` sticky comment와 generated HTML 링크 게시, 각 링크 HTTP 확인

These remote checks are release/PR gates and are intentionally not inferred from the local
verification above.

## Next Steps

Open the implementation PR, wait for all required CI checks to pass, and verify the actual
report-pr comment and Pages HTML before merging. After that, continue with diskmap D1 Slice 2:
cycle-safe symlink traversal, hardlink aggregate accounting, and filesystem path semantics.
