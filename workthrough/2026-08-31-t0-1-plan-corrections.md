# T0-1 계획 상태와 Qt 환경 문서 정리

## Overview

toy-projects의 승인된 제품 포트폴리오 마스터 계획과 이전 인수인계 문서 사이의 상태 차이를
정리했다. 병합된 계획 PR과 loglens T0-2 PR을 현재 기준에 반영하고, 로컬 Qt 5.15/Qt 6
설치 상태와 ici 실측 명령을 재현 가능한 형태로 기록했다.

## Context

- toy-projects 마스터 계획은 [PR #12](https://github.com/jihoon22-lee/toy-projects/pull/12)로
  병합됐지만 계획 체크리스트와 인수인계서는 아직 이전 세션의 진행 중 상태를 가리키고 있었다.
- loglens의 poll 경계 상태 보존은 [PR #14](https://github.com/jihoon22-lee/toy-projects/pull/14)로
  이미 병합됐으므로 T0-2만 완료 상태로 동기화했다.
- 로컬에는 Qt 5.15.18과 Qt 6.10.2가 모두 설치돼 있었지만, 문서는 Qt6만 현재 환경처럼
  기술하거나 Qt5 지원 완료와 설치 사실을 구분하지 않았다.

## Changes Made

### 1. 마스터 계획 상태 동기화

File: `docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md`

- 기준일과 PR #12 병합을 추가했다.
- T0-1 네 항목을 완료로 표시했다.
- T0-1의 실제 Qt/ici evidence와 T0 전체 완료가 아님을 기록했다.
- 이미 병합된 T0-2 체크리스트는 유지하고, T0-3~T0-5는 미완료로 남겼다.

### 2. 인수인계와 README 정리

Files: `docs/superpowers/2026-08-30-handover.md`, `README.md`

- 과거의 진행 중 branch 상태와 “세부 계획을 그대로 실행”으로 읽힐 수 있는 표현을 제거했다.
- master plan이 작업 순서와 완료 조건의 우선 기준임을 명시했다.
- loglens의 현재 native test 수(9)와 T0-2 병합 상태를 반영했다.
- README에 Qt major별 명령과 실제 버전을 기록하고, Qt5 설치와 Qt5 build/test 지원 완료를
  구분했다.
- Qt 셸 테스트가 아직 남은 갭이라는 설명은 T0-3/T0-4에 연결해 유지했다.

### 3. ROADMAP 및 갭 문서 연결

Files: `ROADMAP.md`, `ICI-GAPS.md`

- ROADMAP에 T0/L/D/B/E/A/Q stream과 마스터 계획의 연결표를 추가했다.
- 병합된 #12/#14와 T0 전체 완료 조건을 반영했다.
- 결함 이력의 17 + 9 = 26건, 수정 22건, 잔여 4건 설명이 서로 모순되지 않도록 정리했다.
- ICI-GAPS의 기준 ici 버전을 0.6.0, 현황일을 2026-08-31로 갱신했다.

## Code Examples

```markdown
| Stream | 역할 | 마스터 계획 |
|---|---|---|
| T0 | 현재 Qt 셸 보정, parser state와 Qt5/Qt6 안전망 | product-portfolio-master-plan.md |
| L | loglens incident explorer | product-portfolio-master-plan.md |
| D | diskmap storage workbench | product-portfolio-master-plan.md |
```

```bash
qmake -v       # Qt 5.15.18
qmake6 -v      # Qt 6.10.2
QT_QPA_PLATFORM=offscreen ../../ici/dist/ici.pyz verify \
  --report --html verify_report.html --github-summary
```

## Verification Results

### Environment evidence

```text
cmake version 4.2.3
ctest version 4.2.3
qmake -> Using Qt version 5.15.18 in /usr/lib/x86_64-linux-gnu
qmake6 -> Using Qt version 6.10.2 in /usr/lib/x86_64-linux-gnu
pkg-config Qt5Core/Widgets/Concurrent/Test -> 5.15.18 (all four)
pkg-config Qt6Core/Widgets/Concurrent/Test -> 6.10.2 (all four)
ici.pyz --version -> ici 0.6.0
```

### Native Qt6 tests

- loglens: `cmake -S . -B build/t0-docs -DCMAKE_BUILD_TYPE=Debug` followed by
  `cmake --build build/t0-docs --parallel` and
  `QT_QPA_PLATFORM=offscreen ctest --test-dir build/t0-docs --output-on-failure` — **9/9 passed**.
- diskmap: `qmake6 ../../diskmap.pro`, `make`, and
  `QT_QPA_PLATFORM=offscreen make check TESTARGS=-xunitxml` — **6 test programs passed**;
  QtTest reported `tests="6" failures="0" errors="0"` for the widget test.

### ici verification

- loglens: `Suite PASS`, TEM `4.08`, `10 passed`, `2 skipped`.
- diskmap: `Suite PASS`, TEM `4.85`, `10 passed`, `2 skipped`.

Generated HTML/JSON reports were kept outside the worktree because they are verification artifacts,
not source documentation.

## Next Steps

- T0-3/T0-4에서 실제 Qt 셸 테스트를 추가한다.
- T0-5에서 Qt5 강제 CMake와 Qt5 qmake build/test를 실행하고, 그 결과를 지원 matrix에 반영한다.
- T0 전체 완료 후에만 마스터 계획의 T0 checkpoint를 닫는다.
