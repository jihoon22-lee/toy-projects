# loglens Qt 셸 상태 테스트와 Qt5/Qt6 CMake 검증

## Overview

`loglens`의 Qt 셸을 실제 파일 tailing 상태까지 검증하는 `test_main_window`를 추가했다.
open, append growth, truncation, read error, failed-open recovery를 확인하고 follow
checkbox/timer/status와 접근성 식별자를 회귀 테스트한다. TimelineWidget의 빈 상태와
색상 막대 렌더링도 같은 헤드리스 QtTest에서 확인한다.

## Context

기존에는 `LogModel`만 QtTest로 검증됐고 `MainWindow`의 500ms follow timer와
`TimelineWidget::paintEvent`는 헤드리스 smoke에만 의존했다. 특히 새 파일을 열 수 없을 때
이전 모델과 timeline이 남거나 기존 timer가 계속 실행되는지에 대한 계약이 없었다.
또한 CMake가 Qt6를 고정해 Qt5.15 환경에서 같은 소스를 configure할 수 없었다.

## Changes Made

### 1. MainWindow 셸 계약과 상태 정리

- `loglens/src/gui/main_window.cpp`
  - open button, filter, follow checkbox, timeline, table, status label, poll timer에
    안정적인 `objectName`과 필요한 `accessibleName`을 부여했다.
  - 파일 open 실패 시 tailer/parser/model뿐 아니라 timeline, window title, follow timer도
    정리한다. 따라서 실패한 새 경로가 이전 source의 상태를 보여주지 않는다.

### 2. Qt5/Qt6 CMake와 CTest

- `loglens/CMakeLists.txt`와 `loglens/src/gui/CMakeLists.txt`
  - `find_package(QT NAMES Qt6 Qt5 ...)`와 versioned targets를 사용한다.
  - `CMAKE_DISABLE_FIND_PACKAGE_Qt6=ON`으로 Qt5를 강제할 때 REQUIRED names에서 Qt6를
    먼저 제거해 CMake의 disable-package 오류를 피한다.
  - `test_main_window`를 `loglens_gui`에 링크하고, Qt 위젯 테스트는 CTest 자체에서
    `QT_QPA_PLATFORM=offscreen`을 사용한다.

```cmake
set(_qt_package_names Qt6 Qt5)
if(CMAKE_DISABLE_FIND_PACKAGE_Qt6)
    list(REMOVE_ITEM _qt_package_names Qt6)
endif()
find_package(QT NAMES ${_qt_package_names} REQUIRED COMPONENTS Widgets Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Test)
```

### 3. 회귀 테스트와 품질 기준

- `loglens/tests/test_main_window.cpp`
  - 임시 fixture 파일 open, timer 기반 append, truncation reset, read error stop,
    failed open stale-state clear를 검증한다.
  - `QTRY_COMPARE_WITH_TIMEOUT`은 실제 timer 연결을 확인하면서 고정 sleep을 피한다.
  - 파일 교체와 오류는 Qt meta-object로 기존 private `pollSource` slot을 동기 호출해
    filesystem/timer 경계를 결정적으로 만들며, 테스트만을 위한 public API는 추가하지 않는다.
  - object/accessibility names와 TimelineWidget empty/populated paint path를 검증한다.
- `loglens/ici.toml`
  - Qt5/Qt6 pkg-config 이름을 모두 선언했다.
  - ici 0.6.0 실측 line 93.2% / function 96.9% / branch 81.8% / TEM 4.84를 기록하고,
    안정적인 slack을 둔 function 92.0 / branch 75.0으로 threshold를 상향했다.
- `README.md`, `ROADMAP.md`, 두 계획 문서
  - loglens Qt 셸 테스트 완료 상태, Qt major 실행 증거, 남은 C++ type-check/Painter
    내부 한계를 최신화했다.

## Verification Results

### Qt6

```text
cmake -S . -B build/qt6-shell-0831 -DCMAKE_BUILD_TYPE=Debug
cmake --build build/qt6-shell-0831 --parallel
ctest --test-dir build/qt6-shell-0831 --output-on-failure
100% tests passed, 0 tests failed out of 10
loglens: using Qt6 6.10.2
```

### Qt5

```text
cmake -S . -B build/qt5-shell-0831 -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
cmake --build build/qt5-shell-0831 --parallel
ctest --test-dir build/qt5-shell-0831 --output-on-failure
100% tests passed, 0 tests failed out of 10
loglens: using Qt5 5.15.18
```

### ici release verification

```text
QT_QPA_PLATFORM=offscreen /home/jihoon/projects/ici/dist/ici.pyz verify --report
ici Unified Verification Suite — PASS
10/10 Tests Passed | Line: 93.2%, Func: 96.9%, Branch: 81.8%
TEM: 4.84 / 5.0
Total Engines: 12 (Pass: 10, Warn: 0, Fail: 0, Error: 0, Skip: 2)
Suite: PASS — all applicable engines passed
```

The two skipped engines are expected for this C++-only project: Python dead-code analysis and
the not-yet-implemented C++ type checker.

## Next Steps

- T0-5에서 이미 로컬 실측을 마친 loglens/diskmap Qt5·Qt6 선택을 명시적인 CI matrix로
  강제한다.
- Treat larger/equal-size inode replacement and retryable read semantics as the planned loglens
  L1 tailing work; this patch intentionally retains the existing size-based generation contract.
- Add a C++ type-check engine in ici before claiming type coverage for this project.
