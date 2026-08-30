# toy-projects

C++17 / Qt6 로 만드는 실사용 데스크톱 도구 모음.
동시에 [ici](https://github.com/jihoon22-lee/ici) 품질 게이트의 **외부 C++ 검증 대상**으로 쓰인다.

앞으로의 방향과 그 순서를 정한 이유는 [ROADMAP.md](ROADMAP.md) 에, 이 과정에서 발견한
ici 결함은 [ICI-GAPS.md](ICI-GAPS.md) 에 있다.

## 프로젝트

| 이름 | 설명 | 상태 |
|---|---|---|
| [diskmap](diskmap/) | 디스크 사용량 트리맵 뷰어 | 코어 + Qt6 GUI |
| [loglens](loglens/) | 로그 뷰어 / 분석기 | 코어 + Qt6 GUI · 라이브 팔로우 |

## 공통 구조 규칙

모든 프로젝트는 아래 배치를 따른다. 로직은 `core`, Qt 는 그 위의 껍데기라는 설계를 강제한다.

```
<project>/
├── ici.toml              project.source_dirs = ["src"]
├── <빌드 정의>           loglens 는 CMakeLists.txt, diskmap 은 diskmap.pro
├── include/<project>/    코어의 공개 헤더
├── src/                  구현. 전부 ici 검증 대상이다
│   ├── main.cpp          CLI 드라이버
│   └── gui/              Qt6 셸. 라이브러리 + 실행 파일로 나뉜다
└── tests/                각각 자체 실행 파일이 되는 테스트
```

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
cpp_pkg_config = ["Qt6Widgets"]   # lint 가 Qt 헤더를 찾을 수 있게
```

`cpp_external_build_dirs` 는 더 이상 쓰지 않는다. 루트에 빌드 디스크립터가 없는 프로젝트
(g++ 경로)에서는 여전히 유효하다.

### 알려진 갭: Qt 셸에 단위 테스트가 없다

GUI 가 커버리지에 잡히기 시작하면서 드러난 사실이다.

| 파일 | 상태 |
|---|---|
| `loglens/src/gui/log_model.cpp` | `QAbstractItemModelTester` 로 검증 |
| `diskmap/src/gui/treemap_widget.cpp` | `QSignalSpy` 로 검증 |
| `loglens/src/gui/main_window.cpp` | **미검증** (128 statements) |
| `loglens/src/gui/timeline_widget.cpp` | **미검증** (37 statements) |
| `diskmap/src/gui/main_window.cpp` | **미검증** |

지금은 CI 의 헤드리스 스모크 실행이 유일한 커버다. `loglens` 의 커버리지 임계값이 80/90 에서
55/80 으로 내려간 것이 이 때문이며, **코드가 나빠져서가 아니라 이전에 보이지 않던 코드가
보이기 시작해서다.** 근거는 `loglens/ici.toml` 에 수치와 함께 적어 두었다. 셸에 테스트가
붙으면 도로 올린다.

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

## 검증

```bash
cd <project>
QT_QPA_PLATFORM=offscreen ../../ici/dist/ici.pyz verify --report
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

# diskmap (qmake)
cd diskmap
mkdir -p build/gui && cd build/gui
qmake6 ../../diskmap.pro && make -j
./src/gui/diskmap-gui [경로]
```

경로를 주면 폴더 선택 대화상자를 건너뛰고 바로 스캔한다. 덕분에
`QT_QPA_PLATFORM=offscreen` 으로 헤드리스 스모크 실행이 가능하고, CI 가 그렇게 쓴다.
