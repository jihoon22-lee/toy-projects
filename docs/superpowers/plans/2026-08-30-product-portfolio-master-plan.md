# toy-projects 제품 포트폴리오와 ici 실물 검증 마스터 계획

**상태:** 승인된 장기 마스터 계획. 2026-08-30 이후 toy-projects의 기능 우선순위, 신규 프로젝트 선정과 완료 조건은 이 문서를 기준으로 판단한다.
**문서 기준일:** 2026-08-31. 이 계획은 toy-projects [PR #12](https://github.com/jihoon22-lee/toy-projects/pull/12)로 `main`에 병합됐다. 현재 완료 상태는 이 문서의 체크리스트와 병합된 PR을 함께 기준으로 삼는다.

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

2026-08-30 기준:

| 프로젝트 | 현재 형태 | 실측 | 제품 상태 |
|---|---|---|---|
| loglens | C++17, Qt, CMake | 8 tests, TEM 4.08 | 유용한 골격, streaming 신뢰성과 대용량 UX 부족 |
| diskmap | C++17, Qt, qmake | 7 tests, TEM 4.85 | treemap viewer, 정리 도구의 안전 모델 부족 |
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
| known-bad detection recall | 작은 ici fixture뿐 | quality-zoo |

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
- ici 미지원으로 gate가 깨지면 우회 디렉터리로 옮기지 않고 `ICI-GAPS.md`에 재현을 남긴다.
- ici 신규 기능이 필요한 toy PR은 release candidate pyz로 먼저 검증하고, 정식 release 후 workflow pin을 갱신해 병합한다.

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
완료해야 하며, 아직 T0 전체 완료로 표시하지 않는다.

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

- [ ] POSIX에서는 device/inode, 다른 platform에서는 사용 가능한 file identity abstraction을 도입한다.
- [ ] 더 크거나 같은 새 파일로 교체돼도 rotation을 탐지한다.
- [ ] truncate, replace, permission loss, file disappear/reappear를 구분한다.
- [ ] poll 결과를 `records`, `generation_changed`, `error`, `position` 구조로 만든다.
- [x] stopped/retryable/fatal 상태와 사용자의 resume 동작을 정의한다.
- [ ] real filesystem integration test와 fake source deterministic test를 함께 둔다.

**Slice 2 구현·검증 증거 (2026-08-31):** `FollowState`를 `Stopped`, `Following`,
`WaitingRetry`로 분리했다. retryable missing/permission/open/stat/read 오류에서는 기존
행과 timer를 유지하고 시도 횟수와 대기 상태를 표시한다. 새 file identity가 확인된
replacement를 성공적으로 읽을 때만 assembler/model을 새 generation으로 reset한다. 사용자가
Follow를 끄면 retry를 중지하고, 다시 켜면 명시적으로 재개한다. fatal unsupported file type은
follow를 중지하되 마지막 정상 화면은 보존한다. `retryableSourceErrorKeepsFollowingAndVisibleRows`,
`sourceReplacementRecoversWithCleanRows`, `disablingFollowWhileWaitingStopsPolling`,
`unsupportedSourceStopsFollowing`를 포함한 `test_main_window` 회귀 테스트가 추가됐고,
Qt 6 및 Qt 5 CMake/CTest에서 각각 10/10 PASS를 기록했다. 구현 커밋은 `4094ff5`, 테스트
커밋은 `ca56d2e`다. 로컬 ici 0.6.0 verify도 Suite PASS(10 pass, 2 skip), TEM 4.86,
line/function/branch 92.4/97.1/80.9%를 기록했고 Zero-CDN HTML과 Qt5·Qt6 8초 headless
smoke를 확인했다. 원격 CI Merge Gate와 report-pr sticky HTML·Pages는 해당 PR에서
별도로 확인한다.

### L2. bounded storage와 큰 파일 UX

**브랜치:** `feat/loglens-bounded-model`

- [ ] 기존 RingBuffer를 GUI/CLI 실제 record store에 연결한다.
- [ ] capacity, dropped record count와 oldest/newest line을 노출한다.
- [ ] model reset 대신 incremental insert/remove contract를 테스트한다.
- [ ] 초기 open은 tail N 또는 streaming index mode 중 사용자 선택을 제공한다.
- [ ] background parsing 중 UI가 filter/search를 안전하게 처리한다.
- [ ] 1 GiB synthetic log와 100만 record benchmark를 만들고 first-paint, throughput, peak RSS를 기록한다.
- [ ] 실측 후 default capacity와 성능 budget을 고정한다.

### L3. parser와 filter 완성도

**브랜치:** `feat/loglens-parser-pipeline`

- [ ] JSON string escape와 Unicode를 손으로 일부 파싱하지 않고 검증된 범위의 parser contract로 처리한다.
- [ ] ISO, syslog, JSONL, raw와 multiline을 source profile로 저장한다.
- [ ] malformed line을 유실하지 않고 parse error metadata와 raw를 보존한다.
- [ ] timestamp timezone/precision과 missing timestamp 정책을 정의한다.
- [ ] filter AST에 syntax diagnostic range와 saved query를 추가한다.
- [ ] regex catastrophic input에 timeout/limit 또는 안전 정책을 둔다.

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

- [ ] RealFsSource가 symlink를 따라간 결과와 링크 자체를 혼동하지 않는다.
- [ ] follow_symlinks=true에서는 visited identity로 cycle을 막는다.
- [ ] hardlink는 allocated/reclaimable 합계에서 중복 계산하지 않는다.
- [ ] permission/error로 incomplete한 directory total을 완전한 값처럼 보이지 않게 한다.
- [ ] path string 결합 대신 filesystem path abstraction을 사용한다.

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

- [ ] `FileIdentity` visited set으로 follow-symlink directory cycle을 차단한다.
- [ ] hard-link identity를 기준으로 allocated/reclaimable aggregate를 중복 계산하지
  않는다.
- [ ] string path join을 filesystem path abstraction으로 교체하고 platform별 경로
  의미론을 검증한다.

### D2. cancellable scanner와 stale result 방지

**브랜치:** `feat/diskmap-cancellable-scan`

- [ ] progress callback을 GUI에 실제 연결한다.
- [ ] atomic/token 기반 cancellation과 cooperative checkpoint를 제공한다.
- [ ] scan generation id가 이전 worker 결과로 새 UI를 덮지 못하게 한다.
- [ ] mount boundary, exclude pattern, min size, max depth 옵션을 제공한다.
- [ ] aggregate/sort/count의 deep tree recursion을 iterative 또는 안전한 bound로 바꾼다.
- [ ] cancellation 후 partial result를 보여줄지 폐기할지 명확히 한다.
- [ ] 100만 entry fake source benchmark로 throughput과 memory를 측정한다.

### D3. 탐색과 설명 UX

**브랜치:** `feat/diskmap-explorer-ui`

- [ ] treemap과 sortable table/list를 함께 제공한다.
- [ ] breadcrumb에 실제 path segment와 accessible action을 제공한다.
- [ ] search, size/type/age filter와 largest files view를 추가한다.
- [ ] logical/allocated/reclaimable size 차이를 설명한다.
- [ ] unreadable/incomplete subtree를 시각적으로 구분한다.
- [ ] rescan, refresh와 selection 유지 의미론을 테스트한다.

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

- [ ] minimal Python CLI와 Qt window가 각각 독립 실행된다.
- [ ] Python output을 C++ CLI 또는 GUI model이 소비하는 E2E test가 있다.
- [ ] Qt5/Qt6와 Python 3.10에서 skeleton build/test가 통과한다.

### B1. compile database Python core

**브랜치:** `feat/buildscope-compile-db`

- [ ] `arguments`와 `command` entry, directory/file/output을 읽는다.
- [ ] shell 실행 없이 command를 token화하고 원문도 보존한다.
- [ ] compiler, language, standard, defines, include paths, sysroot, target hint를 정규화한다.
- [ ] duplicate/stale/missing file과 여러 configuration을 보존한다.
- [ ] project root 밖 system/vendor path를 분류한다.
- [ ] versioned JSON schema와 deterministic ordering을 제공한다.
- [ ] malformed/huge DB와 path separator fixture를 테스트한다.

### B2. C++ model과 Qt UI

**브랜치:** `feat/buildscope-qt-explorer`

- [ ] JSON schema parser와 validation error location을 구현한다.
- [ ] source/target tree, command detail, define/include tables를 제공한다.
- [ ] filter/search와 missing/stale entry 표시를 제공한다.
- [ ] `.ui`로 MainWindow layout, `.qrc`로 local icons/theme asset을 사용한다.
- [ ] QAbstractItemModelTester와 MainWindow shell test를 추가한다.
- [ ] 10만 entry synthetic DB를 lazy model로 처리하고 성능을 측정한다.

### B3. include explanation

**브랜치:** `feat/buildscope-include-explain`

- [ ] ici compilation context 또는 compiler dependency output을 입력으로 받는다.
- [ ] include search order와 최종 resolved path를 보여준다.
- [ ] same-basename collision, missing/generated/system header를 구분한다.
- [ ] include edge에서 source command와 file location으로 이동한다.
- [ ] “추정”과 compiler-measured edge를 UI에서 구분한다.

### B4. configuration diff

**브랜치:** `feat/buildscope-config-diff`

- [ ] 두 compile DB의 added/removed/moved translation unit을 비교한다.
- [ ] standard, define, include order, compiler/target drift를 구조화한다.
- [ ] noisy absolute build path와 output filename 차이를 normalization policy로 줄인다.
- [ ] suppression/ignore rule과 diff report export를 제공한다.

### B5. hybrid integration과 release

- [ ] Python analyzer → JSON → C++ consumer contract를 ici integration engine으로 검증
- [ ] CMake compile DB가 모든 production TU를 포함하는지 ici가 검증
- [ ] clang-tidy/clazy가 설치된 환경에서 deep profile 실측
- [ ] Qt5/Qt6, Python 3.10/latest matrix 통과
- [ ] sample CMake/qmake databases와 tutorial 제공
- [ ] standalone Python CLI와 GUI release artifact 제공

---

## 10. E stream — envlens 신규 pure-Python environment explorer

### E0. 제품 범위

**브랜치:** `feat/envlens-skeleton`

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

### E1. environment snapshot

**브랜치:** `feat/envlens-snapshot`

- [ ] interpreter를 shell 없이 argv로 실행해 identity와 sysconfig를 JSON으로 수집한다.
- [ ] installed distribution, version, Requires-Python, dependency, entry point와 location을 기록한다.
- [ ] snapshot schema version, source identity와 timestamp를 분리한다.
- [ ] secret-bearing environment variables와 user path를 기본 redaction한다.
- [ ] malformed metadata와 permission error를 distribution별로 보존한다.
- [ ] atomic JSON write와 deterministic output을 테스트한다.

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

**브랜치:** `test/quality-zoo-runner`

권장 구조:

```text
quality-zoo/
  README.md
  manifest.toml
  runner/
  scenarios/
    python/<rule-case>/
    cpp/<rule-case>/
    qt/<rule-case>/
    build/<adapter-case>/
    hybrid/<integration-case>/
```

각 scenario는 다음을 선언한다.

- project root와 ici profile/config
- 실행할 command와 expected suite/engine status
- expected rule id, evidence, confidence
- expected primary path와 line 또는 허용 line 범위
- expected related location/metric 일부
- 반드시 없어야 하는 finding
- 필요한 capability와 skip 조건

- [ ] runner는 `ICI_BIN`으로 local/release pyz를 받는다.
- [ ] scenario project 밖 path와 shell command를 거부한다.
- [ ] report schema mismatch와 runner failure를 engine failure와 구분한다.
- [ ] line number가 변경되면 무조건 snapshot을 갱신하지 않고 source/expectation을 함께 검토한다.

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
- [ ] ASan UAF, UBSan overflow, leak와 clean counterpart
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
- [ ] manual workflow는 `ICI_BIN` artifact 또는 candidate URL/checksum을 명시적으로 받는다.
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

- [ ] T0: loglens/diskmap Qt shell과 Qt5/Qt6 matrix 완료
- [ ] L1~L3: loglens streaming correctness, bounded storage, parser pipeline 완료
- [ ] L4~L6: loglens triage/window analysis와 release 완료
- [ ] D1~D3: diskmap identity-safe scan, cancellation, explorer UX 완료
- [ ] D4~D7: cleanup/trash/snapshot과 release 완료
- [ ] B0~B5: buildscope hybrid compile explorer와 release 완료
- [ ] E0~E4: envlens pure-Python environment explorer와 release 완료
- [ ] A0~A4: abilens Makefile/ELF explorer와 release 완료
- [ ] Q0~Q5: Python/C++/Qt/build/hybrid stable expected-finding corpus 완료
- [ ] repository path-aware CI, ici pin/candidate와 artifact 정책 완료

체크포인트는 기능이 시연되는 것만으로 닫지 않는다. 공통 제품 완성 불변식, native tests, ici 실측, 문서와 오류 처리까지 모두 충족해야 한다.

---

## 16. 당장 시작할 순서

1. 이 마스터 계획과 ici 마스터 계획을 문서 PR로 보존한다.
2. T0에서 기존 Qt shell 계획을 stateful log parsing과 정확한 failure-state 테스트로 보정해 실행한다.
3. L1과 D1로 기존 앱의 신뢰성 기반을 만든다.
4. ici finding v3와 맞춰 Q0 runner를 만든다.
5. ici compile context I3와 함께 B0/B1 buildscope를 시작한다.
6. ici Python compatibility I5와 함께 E0/E1 envlens를 시작한다.
7. ici Make/ABI I7와 함께 A0/A1 abilens를 시작한다.

이 순서를 바꾸려면 제품 dependency나 ici release boundary라는 구체적 근거를 문서에 남긴다.
