# toy-projects

C++17 / Qt 5.15 및 Qt 6 환경을 대상으로 만드는 실사용 데스크톱 도구 모음.
동시에 [ici](https://github.com/jihoon22-lee/ici) 품질 게이트의 **외부 C++ 검증 대상**으로 쓰인다.

앞으로의 방향과 그 순서를 정한 이유는 [ROADMAP.md](ROADMAP.md) 에, 이 과정에서 발견한
ici 결함은 [ICI-GAPS.md](ICI-GAPS.md) 에 있다.

## 릴리스 버전 규율

이 저장소의 버전은 포트폴리오 진행률이나 검증 인프라의 변경 횟수를 세는 숫자가 아니다.
`loglens`, `diskmap`, `buildscope` 등 각 toy 제품은 서로 독립적으로 버전을 결정한다.

- 포트폴리오 방향, B-stage 진행, ici pin, CI/runner-only 변경은 toy 버전을 자동으로 올리지 않는다.
- `patch`는 이미 공개된 stable 제품의 defect, security, compatibility regression을 고칠 때만 사용한다.
- `minor`는 하나의 응집된 사용자 가치가 실제로 쓸 수 있는 제품 checkpoint가 된 뒤에만 올린다.
  이때 native tests, released-ici verification, PR 및 exact-main CI/Pages, 문서와 limitations,
  재현 가능한 release assets를 모두 확인하고 기록한다.
- candidate, pre-release, unreleased 상태는 stable release로 간주하지 않는다.
- 지금까지 이 규율을 통과한 stable release는 BuildScope `0.5.0` 하나다. 나머지 제품은
  `Unreleased`로 쌓인다. 각 release의 판정 근거와 asset 증거는 [ROADMAP.md](ROADMAP.md)에
  기록한다.

## 프로젝트

| 이름 | 설명 | 빌드 | 버전 |
|---|---|---|---|
| [diskmap](diskmap/) | 디스크 사용량 트리맵 뷰어 | qmake · Qt5/Qt6 GUI | `0.1.0`/`Unreleased` |
| [loglens](loglens/) | 로그 뷰어·분석기와 investigation workbench | CMake · Qt5/Qt6 GUI | `0.1.0`/`Unreleased` |
| [buildscope](buildscope/) | compile database explorer | CMake · Qt5/Qt6 GUI | `0.5.0` stable |
| [envlens](envlens/) | Python 환경 snapshot CLI/library | pure Python 3.10+ | `0.1.0`/`Unreleased` |
| [abilens](abilens/) | Linux ELF/ABI artifact inspector | 손으로 쓴 Make · C++20 | `0.1.0`/`Unreleased` |
| [quality-zoo](quality-zoo/) | ici known-answer expected-finding corpus | stdlib Python 3.10 runner | 제품 release 없음 |

각 제품이 어떤 slice를 어떤 근거로 끝냈는지는 README가 아니라 아래에 있다. README에
증거를 복사하면 반드시 낡기 때문이다.

- [ROADMAP.md](ROADMAP.md) — 진행 순서, 완료 판정과 제품별 slice 증거
- [CHANGELOG.md](CHANGELOG.md) — 제품별 버전과 변경 내역
- [workthrough/](workthrough/) — 개별 작업의 실측 기록
- [ICI-GAPS.md](ICI-GAPS.md) — 이 과정에서 발견한 ici 결함
- [인수인계 문서](docs/superpowers/2026-08-30-handover.md) — 현재 상태와 결정의 이유

제품별 사용법은 각 디렉터리의 README에 있다.
[abilens](abilens/README.md) · [buildscope](buildscope/README.md) ·
[envlens](envlens/README.md) · [loglens](loglens/README.md) ·
[quality-zoo](quality-zoo/README.md)

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

그래서 GUI 프로젝트는 모두 GUI 를 **라이브러리와 실행 파일로 나눈다.** 실행 파일 하나뿐이면
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

2026-08-31 Qt6 기본 환경에서 ici 0.6.0 release asset으로 수행한 T0/D1 기준 snapshot은
다음과 같이 통과했다. 이는 D2 `b7218c6` 이후 local candidate의 full post-refactor
`ici verify` 결과가 아니다.

```bash
for p in loglens diskmap; do
  (cd "$p" && QT_QPA_PLATFORM=offscreen ../../ici/dist/ici.pyz verify \
    --report --html verify_report.html --github-summary)
done
```

결과는 `loglens: Suite PASS, TEM 4.84 (10 passed, 2 skipped)`와 `diskmap: Suite PASS,
TEM 4.85 (10 passed, 2 skipped)`다. 이 historical snapshot과 별개로 D2 candidate의 full
post-refactor local `ici verify`는 위 current-ici evidence로 완료됐다. D2의 toy remote PR/CI,
sticky comment/Pages evidence와 병합된 main full benchmark는 위 원격 증거와 아래 benchmark
기록으로 완료됐다. Qt5 강제 CMake build는 loglens에서
`CMAKE_DISABLE_FIND_PACKAGE_Qt6=ON`으로, diskmap의 Qt5 qmake build/test는
`/usr/bin/qmake`로 각각 별도 실측했다. T0-5에서는 이 선택을 매 PR의 명시적 CI matrix로
강제한다. 현재 로컬에서는 CMake Qt6/Qt5와 qmake6/Qt5 네 조합 모두 native test와
`QT_QPA_PLATFORM=offscreen` 실제 GUI smoke가 통과했다.

### Qt major matrix 계약

[`ci/check_manifest.py`](ci/check_manifest.py)의 `SUPPORTED_QT_MAJORS = (5, 6)`가
`gui.enabled = true`인 각 manifest 항목을 자동으로 두 개의 discovery 항목으로 확장한다.
따라서 현재 `diskmap`, `loglens`, `buildscope`는 다음 여섯 job이 되고, 새 GUI 프로젝트도
manifest에 한 번만 추가하면 같은 양쪽 검증을 받는다.

| 프로젝트 | Qt5 | Qt6 |
|---|---|---|
| `diskmap` | `/usr/bin/qmake` · `make check` · headless smoke | `/usr/bin/qmake6` · `make check` · headless smoke |
| `loglens` | CMake + Qt6 disable guard · CTest · headless smoke | CMake + Qt5 disable guard · CTest · headless smoke |
| `buildscope` | CMake + Qt6 disable guard · CTest · headless smoke | CMake + Qt5 disable guard · CTest · headless smoke |

각 matrix leg는 선택 major의 Core/Gui/Widgets/Concurrent/Test pkg-config 버전을 로그에
출력하고 major prefix를 검증한다. CMake leg는 반대 major의 `CMAKE_DISABLE_FIND_PACKAGE_*`
guard와 configure output, 최종 `ldd`의 `libQt${major}Widgets`를 확인한다. qmake leg는
고정 경로와 `-query QT_VERSION`을 확인한 뒤 같은 binary를 테스트와 smoke에 사용한다.
CMake GUI descriptor는 `-- <project>: using Qt<major> <version>` status line을 출력하는
공통 convention을 따른다.

LogLens의 스트림·bounded storage·filter 계약은 제품 문서인
[loglens/README.md](loglens/README.md)에 있다.

### Qt 셸 테스트 현황

GUI는 헤드리스 QtTest와 실제 fixture 파일을 사용해 상태 전이를 검증한다. `loglens`는
Qt5/Qt6 CMake/CTest에서 같은 14개 CTest target을 실행했고, `diskmap`의 explorer workbench는
현재 native qmake aggregate check target `17/17`을 Qt5와 Qt6에서 통과했다.
새 `test_storage_cli`와 기존 `test_cleanup`·`test_trash`도 aggregate manifest에 포함되며,
각 focused 실행도 유지한다. 각 qmake test leaf는 core 소스를 다시 컴파일하지 않고 실제
`diskmap_core` static library를 `LIBS`/`PRE_TARGETDEPS`로 링크해 duplicate coverage object를
만들지 않는다. MainWindow는 29개 test slot(QTest 출력 31 PASS), TreemapWidget은 12개,
NodeTableModel은 11개, StorageWorkbench는 3개 test slot(QTest 출력 5 PASS)을 가진다. `buildscope` B2는 Qt5/Qt6 CMake/CTest에서
각각 6/6을 통과했다. T0-5의
CI matrix는 GUI 프로젝트를 Qt5와 Qt6로 각각 빌드·native test·실제 headless smoke까지
실행한다.

| 파일 | 상태 |
|---|---|
| `loglens/src/gui/log_model.cpp` | `QAbstractItemModelTester` 로 검증 |
| `loglens/src/gui/log_load_worker.cpp` | `test_log_load_worker` — 전용 QThread, 512-record ACK backpressure, stale job/sequence, Tail N line number, invalid source와 rotation |
| `diskmap/src/gui/treemap_widget.cpp` | `QSignalSpy` 로 검증 — treemap activation/hover/uncertainty (12 tests) |
| `loglens/src/gui/main_window.cpp` | `test_main_window` — Tail N/From start, open/growth/truncation, retryable missing/reappear, follow stop/resume, search/filter during load, stale sequence와 status |
| `loglens/src/gui/timeline_widget.cpp` | `test_main_window` — empty/populated paint branch |
| `diskmap/src/gui/main_window.cpp` | `test_main_window` — scan, filters, metric/largest-files, navigation, rescan/selection/generation/freeze (29 test slots; QTest 31 PASS) |
| `diskmap/src/gui/node_table_model.cpp` | `test_node_table_model` — shared document, keyed rows, sorting, filters, knownness and largest-files (11 tests) |
| `diskmap/src/gui/main_window_storage.cpp` | `test_storage_workbench` — snapshot round-trip/read-only state, conservative compare, duplicate evidence staging (3 test slots; QTest 5 PASS) |
| `diskmap/src/storage_cli.cpp` | `test_storage_cli` — text and versioned JSON snapshot-diff/duplicate report rendering |
| `buildscope/src/core/compilation_model.cpp` | `test_compilation_model` — normalized grouping, roles, v1 projection, status aggregation, entry view, JSON argv rendering |
| `buildscope/src/gui/main_window.cpp` | `test_main_window` — v2 tree/detail population, raw-vs-JSON command view, status/source/define filters, malformed-input location |

`loglens` bounded foundation의 ici 0.6.0 실측은 line 93.9% / function 96.9% /
branch 83.1% / TEM 4.85이다.
`loglens/ici.toml`은 이 실측 아래에 slack을 둔 branch 75.0 / function 92.0을 게이트로
사용한다. QPainter/Qt 내부 예외 경로와 C++ type-check 미지원은 남은 명시적 한계다.

L1 Slice 2의 GUI 회귀 테스트는 `QTemporaryDir`로 실제 파일을 만들고
`QMetaObject::invokeMethod(..., Qt::DirectConnection)`으로 poll 경계를 결정적으로 구동한다.
`test_main_window`는 retryable missing 상태에서 기존 행을 보존하는지, 동일 경로의 replacement를
새 generation으로 재개하는지, 사용자의 Follow 중지/재개와 fatal unsupported source를 각각
확인한다. 현재 L2 benchmark의 native CMake/CTest는 Qt 6과 Qt 5 각각 12/12 PASS이고,
Qt 6 strict `-Wall -Wextra -Wpedantic -Werror` benchmark build도 통과했다. ici 0.6.0 deep
no-cache는 Suite PASS, 12/12 tests, TEM 4.83, line/function/branch
93.6%/96.6%/81.8%, maximum complexity 15, duplication 1.71%, sanitizer PASS였으며 HTML은
433,351 bytes·external refs 0개였다. background/Tail N의 원격 검증은 PR #25에서
완료됐고, L2 benchmark도 PR #26의 squash merge, green PR gate, sticky comment, Pages와
main Qt5/Qt6 full sweep 검증을 완료했다.

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
matrix와 Qt GUI build/smoke matrix는 모두 같은 manifest 출력에서 생성되며, GUI 항목은
discovery 단계에서 자동으로 Qt5·Qt6 두 major로 확장된다. 따라서 새 프로젝트를 추가할 때
한쪽 matrix만 수정하는 실수를 허용하지 않는다. GUI가 없는 순수 CLI 프로젝트는
`gui.enabled = false`를 명시해야 하며, 그 경우에도 ici verify에는 포함된다.

PR에서는 변경된 경로를 기준으로 영향받은 프로젝트와 Quality Zoo scenario만 선택한다.
프로젝트 공통 계약(`ci/`, workflow, runner/test, manifest)이 바뀌면 전체 프로젝트와 전체
stable scenario로 확장하고, `main` push·수동 실행은 전체 범위를 사용한다. 선택되지 않은
프로젝트는 성공으로 위장하지 않고 별도 skipped ledger에 남는다. 문서 전용 PR처럼 선택
대상이 없는 경우에도 scope-only sticky comment 하나를 남겨 오래된 report link를 재사용하지
않는다. 이 경로 선택은 실행 시간을 줄이면서도 공통 계약 변경의 누락을 fail closed로
방지하기 위한 CI 동작이며, 제품 버전이나 release cadence를 변경하지 않는다.

DiskMap qmake test discovery에도 같은 누락 방지 계약을 적용한다. `ci/test_check_manifest.py`는
`diskmap/tests/`에서 찾은 모든 `test_*.pro`와 `diskmap/tests/tests.pro`의 등록 집합을 비교한다.
따라서 새 Qt test leaf를 추가하면 aggregate manifest에도 반드시 등록해야 하며, cleanup/Trash와
`test_storage_cli`처럼 GUI가 아닌 focused test도 Qt5/Qt6 aggregate에서 빠지지 않는다.

discovery contract 자체는 의존성 없는 Python unit suite로 manifest expansion, 공개 HTML의
exact-title/Zero-CDN 규칙, PR/main publisher event split과 exact-SHA/digest/path/byte 검증 불변식을
검사한다. `gui-build`를 비롯해 PR 소스를 체크아웃하는 품질 job은 repository-level
`contents: read`만 상속한다. write 권한이 필요한 `report-pr`는 PR 소스를 실행 validator로
사용하지 않고, PR base SHA에서 `ci/check_published_html.py`만 `persist-credentials: false`로
sparse-checkout한 뒤 체크섬을 검증한 ici release asset과 verify artifact를 처리한다. 별도 `publish-main`은 모든
품질 job이 성공한 push에서만 exact `main` 소스를 체크아웃한다.

PR의 `report-pr`는 verify·GUI matrix가 성공 또는 실패로 끝난 뒤 항상 평가된다. 성공한
실행에서는 checksum을 확인한 ici `v0.10.2` release asset으로 모든 report artifact를
`gh-pages`에 순차 게시하고, repository 단위 concurrency로 Pages 쓰기 경합을 막는다. 이어서
실제 sticky 댓글을 API로 읽어 manifest 프로젝트 수만큼의 HTML 링크가 정확히 있는지 확인하고,
각 링크가 Pages에서 `text/html` 응답을 반환할 때까지 기다린다.

`Merge Gate`는 branch protection에서 required check로 설정해야 한다. 이 stable check가
manifest discovery, 모든 ici verify·GUI matrix leg, benchmark smoke, BuildScope deep Qt5/Qt6,
Python quality, release contract와 event별 게시 경계를 함께 요구한다. PR에서는 sticky 댓글과
`<project>/pr/<number>/` Pages가 필수이고 `publish-main`은 건너뛴다. `main` push에서는 반대로
PR publisher를 건너뛰고, exact current SHA와 public ici checksum을 다시 확인한 뒤
`<project>/main/`에 명시적 label로 게시해야 한다. 공개 응답은 local artifact와 byte-identical,
exact-title, Zero-CDN HTML이어야 `Merge Gate`가 성공한다.

## 검증

```bash
cd <project>
QT_QPA_PLATFORM=offscreen ../../ici/dist/ici.pyz verify --report
```

`loglens`의 셸 상태 테스트만 빠르게 돌리려면 CTest를 쓴다. 테스트는 프로젝트 루트에서
실행되며, 실제 follow timer는 `QTRY_COMPARE`로 기다리고 truncation/read error는 Qt
meta-object로 기존 `pollSource` slot을 동기 호출해 구동한다.
전용 loader 테스트는 `QSignalSpy`/`QTRY`와 queued 또는 blocking meta-object 경계를 사용해
고정 sleep 없이 batch ACK, stale job, Tail N line number와 Follow/cancel 계약을 검증한다.

```bash
# manifest/discovery
python3 -m unittest \
  ci/test_check_manifest.py \
  ci/test_check_published_html.py \
  ci/test_ci_workflow_contract.py \
  -v

# loglens — Qt 6 and Qt 5 CMake legs
cd loglens
cmake -S . -B build/gui -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=ON
cmake --build build/gui --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir build/gui --output-on-failure
cmake -S . -B build/gui-qt5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
cmake --build build/gui-qt5 --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir build/gui-qt5 --output-on-failure

# diskmap — explicit qmake6 and Qt 5 legs
cd ../diskmap
mkdir -p build/gui-qt6 && cd build/gui-qt6
/usr/bin/qmake6 ../../diskmap.pro && make -j"$(nproc)"
QT_QPA_PLATFORM=offscreen make check
mkdir -p ../gui-qt5 && cd ../gui-qt5
/usr/bin/qmake ../../diskmap.pro && make -j"$(nproc)"
QT_QPA_PLATFORM=offscreen make check
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
cmake -S . -B build/gui -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=ON
cmake --build build/gui --parallel
./build/gui/src/gui/loglens-gui [경로]

# Qt5를 명시적으로 검증할 때
cmake -S . -B build/qt5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
cmake --build build/qt5 --parallel

# diskmap (qmake)
cd diskmap
mkdir -p build/gui && cd build/gui
/usr/bin/qmake6 -query QT_VERSION
/usr/bin/qmake6 ../../diskmap.pro && make -j
./src/gui/diskmap-gui [경로]

# diskmap (qmake, Qt 5.15)
mkdir -p ../gui-qt5 && cd ../gui-qt5
/usr/bin/qmake -query QT_VERSION
/usr/bin/qmake ../../diskmap.pro && make -j
./src/gui/diskmap-gui [경로]
```

경로를 주면 폴더 선택 대화상자를 건너뛰고 바로 스캔한다. 덕분에
`QT_QPA_PLATFORM=offscreen` 으로 헤드리스 스모크 실행이 가능하고, CI 가 그렇게 쓴다.

## CLI root scan

CLI는 디렉터리뿐 아니라 regular file도 직접 받을 수 있다. 파일 root는 JSON에서
`"is_dir":false`인 한 노드로 출력되며, logical size는 실행 시 파일 metadata에서 읽는다.
root symlink는 항상 target을 확인하고, descendant symlink follow 여부는 scanner 옵션의
의미를 따른다. 실제 파일 root smoke는 다음처럼 실행한다.

```bash
cd diskmap
./build/gui/src/diskmap path/to/main.cpp --json
```

파일 크기는 source에 따라 달라지므로 예시 출력의 `size` 숫자를 고정된 fixture로 취급하지
않는다. 깨진 root symlink는 JSON 출력과 함께 stderr 및 `ScanResult.errors`에 오류를 남긴다.

### DiskMap snapshots and duplicate evidence

DiskMap can persist a bounded `diskmap.snapshot/v1` inventory and compare it with a later live
scan. Snapshot writes use a same-directory temporary file and atomic installation; loading a
snapshot is explicitly read-only. The GUI exposes Save/Load/Compare snapshot buttons and a
conservative change table. It also exposes duplicate evidence (partial/full hashes, identity and
hard-link facts, and candidate confidence). A certain reclaimable copy can be staged into the
existing cleanup dry run, but nothing is moved until the normal confirmation, revalidation, and
recoverable Trash workflow completes.

```bash
cd diskmap
./build/gui/src/diskmap path/to/tree --save-snapshot before.json
./build/gui/src/diskmap path/to/tree --compare-snapshot before.json --json
./build/gui/src/diskmap path/to/tree --duplicates --json
./build/gui/src/diskmap --load-snapshot before.json --json
./build/gui/src/diskmap --load-snapshot before.json --duplicates
```

`--load-snapshot` does not require a scan path. Its JSON output is the versioned snapshot document;
`--compare-snapshot` and `--duplicates` emit separate versioned report schemas. Incomplete scans,
stale identities, changing files, symlink targets, and hard-link aliases remain visible as
uncertain or non-reclaimable evidence rather than becoming deletion instructions.
Added/Removed certainty additionally requires complete structural evidence and a known size metric on
the source entry plus complete structural evidence on the opposite snapshot.
Cleanup and Trash paths also reject relative `CleanupTarget.path` values before opening a parent
directory or mutating an entry; a value such as `relative-source` returns `InvalidRequest` instead of
being accepted as an authorized mutation target. Final execution revalidates identity, type,
size/allocation, and known hard-link evidence from the reviewed scan.

On Linux, snapshot installation takes a nonblocking advisory `flock` on the anchored destination
parent directory, and Trash move/restore takes one on the anchored Trash root. This serializes only
cooperating DiskMap mutating operations for the same destination or Trash root; a contending
operation fails promptly instead of waiting or interleaving. Snapshot reads, analysis, and
capability probes are not described as globally locked. Advisory locking does not control a
same-UID non-cooperating or malicious process that ignores the lock and directly changes
user-owned paths. No-follow descriptors, identity revalidation, and rollback reduce ordinary path
races and fail closed, but do not claim to provide that stronger isolation boundary.

### DiskMap generated-source benchmark (opt-in/nightly)

`diskmap/benchmarks/run_benchmark.py`는 실제 파일을 만들지 않고 deterministic fake source에서
full scan과 cooperative cancellation을 각각 실행한다. 아래는 Qt6 local 재현 명령이다. Qt5는
두 qmake 호출의 `/usr/bin/qmake6`를 `/usr/bin/qmake`로 바꾸고 별도 `benchmark_root`를 사용한다.

```bash
cd diskmap
repo_root="$(pwd)"
benchmark_root="$(mktemp -d /tmp/diskmap-benchmark.XXXXXX)"
artifact_dir="$benchmark_root/artifact"
mkdir -p "$benchmark_root/src" "$benchmark_root/benchmarks" "$artifact_dir"
(
  cd "$benchmark_root/src"
  /usr/bin/qmake6 "$repo_root/src/src.pro"
  make -j"$(nproc)"
)
(
  cd "$benchmark_root/benchmarks"
  /usr/bin/qmake6 "$repo_root/benchmarks/scan_benchmark.pro"
  make -j"$(nproc)"
)
python3.10 benchmarks/run_benchmark.py \
  --binary "$benchmark_root/benchmarks/diskmap-scan-benchmark" \
  --entries 1000000 --cancel-after 10000 --timeout-seconds 60 \
  --output-dir "$artifact_dir"
sha256sum "$artifact_dir/summary.json"
```

runner의 기본 budget은 full throughput `≥ 100000 entries/s`, full peak RSS `≤ 1536 MiB`,
full elapsed `≤ 30000 ms`, cancellation elapsed `≤ 2000 ms`이며 각 process에는 60초 hard
timeout을 둔다. PR smoke처럼 correctness만 확인할 때는 `--skip-budgets`를 명시한다.

2026-08-31에 기록한 local summary는 다음과 같다.

| 시나리오 | 입력/생성 | elapsed | throughput | peak RSS | correctness |
|---|---:|---:|---:|---:|:---:|
| full | 1,000,000 / 1,000,000 | 4820.934 ms | 207428.692 entries/s | 1063.496 MiB | PASS |
| cancellation | 1,000,000 / 10,000 | 2.676 ms | 3737316.017 entries/s | 15.414 MiB | PASS |

full sample은 `nodes_retained=1,000,001`을 확인했고, summary JSON의 SHA-256은
`743d5c5409101cfd9ef889da2da421e94cc205f585770ab19bb611472926246d`다. 이 수치는 단일
local candidate artifact의 evidence이며, 실행마다 scheduler와 host RSS에 따라 시간·메모리는
달라질 수 있다.

PR #28 병합 후 head `ec075e5`에서 수행한 [main full benchmark run `33369288586`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33369288586)은
Qt5/Qt6 full, combine, verdict job을 모두 성공시켰다. 설정은 1,000,000 entries,
`cancel-after 10000`, process timeout 60초였고, 두 Qt 결과 모두 correctness `true`, failures
0, budgets enforced `PASS`였다.

| Qt | full elapsed | full throughput | full peak RSS | cancellation elapsed |
|---|---:|---:|---:|---:|
| 5 | 2131.069 ms | 469248.020 entries/s | 1064.094 MiB | 1.479 ms |
| 6 | 3120.463 ms | 320465.315 entries/s | 1064.137 MiB | 1.580 ms |

combined `summary.json`은 3567 bytes이며 SHA-256은
`26391797763aed17fedb04e2a4aeb5cf8238ec4d5b5d040d473d32a513369251`이다.

PR 경로의 `.github/workflows/ci.yml` `diskmap-benchmark-smoke`는 Qt5/Qt6 matrix에서
10,000 entries와 1,000-entry cancellation, 30초 timeout, `--skip-budgets`로 harness
correctness만 확인하고 `Merge Gate` required check에 포함한다. 전체 1,000,000-entry 측정은
`.github/workflows/diskmap-benchmark.yml`의 `workflow_dispatch`와 주간(일요일 03:37 UTC)
schedule에서만 Qt5/Qt6 matrix로 실행한다. 이 workflow는 기본값 1,000,000/10,000/60초를
runner에 전달하고 Qt별 summary를 aggregate/verdict job에서 합치며, artifact에는 JSON·MD·TXT
report만 남기고 generated input과 process log는 올리지 않는다. 별도 workflow threshold를
추가하지 않고 runner 기본 budget을 단일 정책으로 사용한다.
