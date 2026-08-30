# toy-projects

C++17 / Qt 5.15 및 Qt 6 환경을 대상으로 만드는 실사용 데스크톱 도구 모음.
동시에 [ici](https://github.com/jihoon22-lee/ici) 품질 게이트의 **외부 C++ 검증 대상**으로 쓰인다.

앞으로의 방향과 그 순서를 정한 이유는 [ROADMAP.md](ROADMAP.md) 에, 이 과정에서 발견한
ici 결함은 [ICI-GAPS.md](ICI-GAPS.md) 에 있다.

## 프로젝트

| 이름 | 설명 | 상태 |
|---|---|---|
| [diskmap](diskmap/) | 디스크 사용량 트리맵 뷰어 | Qt5/Qt6 GUI · 7 tests |
| [loglens](loglens/) | 로그 뷰어 / 분석기 | CMake + Qt5/Qt6 GUI · 라이브 팔로우 · 10 tests |

## 공통 구조 규칙

모든 프로젝트는 아래 배치를 따른다. 공용 파서·모델은 `core`에 두고, Qt 셸은 그 계약을
사용하는 별도 계층으로 둔다. Qt 사용 자체를 core에서 금지하지는 않지만, CLI와 GUI가 같은
핵심 의미론을 공유하도록 한다.

```
<project>/
├── ici.toml              project.source_dirs = ["src"]
├── <빌드 정의>           loglens 는 CMakeLists.txt, diskmap 은 diskmap.pro
├── include/<project>/    헤더는 전부 여기. 코어와 GUI 모두
│   └── gui/              Qt5/Qt6 셸의 헤더
├── src/                  구현. 전부 ici 검증 대상이다
│   ├── main.cpp          CLI 드라이버
│   └── gui/              Qt5/Qt6 셸의 .cpp. 라이브러리 + 실행 파일로 나뉜다
└── tests/                각각 자체 실행 파일이 되는 테스트
```

**헤더는 예외 없이 `include/<project>/` 아래에 둔다.** GUI 헤더를 `.cpp` 옆에 두던
시절도 있었지만, 그러면 같은 저장소에 배치 규칙이 둘이 된다. GUI 를 라이브러리로 분리한
뒤로는 테스트가 그 헤더를 직접 include 하므로 더더욱 그렇다 — `src/` 밖에서 쓰이는
헤더는 공개 헤더다.

`gui/` 하위를 두되 접두사(`loglens/`, `diskmap/`)는 유지한다. `-Iinclude` 하나로
`#include "loglens/gui/log_model.hpp"` 와 `#include "loglens/log_parser.hpp"` 가 같은
모양이 된다.

빌드 정의에는 **GUI 헤더를 명시적으로 나열해야 한다.** CMake 의 `AUTOMOC` 은 `.cpp` 와
같은 디렉터리에 같은 이름의 헤더가 있을 때만 알아서 찾고, qmake 는 `HEADERS` 에 적힌
것만 moc 에 넘긴다. 헤더가 `include/` 로 가면 둘 다 자동 탐지가 안 되므로, 적어두지
않으면 `Q_OBJECT` 클래스가 조용히 vtable 미해결로 링크에 실패한다.

### 빌드 시스템이 프로젝트마다 다른 것은 의도다

| 프로젝트 | 빌드 | ici 가 쓰는 것 |
|---|---|---|
| `loglens` | CMake | CMake 어댑터 + CTest |
| `diskmap` | qmake | qmake 어댑터 + `make check` |

ici 0.6.0 은 어댑터를 둘 갖는다. **각각 실물 프로젝트 하나씩으로 검증하지 않으면 한쪽은
픽스처만으로 설계한 것이 된다** — ici 저장소의 `examples/cpp-fixtures/` 는 단위 테스트용이지
실측 근거가 아니다. 실제로 이 배치가 아니었으면 놓쳤을 결함이 나왔다: qmake 는 Qt 링크
테스트를 `target_wrapper.sh` 로 실행하는데, ici 가 그 줄을 못 읽어 **`diskmap` 의 테스트
6개 중 5개만 세고 있었다.** 픽스처는 Qt 테스트만 있어서 다른 경로로 구제되는 바람에 이걸
드러내지 못했다.

### GUI 는 빌드되고 테스트된다

Qt 셸을 `src/` 밖에 두면 "검증할 필요 없는 코드" 라는 뜻이 되어버리므로 `src/gui/` 에 둔다.
0.5.x 까지는 `cpp_external_build_dirs` 로 GUI 를 링크에서 빼야 했다 — ici 가 moc 를 돌리지
못해 `Q_OBJECT` 클래스가 vtable 미해결로 링크되지 않았기 때문이다. **0.6.0 의 빌드 어댑터가
그 전제를 없앴다.** 이제 GUI 는 다른 소스와 똑같이 빌드·단위 테스트·sanitize 된다.

그래서 두 프로젝트 모두 GUI 를 **라이브러리와 실행 파일로 나눈다.** 실행 파일 하나뿐이면
테스트가 링크할 대상이 없다.

```toml
cpp_pkg_config = ["Qt6Widgets", "Qt5Widgets"]   # 설치된 Qt 헤더를 lint가 찾게
```

`cpp_external_build_dirs` 는 더 이상 쓰지 않는다. 루트에 빌드 디스크립터가 없는 프로젝트
(g++ 경로)에서는 여전히 유효하다.

### Qt 환경과 현재 실측

개발 환경에는 Qt 5.15.18과 Qt 6.10.2가 함께 설치돼 있다. 버전은 다음 명령으로 확인한다.

```text
$ qmake -v
Using Qt version 5.15.18 in /usr/lib/x86_64-linux-gnu
$ qmake6 -v
Using Qt version 6.10.2 in /usr/lib/x86_64-linux-gnu
$ pkg-config --modversion Qt5Core Qt5Widgets Qt5Concurrent Qt5Test
5.15.18
5.15.18
5.15.18
5.15.18
$ pkg-config --modversion Qt6Core Qt6Widgets Qt6Concurrent Qt6Test
6.10.2
6.10.2
6.10.2
6.10.2
```

2026-08-31 현재 Qt6 기본 환경에서 ici 0.6.0 release asset으로 다음 실측도 통과했다.

```bash
for p in loglens diskmap; do
  (cd "$p" && QT_QPA_PLATFORM=offscreen ../../ici/dist/ici.pyz verify \
    --report --html verify_report.html --github-summary)
done
```

결과는 `loglens: Suite PASS, TEM 4.84 (10 passed, 2 skipped)`와 `diskmap: Suite PASS,
TEM 4.85 (10 passed, 2 skipped)`다. Qt5 강제 CMake build는 loglens에서
`CMAKE_DISABLE_FIND_PACKAGE_Qt6=ON`으로, diskmap의 Qt5 qmake build/test는 #16에서
`/usr/bin/qmake`로 각각 별도 실측했다. T0-5에는 이 양 major 선택을 매 PR의 명시적 CI
matrix로 강제하는 공통 계약이 남아 있다.

### loglens 스트림 계약

`loglens`의 CLI와 GUI는 같은 `RecordAssembler`를 사용한다. `FileTailer::pollChunk`가
poll에서 읽은 raw bytes와 source generation을 전달하고, assembler가 다음 상태를 한 곳에서
보존한다.

- newline을 기준으로 한 physical line 번호
- 다음 poll에서 이어 붙일 partial bytes
- stack-trace continuation을 확장할 pending record
- truncation/restart 뒤 초기화되는 generation
- 선택된 format과 byte-preserving encoding/error policy

새 record는 `Append`, 기존 record의 continuation은 `Extend` delta로 구분된다. 따라서 GUI는
이미 표시한 행을 갱신하고 CLI는 같은 결과 벡터를 갱신한다. newline 없는 마지막 조각은
follow 모드에서는 보류하며, one-shot CLI의 명시적 EOF `flush()`에서만 record가 된다.

### Qt 셸 테스트 현황

GUI는 헤드리스 QtTest와 실제 fixture 파일을 사용해 상태 전이를 검증한다. `loglens`는
Qt5/Qt6 CMake/CTest에서 같은 테스트를 실행했고, `diskmap`의 내비게이션 셸도 T0-4에서
완료했다.

| 파일 | 상태 |
|---|---|
| `loglens/src/gui/log_model.cpp` | `QAbstractItemModelTester` 로 검증 |
| `diskmap/src/gui/treemap_widget.cpp` | `QSignalSpy` 로 검증 |
| `loglens/src/gui/main_window.cpp` | `test_main_window` — open/growth/truncation/read error/follow timer |
| `loglens/src/gui/timeline_widget.cpp` | `test_main_window` — empty/populated paint branch |
| `diskmap/src/gui/main_window.cpp` | `test_main_window` — scan, breadcrumb, descend/up, leaf no-op |

`loglens`의 ici 0.6.0 실측은 line 93.2% / function 96.9% / branch 81.8% / TEM 4.84이다.
`loglens/ici.toml`은 이 실측 아래에 slack을 둔 branch 75.0 / function 92.0을 게이트로
사용한다. QPainter/Qt 내부 예외 경로와 C++ type-check 미지원은 남은 명시적 한계다.

## CI 리포트

프로젝트마다 `ici verify` 가 따로 돌아 HTML 리포트가 여러 개 나온다. 이걸 PR 에 남기는 방식은
매트릭스 레그가 각자 게시하는 게 아니라 **집계 잡 하나가 전부 모아서** 게시한다.

레그별 게시가 안 되는 이유가 둘 있다. ici 의 sticky 마커는 고정 문자열 하나라 두 번째 게시가
첫 번째 댓글을 덮어쓰고, 병렬 레그가 같은 gh-pages 브랜치에 동시에 쓰면 Contents API 가
요구하는 blob sha 를 두고 경쟁하다 하나가 유실된다.

그래서 레그는 아티팩트만 올리고, `report-pr` 잡이 전부 내려받아
`ici publish --report-dir` 로 **순차 업로드 후 댓글 하나**를 남긴다. 프로젝트별로 행과 링크가
하나씩 붙는다. 그 잡은 PR 소스를 체크아웃하지 않는다 — 실행물은 체크섬 검증된 릴리스 pyz,
게시물은 verify 잡의 아티팩트뿐이라 PR 이 쓰기 토큰에 닿지 않는다.

## CI 품질 게이트 계약

현재 프로젝트 목록과 각 Qt GUI의 빌드·smoke 입력은
[`ci/projects.json`](ci/projects.json)이 유일한 기준이다. `discover` 잡은 이 manifest를
검증하고 `ici.toml`을 가진 프로젝트가 하나라도 목록에서 빠지면 실패한다. `ici verify`
matrix와 Qt GUI build/smoke matrix는 모두 같은 manifest 출력에서 생성되므로, 새 프로젝트를
추가할 때 한쪽 matrix만 수정하는 실수를 허용하지 않는다. GUI가 없는 순수 CLI 프로젝트는
`gui.enabled = false`를 명시해야 하며, 그 경우에도 ici verify에는 포함된다.

PR의 `report-pr`는 verify·GUI matrix가 성공 또는 실패로 끝난 뒤 항상 평가된다. 성공한
실행에서는 checksum을 확인한 ici `v0.6.0` release asset으로 모든 report artifact를
`gh-pages`에 순차 게시하고, repository 단위 concurrency로 Pages 쓰기 경합을 막는다. 이어서
실제 sticky 댓글을 API로 읽어 manifest 프로젝트 수만큼의 HTML 링크가 정확히 있는지 확인하고,
각 링크가 Pages에서 `text/html` 응답을 반환할 때까지 기다린다.

`Merge Gate`는 branch protection에서 required check로 설정해야 한다. 이 stable check가
manifest discovery, 모든 ici verify matrix leg, 모든 GUI matrix leg와 PR report 검증을 함께
요구한다. `push` 실행에서는 report 게시를 건너뛸 수 있지만, verify와 GUI 결과는 항상
성공해야 한다.

## 검증

```bash
cd <project>
QT_QPA_PLATFORM=offscreen ../../ici/dist/ici.pyz verify --report
```

`loglens`의 셸 상태 테스트만 빠르게 돌리려면 CTest를 쓴다. 테스트는 프로젝트 루트에서
실행되며, 실제 follow timer는 `QTRY_COMPARE`로 기다리고 truncation/read error는 Qt
meta-object로 기존 `pollSource` slot을 동기 호출해 구동한다.

```bash
cd loglens
QT_QPA_PLATFORM=offscreen ctest --test-dir build/gui --output-on-failure
```

위 명령은 `ici`와 `toy-projects`를 같은 프로젝트 디렉터리 아래의 형제 저장소로 둔 배치를
기준으로 한다. `QT_QPA_PLATFORM` 이 필요한 이유는 `diskmap` 의 위젯 테스트가 `QWidget` 을
만들기 때문이다.

ici 0.6.0 이상이 필요하다. 그 아래 버전에는 빌드 어댑터가 없어 루트의 빌드 디스크립터를
거부한다.

## GUI 빌드

```bash
# loglens (CMake)
cd loglens
cmake -S . -B build/gui -DCMAKE_BUILD_TYPE=Release
cmake --build build/gui --parallel
./build/gui/src/gui/loglens-gui [경로]

# Qt5를 명시적으로 검증할 때
cmake -S . -B build/qt5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
cmake --build build/qt5 --parallel

# diskmap (qmake)
cd diskmap
mkdir -p build/gui && cd build/gui
qmake6 ../../diskmap.pro && make -j
./src/gui/diskmap-gui [경로]

# diskmap (qmake, Qt 5.15)
mkdir -p ../gui-qt5 && cd ../gui-qt5
qmake ../../diskmap.pro && make -j
./src/gui/diskmap-gui [경로]
```

경로를 주면 폴더 선택 대화상자를 건너뛰고 바로 스캔한다. 덕분에
`QT_QPA_PLATFORM=offscreen` 으로 헤드리스 스모크 실행이 가능하고, CI 가 그렇게 쓴다.
