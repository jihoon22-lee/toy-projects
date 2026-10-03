# toy-projects

실사용을 목표로 하는 데스크톱·CLI 도구 모음. 각 프로젝트는 독립적인 제품이며,
자체 빌드·테스트·릴리스를 가진다.

## 프로젝트

| 이름 | 설명 | 빌드 |
|---|---|---|
| [diskmap](diskmap/) | 디스크 사용량 트리맵 뷰어와 cleanup·storage workbench | CMake · Qt6 GUI |
| [loglens](loglens/) | 로그 뷰어·분석기와 investigation workbench | CMake · Qt6 GUI |
| [buildscope](buildscope/) | compile database explorer (Python producer + C++/Qt consumer) | CMake · Python 3.10+ · Qt5/Qt6 |
| [envlens](envlens/) | Python 환경 snapshot·diff·runtime inspection CLI/library | pure Python 3.10+ |
| [abilens](abilens/) | Linux ELF/ABI artifact inspector | 손으로 쓴 Make · C++20 |

각 프로젝트의 사용법·빌드·테스트는 제품 디렉터리의 README에 있다.

- [abilens](abilens/README.md) · [buildscope](buildscope/README.md) ·
  [diskmap](diskmap/README.md) · [envlens](envlens/README.md) ·
  [loglens](loglens/README.md)
- [CHANGELOG.md](CHANGELOG.md) — 제품별 버전과 변경 내역
- [ROADMAP.md](ROADMAP.md) — 제품별 방향

## 릴리스 버전 규율

각 제품은 서로 독립적으로 버전을 결정한다. 포트폴리오 차원의 공용 버전은 없다.

- `patch`는 이미 공개된 제품의 defect, security, compatibility regression을 고칠 때만
  사용한다.
- `minor`는 하나의 응집된 사용자 가치가 실제로 쓸 수 있는 제품 checkpoint가 된 뒤에만
  올린다.
- 하나의 PR이 하나의 릴리스를 의미하지 않는다. 릴리스 태그는 `{product}/vX.Y.Z`
  스킴을 사용한다.

## 공통 구조 규칙

Qt를 쓰는 프로젝트는 아래 배치를 따른다. 공용 파서·모델은 `core`에 두고, Qt 셸은 그
계약을 사용하는 별도 계층으로 둔다. CLI와 GUI가 같은 핵심 의미론을 공유하도록 하기
위해서다.

```
<project>/
├── CMakeLists.txt        빌드 정의
├── include/<project>/    헤더는 전부 여기. 코어와 GUI 모두
│   └── gui/              Qt5/Qt6 셸의 헤더
├── src/                  구현
│   ├── main.cpp          CLI 드라이버
│   └── gui/              Qt 셸의 .cpp. 라이브러리 + 실행 파일로 나뉜다
└── tests/                각각 자체 실행 파일이 되는 테스트
```

**헤더는 예외 없이 `include/<project>/` 아래에 둔다.** GUI 를 라이브러리로 분리한 뒤로는
테스트가 그 헤더를 직접 include 하므로, `src/` 밖에서 쓰이는 헤더는 공개 헤더다.
`gui/` 하위를 두되 접두사(`loglens/`, `diskmap/`)는 유지한다. `-Iinclude` 하나로
`#include "loglens/gui/log_model.hpp"` 와 `#include "loglens/log_parser.hpp"` 가 같은
모양이 된다.

빌드 정의에는 **GUI 헤더를 명시적으로 나열해야 한다.** CMake 의 `AUTOMOC` 은 `.cpp` 와
같은 디렉터리에 같은 이름의 헤더가 있을 때만 알아서 찾는다. 헤더가 `include/` 로 가면
자동 탐지가 안 되므로 타겟 소스에 적어두지 않으면 `Q_OBJECT` 클래스가 조용히 vtable
미해결로 링크에 실패한다.

GUI 프로젝트는 GUI를 **라이브러리와 실행 파일로 나눈다.** 실행 파일 하나뿐이면 테스트가
링크할 대상이 없기 때문이다.

## Qt 환경

개발 환경의 기준 Qt는 6.10.2다.

```text
$ pkg-config --modversion Qt6Core Qt6Widgets Qt6Concurrent Qt6Test
6.10.2
6.10.2
6.10.2
6.10.2
```

GUI 테스트는 `QT_QPA_PLATFORM=offscreen`으로 헤드리스로 돌린다.

## 빌드와 테스트

각 제품은 자체 빌드·테스트 명령을 가진다. CI는 같은 명령을 그대로 실행한다.

```bash
# loglens — CMake/Qt6
cd loglens
cmake -S . -B build/gui -DCMAKE_BUILD_TYPE=Release
cmake --build build/gui --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir build/gui --output-on-failure

# diskmap — CMake/Qt6
cd ../diskmap
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure

# buildscope — Python 테스트 + CMake/CTest
cd ../buildscope
PYTHONPATH=python python3 -m unittest discover -s tests/python -p 'test_*.py'
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure

# envlens — pytest + ruff + mypy
cd ../envlens
uv run pytest && uvx ruff check . && uv run mypy src

# abilens — Make
cd ../abilens
make -j"$(nproc)" && make check
```

## GUI 빌드

```bash
# loglens (CMake)
cd loglens
cmake -S . -B build/gui -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=ON
cmake --build build/gui --parallel
./build/gui/src/gui/loglens-gui [경로]

# Qt5를 명시적으로 검증할 때
cmake -S . -B build/qt5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
cmake --build build/qt5 --parallel

# diskmap (CMake/Qt6)
cd diskmap
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
./build/src/gui/diskmap-gui [경로]
```

GUI에 경로를 주면 폴더 선택 대화상자를 건너뛰고 바로 스캔·로드하므로,
`QT_QPA_PLATFORM=offscreen` 헤드리스 스모크 실행이 가능하다.
