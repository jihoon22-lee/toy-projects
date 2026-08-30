# diskmap Qt 셸 내비게이션 테스트

## Overview

`diskmap`의 qmake GUI 라이브러리를 실제 `MainWindow` QtTest가 링크하도록 확장하고,
스캔 결과 표시와 탐색 trail의 정상·경계 동작을 검증했다. Qt 6 전용 마우스 좌표 API를
호환 경계로 감싸 Qt 5.15와 Qt 6에서 같은 소스와 테스트가 모두 실행되도록 했다.

## Context

기존 diskmap 테스트는 core와 `TreemapWidget` 신호만 확인했고, `MainWindow`의 비동기
스캔 결과 전달·breadcrumb·Up 동작은 헤드리스 smoke에만 의존했다. 또한 `position()`은
Qt 6에서만 제공되어 Qt 5.15 내부망 빌드를 막는 지점이었다. 이번 범위에서는 rescan
동시성 자체를 초록불로 가장하지 않고, stale generation 경합을 D1/D2 설계 입력으로
기록했다.

## Changes Made

### 1. MainWindow QtTest

- `diskmap/tests/test_main_window.cpp` 추가
  - 실제 `QTemporaryDir` fixture를 `scanPath()`에 전달
  - scan result가 treemap/status/breadcrumb에 나타나는지 확인
  - `nodeActivated` public signal seam으로 directory descend 확인
  - objectName으로 찾은 Up 버튼의 click을 통해 root 복귀와 root no-op 확인
  - leaf activation이 현재 node와 breadcrumb를 바꾸지 않는지 확인
- `diskmap/tests/test_main_window.pro` 추가 및 `tests/tests.pro`에 등록

```cpp
const diskmap::FsNode* root = view->currentNode();
emit view->nodeActivated(&root->children.front());
QVERIFY(upButton(window)->isEnabled());

upButton(window)->click();
QCOMPARE(view->currentNode(), root);
QVERIFY(!upButton(window)->isEnabled());
```

테스트는 child widget 순서나 QLabel 개수에 의존하지 않고
`breadcrumb`, `status`, `upButton`, `treemap` objectName과 공개 `scanPath()`/
`nodeActivated` 계약만 사용한다.

### 2. Qt 5/6 셸 호환성

- `MainWindow`의 버튼·상태·breadcrumb·treemap에 안정적인 objectName과 accessibility name을
  부여했다.
- `TreemapWidget`의 마우스 좌표를 `eventPos()`로 추상화했다.
  - Qt 6: `QMouseEvent::position()`
  - Qt 5: `QMouseEvent::localPos()`
- `const FsNode*` signal payload를 metatype으로 선언·등록해 Qt 5 `QSignalSpy`의
  `Unable to handle parameter` 경고도 제거했다.

### 3. Stale generation 입력

마스터 계획의 D1에 다음 경합을 기록했다: worker scan A가 실행 중일 때 D2에서 rescan B를
허용하면 완료 순서에 따라 오래된 A 결과가 B 화면과 breadcrumb를 덮을 수 있다. generation
token을 결과에 붙이고 완료 시 현재 generation이 아니면 폐기하는 계약과 race 테스트는
D2에서 구현한다.

## Verification Results

### Qt 6

```text
qmake6 -query QT_VERSION: 6.10.2
make check: 7 test binaries passed
TestTreemapWidget: 6 tests, failures=0, errors=0
TestMainWindow: 6 tests, failures=0, errors=0
```

### Qt 5

```text
qmake -query QT_VERSION: 5.15.18
make check: 7 test binaries passed
TestTreemapWidget: 6 tests, failures=0, errors=0
TestMainWindow: 6 tests, failures=0, errors=0
system-err: empty
```

### ici

```text
ici 0.6.0
Suite: PASS — all applicable engines passed
test: 7/7, line 98.6%, function 97.1%, branch 88.5%, TEM 4.85/5.0
```

`type`과 `dead`의 C++ 미지원 SKIP은 기존 프로젝트 정책에 따른 것으로, suite는 PASS다.

## Next Steps

- D1/D2에서 scan identity와 cancellation/generation guard를 설계한 뒤 rescan race를
  결정적 fake source 테스트로 추가한다.
