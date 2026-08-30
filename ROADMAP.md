# ROADMAP

이 저장소의 프로젝트들을 **어느 방향으로, 왜 그 순서로** 키울지 적는다.
발견한 ici 결함은 [ICI-GAPS.md](ICI-GAPS.md) 에, 프로젝트 소개와 구조 규칙은
[README.md](README.md) 에 있다.

> **확장된 실행 계획 (2026-08-31 기준):** 기존 두 앱의 제품 완성, 신규 `buildscope`·`envlens`·
> `abilens`, `quality-zoo`와 ici 교차 개발 순서는
> [제품 포트폴리오 마스터 계획](docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md)이
> 기준이다. 이 계획은 [PR #12](https://github.com/jihoon22-lee/toy-projects/pull/12)로
> `main`에 병합됐다. 아래의 배경과 문제 발견 이력은 마스터 계획을 보충하는 역사적 기록으로
> 유지한다.

## 마스터 계획 stream 연결

세부 기능의 우선순위와 완료 조건은 아래 링크의 마스터 계획을 기준으로 한다. 이 문서의
과거 단계 설명과 마스터 계획의 현재 stream이 충돌하면 마스터 계획이 우선한다.

| Stream | 역할 | 마스터 계획 |
|---|---|---|
| T0 | 현재 Qt 셸 보정, parser state와 Qt5/Qt6 안전망 | [T0](docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md) |
| L | loglens incident explorer | [L stream](docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md) |
| D | diskmap storage workbench | [D stream](docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md) |
| B | buildscope hybrid build explorer | [B stream](docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md) |
| E | envlens Python environment explorer | [E stream](docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md) |
| A | abilens Make/ELF/ABI explorer | [A stream](docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md) |
| Q | quality-zoo expected-finding corpus | [Q stream](docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md) |

## 배경

이 프로젝트들은 재미와 검증을 동시에 노리고 시작했다. 실제로 쓸 C++/Qt 앱을 만들고,
그걸 [ici](https://github.com/jihoon22-lee/ici) 로 검증한다. ici 는 그 전까지 **자기 자신
(순수 Python 프로젝트) 하나만** 도그푸딩하고 있어서 C++ 경로에 E2E 회귀 커버리지가 없었다.

결과는 기록이 말한다. 초기 실물 개발에서 **ici 결함 17건을 찾았고 그중 13건이 수정됐다.**
어댑터 작업에서 9건이 더 발견돼 현재 추적 항목은 26건이며, 그중 22건이 수정됐다. 전부
만들면서 나왔고, 코드를 읽어서 찾은 것은 하나도 없다. 남은 항목은
[ICI-GAPS.md](ICI-GAPS.md)의 A-2, B-2, B-3, C-7이다.

## 완료: 어댑터와 loglens stream state (2026-08-31)

1단계와 그에 딸린 전환이 끝났다. **ici 0.6.0 이 CMake/CTest 와 qmake/Make 어댑터를 갖췄고**,
A-3 은 닫혔다. `Q_OBJECT` 클래스의 단위 테스트가 `tests/` 안에서 통과한다. 이어서
loglens의 poll 경계 parser state와 CLI/GUI 공통 delta 계약도
[#14](https://github.com/jihoon22-lee/toy-projects/pull/14)로 병합됐다. 이것은 T0-2 완료이며,
Qt 셸 테스트와 양 Qt major matrix까지 끝나야 T0 전체가 완료된다. T0-5 구현의 원격 PR
gate에서는 아래 네 leg가 같은 계약을 다시 실행한다.

| 프로젝트 | 빌드 | 검증 | Qt 테스트 |
|---|---|---|---|
| `loglens` | CMake · Qt5/Qt6 | PASS · TEM 4.84 | `QAbstractItemModelTester` + MainWindow QtTest |
| `diskmap` | qmake · Qt5/Qt6 | D1 Slice 2 local PASS · TEM 4.90 | `QSignalSpy` + 9 native tests + MainWindow QtTest |
| `ici/viewer` | CMake · Qt5/Qt6 | PASS · TEM 4.86 | MainWindow QtTest 4/4 |

T0-5의 `discover`는 GUI 프로젝트 한 항목을 Qt5·Qt6 두 항목으로 확장한다. 따라서 현재
`gui-build`는 `diskmap/Qt5`, `diskmap/Qt6`, `loglens/Qt5`, `loglens/Qt6` 네 leg이며,
job 이름에도 실제 major가 표시된다. Qt5는 `qtbase5-dev`/`qt5-qmake`, Qt6는
`qt6-base-dev`/`qmake6`를 조건부 설치하고, 두 leg 모두 선택된 pkg-config Core/Gui/Widgets/
Concurrent/Test 버전, native tests, 실제 headless GUI smoke를 기록한다. report-pr는 기존처럼
PR 소스를 실행하지 않는 별도 집계 job으로 유지한다.

### 완료 기준이 실제로는 바뀌었다

원래 1단계의 완료 기준은 **"ici 가 어디서 막히는지 `ICI-GAPS.md` 에 기록됐다"** 였다.
실제로는 **"ici 를 고쳐서 통과시켰다"** 가 됐다.

이유는 단순하다. 기록만 남기려면 실패하는 Qt 테스트를 저장소에 커밋해야 하는데, 그러면
게이트가 영구히 빨간불이 되거나 테스트를 ici 가 보지 않는 곳으로 옮겨야 한다. **후자는 우회를
없애려다 새 우회를 만드는 것이다.** 그래서 측정과 구현을 같은 루프에 넣었다 — 실패하는
테스트를 먼저 쓰고, 그것이 ici 구현을 끌고 가게 했다. 요구사항이 실측에서 나온다는 원칙은
그대로 지켜졌고, 빨간 상태만 커밋되지 않았다.

### 픽스처로는 안 나왔을 결함들

어댑터 작업에서 새 결함 9건이 나왔고 **전부 만들다 나왔다.** 그중 여럿은 실물 프로젝트라야
드러났다 — 목록과 근거는 [ICI-GAPS.md](ICI-GAPS.md) 에 있다.

가장 좋은 예가 D-7 이다. qmake 는 Qt 링크 테스트를 `target_wrapper.sh` 로 실행하는데 ici 가
그 줄을 못 읽어 **`diskmap` 의 테스트 6개 중 5개만 세고 있었다.** ici 의 qmake 픽스처는 Qt
테스트 하나뿐이라 다른 경로로 구제됐고, 그래서 픽스처는 통과했다. QtTest 와 수제 `main()`
테스트가 섞인 실물 프로젝트라야 나오는 결함이었다.

**`diskmap` 을 qmake 로 옮긴 것이 이 때문이다.** 어댑터가 둘인데 실물 프로젝트가 하나뿐이면
나머지 하나는 픽스처만으로 설계한 것이 된다.

## 기존 로드맵에서 남은 것 (마스터 계획에 흡수됨)

### 2단계 — diskmap 정리 작업대 (①)

**아직 남아 있다.** 이번 작업은 `diskmap` 을 qmake 로 옮기고 기존 위젯에 시그널 테스트를
붙였을 뿐, 정리 작업대 기능은 만들지 않았다.

디스크 사용량을 보는 이유는 대개 **지우려고** 인데 지금은 보고 끝이다. 다중 선택으로 삭제
후보를 담고, 회수 용량을 보여주고, 실행 전 검토한다. 휴지통으로 옮기므로 되돌릴 수 있다.

- 순수 core: 회수 용량 계산, 중첩 선택 정규화, 안전 규칙(심볼릭 링크·시스템 경로 거부)
- Q_OBJECT: 스테이징 모델 + `QUndoCommand` 스택

**이제 이것을 단위 테스트할 수 있다.** 1단계 전에는 불가능했고, 그게 이 단계를 미룬 이유였다.

### 과거 3단계 — diff/merge 도구: 근거를 다시 판단할 것

이 역사적 단계의 `A` 표기는 현재 마스터 계획의 `abilens` A stream과 다르다.

**원래 목적은 이미 달성됐다.** 이 단계는 A-2·A-3 을 실측하기 위한 것이었는데 둘 다 처리됐다.
남은 미충족 조건은 하나뿐이다 — **`.qrc`/`.ui` 생성 단계(`AUTOUIC`/`AUTORCC`)** 가 어느
프로젝트에도 없어 검증되지 않았다. `AUTOMOC` 만 실측됐다.

그 조건 하나를 위해 새 프로젝트를 만들 가치가 있는지는 다시 판단해야 한다. 더 싼 방법이
있다 — 기존 프로젝트에 아이콘 `.qrc` 하나를 넣으면 같은 것이 실측된다. 도구 자체가 갖고
싶다면 그건 별개의 이유이고, 그렇다면 그 이유로 정당화해야 한다.

### Qt 셸 테스트 현황

`loglens`는 `test_main_window`에서 실제 임시 파일의 open/growth/truncation/read error와
follow checkbox/timer/status 상태를 검증한다. `TimelineWidget`도 빈 상태와 색상 막대
렌더링 분기를 헤드리스 렌더링으로 확인한다. Qt5.15와 Qt6.10에서 동일한 CMake/CTest
10개 테스트가 통과했다.
`diskmap`도 MainWindow 내비게이션을 `test_main_window`로 검증하며 T0-4를 완료했다.

T0-5 로컬 실측은 Qt6 CMake와 Qt5 CMake 각각 `10/10 CTest PASS`, qmake6와 `/usr/bin/qmake`
각각 `make check PASS`, 네 GUI binary 모두 해당 `libQt{5,6}Widgets` 링크와 2초 이상
headless smoke 생존을 확인했다. CMake는 반대 major disable guard와 configure output을,
qmake는 `-query QT_VERSION`을 검증한다. `ci/test_check_manifest.py`의 stdlib 테스트는
새 GUI manifest 항목도 `(5,6)` 두 leg로 확장되는지 확인한다.

ici 0.6.0 실측은 line 93.2% / function 96.9% / branch 81.8% / TEM 4.84이다.
`loglens/ici.toml`은 실측값에 6.8%p/4.9%p slack을 둔 branch 75.0 / function 92.0을
게이트로 사용한다. 남은 것은 QPainter/Qt 내부 예외 경로와 ici의 C++ type-check 미지원이다.
`diskmap/src/gui/main_window.cpp`는 T0-4에서 scan, breadcrumb, descend/up, leaf no-op를
검증했다.

D1 Slice 2에서는 이 셸 검증에 identity-safe scanner 계약을 추가했다. `std::filesystem::path`
경계, followed-directory cycle 방지, hard-link allocated/reclaimable deduplication,
symlink alias 비소유, identityless target의 incomplete 전파와 aggregate overflow를 fake
source 및 실제 POSIX filesystem으로 확인한다. Qt5/qmake와 Qt6/qmake6 모두 fresh full build와
`make check` 9/9, GUI offscreen smoke 8초 생존을 통과했으며 public ici 0.6.0 verify는 TEM
4.90, line/function/branch 96.9%/97.9%/85.2%였다. 원격 PR CI와 report comment/Pages 증거는
해당 Slice 2 PR에서 추가한다.

### 4단계 — 여유가 되면

- loglens 하이라이트 룰 편집기 (④) — 룰이 코드에만 있다. UI 로 정의·저장·드래그 재정렬
- diskmap 스냅샷 비교 (②) — "지난주 대비 뭐가 늘었나". 트리 diff 는 좋은 core 소재

## 원칙

**완료 기준을 "기능이 동작한다" 가 아니라 "요구사항이 실측됐다" 로 둔다.** 기능은 수단이다.
각 단계는 PR 하나 크기로 끊고, 매번 ici 가 어디서 어떻게 깨지는지를 재현 조건과 함께 남긴다.

수정·미수정을 합친 26건의 결함이 전부 그렇게 나왔다 — 만들다 이상해서 파고든 결과다.

여기에 한 줄을 덧붙인다. **픽스처는 실물 프로젝트를 대신하지 못한다.** ici 저장소의 qmake
픽스처는 통과하는데 `diskmap` 은 테스트 하나를 통째로 잃고 있었다(D-7). 픽스처는 만든 사람이
상상한 모양만 담고, 실물은 그렇지 않은 모양을 갖고 있다.
