# toy-projects 제품 포트폴리오와 ici 실물 검증 마스터 계획

**상태:** 승인된 장기 마스터 계획. 2026-08-30 이후 toy-projects의 기능 우선순위, 신규 프로젝트 선정과 완료 조건은 이 문서를 기준으로 판단한다.
**문서 기준일:** 2026-09-03. 이 계획은 toy-projects [PR #12](https://github.com/jihoon22-lee/toy-projects/pull/12)로 `main`에 병합됐다. 현재 완료 상태는 이 문서의 체크리스트와 병합된 PR을 함께 기준으로 삼는다.

**목표:** `loglens`와 `diskmap`을 실제로 쓸 만한 완성도 높은 Qt 제품으로 발전시키고, ici의 Python·C++·Qt 분석 공백을 메우는 `buildscope`, `envlens`, `abilens`와 `quality-zoo`를 단계적으로 구축한다.

**제품 원칙:** 한 프로젝트에 검증 조건을 억지로 몰아넣지 않는다. 각 프로젝트는 독립적인 사용자 문제를 해결해야 하며, 제품과 어울리지 않는 의도된 결함은 `quality-zoo`가 담당한다.

**대응 ici 계획:** [`Python·C++·Qt 품질 분석기 마스터 계획`](https://github.com/jihoon22-lee/ici/blob/main/docs/superpowers/plans/2026-08-30-python-cpp-qt-quality-analyzer-master-plan.md).

---

## 1. 기존 계획과의 관계

- `2026-08-30-qt-shell-tests.md`는 T0의 세부 입력으로 보존하되 그대로 실행하지 않는다.
  - stateless `parseLines(vector<string>)`는 poll마다 line number를 1로 되돌리고 continuation state를 잃으므로 stateful record assembler로 바꾼다.
  - loglens와 diskmap Qt 셸 테스트는 계획대로 추가한다.
  - 현재 환경에는 Qt 5.15와 Qt 6.10이 모두 있으므로 두 버전을 실제 검증한다.
  - Qt 5 강제 CMake 검증은 Qt 6 탐색을 명시적으로 비활성화한다.
- `2026-08-30-loglens-cmake-diskmap-qmake.md`는 완료된 adapter 전환 이력이다. 체크박스가 남았더라도 재실행하지 않는다.
- 기존 `ROADMAP.md`의 diskmap 정리 작업대, loglens highlight editor, snapshot diff는 이 계획의 D·L stream으로 흡수한다.
- ici viewer는 ici 저장소에서 구현한다. 이 문서에는 교차 의존성과 사용자 시나리오만 기록한다.

---

## 2. 현재 포트폴리오와 검증 공백

2026-09-03 기준:

| 프로젝트 | 현재 형태 | 실측 | 제품 상태 |
|---|---|---|---|
| loglens | C++17, Qt, CMake | L2 bounded/background slice · PR #25 CI green · local Qt5/Qt6 12 CTest targets · 1 GiB benchmark PR #26 merged · main Qt5/Qt6 sweep green | Tail N/From start와 worker UX, benchmark/default 8192 완료 |
| diskmap | C++17, Qt, qmake | D1/D2 complete · D3 explorer workbench merged by PR #46 · exact-main evidence green · `0.1.0`/`Unreleased` | treemap/table, filters, uncertainty, accessible navigation, rescan restoration verified; cleanup UX는 D4~D6에서 확장 |
| buildscope | Python + C++17/Qt, CMake | B0~B5 implementation, remote acceptance, exact-main CI, trusted main Pages와 `0.5.0` tag/release/public asset audit complete | B3~B5 첫 usable release boundary published and audited |
| envlens | pure Python 3.10+ package/CLI | deterministic snapshot core · path-aware manifest/CI matrix · PR #50 merged · exact-main green | `0.1.0`/`Unreleased`; E2/E3 and release remain pending |
| quality-zoo | Python 3.10, stdlib runner | scenario contract/local candidate consumer · four C++ sanitizer scenarios locally verified for released/candidate digests · released-artifact PR #49 remote PR CI/sticky/exact-main complete · candidate cross-repo acceptance pending | test asset only; broader Q1~Q5 corpus pending |
| ici/viewer | Qt-free core + Qt6 GUI | 3 tests, TEM 4.94 | core 중심, 셸과 report workflow 부족 |

현재 공백:

| 검증 축 | 실물 프로젝트 | 공백 처리 |
|---|---|---|
| 독립 pure Python | ici 자기 자신뿐 | envlens |
| pure C++/Qt + CMake | loglens | 확장 유지 |
| pure C++/Qt + qmake | diskmap | 확장 유지 |
| Python+C++ hybrid | 없음 | buildscope |
| Qt5/Qt6 | 계획만 있음 | 기존 앱과 buildscope에서 양쪽 검증 |
| AUTOUIC/AUTORCC | 없음 | buildscope |
| compile_commands | 없음 | buildscope |
| C++20/23 | 없음 | buildscope C++20, abilens C++20 이상 |
| 수제 Makefile | 없음 | abilens |
| executable+shared library | 없음 | abilens |
| ELF/ABI/RPATH | 없음 | abilens |
| Python runtime/package matrix | ici self뿐 | envlens |
| hybrid process contract | 없음 | buildscope |
| known-bad detection recall | quality-zoo의 1개 stable Python scenario와 4개 C++ sanitizer scenario | broader C++/Qt/build/binary/hybrid corpus 확장 |

새 프로젝트가 ici 기능보다 먼저 빈 껍데기로 만들어지지 않도록 각 신규 프로젝트는 대응 ici milestone과 함께 시작한다.

---

## 3. 포트폴리오 역할

### 3.1 사용자 제품

- **loglens:** 큰 로그와 라이브 로그를 안전하게 탐색하고 장애 구간을 좁히는 오프라인 incident explorer.
- **diskmap:** 디스크 사용량을 설명하고, 변경을 비교하며, 사용자가 검토한 항목만 안전하게 휴지통으로 옮기는 storage workbench.
- **buildscope:** translation unit별 실제 compile flags, include resolution과 configuration drift를 설명하는 C++/Qt build explorer.
- **envlens:** Python interpreter와 설치 환경의 metadata·호환성 차이를 오프라인에서 비교하는 CLI/library.
- **abilens:** ELF binary의 runtime dependency와 ABI floor 차이를 읽고 비교하는 C++ CLI.
- **ici/viewer:** ici report의 신규·해결 finding과 suppression을 조사하는 품질 workbench. 구현은 ici 계획 I8에서 담당한다.

### 3.2 검증 제품이 아닌 test asset

- **quality-zoo:** 의도된 결함과 예상 ici finding을 가진 격리 scenario 모음이다.
- quality-zoo를 일반 사용자가 설치할 앱처럼 포장하지 않는다.
- production 프로젝트는 green gate를 유지하고 known-bad code를 제품 source에 섞지 않는다.

---

## 4. 공통 제품 완성 불변식

모든 사용자 제품은 다음을 만족해야 “완료”라고 부른다.

- README 첫 화면에 사용자가 해결할 문제와 3분 quick start가 있다.
- core public contract, CLI 또는 GUI의 정상 흐름과 주요 실패 흐름에 테스트가 있다.
- 파일 없음, 권한 오류, malformed input, 취소, stale state를 사용자에게 설명한다.
- 무한히 증가하는 memory/state가 없거나 명시적 configurable bound가 있다.
- long-running 작업은 UI thread를 막지 않고 진행률과 취소 의미론이 있다.
- user data를 변경하는 기능은 preview, 대상 재검증, recoverable operation과 audit 결과가 있다.
- 사용하지 않는 “미래 기능” class를 테스트만 해두고 제품 경로에 연결하지 않은 채 방치하지 않는다.
- ici verify의 모든 WARN/SKIP은 limitation 또는 backlog와 연결된다.
- build/test/package/release 명령과 supported platform을 문서화한다.
- Qt 프로젝트는 Qt 5.15와 현재 Qt 6에서, Python 프로젝트는 Python 3.10 하한과 최신 지원 runtime에서 실측한다.
- 각 milestone은 기능 테스트, ici report, 성능 또는 규모 측정, 문서 변경을 포함한다.

### 4.1 PR 운영

- 한 PR은 한 제품의 하나의 사용자 또는 infrastructure milestone만 다룬다.
- branch는 `feat/<project>-<feature>`, `fix/<project>-<issue>`, `test/<project>-<scope>`, `docs/<scope>` 형식을 사용한다.
- 의미 있는 단위마다 Conventional Commit을 만든다.
- PR 제목과 요약은 plan code가 아니라 제품/기술 결과를 설명한다. `T0`, `B1`, `D2` 같은 plan code는
  본문이나 label의 보조 메타데이터로만 쓰며 제목·요약의 유일하거나 주된 식별자로 삼지 않는다.
  이미 남은 historical PR 제목과 문서는 증거이므로 이름을 바꾸지 않는다.
- ici 미지원으로 gate가 깨지면 우회 디렉터리로 옮기지 않고 `ICI-GAPS.md`에 재현을 남긴다.
- ici 신규 기능이 필요한 toy PR은 release candidate pyz로 먼저 검증하고, 정식 release 후 workflow pin을 갱신해 병합한다.

### 4.2 제품 버전과 release cadence

- 포트폴리오 방향, B-stage 진행, ici pin, CI/runner-only 변경은 toy 제품 버전을 자동으로 올리지 않는다.
- 각 toy 제품은 독립적인 버전을 가진다. 한 제품의 milestone이나 ici 변경이 다른 제품의 버전을
  함께 bump하지 않는다.
- `patch`는 이미 공개된 stable 제품의 defect, security, compatibility regression을 수정할 때만
  사용한다.
- `minor`는 응집된 사용자 가치가 실제로 쓸 수 있는 제품 checkpoint가 된 경우에만 사용한다. 다음
  증거를 모두 완료·기록한 뒤에만 stable release를 만든다.
  - native tests
  - released-ici verification
  - PR CI와 exact-main CI/Pages
  - 사용자 문서와 limitations
  - 재현 가능한 release assets와 checksum/provenance
- candidate, pre-release, unreleased 상태는 stable release로 세지 않는다.
- BuildScope `0.5.0`은 B3 include explanation, B4 semantic configuration diff, B5 hybrid
  packaging/integration을 함께 묶는 첫 usable release boundary로 유지한다. 이 경계 이후의 작업은
  같은 수준의 checkpoint가 마련될 때까지 `Unreleased`에 누적한다.

---

## 5. 전체 실행 순서

| Wave | toy 결과물 | 대응 ici 단계 | 병렬 가능 범위 |
|---|---|---|---|
| T0 | 현재 Qt shell 계획 보정·완료 | I0 | loglens/diskmap 독립 가능 |
| T1 | loglens streaming 신뢰성, diskmap scan 안전 기반 | I1·I2 | L과 D 병렬 가능 |
| T2 | quality-zoo v1, 기존 앱 제품 UX | I1·I4·I5·I6 | corpus와 앱 병렬 가능 |
| T3 | buildscope MVP | I3·I4·I7 | ici compile context와 교차 진행 |
| T4 | envlens MVP | I5 | buildscope와 독립 가능 |
| T5 | abilens MVP | I7 | Make adapter/binary engine과 교차 진행 |
| T6 | 세 제품 고급 기능과 release | I8·I9 | 안정된 core 위에서 병렬 가능 |

동시에 너무 많은 미완성 앱을 만들지 않는다. 신규 사용자 제품은 최대 두 개 milestone만 동시에 in progress로 둔다. 기존 앱 reliability 작업과 quality-zoo scenario 추가는 이 제한에서 제외한다.

---

## 6. T0 — 현재 Qt 셸 계획 보정과 안전망

### T0-1. 계획 상태와 Qt 환경 정리

**브랜치:** `docs/qt-plan-corrections`

- [x] 이 마스터 계획을 [PR #12](https://github.com/jihoon22-lee/toy-projects/pull/12)로 병합했다.
- [x] 인수인계서는 과거 세부 계획보다 이 master plan을 우선하도록 갱신했다.
- [x] Qt 5.15 설치 상태와 실제 검증 명령을 기록했다.
- [x] ROADMAP에서 master plan과 T0/L/D/B/E/A/Q 각 stream으로 연결했다.

**T0-1 실측 증거 (2026-08-31, WSL2):** `/usr/bin/qmake`는 Qt 5.15.18,
`/usr/bin/qmake6`는 Qt 6.10.2를 보고했고, `pkg-config`의 Qt5/Qt6 Core·Widgets·Concurrent·Test
모듈도 각각 같은 버전으로 해석됐다. ici 0.6.0 release asset으로 최신 `main`의 두 프로젝트를
`QT_QPA_PLATFORM=offscreen ici.pyz verify --report --html ... --github-summary`로 실행한 결과는
`loglens: Suite PASS, TEM 4.08 (10 passed, 2 skipped)`와 `diskmap: Suite PASS, TEM 4.85
(10 passed, 2 skipped)`였다. Qt major별 native build/test는 T0-3~T0-5에서 이 증거와 별도로
완료됐고, 그 remote/native evidence는 T0-5 절과 handover에 기록한다. 현재 T0 전체 checkpoint는
§15에서 완료로 표시한다.

### T0-2. loglens parser contract부터 수정

**브랜치:** `fix/loglens-stream-state`

`parseLines(vector<string>)` 대신 상태를 보존하는 `RecordAssembler` 또는 동등한 이름의 core object를 설계한다.

상태:

- 다음 physical line number
- 아직 newline으로 끝나지 않은 partial bytes
- continuation을 붙일 수 있는 current/pending record
- selected format과 encoding/error policy
- source generation/rotation id

- [x] initial batch가 line 1부터 정확히 번호를 붙인다.
- [x] 다음 poll은 이전 physical line 다음 번호에서 시작한다.
- [x] stack trace continuation이 poll 경계를 넘어도 이전 record에 붙는다.
- [x] 파일 첫 줄이 continuation이면 유실하지 않는다.
- [x] partial line은 newline 또는 explicit flush 전까지 완성 record가 되지 않는다.
- [x] truncation/rotation 시 partial과 continuation state를 초기화한다.
- [x] CLI와 GUI가 같은 assembler를 사용하고 중복 `appendLine`/`parseLines`를 제거한다.

### T0-3. loglens Qt 셸 테스트

**브랜치:** `test/loglens-qt-shell`

- [x] root CMake에 GUI library와 MainWindow QtTest target을 둔다.
- [x] 실제 fixture open, append growth, truncation, read error를 검증한다.
- [x] stale model, follow checkbox/timer와 status message를 검증한다.
- [x] Qt object에 안정적인 objectName/accessibility name을 부여한다.
- [x] Qt 5와 Qt 6 CMake build/CTest를 각각 실행한다.
- [x] ici coverage를 실측해 threshold를 도달값 근처로 올린다.

### T0-4. diskmap Qt 셸 테스트

**브랜치:** `test/diskmap-qt-shell`

- [x] MainWindow를 library로 링크 가능한 qmake 구조를 유지한다.
- [x] scan result 표시, directory descend, breadcrumb, up, leaf no-op를 검증한다.
- [x] widget type 순서가 아니라 objectName과 public test seam을 사용한다.
- [x] stale scan generation을 위한 failing test를 D1 입력으로 남긴다.
- [x] Qt5 `localPos()`와 Qt6 `position()` 양쪽에서 build/test한다.

### T0-5. 공통 Qt version 계약

**브랜치:** `chore/qt5-qt6-matrix`

- [x] CMake는 `find_package(QT NAMES Qt6 Qt5 ...)`와 versioned target을 쓴다.
- [x] Qt5 강제 job은 `CMAKE_DISABLE_FIND_PACKAGE_Qt6=ON`으로, Qt6 job은 Qt5 disable로 선택을 확인한다.
- [x] qmake는 명시적인 `/usr/bin/qmake6`/`/usr/bin/qmake` job에서 각각 실제 executable을 선택한다.
- [x] pkg-config scope에는 Qt5/Qt6 이름과 필요한 Widgets/Concurrent modules를 모두 두고 실제 선택 evidence를 CI log에서 확인한다.
- [x] CI와 README에 “컴파일 가능”이 아니라 실행한 version을 기록한다.

**T0-5 실측·구현 증거 (2026-08-31):** `ci/check_manifest.py`가 각 GUI manifest 항목을
`SUPPORTED_QT_MAJORS = (5, 6)`으로 확장해 현재 네 leg를 생성한다. Qt6 leg는
`qt6-base-dev`/`qmake6`, Qt5 leg는 `qtbase5-dev`/`qt5-qmake`를 설치하며, 선택 major의
Core/Gui/Widgets/Concurrent/Test pkg-config 버전을 출력·검증한다. loglens CMake는 반대
major disable guard, configure output, `ldd` linked library와 CTest를 확인하고, diskmap qmake는
`-query QT_VERSION`, linked library, `make check`를 확인한 뒤 양쪽에서 실제 headless GUI
smoke를 실행한다. discovery 자동 확장은 `ci/test_check_manifest.py` stdlib 테스트로
검증했고, report-pr의 PR 소스 미체크아웃·write 권한 분리 경계는 유지했다. 네 조합의
로컬 실측은 모두 통과했으며, 원격 PR에서는 이 matrix와 Merge Gate가 최종 재검증한다.

**T0 완료 조건:** loglens와 diskmap shell test가 양 Qt major에서 통과하고, loglens line/continuation state가 poll 경계에서도 정확하다.

---

## 7. L stream — loglens를 incident explorer로 완성

### L1. file tailing identity와 오류 의미론

**브랜치:** `feat/loglens-reliable-tailing`

**L1 진행 분할 (2026-08-31):**

- [x] Slice 1 — source identity/typed chunk contract: POSIX device/inode 기반 교체 감지와
  generation/position/error를 담은 core poll 결과, real-filesystem 회귀 테스트.
- [x] Slice 2 — follow recovery GUI state: missing/reappear retry, stopped/follow resume와
  그 상태를 표시하는 사용자 경험.

- [x] POSIX에서는 device/inode, 다른 platform에서는 사용 가능한 file identity abstraction을 도입한다.
- [x] 더 크거나 같은 새 파일로 교체돼도 rotation을 탐지한다.
- [x] truncate, replace, permission loss, file disappear/reappear를 구분한다.
- [x] poll 결과를 `records`, `generation_changed`, `error`, `position` 구조로 만든다.
- [x] stopped/retryable/fatal 상태와 사용자의 resume 동작을 정의한다.
- [x] real filesystem integration test와 fake source deterministic test를 함께 둔다.

**Slice 2 구현·검증 증거 (2026-08-31):** `FollowState`를 `Stopped`, `Following`,
`WaitingRetry`로 분리했다. retryable missing/permission/open/stat/read 오류에서는 기존
행과 timer를 유지하고 시도 횟수와 대기 상태를 표시한다. 새 file identity가 확인된
replacement를 성공적으로 읽을 때만 assembler/model을 새 generation으로 reset한다. 사용자가
Follow를 끄면 retry를 중지하고, 다시 켜면 명시적으로 재개한다. fatal unsupported file type은
follow를 중지하되 마지막 정상 화면은 보존한다. `retryableSourceErrorKeepsFollowingAndVisibleRows`,
`sourceReplacementRecoversWithCleanRows`, `disablingFollowWhileWaitingStopsPolling`,
`unsupportedSourceStopsFollowing`를 포함한 `test_main_window` 회귀 테스트가 추가됐고,
Qt 6 및 Qt 5 CMake/CTest에서 각각 10/10 PASS를 기록했다. 구현 커밋은 `4094ff5`, 테스트
커밋은 `ca56d2e`다. PR #22 첫 CI 실행
[`33335607699`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33335607699)에서
missing 직후 inode이 재사용되는 파일시스템 경계가 드러나
`disablingFollowWhileWaitingStopsPolling`의 기대 rowCount 1 대신 2가 관측됐고, Qt5·Qt6
native와 `ici verify`가 실패했다. 더 큰 replacement가 이전 offset에서 읽혀 stale/new
bytes가 섞이는 것이 원인이었다. `d419d2f`가 `recovery_pending_`/
`recovery_restart_started_`로 unavailable interval 뒤 단일 generation 증가와 offset 0
재시작을 강제하도록 수정했고, 수정 후 Qt5·Qt6 전체 CTest는 다시 10/10 PASS다. 로컬 ici
0.6.0 verify도 Suite PASS(10 pass, 2 skip), TEM 4.86,
line/function/branch 92.5/97.1/80.8%를 기록했고 Zero-CDN HTML과 Qt5·Qt6 8초 headless
smoke를 확인했다. 이 수치는 원격 PR 집계와 분리된 local evidence다. 검증 대상 구현 head
`3d7a7a5`의 PR #22 workflow
[`33336242400`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33336242400)은
manifest, `ici verify`(diskmap/loglens), 두 프로젝트의 Qt5·Qt6 GUI,
`Publish Reports & Sticky Comment`, `Merge Gate`를 포함한 모든 checks가 SUCCESS였다.
[sticky comment](https://github.com/jihoon22-lee/toy-projects/pull/22#issuecomment-5471277839)는
`diskmap: PASS · TEM 4.87`, `loglens: PASS · TEM 4.82`와 최신 run 링크를 게시했다.
Pages의 `diskmap/pr/22/`는 HTTP 200·161211 bytes·external refs 0개,
`loglens/pr/22/`는 HTTP 200·262156 bytes·external refs 0개였다. 이로써 L1 Slice 2의
원격 CI, sticky comment, Pages 완료 조건까지 닫혔다.

### L2. bounded storage와 큰 파일 UX

**브랜치:** `feat/loglens-bounded-model` (foundation), `feat/loglens-background-loader` (current slice), `feat/loglens-large-file-benchmark` (PR #26 merged)

- [x] 기존 RingBuffer를 GUI/CLI 실제 record store에 연결한다.
- [x] capacity, dropped record count와 oldest/newest line을 노출한다.
- [x] model reset 대신 incremental insert/remove contract를 테스트한다.
- [x] 초기 open은 `Latest records`(Tail N) 또는 `From start` 중 사용자 선택을 제공한다.
- [x] background parsing 중 UI가 filter/search를 안전하게 처리한다.
- [x] 1 GiB synthetic log와 100만 record benchmark를 만들고 first-paint, throughput, peak RSS를 기록한다.
- [x] 실측 후 default capacity와 성능 budget을 고정한다.

**L2 bounded foundation 로컬 증거 (2026-08-31):** GUI와 CLI는 absolute record ID를
유지하는 같은 `RingBuffer` 계약을 사용한다. 기본 보존량은 8,192 records, 허용 상한은
1,000,000이며 eviction 때 visible model은 contiguous remove/insert signal을 낸다. source
poll은 기본 1 MiB·상한 16 MiB chunk로 제한되고, GUI는 Follow가 꺼져도 초기 backlog를
zero-delay event로 협력적으로 소진한다. one-shot CLI는 첫 file-size snapshot까지만 읽으므로
동시 append를 무한 추격하지 않는다. pathological physical/logical record는 기본 64 KiB·상한
1 MiB로 제한하고 `input_bytes`/`omitted_bytes`로 손실을 명시한다.

초기 GUI 로드는 `LogLoadWorker`가 전용 `QThread`에서 `FileTailer`와 `RecordAssembler`를
소유하고, GUI thread의 `LogModel`에는 `LoadBatch`만 전달한다. Tail N은 continuation을
포함한 logical record root의 byte offset을 먼저 찾은 뒤 parser를 선택된 offset에서 시작해
physical line number를 보존한다. From start와 Tail N 모두 source identity와 snapshot boundary를
확인하며, 선택 중 rotation이면 retryable error로 보고 stale bytes를 섞지 않는다.
각 batch는 최대 512개 delta로 제한되고, GUI ACK 전에는 worker가 다음 chunk/batch를 읽거나
발행하지 않는다. `job_id`/`sequence` 검증은 stale job·queued ack·순서 오류를 차단하며,
Follow 중지/취소는 pending poll을 버린다. structured filter와 대소문자 구분 없는 search는
background load 중에도 GUI thread에서 즉시 변경할 수 있고 timeline 갱신은 debounce된다.

**L2 대용량 benchmark 결정 (2026-08-31):** canonical input은 정확히 1,073,741,824 bytes,
1,000,000 records이며 SHA-256은
`11186d3021e558c8ed5e33473198a6f9f281ca0605ae79739a928a87156435bb`다. capacity
`8192, 16384, 32768, 65536, 131072, 262144`를 각 3회, process timeout 180초로 실행했다.
두 Qt major에서 `8192..65536`이 correctness·성능·RSS budget을 만족했고, `131072`은 core
RSS, `262144`는 core와 GUI RSS budget을 초과했다. budget은 first result `≤ 5000 ms`, first
paint `≤ 5000 ms`, load `≤ 60000 ms`, throughput `≥ 25 MiB/s`, records `≥ 25000/s`,
core peak RSS `≤ 256 MiB`, GUI peak RSS `≤ 512 MiB`다. best median load time 대비 10% 이내의
가장 작은 적격 capacity를 고르는 규칙으로 GUI/CLI 기본 capacity를 `8192`로 결정했다.

capacity 8192의 median은 다음과 같다.

| Qt | component | first result | first paint | load | throughput | records/s | peak RSS |
|---|---|---:|---:|---:|---:|---:|---:|
| 5 | core | 3.041 ms | — | 1510.632 ms | 677.862 MiB/s | 661974.809 | 24.465 MiB |
| 5 | GUI | 18.031 ms | 19.616 ms | 17717.171 ms | 57.797 MiB/s | 56442.421 | 53.336 MiB |
| 6 | core | 3.049 ms | — | 1480.219 ms | 691.790 MiB/s | 675575.761 | 24.469 MiB |
| 6 | GUI | 18.055 ms | 18.843 ms | 18490.615 ms | 55.379 MiB/s | 54081.488 | 55.980 MiB |

runner는 input의 정확한 크기·record 수·SHA-256을 확인하고 raw sample을 집계한다. benchmark
target은 기본 빌드에 포함하지 않으며, local runner는 README의 [재현 명령](../../../README.md#1-gib-benchmark-재현-opt-in)으로
실행한다. artifact에는 `summary.json`, `summary.md`, `toolchain.json`, `toolchain.txt`,
`samples/*.json`만 남기고 1 GiB input과 process log는 scratch에 둔다. workflow는
`workflow_dispatch`와 주간 schedule의 Qt5/Qt6 matrix이며 일반 PR/merge gate가 아니다.
PR #26은 `c45176ce25f2efd66ea9b0ed9b48690e34cc8679`로 squash merge됐다. 최종 PR gate인
[workflow run `33355058919`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33355058919)은
모든 checks가 green이었고, 기존 [sticky comment](https://github.com/jihoon22-lee/toy-projects/pull/26#issuecomment-5473343910)는
`diskmap: PASS · TEM 4.90`, `loglens: PASS · TEM 4.80`, warn 0과 HTML 링크를 포함한다.
Pages `diskmap/pr/26/`와 `loglens/pr/26/`는 각각 HTTP 200·`text/html`·external refs 0개
(180160/334215 bytes)였다. 병합된 main의 [대용량 workflow run `33355312096`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33355312096)은
Qt5/Qt6 benchmark, combine, verdict를 모두 green으로 완료했고, combined summary SHA-256은
`5e3292950958a4c678a0c54bf75e7b2546ad1528f43529b6cce1c3dff4e150a8`이다.

background/Tail N 변경은 최신 구현 head `ce2a7cd91ff0a47c4f153b60f7fb7984de406ce9`로
[PR #25](https://github.com/jihoon22-lee/toy-projects/pull/25)의
[workflow `33351033448`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33351033448)에서
모든 checks를 통과했고 merge commit `69db15966ca0c032026aeb7b742c4eed6335910d`로
병합됐다. [sticky comment](https://github.com/jihoon22-lee/toy-projects/pull/25#issuecomment-5472960253)는
두 프로젝트 PASS와 HTML 링크를 포함하며, Pages `diskmap/pr/25/`와 `loglens/pr/25/`는 각각
HTTP 200·`text/html`·external refs 0개(180160/327074 bytes)였다. 이 원격 evidence는
background/Tail N 변경에 대한 것이고 1 GiB benchmark와는 별개다.

일반 PR의 `.github/workflows/ci.yml`에는 1 MiB/1,000 records, capacity `64,256`, 1회,
30초 timeout, budget skip의 `benchmark-smoke`가 있으며, `Merge Gate`가 이 harness correctness
run의 성공을 required check로 요구한다. full 1 GiB budget sweep은 비용 때문에
`workflow_dispatch`/주간 schedule workflow로만 실행한다.

Qt 5.15과 Qt 6.10의 전체 CTest는 각각 12/12였고 Qt6 strict benchmark build도 PASS였다.
현재 L2 benchmark의 ici 0.6.0 deep no-cache는 Suite PASS, 12/12 tests, TEM 4.83,
line/function/branch 93.6%/96.6%/81.8%, maximum complexity 15(0 issues), duplication 1.71%,
sanitizer PASS, HTML 433,351 bytes·external refs 0개였다. 이전 bounded foundation의 ici
0.6.0 local verify는 Suite PASS, 10 pass / 0 warn / 0 fail / 0 error / 2 skip, TEM 4.85,
line/function/branch 93.9%/96.9%/83.1%, maximum complexity 15(0 issues), duplication 1.43%,
sanitizer clean, HTML 283,077 bytes·외부 script/link/image 참조 0개였다. 구현·local evidence
head `fa4fd1a`의 PR
[#24](https://github.com/jihoon22-lee/toy-projects/pull/24) workflow
[`33348597272`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33348597272)는 manifest,
두 프로젝트의 `ici verify`, Qt5·Qt6 GUI, report publish와 Merge Gate를 모두 통과했다.
[sticky comment](https://github.com/jihoon22-lee/toy-projects/pull/24#issuecomment-5472700934)는
두 프로젝트 PASS와 HTML 링크를 포함하며, Pages `diskmap/pr/24/`와 `loglens/pr/24/`는 각각
HTTP 200·`text/html`·180,160/279,484 bytes·외부 참조 0개였다. 위 원격 수치는 background
loader/Tail N 변경 이전 bounded foundation에 대한 historical evidence다. background/Tail N
현재 원격 evidence는 위 PR #25 기록이며, L2 benchmark의 merged-main evidence는 위에
정리했다.

L2 bounded/background 구현과 1 GiB benchmark, 성능 budget/default capacity 결정은 완료됐다.
PR26의 원격 CI·ici·sticky report·Pages 검증과 main Qt5/Qt6 full sweep도 완료됐다. D2
cancellable scan local candidate와 current ici main의 full local verify, PR #28 원격
PR/CI·sticky·Pages evidence와 merged-main full benchmark도 모두 닫혔다. D3 core projection은
PR #45로, GUI explorer workbench는 PR #46으로 병합됐고 exact-main verification도 완료됐다.
DiskMap product version은 `0.1.0`/`Unreleased`를 유지한다. exact PR/main artifact·Pages 표는
[D3 explorer workthrough](../../../workthrough/2026-09-02-diskmap-explorer-workbench.md)에
중앙화했다.
L3 parser/filter와 L6 release 완료 조건은 이 결정으로 닫히지 않으며 체크리스트를 유지한다.

### L3. parser와 filter 완성도

**브랜치:** `feat/loglens-parser-pipeline`

- [ ] JSON string escape와 Unicode를 손으로 일부 파싱하지 않고 검증된 범위의 parser contract로 처리한다.
- [ ] ISO, syslog, JSONL, raw와 multiline을 source profile로 저장한다.
- [ ] malformed line을 유실하지 않고 parse error metadata와 raw를 보존한다.
- [ ] timestamp timezone/precision과 missing timestamp 정책을 정의한다.
- [ ] filter AST에 syntax diagnostic range와 saved query를 추가한다.
- [x] regex catastrophic input에 timeout/limit 또는 안전 정책을 둔다.
  - filter 언어는 regex를 실행하지 않고 `~`/`!~`를 bounded literal substring으로 고정한다.

#### L3 filter diagnostics slice — bounded recursive parser (2026-09-02)

- [x] 기존 recursive-descent filter에 4,096-byte query, 256-node AST, 1,024-byte decoded
  literal, depth 64 bounds를 적용하고, oversized/unknown/trailing/invalid syntax를
  deterministic message로 거부한다.
- [x] `ParseError::position`을 기존 시작 offset API로 유지하면서 `[position, end)` UTF-8
  input byte range를 추가했다. quoted value의 `\\"`/`\\\\`만 해석하고 나머지 bytes는 보존하며,
  `~`/`!~` literal substring 의미론과 기존 precedence를 유지한다.
- [x] CLI의 `--level`과 `--filter`를 독립 parse해 사용자 argument 기준으로 diagnostic range를
  매핑하고, GUI는 untrimmed UTF-8 input을 사용한다. 이전 GUI filter는 failed apply 뒤에도
  유지하며, depth error는 추가 nesting token을, unsupported UTF-8 escape는 전체 scalar를
  가리킨다. `TokenRange`/`PredicateTokens` 구조화로 새 clang-tidy swapped-parameter 경고를
  제거했다.
- [x] Qt5/Qt6 native suite는 각각 12/12 pass했다. public ici `v0.10.2` `ici.pyz`의 SHA-256은
  `2af5198d1348a64c39f4f37d12657aa9a2c4bf3ddf034a9099909c41e86e30e7`이며, uncached deep suite는
  clazy unavailable 및 pre-existing lint findings로 인한 `WARN`만 남겼다. 변경된
  `filter_expr.cpp`/`main.cpp`/`main_window.cpp`는 actionable lint target 0건이고, 전체 lint
  26개 target 중 clang-tidy `note:` 16줄이 ici에서 별도 target으로 집계되는 알려진 engine
  follow-up이 있다. `compile_db`는 40 configurations의 production unit 14/14 `PASS`, `test`는
  12/12 `PASS`와 line/function/branch `93.3% / 96.7% / 82.4%`, `complexity`는 218개 대상
  max 15 `PASS`, `sanitize`는 `PASS`다. HTML은 484,899 bytes, exact title
  `ici Verification Report — loglens`, Zero-CDN이다. 이 증거는 version/release를 올리거나
  broader L3 parser-pipeline contract를 닫지 않는다.
- [ ] saved query, source-profile parser, malformed log-line metadata와 나머지 L3 contract는
  후속 slice에서 다룬다.

### L4. 실제 사용되는 highlight와 triage

**브랜치:** `feat/loglens-triage-workbench`

- [ ] 기존 HighlightRules를 table delegate/detail view에 연결한다.
- [ ] rule create/edit/delete/reorder, preview, persistence를 제공한다.
- [ ] bookmark, annotation, selected rows export를 제공한다.
- [ ] record detail pane에 raw/parsed fields와 source location을 표시한다.
- [ ] filter/timeline/table selection을 같은 visible record set으로 동기화한다.
- [ ] malformed rules와 config migration을 테스트한다.

### L5. 구간 비교와 이상 신호

**브랜치:** `feat/loglens-window-analysis`

- [ ] 두 시간 구간의 level/source/pattern rate를 비교한다.
- [ ] deterministic rate spike와 new-pattern detection을 먼저 제공한다.
- [ ] correlation/request/thread id field grouping을 지원한다.
- [ ] heuristic 결과에 score와 설명을 붙이고 “AI 판정”처럼 표현하지 않는다.
- [ ] 분석 결과에서 원본 record 구간으로 이동한다.

### L6. loglens release 완료 조건

- [ ] bounded memory와 partial/rotation correctness benchmark 통과
- [ ] Qt5/Qt6 CI, headless test와 실제 WSLg smoke 통과
- [ ] CLI와 GUI가 parser/filter/store 의미론을 공유
- [ ] highlight/bookmark/export가 실제 UI에서 사용 가능
- [ ] ici standard profile PASS, deep profile limitation 문서화
- [ ] install/run/recovery guide와 sample data 제공

---

## 8. D stream — diskmap을 안전한 storage workbench로 완성

### D1. identity-safe filesystem model

**브랜치:** `refactor/diskmap-node-identity`

**T0-4에서 넘겨받는 실패 시나리오:** 현재 `MainWindow`는 worker가 실행 중인 동안 두 번째
`scanPath()`를 무시한다. D2에서 취소/rescan을 허용할 때 scan A가 끝난 뒤 scan B를 시작하거나,
그 반대 순서로 완료되는 경합을 재현하면 이전 결과가 새 화면·breadcrumb를 덮을 수 있다. 각
작업에 generation token을 붙이고 완료 시 현재 generation과 다르면 결과를 폐기하는 계약을
먼저 정한 뒤, rescan race 테스트를 D2에서 추가한다. T0-4의 안정적인 탐색 테스트는 이
미구현 동작을 초록불로 위장하지 않는다.

D1 입력으로 보존할 예정 실패 테스트의 이름과 기대는
`rescanCannotDisplayAnOlderGeneration`이다. scan A가 실행 중인 상태에서 scan B를 요청하고,
B의 root가 표시된 뒤 A의 completion을 전달했을 때 `currentNode()->name`과 breadcrumb가
B를 유지해야 한다. 현재 구현은 실행 중인 두 번째 `scanPath()`를 거부하므로 이 테스트를
지금 활성화하면 실패한다. D2에서 cancellation/rescan API와 generation guard를 함께 만든
뒤에만 활성화한다.

`FsNode`에 cleanup과 정확한 size 계산에 필요한 사실을 보존한다.

- canonical/display path
- device/inode 또는 platform file id
- file, directory, symlink와 followed 여부
- logical size와 allocated size
- owner/permission, modified time
- mount/device boundary
- scan generation과 error/incomplete state
- hardlink reference identity

- [x] RealFsSource가 symlink를 따라간 결과와 링크 자체를 혼동하지 않는다.
- [x] follow_symlinks=true에서는 visited identity로 cycle을 막는다.
- [x] hardlink는 allocated/reclaimable 합계에서 중복 계산하지 않는다.
- [x] permission/error로 incomplete한 directory total을 완전한 값처럼 보이지 않게 한다.
- [x] path string 결합 대신 filesystem path abstraction을 사용한다.

**D1 구현 slice 상태**

Slice 1 — metadata/source boundary (2026-08-31):

- [x] `FsMetadata`와 `FileIdentity`를 도입하고 allocated bytes, hard-link count,
  permissions, ownership, modified time의 `*_known` 상태를 보존한다.
- [x] POSIX `lstat`(link)와 `stat`(followed target)를 분리하고, full path와
  link/target metadata를 `DirEntry`에서 `FsNode`까지 전달한다.
- [x] stat metadata와 scan aggregate size를 분리하고, directory listing/open/iterator
  오류를 incomplete node와 명시적 error로 보존한다.
- [x] Qt5/qmake와 Qt6/qmake6의 전체 빌드 및 모든 `make check`를 통과시킨다.

Slice 1의 현재 검증 증거는 위 Qt5/Qt6 qmake build와 전체 `make check`에 한정한다.
ici verify와 GUI smoke는 이 문서에서 완료로 주장하지 않으며, PR/release 검증에서 별도로
수집한다.

Slice 2 — safe traversal and path semantics:

- [x] `FileIdentity` visited set으로 follow-symlink directory cycle을 차단한다.
- [x] hard-link identity를 기준으로 allocated/reclaimable aggregate를 중복 계산하지
  않는다.
- [x] string path join을 filesystem path abstraction으로 교체하고 platform별 경로
  의미론을 검증한다.

**Slice 2 구현·검증 증거 (2026-08-31):** 구현 브랜치는
`refactor/diskmap-identity-scan`이며, Slice 1의 metadata/source 경계를 이어받아
`02851f7`, `f4bc717`, `3fdeb55`, `9c50fd0`, `0a7b013`, `c65f0d8`에서 계약·구현·실제
filesystem 회귀·coverage 보강·qmake relink를 완료했다. `FsNode::path`와 source boundary는
`std::filesystem::path`를 사용하고, scanner는 physical `FileIdentity` 방문 집합으로
followed-directory cycle/back-edge를 노드로 보존하면서 확장을 차단한다. target identity가
없는 followed directory는 `complete=false`와 오류로 남긴다. logical bytes는 directory entry마다
계산하고, allocated bytes는 non-directory entry data를 유효 identity별로 deduplicate한다.
directory 자체의 filesystem metadata block은 이 aggregate 범위에서 제외하며, reclaimable
bytes는 subtree가 known hard-link reference를 모두 소유할 때만 확정한다. symlink target alias는 소유 reference로
세지 않고, incomplete/unknown은 `*_known=false`로 전파한다. 유한한 `max_depth`로 잘린
directory도 `complete=false`와 `scan depth limit reached`를 남겨 physical aggregate를
unknown으로 만든다. logical aggregate도 `FsNode::logical_size_known`으로 정확성을 보존하며,
`uint64_t` overflow에서는 최댓값으로 포화하고 해당 flag를 false로 설정한다.

- [x] fixed-identity `FakeFsSource`가 공백이 있는 nested path, cycle, hard-link ownership,
  symlink alias, unknown allocation/link-count와 overflow를 검증한다.
- [x] POSIX temporary filesystem test가 root identity, hard-link deduplication과 symlink
  back-edge를 실제 metadata로 재검증한다.
- [x] qmake static consumer에 `PRE_TARGETDEPS`를 연결해 test binary가 최신 archive를
  다시 링크하도록 하고 stale `.gcda`/`.gcno` coverage 혼입을 막는다.
- [x] regular-file root를 complete한 one-node scan으로 반환하고, 명시적으로 선택한 root
  symlink는 descendant `follow_symlinks` 설정과 무관하게 dereference한다. symlink-to-file은
  leaf로 남기며, broken root target 오류는 `ScanResult.errors`에도 포함한다.

**Slice 2 로컬 실측(보수적 truncation 후속 커밋 전 기준):** Qt 5.15.18
(`/usr/bin/qmake`)과 Qt 6.10.2(`/usr/bin/qmake6`)에서
fresh full build와 `make check` 9/9가 각각 통과했다. 두 GUI는
`QT_QPA_PLATFORM=offscreen` smoke에서 8초 동안 살아 있었고 timeout exit 124는 기대한
결과다. public ici 0.6.0 release asset verify는 `Suite PASS`, 10 pass / 0 warn / 0 fail /
0 error / 2 skip, TEM 4.90, line 96.9% / function 97.9% / branch 85.2%, maximum complexity
14, duplication 2.1, duration 24.24초였다. HTML은 180,624 bytes이며 외부 `src`/`href`
참조가 0개다. 첫 coverage 실행은 stale `.gcda` stamp 1417858375와 `.gcno` stamp
1418147347가 섞여 scanner.cpp가 0%로 집계되어 line 73.4% / function 83.3% / branch
60.2%로 실패했으며, `PRE_TARGETDEPS` relink 후 stamps가 1418147347로 일치하고 위 PASS
결과를 얻었다. 이후 `5ceb059`/`ebe3d86`에서 finite `max_depth`로 잘린 directory를
`complete=false` 및 `scan depth limit reached`로 표시하고, logical size 합산을
`uint64_t` 최댓값에서 포화하도록 보수적 truncation semantics를 추가했다. 이 후속 커밋
직후의 최종 full native/ici 검증은 이후 candidate qmake-clean 검증으로 local ici를 다시
확인했다. 원격 PR CI·sticky HTML comment·Pages 응답은 아래 원격 완료 증거로 확인했다.

이후 `6861b7a`/`2f56caf`에서 regular-file root의 실제 CLI 경로와 root symlink semantics를
추가했다. 실제 file-root smoke는 JSON에서 `name=main.cpp`, `is_dir=false`, `size`가 source
metadata와 일치하는 one-node 결과를 냈다(파일 크기는 source 변경에 따라 고정하지 않는다).
이 후속 커밋 이후 D1 Slice 2의 최신 로컬 native 확인은 Qt 5.15.18(`/usr/bin/qmake`)과
Qt 6.10.2(`/usr/bin/qmake6`)에서 각각 full build target과 `make check` 9/9 PASS였다.
Qt5/Qt6 GUI offscreen smoke도 각각 8초 생존 후 기대된 timeout exit 124로 확인했다.
`31d8b48`의 root inspection stage 분리 후 public ici complexity-only 검증은 PASS,
maximum cyclomatic 14 across 101 functions, 0 issues였다.

**D1 Slice 2 최종 local candidate ici qmake-clean 검증:** `Suite PASS`, 10 pass / 0 warn / 0 fail /
0 error / 2 skip, 9/9 tests, line 96.6% / function 98.0% / branch 85.0%, TEM 4.90,
complexity 14 across 101 functions / 0 issues, duplication 2.0, sanitizer clean,
duration 85.96초였다. capability inventory는 30 tools / 21 ready / 0 incomplete /
9 unavailable, required `g++` ready, health `READY`였다. candidate JSON에는 test와 sanitize
각각의 성공한 `/usr/bin/make clean` evidence가 포함됐고, tool snapshot도 렌더링됐다.
HTML은 281,264 bytes이며 외부 `src`/`href` 참조가 0개다. 이 결과는 이전 public ici
0.6.0 complexity WARN-era 결과를 대체하고 새 freshness guard를 확인한다. 최종 local ici
검증은 완료됐으며 toy remote PR CI·sticky HTML comment·Pages 응답도 아래와 같이 확인했다.

**D1 Slice 2 원격 완료 증거 (2026-08-31):** [PR #23](https://github.com/jihoon22-lee/toy-projects/pull/23)이
merge commit `039052f9f30e355e12f3c812065657e3be4576f2`로 병합됐다. CI run
[`33338809225`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33338809225)은
green이었다. [sticky comment](https://github.com/jihoon22-lee/toy-projects/pull/23#issuecomment-5471613383)가
게시됐고, Pages의 `diskmap/pr/23/`와 `loglens/pr/23/`는 모두 HTTP 200 `text/html`이며
외부 참조가 0개였다.

### D2. cancellable scanner와 stale result 방지 — 완료

**브랜치:** `feat/diskmap-cancellable-scan`

- [x] progress callback을 GUI에 실제 연결한다.
- [x] atomic/token 기반 cancellation과 cooperative checkpoint를 제공한다.
- [x] scan generation id가 이전 worker 결과로 새 UI를 덮지 못하게 한다.
- [x] mount boundary, exclude pattern, min size, max depth 옵션을 제공한다.
- [x] aggregate/sort/count의 deep tree recursion을 iterative 또는 안전한 bound로 바꾼다.
- [x] cancellation 후 partial result를 보여줄지 폐기할지 명확히 한다.
- [x] 100만 entry fake source benchmark로 throughput과 memory를 측정한다.

2026-08-31 local candidate에서 cancellation은 atomic token과 cooperative checkpoint로
동작하고, GUI는 최신 generation만 progress/result에 반영한다. 취소된 partial result는
`Scan cancelled — partial result discarded`로 폐기하며 기존 표시 tree를 보존한다. root
metadata/open 및 empty-root listing failure는 fatal, child listing/stat/iterator failure는
불완전 subtree를 보존하는 partial/non-fatal 오류다. CLI는 `--max-depth`,
`--follow-symlinks`, `--min-size`, `--one-file-system`, repeatable `--exclude`를 legacy
출력 전용 `--depth`와 함께 제공한다. scanner와 집계/layout은 iterative이며 structural
depth는 `kMaxTreeDepth=512`로 제한된다. benchmark runner 기본 budget은 1,000,000 entries,
cancel-after 10,000, 60초 timeout, 최소 100,000 entries/s, 최대 1,536 MiB RSS, 최대
30,000 ms full/2,000 ms cancellation이다.

로컬 native 증거는 Qt 5.15.18(`/usr/bin/qmake`)와 Qt 6.10.2(`/usr/bin/qmake6`)에서 full
build 및 `make check` PASS, 각 `test_main_window` 10/10 PASS, CLI integration smoke PASS다.
full benchmark는 4,820.934 ms, 207,428.692 entries/s, 1,063.496 MiB RSS이며 cancellation은
2.676 ms다. summary SHA-256은
`743d5c5409101cfd9ef889da2da421e94cc205f585770ab19bb611472926246d`다. 초기 ici
complexity-only FAIL(scan complexity/nesting)은 `b7218c6`의 상태 전이 분리 refactor 뒤
maximum cyclomatic 14/limit 15, 129 functions, 0 issues로 PASS했다. ici main commit
`6a0eadb`의 candidate `dist/ici.pyz` SHA-256
`8cd2d4b128ab2d181e708660c4c4f38bcc9d50f9ad91e3aa5670f557e6077fed`로 수행한 full
post-refactor local `ici verify`는 `Suite PASS`, 10 pass / 0 warn / 0 fail / 0 error /
2 skip, tests 9/9, TEM 4.92초, line 95.7% / function 98.5% / branch 84.4%, complexity
max14 across 129 functions / 0 issues, sanitizer clean, duplication 3.11%, tools 30 discovered /
21 ready / 0 incomplete / 9 unavailable, cache hits 0, total 82.29초였다. HTML
`/tmp/tmp.RFA39KzhyH.html`은 299034 bytes, SHA-256
`cf75f9d6f28179d95645d0e1582022008804078d5e3844de503a8c1a130c64a0`이며 external
script/link/img/iframe resource tags는 0개였다. 이 local evidence에 아래 원격 증거를 더해 D2의
병합·검증 조건을 모두 닫았다.

2026-08-31 [PR #28](https://github.com/jihoon22-lee/toy-projects/pull/28)은 squash merge
commit `ec075e57874d20654f7cbfbc604ad8aaee8401a6`으로 toy-projects `main`에 병합됐다.
[PR CI run `33368958698`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33368958698)의
12개 check가 모두 green이었고, DiskMap benchmark smoke Qt5/Qt6, 네 GUI matrix job, 두 ici
verification job, publish와 Merge Gate를 포함한다. [sticky comment](https://github.com/jihoon22-lee/toy-projects/pull/28#issuecomment-5475254935)에는
`diskmap: PASS · TEM 4.92 · 10 pass / 2 skip · tests 9/9`와
`loglens: PASS · TEM 4.80 · 10 pass / 2 skip · tests 12/12`, 양쪽 report link가 게시됐다.
Pages `diskmap/pr/28/`와 `loglens/pr/28/`는 각각 HTTP/2 `200`, content-type `text/html`,
외부 `script`/`link`/`img`/`iframe` resource 0개로 확인됐다.

| Pages 경로 | bytes | SHA-256 |
|---|---:|---|
| `diskmap/pr/28/` | 199843 | `c8a0d8009e1c19cd2d9df041969396f6abce95275713fe7ead6a499ac0b33b72` |
| `loglens/pr/28/` | 334215 | `acda3bfb29bf5f3534256f614719e678ec89ed21b3420ee2b282ec55e2107830` |

PR #28 병합 후 head `ec075e5`에서 수행한 [main full benchmark run `33369288586`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33369288586)은
Qt5/Qt6 full, combine, verdict job을 모두 성공시켰다. 설정은 1,000,000 entries,
`cancel-after 10000`, process timeout 60초이며, 양쪽 correctness는 `true`, failures는 0,
budgets enforced는 `PASS`였다.

| Qt | full elapsed | full throughput | full peak RSS | cancellation elapsed |
|---|---:|---:|---:|---:|
| 5 | 2131.069 ms | 469248.020 entries/s | 1064.094 MiB | 1.479 ms |
| 6 | 3120.463 ms | 320465.315 entries/s | 1064.137 MiB | 1.580 ms |

combined `summary.json`은 3567 bytes이며 SHA-256은
`26391797763aed17fedb04e2a4aeb5cf8238ec4d5b5d040d473d32a513369251`이다. 이는 toy-projects
`main`의 기능 병합 및 검증 기록이며, 별도 제품 버전 release를 의미하지 않는다. 따라서 D2는
local/native/ici, PR CI, sticky report, Pages와 merged-main benchmark까지 모두 완료됐다.

### D3. 탐색과 설명 UX

**구현 브랜치(병합 후 삭제):** `feat/diskmap-explorer-ui`

- [x] Qt-free core view projection slice: metric values, deterministic node keys/issues,
  conjunctive search/type/size/age filters, and stable child/largest-file ordering
- [x] treemap과 sortable table/list를 shared immutable scan document로 연결한다.
- [x] breadcrumb에 실제 path segment와 accessible action을 제공하고 table activation과
  equivalent navigation을 제공한다.
- [x] search, size/type/age/state filter와 largest files view를 추가한다.
- [x] logical/allocated/reclaimable size 차이와 known/unknown·non-additive uncertainty를
  설명한다.
- [x] unreadable/incomplete subtree와 cycle/depth/mount/scanner-filtered provenance를
  시각적으로 구분한다.
- [x] rescan, refresh와 `NodeKey` 기반 selection 유지 의미론을 테스트하고 stale/generation
  result를 거부하며 scan-time interaction을 freeze한다.

PR #45는 merge commit `0688e44fa99d1ec69aba0c9bf9995a4a857fea9e`로 `main`에 병합됐다. PR
workflow [`33607634973`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33607634973)와
exact-main workflow [`33608884643`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33608884643)는
required checks, Qt5/Qt6, ici, benchmark, report publication과 Merge Gate를 확인했다. 이
문단은 D3 core의 historical evidence다. 위 체크리스트의 GUI 항목은 역사적
`feat/diskmap-explorer-ui` 구현으로 완료됐고, [PR #46](https://github.com/jihoon22-lee/toy-projects/pull/46)에서
`main`에 병합됐다. PR workflow [`33627322683`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33627322683)와
exact-main workflow [`33628585439`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33628585439)는
모두 green이다. PR sticky marker/link 수와 PR/main artifact·Pages byte-identical 검증의 exact
HTML 표는 [D3 explorer workthrough](../../../workthrough/2026-09-02-diskmap-explorer-workbench.md)에
중앙화했다.

로컬 native qmake는 Qt 5.15.18과 Qt 6.10.2에서 clean full build(`-Werror`)와 `make check`
`11/11` PASS를 기록했다. Focused QtTest 수는 MainWindow `29`, TreemapWidget `12`,
NodeTableModel `11`이다. public ici `v0.10.2` asset SHA-256은
`8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4`이며, deep no-cache는
`WARN` (`10 PASS / 2 WARN / 0 FAIL / 0 ERROR / 2 SKIP`), TEM `4.95`, compile DB `16/16`
production units across `30` configurations, line/function/branch `96.1% / 99.1% / 83.4%`,
complexity max `14`, sanitizer `PASS`였다. HTML은 정확히 `499,265` bytes,
SHA-256 `9b624303b6191c6ead73079aa42636f318b495e807699601cd403a960cf059c3`이며 exact-title /
Zero-CDN checker가 통과했다.

로컬 `clang-tidy`와 `clazy`는 unavailable이고 ici의 C++ type 및 exact dead-symbol 분석은 아직
지원되지 않는다. heuristic duplicate WARN은 `6.42%`/`34` groups로 ici I4-3의 robust
duplicate backlog에 연결된다.
false-positive clone shape를 없애려고 product code를 contort하지 않는다. DiskMap은
`0.1.0`/`Unreleased`를 유지하며 D1~D3 구현·PR/exact-main evidence는 완료됐다. D4~D7
cleanup/trash/snapshot/release는 pending 범위다.

### D4. cleanup staging core

**브랜치:** `feat/diskmap-cleanup-staging`

- [ ] multi-select 후보와 예상 reclaimable bytes를 계산한다.
- [ ] 부모 directory가 선택되면 child 중복 선택을 정규화한다.
- [ ] `/`, home root, project-configured protected roots와 mount root를 거부한다.
- [ ] symlink target이 아니라 선택한 link 자체만 대상으로 삼는다.
- [ ] scan identity, current identity, size/type를 실행 직전 재검증한다.
- [ ] stale/missing/changed target은 실행 대상에서 제외하고 이유를 보여준다.
- [ ] QAbstractItemModel + QUndoStack은 staging 편집만 undo하고 실제 삭제를 가짜로 되돌리지 않는다.

### D5. trash backend와 audit

**브랜치:** `feat/diskmap-trash-workflow`

- [ ] Linux desktop trash backend를 adapter로 격리하고 capability를 탐지한다.
- [ ] command는 shell 없이 argv로 실행한다.
- [ ] dry-run review 화면에서 path, identity, reclaimable size와 위험 표시를 확인한다.
- [ ] 성공/실패/부분 성공을 target별 audit record로 남긴다.
- [ ] backend가 restore token을 제공할 때만 undo/restore를 표시한다.
- [ ] permanent delete는 초기 release 비목표로 둔다.

### D6. snapshot, growth와 duplicate candidate

**브랜치:** `feat/diskmap-snapshots`

- [ ] versioned snapshot schema와 atomic write를 제공한다.
- [ ] 두 snapshot의 new/grown/shrunk/moved candidate를 identity/path로 비교한다.
- [ ] incomplete scan끼리의 차이를 확정값처럼 표시하지 않는다.
- [ ] duplicate candidate는 size → partial hash → full hash의 점진 단계로 계산한다.
- [ ] hash 작업도 진행률, 취소와 stale identity 재검증을 사용한다.

### D7. diskmap release 완료 조건

- [ ] symlink cycle, hardlink, sparse file, mount boundary, permission error fixture 통과
- [ ] scan cancel/rescan race와 stale result 테스트 통과
- [ ] cleanup은 review와 identity revalidation 없이는 실행 불가
- [ ] recoverable trash만 지원하고 audit 결과 제공
- [ ] Qt5/Qt6 build/test와 qmake ici verify PASS
- [ ] 규모 benchmark와 safety guide 제공

---

## 9. B stream — buildscope 신규 hybrid build explorer

### B0. 제품 경계와 repository skeleton

**브랜치:** `feat/buildscope-skeleton`

사용자 시나리오:

1. `compile_commands.json` 또는 build directory를 연다.
2. source/target별 compiler, standard, defines, include path를 본다.
3. 같은 header/source가 configuration마다 왜 다르게 해석되는지 비교한다.
4. 두 build의 compile configuration drift를 diff한다.
5. 문제 finding에서 원시 command와 관련 path로 이동한다.

권장 구조:

```text
buildscope/
  pyproject.toml
  ici.toml
  python/buildscope/
  CMakeLists.txt
  include/buildscope/
  src/core/
  src/gui/
  resources/buildscope.qrc
  ui/main_window.ui
  tests/python/
  tests/cpp/
  tests/integration/
```

- Python 3.10+ backend는 compile DB ingestion, normalization, diff와 JSON contract를 담당한다.
- C++20/Qt frontend는 model, filtering, graph/detail UI를 담당한다.
- frontend가 Python 내부 구현을 embed하지 않고 versioned JSON/process contract로 연결한다.
- CMake `AUTOMOC`, `AUTOUIC`, `AUTORCC`를 모두 사용한다.
- `ici.toml type = "hybrid"`로 양 언어의 test coverage를 요구한다.

- [x] minimal Python CLI와 Qt window가 각각 독립 실행된다. (local/public and PR #31 remote evidence)
- [x] Python output을 C++ CLI 또는 GUI model이 소비하는 E2E test가 있다. (local/public and PR #31 remote evidence)
- [x] Qt5/Qt6와 Python 3.10에서 skeleton build/test가 통과한다. (local/public and PR #31 remote evidence)

**B0 당시 과거 pre-hardening local baseline (2026-09-01; 현재 B1 결과 및 PR/remote evidence 아님; superseded):** Python 3.10 backend는
`compile_commands.json`을 shell 실행 없이 64 MiB/100,000-entry bound 안에서 읽어
deterministic `buildscope.snapshot/v1`을 만든다. C++20/Qt CLI·GUI consumer와
`AUTOMOC`·`AUTOUIC`·`AUTORCC`를 사용하는 CMake skeleton은 Qt 5.15.18과 Qt 6.10.2에서
각각 CTest 4/4를 통과했고, Python producer → C++ consumer E2E도 통과했다. 같은 Python
3.10 interpreter의 `python -m` probe에서 pytest/coverage/mypy capability가 READY였고,
mypy 실제 argv는 C++ roots를 제외한 `python` root만 받아 rc0이었다. 이를
`ICI_PYTHON`으로 지정한 공개 ici v0.7.1 release asset의 cold isolated verify는 suite WARN,
`12 PASS / 1 WARN / 0 FAIL / 0 ERROR / 0 SKIP`, `9/9` tests, TEM 5.00,
line/function/branch 96.3%/100.0%/86.8%, complexity 14 PASS, compile DB 4/4 production
units·13 configurations, 총 63.37초였다. 당시 기록은 C++ type 미지원 WARN 하나를 남겼다.
이 수치는 hardening 이전의 역사 기록으로 현재 최종 결과가 아니며, BuildScope PR/remote
CI/sticky report/Pages 완료를 주장하지 않는다.

### B1. compile database Python core

**브랜치:** `feat/buildscope-compile-db`

- [x] `arguments`와 `command` entry, directory/file/output을 읽는다.
- [x] shell 실행 없이 command를 token화하고 원문도 보존한다.
- [x] compiler, language, standard, defines, include paths, sysroot, target hint를 정규화한다.
- [x] duplicate/stale/missing file과 여러 configuration을 보존한다.
- [x] project root 밖 system/vendor path를 분류한다.
- [x] versioned JSON schema와 deterministic ordering을 제공한다.
- [x] malformed/huge DB rejection과 POSIX/Windows path-separator handling을 구현한다.
- [x] Python 입력 DB 64 MiB/100,000-entry bound와 serialized snapshot/native reader 256 MiB
  bound를 구분해 적용한다.
- [x] lstat/descriptor identity·ctime hardening, alias 보호, POSIX no-follow parent-fd anchored
  atomic output와 portable fallback을 구현한다.
- [x] `--schema-version v1|v2`, `--` output stop, separated POSIX `-o`, Windows `/Fo`, Windows
  GCC path detection을 명시한다.

**B1 구현 기준 (2026-09-01):** BuildScope Python package/version metadata는 `0.2.0`이며,
producer는 additive `buildscope.snapshot/v2`를 출력한다. Python 입력 compile DB는 64 MiB/
100,000-entry, serialized snapshot과 native reader 입력은 각각 256 MiB bound다. v2는 B0 raw
fields를 보존하고 `normalized` argv/command-style/invocation-source plus compiler/language/
standard/defines/includes/sysroot/target/path/configuration, `state`, `diagnostics`를 추가한다.
`arguments`가 `command`보다 우선하고, command-only 입력은 shell 없이 POSIX 또는 Windows argv로
token화한다. 환경·glob·command substitution과 response-file expansion은 하지 않는다.
`--project-root`로 project/vendor/system scope를 정하고, native host에서만 exists/mtime을
확인하므로 foreign-platform 상태는 unknown일 수 있다. foreign Windows `project_root`는 host
filesystem을 probe하지 않고 lexical Windows form을 보존해 scope를 분류하며, dedicated scope
test가 이 경계를 검증한다.

입력은 final-name `lstat`와 지원 플랫폼의 no-follow descriptor identity를 size/mtime/ctime과
함께 read 전후 검사하고 final symlink를 거부한다. `--output`은 DB self/hardlink/symlink alias를 거부한다.
POSIX output은 no-follow parent fd에 anchored한 exclusive mode-0600 temp, flush/fsync,
fd-relative rename으로 atomic race 경계를 제공한다. portable fallback은 resolved real parent를
pin한 채 temp/cleanup/replace와 parent identity/alias 검사를 수행한다. 생성한 temp는 교체 전에
`lstat`로 생성 당시 identity와 regular-file을 재검증하고 symlink resolve를 하지 않는다. POSIX
dir-fd 경로와 같은 atomic race guarantee를 주장하지 않는다.

CLI는 `--schema-version v1|v2`를 명시적으로 지원하고 v1은 raw compatibility projection이다.
metadata/output scan은 `--`에서 중단되며 POSIX `-o`는 separated form만, Windows `/Fo`는
separated/joined form을 지원한다. MSVC option matching은 case-sensitive하며 `/Fo`와 `/Fo:`의
separated/joined form만 인식해 유사 switch false positive를 막는다. drive/UNC/backslash
compiler path도 Windows로 판정해 `C:\\MinGW\\bin\\g++.exe` 같은 GCC path를 인식한다. 중복은 source+configuration 범위이고
`source_configuration_count`는 source별 unique configuration 수다. configuration digest는
동일 source의 recorded invocation identity이지 relocation-stable semantic diff가 아니며,
semantic comparison은 B4 소유다. Source aggregation key는 `command_style`+normalized path로
Python과 C++ native reader가 일치한다.

v1 raw keys를 유지해 additive-field를 허용하는 v1 consumer는 계속 사용할 수 있다. B1 native
contract reader에서 “strict”는 legacy v1 core validation과 v2 bounded/core/cross-entry
validation을 뜻한다. v1은 exactly-one invocation, legacy empty argv compatibility, extension-key
tolerance를 유지하고, v2는 duplicate JSON key rejection, required/unknown fields, field/item
bounds, enum/core shapes, invocation-source/argv, include order, `entry_index`/duplicate/
source-configuration-count cross-entry consistency를 검증한다. command-only normalized argv를
raw command와 재토큰화해 full semantic attestation하지는 않는다. native reader는 snapshot final
symlink도 읽기 전에 거부한다. 공개 Draft 2020-12
`buildscope-snapshot-v1.schema.json` 및 `buildscope-snapshot-v2.schema.json`은 source tree와
pure wheel에 함께 포함된다. v2 `producer.version`의 schema `maxLength` 1 MiB와 native reader
문자열 bound도 일치한다. v2 normalized model/UI 전환은 B2 범위다. B1 구현과 PR #31
remote integration evidence는 complete이며, B2 구현/local/remote verification도 아래와 같이
완료됐다. B3 include explanation은 구현과 원격 증거까지 완료되어 `main`의 `0.4.0` candidate로
기록한다. **B1-era transition (historical):** B4 configuration diff implementation/local
evidence가 feature branch에서 완료됐고, 당시 B4 PR/remote evidence와 B5 hybrid release
integration은 미완료였다. ici I3 target-by-target same-basename 비교는 완료됐다.

**B1 final local/public evidence (2026-09-01):** public
`ici v0.7.1` cold verification은 표준 `sha256sum --check ici.pyz.sha256`가 통과했고 suite
`WARN`을 기록했다. `13 engines = 11 PASS / 2 WARN / 0 FAIL / 0 ERROR / 0 SKIP`, total
`71.09s` (raw `71.08794903755188s`), tests `45/45`, line/function/branch
`95.2% / 100.0% / 84.3%`, TEM `5.00`이었다.
`compile_db`는 `7/7` production units·`16` configurations·`0` failures/warnings, complexity는
max `13`/`140` functions·`0` issues, exception은 `PASS`·`0`이었다. Duplication은 `WARN`
`8.8%` (raw `8.77914951989026`)/`25` groups/`56` findings였고, type WARN은 unsupported
analysis인 `7` C++ sources뿐이며
external dependencies는 `0`이다. HTML은 `489,978` bytes, SHA-256
`538bdde8fae8cc769d212799e80ffeae1e39069662b214b19efb3d35a66f3257`, title은
`ici Verification Report — buildscope`였다. Line inventory는 `2,798` total, `2,453` code,
`345` blank lines across `19` files다. 현재 source line counts는 `contract.cpp` 111,
`contract_json_guard.cpp` 125, `contract_parser.cpp` 382, `contract_parser_v2.cpp` 333이고,
Python tests는 `41` (CTest aggregate `45`)이다. Local cold HTML은 `489,978` bytes,
SHA-256 `538bdde8fae8cc769d212799e80ffeae1e39069662b214b19efb3d35a66f3257`이며 hosted HTML과
별개다.

**B1 PR #31 remote integration evidence:** [PR #31](https://github.com/jihoon22-lee/toy-projects/pull/31)의
initial implementation/docs head는 `1ff08fe5d2accddc0e9107113eb83dd86bd6d50a`이고,
[workflow run 33439733990](https://github.com/jihoon22-lee/toy-projects/actions/runs/33439733990)의
동적 matrix 15 checks가 모두 SUCCESS였다: 3 ici verify, Qt5/Qt6 GUI 6, manifest, benchmark
smoke 3, `Publish Reports & Sticky Comment`, `Merge Gate`. [Sticky comment](https://github.com/jihoon22-lee/toy-projects/pull/31#issuecomment-5484640868)는
marker 1개와 정확히 3개 project link를 포함하고 BuildScope `11 PASS / 2 WARN`, TEM `5.00`,
`45/45`, `7/7`, `16` configurations, complexity max `13`/`140`을 기록했다.

독립 Pages audit는 세 hosted report 모두 HTTP 200, `text/html`, external dependency `0`을
확인했다.

| Project | URL | Bytes | SHA-256 | Title |
|---|---|---:|---|---|
| BuildScope | [buildscope/pr/31](https://jihoon22-lee.github.io/toy-projects/buildscope/pr/31/) | 493,453 | `643a3e9e5c45a1512244cc90940146192399471621eac1a2dcb581cc534089c2` | `ici Verification Report — buildscope` |
| diskmap | [diskmap/pr/31](https://jihoon22-lee.github.io/toy-projects/diskmap/pr/31/) | 311,846 | `a8806808638c584312943d2551c1668a407c45830311de07cb0eed30d15e6924` | `ici Verification Report — diskmap` |
| loglens | [loglens/pr/31](https://jihoon22-lee.github.io/toy-projects/loglens/pr/31/) | 446,796 | `56f3b2d54ed2a05ebf100313b4d9447553e9c6fb9c85f7e7adce8eccc838dc4f` | `ici Verification Report — loglens` |

B1 implementation과 PR #31 remote integration evidence는 complete다. B2 implementation/local 및
remote verification도 complete다. B3 implementation과 원격 evidence도 아래에 기록한다. **B1-era
roadmap transition (historical):** B4 configuration diff implementation/local evidence는 아래에
기록하고, 당시 B4 PR/remote와 B5 hybrid release integration은 pending이었다. ici I3
target-by-target same-basename comparison은 완료됐다.

### B2. C++ model과 Qt UI — implementation/local/remote verification 완료

**브랜치:** `feat/buildscope-qt-explorer`

- [x] B1 legacy-v1/core 및 v2 bounded/core/cross-entry reader가 수락한 결과와 validation error location을 normalized
  C++ model/UI에 연결한다.
- [x] source/target tree, command detail, define/include tables를 제공한다.
- [x] filter/search와 missing/stale entry 표시를 제공한다.
- [x] `.ui`로 MainWindow layout, `.qrc`로 local icons/theme asset을 사용한다.
- [x] QAbstractItemModelTester와 MainWindow shell test를 추가한다.
- [x] 10만 entry synthetic DB를 lazy model로 처리하고 성능을 측정한다.

**B2 local candidate (2026-09-01):** BuildScope `0.3.0`은 v2 snapshot을 소스별로 묶는
`QAbstractItemModel`과 configuration child index를 제공한다. child object tree를 복제하지
않고 snapshot entry index를 `QModelIndex` identity로 사용한다. source status는
missing > stale > present > unknown 순으로 집계하고 target/compiler/standard/configuration,
structured JSON argv, 별도의 raw command, defines/includes/diagnostics를 detail pane에 표시한다.
recursive case-insensitive filter는 source와 모든 구조화 필드를 검색한다. Qt `.ui` splitter/tab
layout과 `.qrc`의 네 로컬 status SVG를 사용하며 외부 UI resource가 없다. v1 snapshot은 raw
compatibility view를 유지한다.

읽는 동안 파일이 삭제·교체·변경되거나 열린 descriptor가 닫히는 경계를 deterministic hook으로
재현해 B1의 post-read identity attestation도 보강했다. public `ici v0.8.0` release asset 검증은
suite WARN(type/dup only), `46/46` tests, line/function/branch
`94.5% / 99.5% / 83.9%`, TEM `4.98`, compile DB `8/8` production units·`19`
configurations, complexity max `14`/`196` functions·0 issues였다. `contract.cpp` module coverage는
`89.9%`로 올라 검사 finding을 닫았다. HTML은 558,384 bytes, SHA-256
`cdaefa06c52de696e0340b698e37b88dde199bc5a7bd2bbba27421618f44e444`, title은
`ici Verification Report — buildscope`, external resource reference는 0개다.

Qt5 5.15.18과 Qt6 6.10.2의 전체 CTest는 benchmark를 포함해 각각 6/6 PASS였다. Qt6 Release
100,000-entry/25,000-source 측정은 model build 45 ms, recursive filter 1,071 ms, peak RSS
132,612 KiB로 각 10,000 ms budget을 통과했다. 이 수치는 local candidate evidence이며, 아래
PR/main 원격 증거로 B2 전체가 complete다.

**B2 remote integration evidence (2026-09-01):** PR #32 head
`41472a66e69477fde7a71fe78c3ae9e47ba7f292`는 main에
`51a3480677a740475857dd92dd5a5a9373a287a4`로 squash-merge됐다. [PR run
`33454143021`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33454143021)의 16개 check가
모두 SUCCESS였고, [sticky comment #5486637533](https://github.com/jihoon22-lee/toy-projects/pull/32#issuecomment-5486637533)는
marker 1개와 link 3개를 포함한다. PR BuildScope report는 `46/46`, branch `84.2%`, TEM `4.98`,
compile DB `8/8` production units·`19` configurations, complexity max `14`/`196` functions였다.
PR benchmark는 model `53 ms`, filter `1,518 ms`, summary JSON SHA-256
`af7162b7603d558da6e7bc49d7bf5a80f546f412b7076992ded5e15739024db7`; exact-main run
`33454634202`는 SUCCESS였고 Report job은 expected skipped였다. main benchmark는 model `58 ms`,
filter `1,527 ms`, summary JSON SHA-256
`247c0b33095e0a09e97a289af556eae30f47f4f5c4136c530e3d6ca0018ae2d2`다.

세 hosted report는 HTTP 200·`text/html`, expected title, external resource reference 0개로
독립 확인됐다.

| Project | URL | Bytes | SHA-256 | Title |
|---|---|---:|---|---|
| BuildScope | [buildscope/pr/32](https://jihoon22-lee.github.io/toy-projects/buildscope/pr/32/) | 562,234 | `f15d18fe42ac172385e682ceb49e4b6d6f1d9bbfcc0ead301c11d1ee049c4c82` | `ici Verification Report — buildscope` |
| diskmap | [diskmap/pr/32](https://jihoon22-lee.github.io/toy-projects/diskmap/pr/32/) | 311,846 | `752f07251bc38285ea1633f5df879985131963e4b99f90532722eaedc9be1802` | `ici Verification Report — diskmap` |
| loglens | [loglens/pr/32](https://jihoon22-lee.github.io/toy-projects/loglens/pr/32/) | 446,791 | `7b2669fb7de82ada30bfdf28a2d82533f5566ad92779ea08c90528e188ea582b` | `ici Verification Report — loglens` |

### B3. include explanation (historical candidate folded into the 0.5.0 boundary)

**브랜치:** `feat/buildscope-include-explain`

BuildScope의 historical `0.4.0` main candidate는 normalized compile DB entry를 바탕으로 include
explanation을 선택적으로 생성한다. CLI는 `--include-analysis estimate|compiler`를 제공하며,
analysis를 생략하면 기존 normalized `buildscope.snapshot/v2`가 기본이다. `--include-analysis`
는 v3를 암시하고, `--schema-version v3`만 지정하면 `estimate`가 선택된다. v1/v2와 analysis
조합은 거부되어 기존 raw compatibility projection과 v2 consumer가 조용히 깨지지 않는다.

- [x] normalized compile entry에서 bounded include explanation을 생성한다. `estimate`는
  source scan으로 lexical 후보를 계산하고, `compiler`는 compiler `-H` include trace를
  수집해 v3에 저장한다.
- [x] v3 strict/self-contained schema와 C++ native reader를 제공한다. root, entry,
  `include_analysis`, edge, search, diagnostic 필드를 required/enum/bounds로 검증하며,
  분석 불가 entry도 `evidence: unavailable` warning record를 유지한다.
- [x] quote include는 current directory와 quote roots, 이후 include/framework, system,
  after roots를 recorded order로 검색하고, selected path·ordered candidates·same-basename
  alternatives를 기록한다.
- [x] edge classification으로 project/vendor/generated/system/missing/unresolved header를
  구분한다. generated는 build/out/.build/cmake-build-* compilation root 아래를, system은
  project root 밖을 기준으로 하며, missing compiler diagnostic과 unresolved estimate를
  별도로 보존한다.
- [x] compiler-measured resolution과 source-scan location evidence를 분리한다. compiler
  trace가 resolved edge를 정하고 source scan이 parent:line/delimiter를 복원하며, missing
  diagnostic은 compiler-diagnostic location evidence를 사용한다.
- [x] shell 없는 allowlisted replay boundary를 적용한다. 직접 승인된 system GCC/Clang
  driver만 `-E -H`로 실행하고, response file/stdin/extra input/plugin/linker escape를 거부하며
  argv/trace/edge/source/unit/time bounds를 적용한다.
- [x] Qt Include Edges UI에서 provenance, search order, collision alternatives와 edge 상세를
  제공하고, edge 더블클릭/Open Source Location으로 parent source 위치를 열며 Compilation
  Command로 command view로 이동한다.

**B3 historical local candidate evidence (2026-09-01; before PR #34):**

- [x] Python 3.10 pytest `57/57` PASS, Ruff check + format `14 files` PASS, mypy `11 source
  files` PASS (replay policy, estimate/compiler, v3 projection, bounded failure paths 포함).
- [x] Qt 6.10.2 Release CMake/CTest `6/6` PASS와 Qt 5.15.18 Release CMake/CTest `6/6` PASS
  (v3 contract, GUI edge navigation, hybrid include contract 포함).
- [x] checksum-verified ici v0.8.0 release asset의 최종 no-cache local validation은 `Suite
  WARN`(검증 통과), engines `11 PASS / 2 WARN / 0 FAIL / 0 ERROR / 0 SKIP`, `63/63` tests,
  line/function/branch `92.6% / 98.9% / 79.1%`, TEM `4.94`, compile_db `8/8` production
  units·`19` configurations·`0` failures/warnings, complexity `PASS` max `14`/`251` functions·0
  issues, total `34.71s`였다. WARN은 C++ type unsupported와 dup뿐이며 `/tmp` HTML `851,656`
  bytes, SHA-256 `07d25971e04ed6a4aece36724ce8cf5e3c0548b7c382941a810454d8521c3e34`, exact title,
  external refs `0`로 확인했다. 이는 B3 PR/remote Pages evidence와 구분되는 local evidence다.
- [x] 100,000 entries/25,000 sources benchmark는 model `61 ms`, filter `1,126 ms`, budget
  `10,000 ms`, correctness `true`였고, pure wheel에 `compiler_replay.py`와 v3 schema가 포함됐다.
- [x] B3 PR/remote CI와 hosted report. [PR #34](https://github.com/jihoon22-lee/toy-projects/pull/34)의
  feature head `c3835cd4b0c859c38ae0f4afbdb20aae970515dc`에 대한 [run
  `33459294092`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33459294092)은 `Merge Gate`와
  `Publish Reports & Sticky Comment`를 포함한 16개 check 모두 SUCCESS였다. [sticky comment
  `#5487386460`](https://github.com/jihoon22-lee/toy-projects/pull/34#issuecomment-5487386460)는 marker
  정확히 1개와 project link 정확히 3개를 포함한다. BuildScope report는 `WARN`, `11 PASS / 2 WARN /
  0 FAIL / 0 ERROR / 0 SKIP`, TEM `4.94`, tests `63/63`, line/function/branch `92.7% / 98.9% /
  79.5%`였고, diskmap/loglens는 `PASS`, TEM `4.92`/`4.80`이었다.
- [x] PR benchmark는 `100,000` entries/`25,000` sources, model `118 ms`, filter `1,424 ms`,
  budget `10,000 ms`, correctness `true`였다. 세 PR Pages report는 HTTP 200 `text/html`, exact
  title, external attributes/CSS references `0`으로 독립 확인됐다.

| Project | URL | Bytes | SHA-256 | Title |
|---|---|---:|---|---|
| BuildScope | [buildscope/pr/34](https://jihoon22-lee.github.io/toy-projects/buildscope/pr/34/) | 858,143 | `e0b9c9ece1fb7268aa519bd0a4c62fd3da7c44a52b2efe6121393474d3ad36d4` | `ici Verification Report — buildscope` |
| diskmap | [diskmap/pr/34](https://jihoon22-lee.github.io/toy-projects/diskmap/pr/34/) | 311,847 | `8a6b01544b99eee6f0c2b95758f81395032f2b67a7c1a600879447cf7fb5f3bf` | `ici Verification Report — diskmap` |
| loglens | [loglens/pr/34](https://jihoon22-lee.github.io/toy-projects/loglens/pr/34/) | 446,786 | `1144759ef7e1b83ef7bd23f7bcfe9d02b05430a37af372350ec3b6e26d6c7ac7` | `ici Verification Report — loglens` |

- [x] PR #34는 `main`에 `9cce2699606e58ed67c3dac46f60dc7bf113bb60`으로 squash-merge됐고 branch는
  삭제됐다. 같은 head의 exact-main [CI run `33459591250`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33459591250)와
  [Dependency Graph run `33459594605`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33459594605)가
  성공했다. Push에서는 PR publish가 올바르게 skip됐고 applicable jobs와 `Merge Gate`는 성공했다.

B3 implementation과 remote evidence는 complete이고 code는 `main`에 shipped됐다. Historical
`0.4.0` candidate는 별도 stable release로 발행하지 않고 `0.5.0` boundary에 포함했다. ici I3
cross-repository comparison은 완료됐으며 B4 configuration diff implementation과
PR/remote/hosted/merged-main evidence도 complete다.

### B4. configuration diff (implementation complete; included in the 0.5.0 boundary)

**코드 기준 브랜치:** `main` (PR #36 squash-merge 완료; feature branch 삭제)

**현재 상태:** B4 implementation은 `feat/buildscope-config-diff`에서 개발된 뒤 PR #36로 `main`의
`0.5.0` boundary에 반영됐고 PR/remote/hosted/merged-main evidence까지 complete다. 입력은 두 개의
raw `compile_commands.json`만 받고,
snapshot v1/v2/v3는 기존 producer compatibility 경계로 유지한다. 출력은 strict
`buildscope.diff/v1`이며 Python producer, C++ parser/model, Qt GUI와 hybrid contract가 같은
의미론을 사용한다.

- [x] 두 raw compile DB의 added/removed/changed/moved translation unit을 비교하고, 보수적인
  basename/role move heuristic과 중복 configuration one-to-one pairing을 적용한다.
- [x] standard, language, ordered define/undefine, include kind/order, compiler
  family/name/path/wrappers, launcher, target/sysroot와 residual flag drift를 구조화한다.
- [x] project-root-relative lexical normalization으로 noisy absolute build path와 output
  filename/build-directory 차이를 정책상 제외한다. Windows path/case와 slash-aware suppression
  glob도 계약에 포함한다.
- [x] suppression/ignore rule과 canonical deterministic export를 제공하고, malformed/oversized/
  duplicate-key input 및 symlink/TOCTOU/alias 경계를 실패로 처리한다.
- [x] strict schema validation과 native `buildscope-cli --diff`/Qt issues-first diff UI를
  Python-to-C++ byte-identical hybrid fixture로 연결한다.

**로컬 증거:** Python 단위/CLI `83/83`, Ruff check/format `19 files`, mypy `15 source files`,
기본 Qt5 5.15.18 및 Qt6 6.10.2 Release CMake/CTest 각각 `9/9`
(`BUILDSCOPE_BUILD_BENCHMARKS=ON`이면 `10/10`)이 PASS다. pure
`buildscope-0.5.0-py3-none-any` wheel에는 snapshot v1/v2/v3와 diff v1 schema가 포함되고
native extension은 없다. checksum-validated pre-PR historical local ici v0.9.0 uncached deep
suite는 `WARN`, 14 engines = 11 PASS / 3 WARN / 0 FAIL / 0 ERROR / 0 SKIP, `92/92` tests,
line/function/branch `93.5% / 99.0% / 76.7%`, sanitizer PASS, compile DB `12/12` production
units와 `27` configurations, TEM `4.95/5.0`이다. Zero-CDN HTML은 1,235,505 bytes, SHA-256
`0c98a38b27e928df2c60dcadff9ecc3daa1072cb620354d9f4a9fe8d9b987f80`이다. 이는 B4 당시 v0.9.1
remote evidence와 별개인 historical local evidence다.

**B4 remote evidence (2026-09-01):** [PR #36](https://github.com/jihoon22-lee/toy-projects/pull/36)의
head `ce64613263f0c4358579012aab135e0b23341a0e`에서 [run `33485837830`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33485837830)이
ici `v0.9.1`로 `16/16` checks를 모두 성공시켰다. BuildScope report는 `WARN` (`10 PASS / 3 WARN /
0 FAIL / 0 ERROR / 0 SKIP`), lint WARN (49 warnings), `92/92` tests, line/function/branch
`93.5% / 99.0% / 77.3%`, sanitizer PASS, compile DB `12/12` production units·`27`
configurations, TEM `4.95/5.0`이었다. 100,000 entries / 25,000 sources benchmark는 model `65 ms`,
filter `1,602 ms`, filtered sources `1`, budget `10,000 ms`, correctness `true`를 기록했다.
[Sticky comment #5489976814](https://github.com/jihoon22-lee/toy-projects/pull/36#issuecomment-5489976814)는
marker 1개와 current-run link 및 hosted report link 3개를 포함한다.

| Project | Hosted report | Bytes | SHA-256 |
|---|---|---:|---|
| BuildScope | [buildscope/pr/36](https://jihoon22-lee.github.io/toy-projects/buildscope/pr/36/) | 1,319,378 | `5dc517d3ec8324cb1aedb6e611120ac9ae2951e27851cbc6b0e28303d02c5d43` |
| diskmap | [diskmap/pr/36](https://jihoon22-lee.github.io/toy-projects/diskmap/pr/36/) | 337,554 | `39a52d1e3d5b9eed6bbc1ec5253f1bf837deb4a7bb33ed2f2ef3b32df0f90e0e` |
| loglens | [loglens/pr/36](https://jihoon22-lee.github.io/toy-projects/loglens/pr/36/) | 492,746 | `0480f07aea266c0777403ac37ebf90d4acd10f84b04e49aa2a9ccf99a6153007` |

세 Pages report는 HTTP 200, exact title, external resource reference 0개였다. 따라서 B4
implementation과 PR/remote/hosted evidence는 complete다. PR #36는 `main`에
`590899a0a9430e9ce35162b301bfef5d7dfc78a4`로 squash-merge됐고 feature branch는 삭제됐다. Exact-main
[CI run `33488169769`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33488169769)는 14개
선행 job과 `Merge Gate`가 모두 성공했으며 PR-only publisher는 expected skip이었다.
[Dependency Graph run `33488174425`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33488174425)도
같은 head에서 성공했다. 이 B4 merge 시점에는 `0.5.0` product release artifact와 B5 hybrid
release integration이 pending/not started였고, 이후 local B5 candidate는 다음 절에 기록한다.

### B5. hybrid integration과 release (historical pre-publication snapshot; publication complete)

**선행 조건:** B3/B4 implementation과 ici I3 compile-context 기능이 public release로 제공되어야
한다. B4 implementation과 PR/remote/hosted/merged-main evidence는 complete다. 2026-09-02 현재
standalone packaging, native install layout, sample/tutorial, public ici pin과 release workflow
contract가 `main`에 반영됐고 B5 remote acceptance도 완료됐다. 이 pre-publication snapshot에서
`0.5.0`의 버전 경계는 그대로 유지했으며, stable release를 닫기 위해 annotated tag, GitHub
Release, 9개 public asset 게시와 독립 checksum/provenance audit을 남겨 두었다. 로컬에서 unavailable이었던 도구 상태는 historical
local evidence로 보존하고, remote preflight matrix와 B5 report contract의 결과를 현재 acceptance
근거로 삼는다.

- [x] standalone builder가 Python/pyproject/CMake/ici version surface와 schema inventory를
  검증하고 fixed-metadata `buildscope.pyz`를 재현 가능하게 생성한다.
- [x] CMake install rule이 native CLI/GUI, docs, examples, schemas를 bundle layout으로 설치한다.
- [x] sample CMake/qmake databases와 [quickstart](../../../buildscope/docs/quickstart.md)를
  제공한다. quickstart는 Python pyz/wheel → JSON → native GUI/CLI 흐름과 qmake capture 제한을
  설명한다.
- [x] local ici report에서 CMake compile DB `12/12` production TU·`27` configurations·`0` issues와
  Qt codegen exact inputs `3`, MOC `1`, UIC `1`, RCC `1`, Qt6 units `12`를 확인한다.
- [x] Python analyzer → JSON → C++ consumer release handoff를 remote ici integration gate로 검증
- [x] remote runner에서 tool-backed deep-profile 경로와 Qt5/Qt6, Python `3.10/3.14` release
  matrix, generated MOC/UIC/RCC/offscreen smoke, wheel/pyz/native handoff를 검증
- [x] PR CI, sticky HTML comment, PR Pages와 exact-main/green Merge Gate provenance를 모두 확인
- [x] trusted main Pages를 `main` 산출물과 byte-identical한 HTTP 200, exact title, Zero-CDN으로 확인
- [x] annotated `buildscope-v0.5.0` tag, GitHub Release, 9개 public release asset 게시와 독립
  checksum/provenance audit (release ID `380863869`, workflow run `33566464110`)

**B5 local ici evidence (2026-09-01, historical):** public ici `v0.10.0` asset의 literal SHA-256 pin
`6d5f8c008b3b5393a61b2c1a418124eb66393c9eaab0abbb7d1c7922162bed9b`과
`ICI_PYTHON=/tmp/toy-b5-py310/bin/python`으로 deep/no-cache를 실행했다. `Suite WARN`, 14 engines =
`11 PASS / 3 WARN / 0 FAIL / 0 ERROR / 0 SKIP`, tests `96/96`, line/function/branch
`93.4% / 99.0% / 76.7%`, TEM `4.95`였다. JSON은 2,873,207 bytes / SHA-256
`ea5fce118e6edad8fa5af24c821663e4290805570377e9b7c190de8da1029612`, Zero-CDN HTML은
1,264,867 bytes / SHA-256 `4e12e77ee11f98b2d5bb146bd3d08252b088083726e6f11235e25a449471a565`,
exact title `ici Verification Report — buildscope`, external refs `0`였다. local clang-tidy/clazy
unavailable은 당시 환경의 historical limitation이며, remote preflight matrix와 B5 report contract에서
release-runner 검증 경계를 확인했다.

**B5 release contract (historical pre-publication; CI preflight validated):** `.github/workflows/buildscope-release.yml`은
고정 annotated `buildscope-vX.Y.Z` tag의 peeled commit이 exact `origin/main`과 green `Merge Gate`를
가리키는지 확인하고, Python `3.10/3.14` 및 Qt `5/6` Release/CTest matrix, generated MOC/UIC/RCC
path checks, Qt6 Linux x86_64 bundle, wheel/pyz → native CLI handoff, ici v0.10.2
sidecar/download/API digest와 `SHA256SUMS`를 검사한다. 최신 `ci/check_buildscope_merge_gate.py`는
exact SHA의 newest `Merge Gate` check-run을 positive ID로 선택하고 GitHub Actions app 및
completed/success를 요구한 뒤 독립 Actions run의 ID, repository/head repository, SHA, workflow
name/path/event/status/conclusion과 canonical URL을 검증한다. Release API의 `target_commitish`
equality는 사용하지 않고 annotated tag peel을 authoritative proof로 삼는다. 예정된 assets는
`buildscope.pyz`, `buildscope.pyz.sha256`, `buildscope-<version>-py3-none-any.whl`,
`buildscope-<version>.tar.gz`, `buildscope-ici-deep.{json,html}`, `buildscope-provenance.json`,
`buildscope-<version>-linux-x86_64.tar.gz`, `SHA256SUMS`다. PR/remote/PR Pages와 trusted main
Pages를 포함한 acceptance는 완료됐지만 tag, GitHub Release와 9개 public asset의 게시·독립
checksum/provenance audit은 아직 pending이다.

**B5 publication hardening (2026-09-02 pre-publication snapshot; historical):** tag workflow는 authenticated paginated slot을
fail-closed로 검사해 빈 슬롯에서만 fixed numeric release ID의 private draft를 만들고, 기존 final은
mutation 없는 audit-only 경로로 처리한다. 기존 draft·중복·모호한 slot은 중단한다. 새 draft body는
repository/run/peeled target SHA를 담은 terminal current-run owner marker로 끝나며, 모호한 생성
응답 recovery는 exact owner-marked zero-asset private draft 중 expected body digest까지 일치하는
하나에만 허용된다. workflow는
`RELEASE_NOTES.md`를 정규화해 별도 final body와 owner-marker draft body 파일을 만들고 각 body의
정확한 UTF-8 SHA-256을 계산한다. draft digest는 create/upload/prepublish/failure-report에서,
final digest는 publish/final audit에서 재검증한다. 정확히 9개 asset은 해당 release ID의 binary
upload endpoint에 no-clobber로 전송되고, 20초 connect/300초 transfer
bound와 HTTP 201/uploaded 응답을 요구한다. Release API의 `target_commitish`는 비교하지 않으며
annotated tag peel이 target proof다. fresh API-ID download를 통해 manifest/sidecar,
payload/archive/schema/provenance, B5 JSON, Zero-CDN HTML과 pyz version을 공개 직전에 다시
검사하고, ZIP은 `ZipFile` 이전에 bounded EOCD/central-directory preflight를 거친다. PATCH 응답이
모호하면 동일 ID의 exact final/draft만 reconciliation하고, write-token publish 단계에서는
다운로드한 원격 BuildScope pyz를 실행하지 않는다. 공개 후에는 9개 final byte를 독립적으로
재다운로드해 current `dist`와 모두 byte-compare하며, 기존 final audit-only mode에도 적용한다.
다운로드 후 ID/tag metadata, assets, peeled tag를 다시 fetch한다. 실패한 owned draft는 자동 삭제하지
않고 명시적 수동 검토를 위해 보존한다. empty-slot failure-report 단계에서 create output ID가
유실되어도 paginated listing에서 exact current-run-owned zero-asset draft 중 expected body
digest까지 일치하는 항목을 복구해 보고·보존만 수행한다. 공개 tag/Release와 최종 독립 감사는 여전히 pending이다.
현재 dependency-free CI helper discovery suite는 Python 3.10/3.14 각각 `145/145`로 통과했고,
Ruff check/format, mypy, actionlint도 통과했다. 이는 현재 구현에 대한 사전 검증이며 공개
release evidence가 아니다.

**B5 remote acceptance (2026-09-02):** PR #38의 public ici pin과 hybrid release integration은
head `3ba645eae5181698e1272729dddaa8a72189b067`, [run `33545168957`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33545168957),
[sticky comment `#5494648837`](https://github.com/jihoon22-lee/toy-projects/pull/38#issuecomment-5494648837),
merge `069a3a86c0164a1d2a88710f9c3c48a398c8087e`, exact-main [run `33546046566`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33546046566)으로
검증·병합됐다. PR #39의 trusted main report publisher는 head
`b861ff5b4cc0314aae5ec9f6dab905648233216d`, [run `33548626203`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33548626203),
[sticky comment `#5499184834`](https://github.com/jihoon22-lee/toy-projects/pull/39#issuecomment-5499184834),
merge `c80e922f0d0911019cfa8b5c67a8b654c556a68c`, exact-main [run `33549475034`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33549475034)으로
검증·병합됐다. 현재 CI와 deep handoff가 사용하는 public ici `v0.10.2` `ici.pyz`의 SHA-256은
`8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4`다.

Exact-main run `33549475034` 시점의 trusted main Pages는 모두 HTTP 200 `text/html`, exact title,
Zero-CDN이며 그 실행의 산출물과 byte-identical했다.

| Project | URL | Bytes | SHA-256 |
|---|---|---:|---|
| BuildScope | [buildscope/main](https://jihoon22-lee.github.io/toy-projects/buildscope/main/) | 1,349,088 | `dd7ec9b49281875f812b9a9c6b4e18a028051936a37441f19d907098c05dcc65` |
| diskmap | [diskmap/main](https://jihoon22-lee.github.io/toy-projects/diskmap/main/) | 339,929 | `1bd6ebdce2206e4538fb28c20c17f2d504f1a160e15f5d8d4e2923b15b399e65` |
| loglens | [loglens/main](https://jihoon22-lee.github.io/toy-projects/loglens/main/) | 495,237 | `11e4c78eed957e237bcea8ec5ea64c3894751eafbe639c454e47ab0acd70df96` |

이 B5 candidate 문단은 publication 이전의 historical snapshot이다. 당시 B5 candidate의 버전
경계는 `0.5.0`으로 유지했으며, candidate 기록이나 ici pin/CI 변경만으로 stable 또는 다음 버전을
선언하지 않는다는 정책을 확인했다. 당시에는 annotated tag와 public release asset audit이 pending
이었지만, 현재 상태는 아래 `B5 publication closeout`에서 닫혔다. `0.5.0` 이후 작업은 4.2의
comparable checkpoint가 닫힐 때까지 `Unreleased`에 남긴다.

### B5 publication closeout (2026-09-02 KST)

The public [BuildScope 0.5.0 release](https://github.com/jihoon22-lee/toy-projects/releases/tag/buildscope-v0.5.0)
is release ID `380863869`, published at `2026-09-01T22:36:42Z`, with `draft=false`,
`prerelease=false`, and `immutable=true`. Workflow [run `33566464110`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33566464110)
completed successfully on `push` at exact main SHA
`fda8b5fb068b68c04c8c40e297812fbe79cee3da`. Annotated tag object
`dcaaf83a5842f6d7fc6c47e3b212e26b9528c342` peels to that commit. The final body SHA-256 is
`9e58639c280655bf50b510ef676bb3e5f458cf2021c3c6c6b24c3b625945dd3b`; Merge Gate is job
`100050176790` in run `33565542193`. Immutable releases are enabled and active ruleset
`22049711` (`buildscope-release-tags`) blocks update/deletion for `refs/tags/buildscope-v*`
with no bypass actor. Exactly nine assets were fresh-downloaded and audited; the canonical
[BuildScope README](../../../buildscope/README.md#buildscope-050-publication-evidence-2026-09-02-kst)
contains their names, bytes, and SHA-256 digests plus the final ici v0.10.2 deep-report evidence.

The B0~B5 release checkpoint is now complete. The product version remains `0.5.0`; subsequent
changes stay in `Unreleased` until a new cohesive checkpoint satisfies the same release policy.

---

## 10. E stream — envlens 신규 pure-Python environment explorer

### E0. 제품 범위

**초기 브랜치:** `feat/envlens-skeleton` · **현재 snapshot 구현:** `feat/envlens-snapshot`

envlens는 네트워크 없이 다음 질문에 답한다.

- 두 Python interpreter/environment에는 어떤 package/version 차이가 있는가?
- project `requires-python`과 대상 interpreter가 맞는가?
- entry point, package metadata, wheel tag 관점에서 배포 가능한가?
- import smoke가 어느 interpreter에서 왜 실패하는가?

권장 구조:

```text
envlens/
  pyproject.toml
  ici.toml
  src/envlens/
  tests/
  tests/data/
```

- Python 3.10 하한, typed src layout, pure-Python wheel을 사용한다.
- core에는 stdlib `importlib.metadata`, `subprocess`, `sysconfig`, `json`을 우선한다.
- 필요 dependency는 pure wheel인지 ici와 CI에서 확인한다.
- CLI와 importable library를 함께 제공한다.

현재 snapshot 구현은 이 pure-Python skeleton과 CLI/library entry point를 포함한다. 한 interpreter의
오프라인 inventory와 deterministic evidence까지만 이번 slice의 범위이며, 두 환경의 비교·호환성
판정과 project/runtime smoke는 E2/E3로 남긴다. E0~E4 전체 checkpoint나 stable release를 닫은
것으로 해석하지 않는다.

### E1. environment snapshot — implementation and exact-main evidence complete

**브랜치:** `feat/envlens-snapshot`

- [x] interpreter를 shell 없이 argv로 실행해 identity와 sysconfig를 JSON으로 수집한다.
- [x] installed distribution, version, Requires-Python, dependency, entry point와 location을 기록한다.
- [x] snapshot schema version, source identity와 timestamp를 분리한다.
- [x] secret-bearing environment variables와 user path를 기본 redaction한다.
- [x] malformed metadata와 permission error를 distribution별로 보존한다.
- [x] atomic JSON write와 deterministic output을 테스트한다.

고정된 `python -c` probe는 명시적인 executable path를 `shell=False`로 실행한다. POSIX session/
process group과 Windows process group을 사용해 timeout 또는 inherited-pipe cleanup에서 descendant를
bounded하게 종료하며, stdout 8 MiB·stderr 보존 64 KiB·기본 timeout 10초 경계를 둔다. strict
`envlens.snapshot/v1`은 interpreter identity/sysconfig, redacted environment, normalized
distribution metadata, `complete`/`partial` collection accounting을 담고, object와 unordered
collection을 정렬해 canonical JSON을 만든다. `captured_at`은 UTC second-precision evidence로
source identity와 분리한다.

기본 redaction은 target/host home path를 `<USER_HOME>`으로 바꾸고, secret-bearing environment
name을 `<REDACTED>`로 치환한다. `_URL`/`_URI`와 token/password/API/access/private-key/auth/
cookie/credential/secret/registry/repository 계열을 포함하며, URL userinfo와 secret query value는
distribution requirement/entry point/location/error 문자열에서도 scrub한다. CLI에는 unredacted
옵션이 없고 library의 `redact=False`는 명시적 통제 경계로만 남긴다. Snapshot file은 atomic
same-directory replacement와 POSIX `0600`을 사용하며 symlink/special file과 selected interpreter
(hardlink alias 포함) overwrite를 거부한다.

2026-09-03 local Python 3.10에서 50/50 tests (CLI 7, I/O 6, probe/process 12, redaction 7,
snapshot normalization/schema 18), Ruff check/format, strict mypy 6 source modules가 통과했다.
Released ici `v0.10.2` local deep도 14 total engines에서 `13 PASS / 0 WARN / 0 FAIL / 0 ERROR /
1 compile_db SKIP`로 PASS했으며, TEM `5.00`, line/function/branch `93.0% / 100.0% / 84.6%`,
complexity max `13`, cycle/sanitize `PASS`였다.
`envlens/ici.toml`은 test/coverage, TEM `≥ 4.0`, branch `≥ 80%`, function `≥ 90%`의 intended
quality contract를 기록한다. Repository path-aware manifest와 Python 3.10/latest quality matrix는
완료됐다. [PR #50](https://github.com/jihoon22-lee/toy-projects/pull/50)은
`c307ac1ab01e12e4ac81a34623eb669da0e43641`로 병합됐고, exact-head push
[run `33698248293`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33698248293)에서 모든
job, main publisher와 `Merge Gate`가 성공했다. PR-only publisher는 push에서 expected skip이다.
Exact-main artifact는 EnvLens ici `9872574260`, Python 3.10 `9872561889`, latest Python
`9872564898`, Quality Zoo `9872561713`이며, 네 project Page는 artifact와 byte-identical이고
exact title/Zero-CDN 검사를 통과했다. 상세 size/hash는 [EnvLens workthrough](../../workthrough/2026-09-03-envlens-snapshot.md)에
중앙화한다. 제품 identity는 `0.1.0`/`Unreleased`로 유지하며 stable release는 아직 pending이다.
E2 snapshot diff/compatibility, E3 project/runtime smoke와 E4 release 조건은 이 E1 완료에 포함되지
않는다.

### E2. diff와 compatibility

**브랜치:** `feat/envlens-diff`

- [ ] added/removed/upgraded/downgraded distribution을 비교한다.
- [ ] normalized project name과 import name 차이를 구분한다.
- [ ] requires-python과 wheel tag/platform compatibility를 검사한다.
- [ ] dependency missing/version conflict를 offline metadata 범위에서 설명한다.
- [ ] certainty가 없는 resolver 결론은 추정으로 표시한다.
- [ ] text/JSON/Markdown report를 제공한다.

### E3. project/runtime smoke

**브랜치:** `feat/envlens-runtime-check`

- [ ] configured interpreter마다 compileall과 explicit import case를 실행한다.
- [ ] timeout, signal, missing interpreter, missing import를 구분한다.
- [ ] pyproject entry point를 dry inspect하고 opt-in smoke를 실행한다.
- [ ] ici Python compatibility engine과 같은 fixture를 공유하지 않고 결과를 상호 대조한다.

### E4. envlens release 완료 조건

- [ ] Python 3.10과 최신 설치 runtime에서 unit/E2E 통과
- [ ] mypy strict 목표와 Ruff explicit rule set 통과
- [ ] pure `py3-none-any` wheel과 clean environment smoke 통과
- [ ] missing interpreter/package/malformed metadata error guide 제공
- [ ] ici standard profile PASS, package/runtime finding의 정확한 위치·evidence 확인

---

## 11. A stream — abilens 신규 Makefile C++ binary explorer

### A0. 제품 범위와 Makefile contract

**브랜치:** `feat/abilens-skeleton`

abilens는 Linux ELF 파일 또는 두 report를 받아 다음을 보여주는 C++ CLI다.

- ELF class, machine, type
- NEEDED libraries
- RPATH/RUNPATH
- required GLIBC, GLIBCXX, CXXABI floor
- 두 binary의 dependency/ABI requirement 차이

권장 구조:

```text
abilens/
  Makefile
  ici.toml
  include/abilens/
  src/
  tests/
  tests/fixtures/
```

- C++20 이상, Qt 없음, native third-party library 없음.
- 손으로 쓴 Makefile이 executable, core static/shared test fixture와 test binaries를 만든다.
- standard targets와 argv는 ici Make adapter 설계와 함께 확정한다.
- readelf/objdump를 실행할 경우 subprocess adapter와 output parser를 core에서 분리한다.

- [ ] `make all`, `make test`, coverage/sanitize variant의 요구 계약을 문서화한다.
- [ ] ici adapter가 없는 초기 red state를 ICI-GAPS에 기록하고 숨기지 않는다.

### A1. ELF evidence reader

**브랜치:** `feat/abilens-elf-reader`

- [ ] input identity, magic/class/endian/machine을 검증한다.
- [ ] system readelf capability와 version을 확인한다.
- [ ] dynamic section과 version need output을 typed record로 파싱한다.
- [ ] stripped binary, static binary, non-ELF, corrupted ELF를 구분한다.
- [ ] tool output truncation/locale variation을 fixture로 테스트한다.
- [ ] binary를 실행하거나 library를 load하지 않는다.

### A2. compatibility policy와 diff

**브랜치:** `feat/abilens-compat-diff`

- [ ] maximum GLIBC/GLIBCXX/CXXABI required version을 계산한다.
- [ ] configured platform floor와 비교해 actionable result를 만든다.
- [ ] NEEDED/RPATH additions/removals과 static/dynamic drift를 비교한다.
- [ ] text/JSON report와 stable schema를 제공한다.
- [ ] ici binary compatibility result와 같은 fixture binary에서 수치를 대조한다.

### A3. Makefile 다중 산출물과 안전성

**브랜치:** `feat/abilens-build-matrix`

- [ ] executable, static core와 shared fixture를 명시적으로 빌드한다.
- [ ] tests가 parser와 real ELF integration을 분리한다.
- [ ] ASan/UBSan, gcov build가 일반 release artifact를 오염시키지 않는다.
- [ ] parallel make와 clean/rebuild가 재현 가능하다.
- [ ] ici artifact manifest가 모든 expected artifact를 찾는다.

### A4. abilens release 완료 조건

- [ ] configured Make adapter로 build/test/sanitize/coverage PASS
- [ ] viewer static CLI와 abilens shared fixture를 정확히 분류
- [ ] malformed/untrusted binary를 실행하지 않고 처리
- [ ] C++20 compile context와 binary compatibility engine 실측
- [ ] supported ELF/binutils 범위와 limitation 문서화

---

## 12. Q stream — quality-zoo expected-finding corpus

### Q0. scenario 계약

**브랜치:** `test/quality-zoo-contract`

권장 구조:

```text
quality-zoo/
  README.md
  manifest.json
  runner/
  scenarios/
    python/<rule-case>/
    cpp/<rule-case>/
    qt/<rule-case>/
    build/<adapter-case>/
    hybrid/<integration-case>/
```

현재 schema-2 scenario는 `scenario.json`에 scenario identity와 exact ici executable SHA-256별
expectation 경로를 선언하고, 각 경로가 가리키는 full strict schema-1 expectation에 다음을
담는다.

- project root와 ici profile/config
- 실행할 command와 expected suite/engine status
- expected rule id, evidence, confidence
- expected primary path와 line 또는 허용 line 범위
- expected related location/metric 일부
- 반드시 없어야 하는 finding
- 필요한 capability와 skip 조건

**현재 상태 (2026-09-03):** scenario contract, dependency-free runner, candidate archive intake와
local candidate consumer 구현은 완료됐다. Released-artifact Q0의 toy PR/exact-main acceptance도
완료됐지만, ici-hosted candidate consumer의 remote cross-repository acceptance는 ici workflow가
병합·dispatch될 때까지 pending이다. Registry는 Python 3.10 표준 라이브러리만 사용하는
`manifest.json`으로 고정했으며, scenario의 `ici.toml`은 ici 입력으로 남기고 runner가 TOML
parser를 추가하지 않는다. `ICI_BIN`은 명시적인 local executable path이고, ordinary CI는
released ici `v0.10.2` pin을 유지한다. Candidate ZIP과 optional exact five authenticated
GitHub API snapshots의 provenance/digest/identity validation은 local evidence에서 통과했다.
Released ici `v0.10.2` digest `8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4`는
legacy `MEASURED`/`high`를, 같은 package version의 candidate digest
`53fc75f0a073a74689babfe9ef8a4b2378995002d7d563bdc52da548fdbb9ee8`는 provenance-aware
`ESTIMATED`/`medium`을 보고한다. 따라서 schema-2 selector는 exact executable SHA-256으로
full strict schema-1 expectation을 고르고 unknown digest는 fail closed한다. Ordinary CI는
released expectation을, candidate validation은 candidate expectation을 사용한다.

- [x] runner는 `ICI_BIN`/`--ici-bin`으로 local pyz를 받고, archive consumer는 검증된 candidate를 받는다.
- [x] scenario project 밖 path, symlink와 shell command를 거부한다.
- [x] report schema mismatch와 runner failure를 engine failure와 구분한다.
- [x] line number가 변경되면 무조건 snapshot을 갱신하지 않고 source/expectation을 함께 검토한다.
- [x] candidate ZIP의 digest/provenance, exact member/mode, sidecar, executable version과 optional
      `artifact.json`, `candidate-run.json`, `gate-check.json`, `gate-job.json`, `gate-run.json`
      five-snapshot evidence를 fail-closed로 검증한다.
- [x] schema-2 `scenario.json`이 exact executable SHA-256으로 full strict schema-1 expectation을
      선택하고, unknown digest를 fallback 없이 fail closed한다.
- [x] contract verdict를 observed suite `WARN`/`FAIL`과 분리하고 expected finding 및 clean
      counterpart absence를 검증한다.
- [x] remote PR CI, sticky report publication과 exact-main evidence를 완료한다. [PR #49](https://github.com/jihoon22-lee/toy-projects/pull/49)의
      [run `33693241255`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33693241255)는
      통과했고, exactly one sticky comment 안에 marker 정확히 1개와 당시 product HTML link 3개가
      게시됐다. Merge `ed5fea2e881da77ac95482cf665e4e40bfe172f1` 뒤 [exact-main run
      `33694452357`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33694452357)도
      통과했으며, 세 product Pages가 HTML artifact와 byte-identical했다.

첫 local candidate evidence는 candidate run `33689056008`, source/target
`7872a7b80899cbd3d40d92d18e7920cd7e2283e7`, artifact `9869395069`, ZIP SHA-256
`640e50ecf5b099174c16f1ef5d2b5b87945329711e96f926d94c3cc04109081e`, candidate `ici.pyz` SHA-256
`53fc75f0a073a74689babfe9ef8a4b2378995002d7d563bdc52da548fdbb9ee8`, version `0.10.2`다.
Authenticated API evidence validation은 통과했다. `python.dead-private-function`은 observed
suite `WARN`이지만 contract `PASS`였고, expected finding 하나만 match됐으며 clean counterpart
false positive가 없었다. 이 evidence는 version/release bump를 의미하지 않으며, same package
version의 released/candidate report를 하나의 expectation으로 취급하지 않는다.

sanitizer-normalization target `9d470edca7ab037a24dcd6594531a822f116548b`의 candidate selector도
추가했다. Producer run `33706057540`은 success이고 artifact `9875319095`의 raw archive
SHA-256은 `4aec084b3a30ac01a1df5124fa3b42b7f51d23f66c12b490194a84549be9db27`, executable
SHA-256은 `e7f1a2ce7147057538873a802715c7bf2b12e530a85070af862e02e378caceb8`다. 이 digest는
`expectations/candidate-9d470ed.json`을 선택하며 authenticated local intake evidence가
성공했다. 해당 Q0 runner는 contract `PASS`, observed suite `WARN`, one matched finding,
zero errors를 기록했다. 이는 기존 Python dead-code known-answer에 대한 local candidate
evidence이며 sanitizer rule 자체를 검증하거나 remote cross-repository acceptance를 닫는
증거가 아니다.

2026-09-03 local C++ sanitizer subset은 `cpp.asan-use-after-free`,
`cpp.lsan-memory-leak`, `cpp.ubsan-signed-overflow`, `cpp.sanitizer-clean` 네 stable
scenario로 구성했다. Released ici `v0.10.2` executable SHA-256
`8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4`와 candidate executable
SHA-256 `e7f1a2ce7147057538873a802715c7bf2b12e530a85070af862e02e378caceb8`를 각각 exact
schema-2 selector로 실행했다. 두 all-scenario run 모두 five scenarios의 contract `PASS`,
zero errors를 기록했다. ASan UAF/LSan leak/UBSan signed overflow defect scenario는 expected
observed `FAIL`, clean scenario는 expected observed `PASS`, 기존 Python scenario는 expected
observed `WARN`이었다. 따라서 Q2의 ASan/UBSan/LSan 및 clean counterpart 한 항목만 local
evidence 기준으로 완료했으며, broader Q2와 Q1/Q3~Q5, candidate remote acceptance는 여전히
pending이다. 이 local evidence는 PR CI, merge 또는 release를 의미하지 않는다.

Quality Zoo의 다음 exact-main push [run `33698248293`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33698248293)에서도
artifact `9872561713`의 contract는 `PASS`였고, stable scenario 하나의 observed `WARN`은 예상된
결과였으며 error는 0개였다. EnvLens merge와 함께 current manifest가 네 project Page를 생성했지만,
Q1~Q5 corpus expansion은 여전히 pending이다.

### Q1. Python stable corpus

**브랜치:** `test/quality-zoo-python`

- [ ] syntax/lint와 formatter mismatch
- [ ] mypy type error와 unresolved import policy
- [ ] open/close/context manager ownership positive·negative case
- [ ] mutable default와 exception swallowing
- [ ] hardcoded secret redaction, unsafe subprocess, weak crypto
- [ ] import cycle, dead/unreachable, complexity, duplication
- [ ] Python 3.10 compatibility와 package metadata error
- [ ] 각 rule의 nearby clean code가 false positive를 만들지 않는지 확인

### Q2. C++ stable corpus

**브랜치:** `test/quality-zoo-cpp`

- [ ] compile error와 warning location
- [ ] target별 C++17/C++20 define/include 차이
- [ ] same-basename include와 actual search order
- [ ] header/include cycle과 unresolved generated header
- [ ] complexity/duplicate/unused finding
- [x] ASan UAF, UBSan overflow, leak와 clean counterpart (released/candidate exact-digest local runs; broader C++ corpus remains pending)
- [ ] gcov function/branch location
- [ ] malformed compile DB와 missing TU

### Q3. Qt stable corpus

**브랜치:** `test/quality-zoo-qt`

- [ ] Q_OBJECT/AUTOMOC success와 missing header/build declaration failure
- [ ] AUTOUIC/AUTORCC generated asset
- [ ] Qt5/Qt6 API conditional compile
- [ ] clazy stable level finding과 clean counterpart
- [ ] QObject ownership/lifetime와 signal/slot misuse
- [ ] QtTest XML과 mixed plain test binaries

### Q4. build, binary와 hybrid corpus

**브랜치:** `test/quality-zoo-integration`

- [ ] CMake/qmake/Make configured success와 failure
- [ ] artifact missing/escape/stale manifest
- [ ] ELF floor, RPATH와 forbidden dependency
- [ ] Python→C++ process contract success, timeout, bad placeholder
- [ ] tool missing, timeout, signal, truncated output evidence

### Q5. corpus 운영 기준

- stable scenario는 ici rule contract의 compatibility surface다.
- experimental tool rule은 별도 profile로 두고 tool version 범위를 선언한다.
- production toy에서 발견한 ici bug는 최소 재현으로 축소하되 real-project regression도 계속 남긴다.
- quality-zoo는 실물 프로젝트를 대체하지 않고 탐지력의 known-answer test를 담당한다.

---

## 13. repository CI와 release 구조

### 13.1 path-aware CI

- [ ] root workflow가 변경된 프로젝트와 공통 문서/runner 영향을 계산한다.
- [ ] 각 프로젝트의 native test와 ici verify를 별도 job으로 보여준다.
- [ ] Qt5/Qt6, Python version matrix는 관련 프로젝트에만 적용한다.
- [ ] quality-zoo full suite는 nightly/release와 ici version bump에서 실행하고 PR에는 affected scenario를 우선 실행한다.
- [ ] job이 실행되지 않은 것을 PASS처럼 표시하지 않고 summary에 scope를 기록한다.

### 13.2 ici pin과 local candidate

- [ ] default CI는 checksum이 있는 released ici version을 사용한다.
- [ ] manual/local consumer는 `ICI_BIN` local path 또는 provenance-bound candidate archive와
      expected ZIP SHA-256을 명시적으로 받는다. Candidate provenance를 기록할 때는 optional
      exact five authenticated GitHub API snapshots(`artifact.json`, `candidate-run.json`,
      `gate-check.json`, `gate-job.json`, `gate-run.json`)을 함께 검증한다.
- [ ] 각 project report에 ici version과 tool capability를 보존한다.
- [ ] version bump PR은 portfolio 전체 standard verify 결과를 첨부한다.

### 13.3 release artifact

- loglens/diskmap/buildscope는 platform별 GUI/CLI build 방법과 checksum을 제공한다.
- envlens는 pure wheel과 source distribution을 제공한다.
- abilens는 Linux binary 또는 reproducible build recipe와 checksum을 제공한다.
- quality-zoo는 앱 release를 만들지 않는다.

---

## 14. 교차 저장소 작업 프로토콜

ici와 toy-projects가 함께 바뀌는 기능은 다음 순서를 따른다.

1. ici fixture와 대응 toy/quality-zoo branch에 기대 계약을 먼저 정의한다. 실물에서 발견한 문제는
   toy 재현이 먼저일 수 있고, 계획된 engine은 ici contract test가 먼저일 수 있다.
2. 현재 release에서 적용 가능한 양쪽 재현이 실패하는지 확인하고 `ICI-GAPS.md`에 기록한다.
   실패 상태는 main에 병합하지 않는다.
3. ici 저장소에서 engine 구현과 전체 gate를 완료한다.
4. candidate pyz로 toy native test와 ici verify를 모두 통과시킨다.
5. ici PR과 release 또는 검증 가능한 release candidate를 먼저 완료한다.
6. toy CI pin과 project 기능 PR을 완료한다.
7. gap 항목에 양쪽 PR과 final evidence를 연결한다.

제품 기능이 ici 변경 없이 독립적으로 완성될 수 있으면 이 release 경계를 기다리지 않는다.

---

## 15. 마스터 완료 체크포인트

- [x] T0: loglens/diskmap Qt shell과 Qt5/Qt6 matrix 완료 (T0-1~T0-5 구현, native/ici 실측,
  PR/exact-main matrix와 Merge Gate evidence complete; 상세 기준은 handover의 현재 기준을 따른다)
- [ ] L1~L3: loglens streaming correctness, bounded storage, parser pipeline 완료 (L1/L2 완료;
  L3 parser/filter pipeline pending)
- [ ] L4~L6: loglens triage/window analysis와 release 완료
- [x] D1~D3: diskmap identity-safe scan, cancellation, explorer UX 구현·PR·exact-main evidence 완료
  (D1/D2는 PR #23/#28, D3 core는 PR #45, D3 GUI는 PR #46 및 exact-main run으로 검증)
- [ ] D4~D7: cleanup/trash/snapshot과 release 완료 (pending)
- [x] B0~B5: buildscope hybrid compile explorer와 release 완료 (implementation·remote acceptance·trusted main Pages·`0.5.0` tag/release/public asset audit complete)
- [ ] E0~E4: envlens pure-Python environment explorer와 release 완료 (E1 snapshot 구현·PR #50
  merge·exact-main evidence complete; E2~E4 pending)
- [ ] A0~A4: abilens Makefile/ELF explorer와 release 완료
- [ ] Q0~Q5: Python/C++/Qt/build/hybrid stable expected-finding corpus 완료 (released-artifact Q0
  implementation과 PR #49 remote acceptance, EnvLens merge의 exact-main QZ artifact, 새
  candidate SHA selector/local authenticated evidence, 그리고 C++ sanitizer ASan/UBSan/LSan 및
  clean counterpart local evidence는 기록됐다. broader Q2 corpus, ici-hosted candidate workflow의
  remote cross-repository acceptance와 Q1/Q3~Q5는 pending)
- [ ] repository path-aware CI, ici pin/candidate와 artifact 정책 완료

체크포인트는 기능이 시연되는 것만으로 닫지 않는다. 공통 제품 완성 불변식, native tests, ici 실측, 문서와 오류 처리까지 모두 충족해야 한다.

---

## 16. 당장 시작할 순서 (계획 수립 당시의 역사적 순서)

1. 이 마스터 계획과 ici 마스터 계획을 문서 PR로 보존한다.
2. T0에서 기존 Qt shell 계획을 stateful log parsing과 정확한 failure-state 테스트로 보정해 실행한다.
3. L1과 D1로 기존 앱의 신뢰성 기반을 만든다.
4. 완료된 D2의 PR #28 원격/merged-main 증거를 기준으로 D3 explorer UX를 진행한다.
5. ici finding v3와 맞춰 Q0 runner를 만든다.
6. ici compile context I3와 함께 B0/B1 buildscope를 시작한다.
7. ici Python compatibility I5와 함께 E0/E1 envlens를 시작한다.
8. benchmark 결과가 안정되면 ici Make/ABI I7와 함께 A0/A1 abilens를 시작한다.

이 순서는 계획 수립 당시의 역사적 실행 순서다. 현재는 B0~B5 implementation, PR/remote/hosted/
merged-main evidence, remote preflight matrix와 hybrid handoff, trusted main Pages, `0.5.0`
tag/GitHub Release와 9개 public asset의 독립 audit까지 완료됐다. 따라서 B0~B5 release
체크포인트는 닫혔고, ici I3 cross-repository comparison은 완료됐다. 이 순서를 다시 바꾸려면
제품 dependency나 ici release boundary라는 구체적 근거를 문서에 남긴다.
