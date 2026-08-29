# WSL ext4 개발 워킹트리 이관

## Overview

`toy-projects`를 WSL 내부 ext4 clone으로 옮기고 `diskmap`과 `loglens`의 전체 C++ 품질
게이트 및 Qt6 GUI를 새 워킹트리에서 검증했다. README의 머신 고정 검증 경로와 실제 소스
구조와 맞지 않던 GUI 빌드 예시도 함께 수정했다.

## Context

- README의 품질 검증 명령이 특정 Windows 드라이브의 `ici.pyz` 절대 경로를 사용했다.
- GUI 소스는 `<project>/src/gui`에 있지만 문서 예시는 존재하지 않는 `<project>/gui`를
  CMake source로 지정했다.
- 기존 build 디렉터리는 이전 경로와 생성기 상태를 포함하므로 새 워킹트리에서 재사용하지
  않고 Release build를 다시 구성해야 했다.

## Changes Made

### 경로 독립 검증 명령

- `README.md`: `ici`와 `toy-projects`가 같은 프로젝트 디렉터리 아래의 형제 저장소라는
  배치를 명시했다.
- 프로젝트 디렉터리에서 `../../ici/dist/ici.pyz verify --report`를 사용하도록 변경했다.

### 정확한 Qt GUI 빌드 예시

- CMake source와 build 디렉터리를 각각 `src/gui`, `src/gui/build`로 수정했다.
- 실행 경로도 `./src/gui/build/<project>-gui`로 수정했다.

## Code Examples

```bash
cd diskmap
../../ici/dist/ici.pyz verify --report
cmake -S src/gui -B src/gui/build -DCMAKE_BUILD_TYPE=Release
cmake --build src/gui/build -j
QT_QPA_PLATFORM=offscreen ./src/gui/build/diskmap-gui "$PWD/src"
```

`loglens`도 같은 빌드 구조를 사용하며 smoke 입력으로 `tests/data/sample.log`를 연다.

## Verification Results

### diskmap

```text
ici suite: PASS
C++ tests: 5/5
TEM score: 4.86/5.0
Qt6 Release build: PASS
8-second offscreen scan smoke: PASS
```

### loglens

```text
ici suite: PASS
C++ tests: 7/7
TEM score: 4.94/5.0
Qt6 Release build: PASS
8-second offscreen log-load smoke: PASS
```

두 프로젝트 모두 lint, security, cycle, complexity, sanitizer, exception 엔진을 통과했다.
C++ type checking과 Python 전용 dead-code 엔진은 설계상 `NOT_APPLICABLE`로 분류됐다.

## Next Steps

- PR의 matrix `ici verify`와 Qt6 GUI build가 모두 통과한 뒤 squash merge한다.
- 두 앱에 필요한 추가 이관 작업은 없다.
