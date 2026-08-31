# toy-projects

C++17 / Qt 5.15 및 Qt 6 환경을 대상으로 만드는 실사용 데스크톱 도구 모음.
동시에 [ici](https://github.com/jihoon22-lee/ici) 품질 게이트의 **외부 C++ 검증 대상**으로 쓰인다.

앞으로의 방향과 그 순서를 정한 이유는 [ROADMAP.md](ROADMAP.md) 에, 이 과정에서 발견한
ici 결함은 [ICI-GAPS.md](ICI-GAPS.md) 에 있다.

## 프로젝트

| 이름 | 설명 | 상태 |
|---|---|---|
| [diskmap](diskmap/) | 디스크 사용량 트리맵 뷰어 | Qt5/Qt6 GUI · identity-safe scan · 9 tests |
| [loglens](loglens/) | 로그 뷰어 / 분석기 | Qt5/Qt6 GUI · bounded 라이브 팔로우 · 10 tests |

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

### diskmap D1 identity-safe scan — Slice 2

diskmap은 파일·디렉터리·심볼릭 링크와 링크 대상, 하드 링크를 서로 혼동하지 않도록
물리적 identity와 메타데이터를 보존한다. 사용자는 이후 탐색·정리 기능에서 “링크 자체”와
“대상 파일”을 구분하고, 권한 오류로 불완전한 디렉터리를 확정된 용량처럼 보지 않게 된다.

개발자 관점에서는 `RealFsSource`가 POSIX `lstat`/`stat` 결과와 `*_known` 플래그를
`DirEntry`에 담고, scanner가 `std::filesystem::path`와 함께 `FsNode`로 전달한다.
`follow_symlinks=true`인 디렉터리는 물리적 `FileIdentity` 방문 집합으로 cycle을 차단하며,
이미 방문한 target은 노드로 남기되 다시 확장하지 않는다. target identity를 확인할 수 없는
followed directory는 안전하게 `complete=false`와 오류를 남긴다. listing/iterator 오류도 같은
방식으로 보존하고, stat의 `metadata.logical_size`와 자식 합계인 `FsNode::size`는 별도 값이다.

명시적으로 선택한 root가 regular file이면 디렉터리로 열려고 실패하지 않고, complete한 한
노드 scan으로 반환한다. 이 노드는 logical/allocated/reclaimable facts와 `files_scanned=1`을
그대로 가진다. 선택한 root가 symlink이면 `follow_symlinks`와 무관하게 target을 dereference
하며, target이 file이면 leaf로 남긴다. 이 옵션은 root 아래 descendants에만 적용된다. 깨진
root symlink target은 root를 incomplete로 남기고 오류를 `ScanResult.errors`에도 포함한다.

`FsNode::size`는 directory entry마다 logical bytes를 세고, `allocated_size`는 유효한 물리
identity별로 한 번만 합산한다. `reclaimable_size`는 해당 subtree가 known hard-link reference를
모두 소유할 때만 known으로 계산하며, symlink target alias는 소유 reference로 세지 않는다.
불완전한 subtree·unknown allocation/link-count는 조용히 0으로 바꾸지 않고 aggregate의
`*_known=false`로 전파한다. 유한한 `max_depth`로 잘린 directory도
`complete=false`, `scan depth limit reached`로 남기므로 allocated/reclaimable total을
확정값처럼 보이지 않게 한다. logical aggregate는 별도 known bit가 없으므로
`uint64_t` overflow에서 최댓값으로 포화(saturate)한다. 따라서 cleanup 기능은 확정된 값과
추정할 수 없는 값을 구분할 수 있다.

D1 Slice 2의 검증은 fixed-identity fake source와 실제 POSIX temporary filesystem을 함께
사용한다. 경로 component의 공백, cycle/back-edge, hard-link 중복·reclaimability, symlink
alias, identity 없는 followed directory, allocation/link-count unknown 및 aggregate overflow를
검증하는 9개 qmake test binary가 등록되어 있다. qmake의 static consumer에
`PRE_TARGETDEPS`를 연결해 테스트가 최신 archive를 다시 링크하도록 했고, stale `.gcda`/`.gcno`
혼입으로 coverage가 낮게 보이는 재현도 제거했다.

최종 D1 Slice 2는 Qt5·Qt6 qmake `make check` 9/9와 8초 headless smoke를 통과했고,
ici 0.6.0 qmake-clean 검증은 Suite PASS, TEM 4.90, line/function/branch
96.6%/98.0%/85.0%, complexity 14, duplication 2.0, sanitizer clean이었다. PR
[#23](https://github.com/jihoon22-lee/toy-projects/pull/23)의 workflow
[`33338809225`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33338809225)는 모든
checks를 통과했고 merge commit `039052f9f30e355e12f3c812065657e3be4576f2`로 병합됐다.
[sticky comment](https://github.com/jihoon22-lee/toy-projects/pull/23#issuecomment-5471613383)와
Pages HTML도 HTTP 200·`text/html`·외부 참조 0개로 확인했다.

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
`CMAKE_DISABLE_FIND_PACKAGE_Qt6=ON`으로, diskmap의 Qt5 qmake build/test는
`/usr/bin/qmake`로 각각 별도 실측했다. T0-5에서는 이 선택을 매 PR의 명시적 CI matrix로
강제한다. 현재 로컬에서는 CMake Qt6/Qt5와 qmake6/Qt5 네 조합 모두 native test와
`QT_QPA_PLATFORM=offscreen` 실제 GUI smoke가 통과했다.

### Qt major matrix 계약

[`ci/check_manifest.py`](ci/check_manifest.py)의 `SUPPORTED_QT_MAJORS = (5, 6)`가
`gui.enabled = true`인 각 manifest 항목을 자동으로 두 개의 discovery 항목으로 확장한다.
따라서 현재 `diskmap`과 `loglens`는 다음 네 job이 되고, 새 GUI 프로젝트도 manifest에
한 번만 추가하면 같은 양쪽 검증을 받는다.

| 프로젝트 | Qt5 | Qt6 |
|---|---|---|
| `diskmap` | `/usr/bin/qmake` · `make check` · headless smoke | `/usr/bin/qmake6` · `make check` · headless smoke |
| `loglens` | CMake + Qt6 disable guard · CTest · headless smoke | CMake + Qt5 disable guard · CTest · headless smoke |

각 matrix leg는 선택 major의 Core/Gui/Widgets/Concurrent/Test pkg-config 버전을 로그에
출력하고 major prefix를 검증한다. CMake leg는 반대 major의 `CMAKE_DISABLE_FIND_PACKAGE_*`
guard와 configure output, 최종 `ldd`의 `libQt${major}Widgets`를 확인한다. qmake leg는
고정 경로와 `-query QT_VERSION`을 확인한 뒤 같은 binary를 테스트와 smoke에 사용한다.
CMake GUI descriptor는 `-- <project>: using Qt<major> <version>` status line을 출력하는
공통 convention을 따른다.

### loglens 스트림 계약

`loglens`의 CLI와 GUI는 같은 `RecordAssembler`를 사용한다. `FileTailer::pollChunk`가
poll에서 읽은 raw bytes와 source generation을 전달하고, assembler가 다음 상태를 한 곳에서
보존한다.

POSIX에서 `FileTailer`는 경로의 timestamp가 아니라 열린 파일 handle의 device/inode를
비교한다. 따라서 같은 경로가 더 작거나, 같은 크기이거나, 더 큰 파일로 원자적 rename
교체되어도 `Replaced`로 감지하며, 같은 inode에 쓰는 in-place 축소는 별도의 `Truncated`로
구분한다. source 계층은 missing, permission denied, open/stat/read failure와 unsupported
file type을 typed error로 보존한다.

GUI는 follow 중 발생한 source 오류를 retryable/fatal로 나눈다. missing, permission, open,
stat, read 계열의 retryable 오류에서는 마지막으로 정상적으로 읽은 행을 유지하고 follow
checkbox와 poll timer를 켠 채 `Follow waiting (attempt N)` 상태를 표시한다. 경로에 새 파일이
다시 나타나 새 identity가 확인되면 이전 generation의 assembler와 모델을 비우고 새 파일의
첫 행부터 표시한다. 사용자가 대기 중 Follow를 끄면 retry timer도 멈추며, 다시 켜면 같은
경로를 명시적으로 재개한다. 디렉터리 같은 fatal unsupported file type은 follow를 중지하지만
마지막으로 읽은 화면은 유지하여 사용자가 원인을 확인하거나 다른 파일을 선택할 수 있게
한다. 최초 open 자체가 실패한 경우에는 기존 source를 비우고 Follow를 끄는 기존 계약을
유지한다.

- newline을 기준으로 한 physical line 번호
- 다음 poll에서 이어 붙일 partial bytes
- stack-trace continuation을 확장할 pending record
- truncation/restart 뒤 초기화되는 generation
- 선택된 format과 byte-preserving encoding/error policy

새 record는 `Append`, 기존 record의 continuation은 `Extend` delta로 구분된다. 따라서 GUI는
이미 표시한 행을 갱신하고 CLI는 같은 결과 벡터를 갱신한다. newline 없는 마지막 조각은
follow 모드에서는 보류하며, one-shot CLI의 명시적 EOF `flush()`에서만 record가 된다.

### loglens bounded storage

GUI와 CLI는 기본 32,768개(최대 1,000,000개)의 같은 bounded record store를 사용한다.
GUI status는 visible/retained/seen/dropped, oldest-newest physical line과 capacity를 표시하고,
CLI는 `--capacity N`으로 보존량을 정하며 일반 출력과 `--stats` 모두 같은 요약을 출력한다.
오래된 record가 제거돼도 assembler의 absolute ID는 바뀌지 않아 continuation update가 다른
행에 적용되지 않는다.

source read는 한 poll당 기본 1 MiB(최대 16 MiB)로 제한된다. GUI는 초기 backlog를 이벤트
루프에 나눠 처리하고, one-shot CLI는 최초 file-size snapshot까지만 읽는다. newline 없는
거대한 line이나 continuation은 기본 64 KiB(최대 1 MiB)에서 잘리며, UI/CLI에 정확한
`omitted_bytes`가 표시된다. 이 slice는 이벤트 루프를 독점하는 전체 파일 read를 없앤 기반
단계다. Tail N/index 선택, worker-thread parsing과 1 GiB·100만 record benchmark는 아직
완료되지 않았으며 마스터 계획의 다음 L2 작업이다.

PR [#24](https://github.com/jihoon22-lee/toy-projects/pull/24)의 구현 head `fa4fd1a`는
workflow [`33348597272`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33348597272)에서
공개 ici 검증, 두 프로젝트 Qt5·Qt6 GUI, report publish와 Merge Gate를 모두 통과했다.
[sticky comment](https://github.com/jihoon22-lee/toy-projects/pull/24#issuecomment-5472700934)에
두 PASS 결과와 HTML 링크가 게시됐고, 두 Pages 문서는 HTTP 200·`text/html`·외부 참조 0개로
직접 확인했다.

### Qt 셸 테스트 현황

GUI는 헤드리스 QtTest와 실제 fixture 파일을 사용해 상태 전이를 검증한다. `loglens`는
Qt5/Qt6 CMake/CTest에서 같은 테스트를 실행했고, `diskmap`의 내비게이션 셸도 T0-4에서
완료했다. T0-5의 CI matrix는 이 두 프로젝트를 Qt5와 Qt6로 각각 빌드·native test·실제
headless smoke까지 실행한다.

| 파일 | 상태 |
|---|---|
| `loglens/src/gui/log_model.cpp` | `QAbstractItemModelTester` 로 검증 |
| `diskmap/src/gui/treemap_widget.cpp` | `QSignalSpy` 로 검증 |
| `loglens/src/gui/main_window.cpp` | `test_main_window` — open/growth/truncation, retryable missing/reappear, follow stop/resume, fatal source와 timer/status |
| `loglens/src/gui/timeline_widget.cpp` | `test_main_window` — empty/populated paint branch |
| `diskmap/src/gui/main_window.cpp` | `test_main_window` — scan, breadcrumb, descend/up, leaf no-op |

`loglens` bounded foundation의 ici 0.6.0 실측은 line 93.9% / function 96.9% /
branch 83.1% / TEM 4.85이다.
`loglens/ici.toml`은 이 실측 아래에 slack을 둔 branch 75.0 / function 92.0을 게이트로
사용한다. QPainter/Qt 내부 예외 경로와 C++ type-check 미지원은 남은 명시적 한계다.

L1 Slice 2의 GUI 회귀 테스트는 `QTemporaryDir`로 실제 파일을 만들고
`QMetaObject::invokeMethod(..., Qt::DirectConnection)`으로 poll 경계를 결정적으로 구동한다.
`test_main_window`는 retryable missing 상태에서 기존 행을 보존하는지, 동일 경로의 replacement를
새 generation으로 재개하는지, 사용자의 Follow 중지/재개와 fatal unsupported source를 각각
확인한다. 이 브랜치에서 기록된 native CMake/CTest 결과는 Qt 6과 Qt 5 각각 10/10 PASS이며,
ici verify, CI report-pr/sticky HTML, headless GUI smoke 결과는 PR 검증에서 별도로 수집한다.

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

discovery contract 자체는 의존성 없는 `python3 -m unittest ci/test_check_manifest.py`로
현재 프로젝트와 새 GUI 프로젝트 fixture의 양 major 확장을 검증한다. `gui-build`는
repository-level `contents: read` 권한만 상속하며, PR 소스를 체크아웃하는 빌드 job에는
write token이 없다. write 권한이 필요한 `report-pr`는 소스를 체크아웃하지 않고 체크섬을
검증한 ici release asset과 verify artifact만 처리한다.

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
# manifest/discovery
python3 -m unittest ci/test_check_manifest.py -v

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
