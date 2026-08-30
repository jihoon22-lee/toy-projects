# ICI-GAPS

> 제품 확장과 신규 실물 검증 프로젝트의 실행 순서는
> [toy-projects 포트폴리오 마스터 계획](docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md),
> ici 엔진 구현 순서는
> [ici 품질 분석기 마스터 계획](https://github.com/jihoon22-lee/ici/blob/main/docs/superpowers/plans/2026-08-30-python-cpp-qt-quality-analyzer-master-plan.md)을 따른다.

`ici` 를 실제 C++/Qt 프로젝트에 적용하면서 발견한 갭 목록.
Phase 5(Qt/CMake 어댑터) 설계의 입력이다. 각 항목은 **코드 위치 + 재현 조건 + 영향**을 남긴다.

버전 기준: `ici 0.5.0` (`dist/ici.pyz`)


## 현황 (2026-08-30)

**A-3 이 닫혔다.** ici 0.6.0 이 CMake/CTest 와 qmake/Make 빌드 어댑터를 갖췄고, 이 저장소의
두 프로젝트가 그 실측 대상이었다 — `loglens` 는 CMake, `diskmap` 은 qmake 로 전환했다.
`Q_OBJECT` 클래스의 단위 테스트가 `tests/` 안에서 실제로 통과한다.

### 남은 것

| | 항목 | 성격 |
|---|---|---|
| **A-2** | 손으로 쓴 `Makefile` 만 있는 프로젝트는 여전히 거부 | **부분 수정** — CMake·qmake 는 해결 |
| B-2 | C++ include 해석이 basename 단독 | 탐지력 저하(조용함) |
| B-3 | `dead`/`cognitive`/`resource` 가 Python 전용 | 문서·표기 문제 |

A-2 를 "수정됨" 으로 옮기지 않는 이유는 남은 거부 경로가 문서에서 사라지지 않게 하기
위해서다. 실측 대상이 될 `Makefile` 전용 프로젝트가 없으므로 어댑터도 만들지 않았다.

### 어댑터 작업에서 새로 발견한 것

전부 **전환하다 나왔고, 코드를 읽어서 찾은 것은 하나도 없다.** 그리고 대부분은
**픽스처로는 드러나지 않았다** — 실물 프로젝트라야 나오는 것들이었다.

| | 발견 | 결과 |
|---|---|---|
| D-1 | `sanitize` 를 어댑터 범위 밖에 둔 설계가 성립 불가 | ✅ ici #76 |
| D-2 | `build` 가 릴리스 산출물에 `--coverage` 를 주입 | ✅ ici #76 |
| D-3 | 어댑터 빌드 실패가 `NOT_APPLICABLE` 로 보고됨 | ✅ ici #76 |
| D-4 | `test` 와 `sanitize` 의 진입점 판정 규칙이 서로 다름 | ✅ ici #76 |
| D-5 | 진입점이 커버리지 분모에 들어가 전환만으로 커버리지 하락 | ✅ ici #76 |
| D-6 | `-xunitxml` 은 QtTest 전용 — 나머지 테스트가 보고에서 사라짐 | ✅ ici #76 |
| D-7 | qmake 의 `target_wrapper.sh` 실행이 테스트 카운트에서 누락 | ✅ ici #76 |
| D-8 | qmake 의 상대 경로 때문에 line 커버리지 전부 유실 | ✅ ici #76 |
| D-9 | CTest·QtTest 가 낸 XML 의 엔티티 확장(DoS) | ✅ ici #76 |

**D-1 이 가장 중요하다.** 스펙은 `sanitize` 를 "나중에 옮겨도 되는 것" 으로 분류했는데,
`sanitize` 는 `tests/**/*.cpp` 를 각각 plain g++ 로 컴파일한다. Qt 테스트를 `tests/` 에 두는
순간 헤더를 못 찾고 깨진다. **"Qt 테스트를 `tests/` 안에 두고 통과시킨다" 는 목표 자체가
`sanitize` 전환을 전제하고 있었다.**

**D-7 은 픽스처가 놓친 전형적인 예다.** qmake 는 Qt 링크 바이너리를 `target_wrapper.sh` 로
실행하는데 ici 가 그 줄을 못 읽어 `diskmap` 의 테스트 6개 중 5개만 셌다. ici 의 qmake 픽스처는
Qt 테스트 하나뿐이라 다른 경로로 구제돼 이 결함이 드러나지 않았다.

### 수정된 것

| | 항목 | 상태 |
|---|---|---|
| A-1 | C++ include 경로 설정 주입 | ✅ #57 |
| B-1 | `cycle` 만 `source_dirs` 무시 | ✅ #62 |
| C-1 | 순수 C++ 프로젝트가 초록불 불가 | ✅ #68 |
| C-2 | 스위트 상태와 콘솔 카운트 모순 | ✅ #70 |
| C-3 | test 엔진이 실패 사유를 안 남김 | ✅ #70 |
| C-4 | C++ 브랜치 커버리지 20%p 과소 집계 | ✅ #50 |
| C-5 | C++ 에서 `type` 이 항상 WARN | ✅ #65 + #68 |
| C-6 | CI 에서 `lint` 가 실행된 적 없음 | ✅ #52 |
| C-8 | AST 폴백이 검사 대상 미보고 | ✅ #73 |
| C-9 | C++ 테스트 CWD 가 엔진마다 다름 | ✅ #69 |
| C-10 | `dup` 의 `warn_pct` 무력화 + 중복률 100% 초과 | ✅ #64 |
| C-11 | 모노repo 리포트 게시 불가 | ✅ #59 |
| — | C++ 함수 경계 탐지 (함수 절반이 미측정) | ✅ #55 |

---

## A. Qt/빌드시스템 지원 부재

### A-1. ✅ [수정됨 · ici PR #57 / v0.5.2] C++ include 경로를 설정으로 주입할 수 없었다
- **위치**: `src/ici/core/project.py` — `get_all_cpp_includes(base_path, config)`
- **현상**: `config` 파라미터를 받지만 **본문에서 전혀 사용하지 않는다**. `-I` 는 `include/`,
  `<source_dir>/include`, 그리고 NAS 경로에서만 수집된다.
- **영향**: Qt 시스템 헤더(`/usr/include/x86_64-linux-gnu/qt6/...`) 를 추가할 방법이 없어
  `src/` 안의 Qt 코드는 `lint`(`-fsyntax-only`)와 `test`(컴파일) 양쪽에서 무조건 실패한다.
- **파생**: `src/ici/engines/lint.py` 는 `get_all_cpp_includes(self.project_root)` 로 **config 인자 없이**
  호출했다. A-1 을 고쳐도 lint 는 설정을 못 읽는 상태였다. 두 곳 다 고쳤다.
- **수정 (v0.5.2)**: `project.cpp_pkg_config` 신설 — 나열한 pkg-config 패키지의 `--cflags` 가
  컴파일 플래그에 추가된다. 경로가 아니라 패키지 이름을 쓰므로 머신이 바뀌어도 깨지지 않는다.
  함께 `project.cpp_external_build_dirs` 도 생겨, moc 가 필요해 ici 가 직접 빌드할 수 없는
  소스를 **분석은 유지한 채** 링크 대상에서만 뺄 수 있다.
- **효과**: 이 저장소의 Qt GUI 가 스코프 밖(검증 엔진 0개)에서 `src/gui/`(8개) 로 들어왔다.

### A-2. ⚠️ [부분 수정 · ici PR #76 / v0.6.0] 루트에 빌드 디스크립터가 있으면 build 엔진이 거부한다
- **위치**: `src/ici/engines/build.py` — `_has_build_descriptor()`, `_compile_cpp()`
- **현상**: 프로젝트 루트에 `CMakeLists.txt` / `Makefile` / `*.pro` 중 하나라도 있으면
  `"C++ build descriptor requires an adapter; generic g++ was not invoked"` ERROR 로 끝난다.
- **영향**: 정상적인 CMake/qmake 프로젝트는 `ici build` 를 아예 쓸 수 없다.
- **수정 (v0.6.0)**: 루트에 `CMakeLists.txt` 나 `*.pro` 가 있으면 이제 그 빌드 시스템에 configure·build 를
  위임한다. 거부하던 바로 그 조건이 어댑터 진입 조건이 됐다.
- **남은 것**: 손으로 쓴 `Makefile` 만 있는 프로젝트는 여전히 거부된다. 어댑터가 없기 때문이며,
  실측 대상이 될 프로젝트가 없어 만들지 않았다.

### A-3. ✅ [수정됨 · ici PR #76 / v0.6.0] 테스트 컴파일이 plain g++ 고정이었다
- **위치**: `src/ici/engines/test.py` — `_run_cpp_tests()`, `_run_cpp_test_case()`
- **현상**: `tests/**/*.cpp` 를 각각 `g++ --coverage -std=c++17` 로 컴파일하고 `main.cpp` 를 제외한
  모든 src cpp 를 링크한다. moc 실행 없음, gtest/Catch2 링크 없음, 사용자 플래그 주입 지점 없음.
- **영향**:
  - `Q_OBJECT` 를 가진 클래스는 vtable 미해결로 링크 실패 → Qt 클래스는 단위 테스트 불가
  - gtest/Catch2 등 표준 프레임워크 사용 불가. 각 테스트 파일이 자체 `main()` 을 가져야 한다
  - `-std=c++17` 고정 → C++20/23 프로젝트 검증 불가
- **수정 (v0.6.0)**: `build`·`test`·`sanitize` 가 프로젝트의 빌드 정의로 configure·build·test 한다.
  moc 는 빌드 시스템이 돌리고, 표준과 프레임워크는 프로젝트가 정한다.
  `loglens/tests/test_log_model.cpp`(`QAbstractItemModelTester`)와
  `diskmap/tests/test_treemap_widget.cpp`(`QSignalSpy`)가 그 증거다.

---

## B. 엔진 간 스코프 규칙 불일치

### B-1. ✅ [수정됨 · ici PR #62] `cycle` 엔진만 `project.source_dirs` 를 무시했다
- **위치**: `src/ici/engines/cycle.py` — `_iter_cpp_and_headers()`
- **현상**: `os.walk(project_root)` 로 **프로젝트 전체**를 훑는다. 제외 대상은 `.venv`/`venv`/`build`/`.git` 뿐.
  다른 C++ 엔진(`lint`/`dup`/`complexity`/`exception`)은 전부 `get_all_cpp_sources()` = `source_dirs` 기준이다.
- **영향**: `source_dirs = ["src"]` 로 스코프를 좁혀도 `gui/`, `tests/`, `third_party/` 가 순환 검사에 들어왔다.
  스코프 규칙이 엔진마다 다르다는 것을 사용자가 알 방법이 없었다.
- **실제로 터진 지점**: ici 저장소에 C++ 탐지 픽스처(`examples/cpp-fixtures/`)를 넣자, 픽스처의
  **의도된 순환이 ici 자기 검증 리포트에 진짜 결함으로 보고**됐다. 다른 엔진은 아무도 못 보는
  파일을 cycle 만 봤기 때문이다. 기록해 둔 갭이 실제 문제를 일으킨 첫 사례.
- **수정 (PR #62)**: 소스 디렉터리 + 최상위 `include/` 만 훑는다. `get_all_cpp_sources()` 를
  그대로 쓸 수는 없다 — 구현 파일만 반환하는데 **include 순환은 대체로 헤더의 성질**이라,
  `get_all_cpp_includes()` 가 공개 헤더를 찾는 곳과 같은 위치를 본다.

### B-2. C++ include 해석이 basename 단독
- **위치**: `src/ici/engines/cycle.py` — `_build_cpp_graph()`
- **현상**: `#include "..."` 를 파일명만으로 해석한다. 같은 basename 이 프로젝트에 둘 이상이면
  **모호하다고 보고 엣지를 버린다**(잘못된 엣지를 만드는 것보다는 안전한 선택).
- **영향**: `src/core/format.hpp` 와 `gui/format.hpp` 처럼 흔한 이름이 겹치면 해당 파일들의 순환은
  탐지되지 않는다. 조용히 탐지력이 떨어지고 리포트에는 아무 표시가 없다.

### B-3. `dead` / `cognitive` / `resource` 는 Python 전용
- **위치**: `src/ici/engines/dead.py`, `cognitive.py`, `resource.py` — 모두 `get_all_python_sources()` 만 사용
- **현상**: C++ 소스를 아예 보지 않는다.
- **영향**: 순수 C++ 프로젝트에서 이 3개 엔진은 항상 빈 결과다. README 의 엔진 설명
  (`dead`: "죽은 코드, 도달 불가능 코드, 미사용 심볼 검출", `resource`: "파일·네트워크 리소스 누수")
  은 언어를 한정하지 않아 C++ 에서도 동작할 것처럼 읽힌다. 최소한 문서에 언어 지원 범위 표기가 필요하고,
  리포트에서도 SKIP 사유를 "이 엔진은 Python 전용" 으로 명시하는 편이 정직하다.

---

## C. 실행 중 발견

### C-1. ✅ [수정됨 · ici PR #68 / v0.5.5] 순수 C++ 프로젝트는 기본 설정으로 초록불이 될 수 없었다
- **위치**: `src/ici/core/models.py:68-79` — `aggregate_suite_status()`
- **현상**: `required = true` 인 엔진이 `SKIP`(또는 `ERROR`, evidence `NOT_RUN`)으로 끝나면
  스위트 전체가 **`ERROR` 로 승격**된다. `dead` 엔진은 Python 전용(B-3)이라 C++ 전용 프로젝트에서
  **항상** `SKIP: no Python source files` 를 낸다. 그리고 `dead.required` 기본값은 `true` 다.
- **재현**: C++ 소스만 있는 프로젝트에서 `ici verify`
  ```
  suite_status = ERROR   (error_count=0, failed_count=1, skipped_count=1)
  dead    SKIP   required=True   evidence=ESTIMATED
  ```
- **영향**: 코드 품질과 무관하게 C++ 프로젝트는 항상 빨간불. CI 게이트로 쓸 수 없다.
- **우회**: 프로젝트 `ici.toml` 에 `[engines.dead] required = false`
- **제안**: 언어 미지원으로 인한 SKIP 은 게이트 승격 대상에서 제외하거나(N/A 상태 신설),
  엔진이 자기 지원 언어를 선언하고 대상 언어가 없으면 애초에 `required` 판정에서 빠지도록 한다.

### C-2. ✅ [수정됨 · ici PR #70 / v0.5.5] 스위트 상태와 콘솔 요약 카운트가 서로 모순됐다
- **현상**: 위와 같은 상황에서 콘솔 요약은 `Total Engines: 12 (Pass: 7, Warn: 3, Fail: 1, Error: 0)`
  을 출력하는데 실제 `suite_status` 는 `ERROR` 다. **`Error: 0` 인데 스위트는 ERROR.**
- **영향**: 사용자가 요약 줄만 보면 왜 실패했는지 영원히 알 수 없다. 카운트는 엔진 상태를 세고,
  스위트 상태는 다른 규칙(required+SKIP 승격)으로 정해지는데 그 규칙이 출력 어디에도 없다.
- **제안**: 요약에 스위트 상태와 그 **결정 사유**를 한 줄로 출력한다.
  (예: `Suite: ERROR — required engine 'dead' was SKIPPED`)

### C-3. ✅ [수정됨 · ici PR #70 / v0.5.5] test 엔진이 실패 사유를 어디에도 남기지 않았다
- **위치**: `src/ici/engines/test.py`
- **현상**: `test` 가 FAIL 인데 요약은 `5/5 Tests Passed | Line: 97.1%, Func: 97.2% -> TEM: 4.86 / 5.0`
  이고, 비-PASS `InspectionTarget` 이 **0건**이다. 실제 원인은 branch 66.0% < 임계값 80.0% 였는데
  **branch 수치가 요약에 아예 없다.**
- **영향**: 통과 항목만 나열된 FAIL. 원인을 알려면 JSON `extra.branch_coverage` 를 직접 까야 한다.
- **제안**: 임계값 미달 항목을 `InspectionTarget` 으로 발행하고 요약에 branch 를 포함한다.
  (`AGENTS.md` 5-1 "위치 추적 필수" 원칙과도 어긋난다)

### C-4. ✅ [수정됨 · ici PR #50] C++ 브랜치 커버리지가 약 20%p 과소 집계됐다
- **위치**: `src/ici/engines/coverage_support.py` — `parse_gcov_dir()`
- **처음 적었던 오진**: "gcov 가 암묵 예외 엣지를 브랜치로 세므로 기본 임계값 80% 는 C++ 에서 달성 불가능하다."
  → **틀렸다.** 같은 `.gcov` 파일을 gcov 자신이 요약하면 92.3% 가 나온다. 수집이 아니라 **ici 의 파싱**이 문제였다.
- **진짜 원인**: gcc 는 예외를 던질 수 있는 거의 모든 호출(예외 활성 C++ 에서는 사실상 모든 STL 할당) 주위에
  `(throw)` 로 표시된 분기 arm 을 방출한다. 예외 unwind 엣지이지 사람이 쓴 분기가 아니고,
  `bad_alloc` 을 인위적으로 일으키지 않는 한 어떤 테스트로도 탈 수 없다.
  파서는 이걸 `taken 0%` 로 읽어 미커버로 셌다.
- **가장 명확한 증거**: `treemap.cpp` 는 `never executed` 분기가 **0개**(= 모든 분기점 도달)인데 73.1% 로 보고됐다.

  | file | 총분기 | never executed | taken 0% | 그중 (throw) |
  |---|---|---|---|---|
  | format.cpp | 34 | 2 | 10 | 9 |
  | treemap.cpp | 104 | 0 | 28 | 25 |

- **대안 비교 (실측)**:

  | 방식 | 결과 | 평가 |
  |---|---|---|
  | gcov `Branches executed` (분기점 도달) | 92.3% | 신호 약함 — else 가 한 번도 안 돌아도 커버로 침 |
  | 기존 ici (`taken at least once`) | 67.8% | 개념은 옳으나 throw arm 에 오염 |
  | **채택: 위 + `(throw)` 제외** | **88.4%** | 개념 유지 + 오염 제거 (lcov 2.x 와 같은 접근) |

- **신호 보존 확인**: 에러 경로가 실제로 덜 검증된 `fs_source.cpp` 는 수정 후에도 **65.8%** 로 남는다.
  전부 100% 로 밀어버리는 수정이 아니다.
- **영향**: 이 버그는 C++ 사용자가 수치를 신뢰하는 대신 **임계값을 낮추도록** 유도하고 있었다.
  실제로 이 저장소도 한때 `min_branch_cov = 35` 로 낮추려다 실측으로 되돌렸다.
- **교훈**: "커버리지가 안 오르는 건 도구 탓" 이라는 결론은 **도구의 원시 출력과 직접 대조해 본 뒤에만** 내릴 것.

### C-5. ✅ [수정됨 · ici PR #65 + #68 / v0.5.5] C++ 프로젝트에서 `type` 엔진이 항상 WARN 을 만들었다
- **현상**: C++ 타입 검사는 미구현이라 소스 파일마다 `[SKIP] C++ type checking is not implemented`
  타깃을 발행하고, 엔진 상태는 `WARN`(mode `pass_warn`) 이 된다.
- **영향**: 모든 C++ 프로젝트가 영구적으로 WARN 1건을 달고 산다. 의도된 동작(README 명시)이지만
  "고칠 수 없는 경고" 는 경고 피로를 유발하고 진짜 WARN 을 묻는다.
- **부분 완화 (ici PR #65)**: `type` 에 전용 탭이 생겼다. 그 전에는 전용 탭이 없는 엔진이
  요약에서 `<details open>` 로 모든 타깃을 펼쳐, C++ 프로젝트의 "검사 안 함" 목록이 요약을 뒤덮었다.
  이제 발견 사항과 미검사 파일이 분리되고 후자는 접힌다. **다만 엔진 상태가 WARN 인 것은 그대로다** —
  미지원 언어를 N/A 로 표기하는 문제는 남아 있다.
- **제안**: 미지원 언어는 WARN 이 아니라 N/A 로 표기 (리포트에 이미 N/A 회색 행 개념이 있다).


### C-6. ✅ [수정됨 · ici PR #52] CI 에서 `lint` 엔진이 한 번도 실제로 실행되지 않았다
- **위치**: `.github/workflows/ci.yml` 린트 단계 + `src/ici/engines/lint.py:207` `_find_ruff_command()`
- **현상**: CI 는 `uvx ruff check .` 로 린트했는데 `uvx` 는 ruff 를 임시 실행할 뿐 `PATH` 에 남기지 않는다.
  ruff 는 dev 의존성에도 없어 `.venv` 에도 없었다. 그래서 도그푸딩 단계의 `lint` 는 ruff 를 못 찾고
  AST 폴백으로 강등돼 `targets: []`, `evidence = ESTIMATED`, `WARN` 으로 게이트를 통과했다.
- **왜 안 드러났나**: 개발 머신에는 ruff 가 전역 설치(`~/.local/bin/ruff`)돼 있어 `shutil.which` 가 찾는다.
  로컬에서는 항상 정상 동작했다. **로컬 통과가 CI 통과와 같은 의미가 아니었다.**
- **수정**: ruff 를 dev 의존성으로 선언(`.venv` 경로로 엔진이 찾음), CI 를 `uv run` 으로 통일,
  저장소 정책 `ruff_required = true`. 이제 ruff 부재 시 `lint` = ERROR / evidence NOT_RUN /
  suite = ERROR / exit 1.

### C-7. 도구 부재로 인한 강등(ESTIMATED)이 부적용(SKIP)보다 관대하다
- **위치**: `src/ici/core/models.py:68-79` `aggregate_suite_status()`
- **현상**: required 엔진이 `SKIP` 이거나 evidence 가 `NOT_RUN` 이면 스위트를 `ERROR` 로 승격한다.
  그러나 evidence `ESTIMATED` 는 승격 대상이 아니다.
- **결과적으로 심각도가 뒤집혀 있다**:
  - `dead` — 이 언어에 **적용 자체가 안 되는** 엔진 → SKIP → 스위트 **ERROR** (C-1)
  - `lint` — **검증이 실제로 일어나지 않은** 엔진 → ESTIMATED → 스위트 **WARN, 통과**
  후자가 훨씬 위험한데 더 관대하게 취급된다.
- **제안**: "언어 부적용" 과 "도구 부재로 검증 못 함" 을 구분되는 상태로 나누고,
  전자는 게이트에서 빼고 후자를 승격 대상으로 삼는다.

### C-8. ✅ [수정됨 · ici PR #73] AST 폴백이 검사 대상을 보고하지 않았다
- **위치**: `src/ici/engines/lint.py:532` `_check_python_syntax()`
- **현상**: `SyntaxError` 가 난 파일만 `InspectionTarget` 으로 남기고, 정상 파싱된 파일은 아무것도 남기지 않는다.
  그래서 폴백이 돌아도 리포트상 "무엇을 검사했는지" 가 0건이다.
- **어긋나는 규약**: `AGENTS.md` 5-1 — "모든 검증 엔진은 PASS/FAIL 여부와 무관하게 검사된 모든 대상의
  파일 경로와 라인 번호(`InspectionTarget`)를 반환해야 한다."
- **영향**: C-6 을 눈치채기 어렵게 만든 직접적 원인. 타깃이 0건이라 리포트만 봐서는
  "깨끗해서 0건" 인지 "아무것도 안 봐서 0건" 인지 구분되지 않는다.

### C-9. ✅ [수정됨 · ici PR #69 / v0.5.5] C++ 테스트 바이너리가 보는 CWD 가 엔진마다 달랐다
- **위치**: `src/ici/engines/test.py` `_run_cpp_test_case()`, `src/ici/engines/sanitize.py:215`
- **현상**: 같은 테스트 바이너리인데 실행 디렉터리가 세 가지다.

  | 엔진 | 조건 | CWD |
  |---|---|---|
  | `test` | gcov 있음 | `build/tests` |
  | `test` | gcov 없음 | 프로젝트 루트 |
  | `sanitize` | 항상 | **프로젝트 밖 임시 디렉터리** |

- **영향**: 테스트가 데이터 파일을 읽는 순간 깨진다. 게다가 `test` 는 **gcov 설치 여부에 따라**
  CWD 가 달라져서, 같은 코드가 환경에 따라 통과하거나 실패한다. 재현성 있는 게이트가 아니다.
- **실제로 밟았다**: `viewer/` 테스트가 `tests/data/*.json` 픽스처를 읽는데,
  로컬 수동 실행은 통과 → `test` 엔진에서 실패 → 상대 경로 후보를 늘려 통과 →
  `sanitize` 에서 다시 실패(임시 디렉터리라 어떤 상대 경로도 무효).
- **우회**: `__FILE__` 에서 경로를 유도한다. ici 가 절대 경로로 컴파일하므로 CWD 와 무관해진다.
  ```cpp
  const std::string source = __FILE__;
  const std::size_t slash = source.find_last_of('/');
  return source.substr(0, slash + 1) + "data/";
  ```
- **제안**: 모든 C++ 테스트를 **프로젝트 루트에서** 실행하도록 통일하고, gcov 산출물 경로는
  `-o` 로 지정한다. 최소한 문서에 "테스트는 CWD 를 가정하면 안 된다" 를 명시해야 한다.

### C-10. ✅ [수정됨 · ici PR #64] `dup` 의 `warn_pct` 는 도달 불가능한 설정이었다
- **위치**: `src/ici/engines/dup.py:116`
- **현상**: `has_warn = dup_pct > warn_pct or len(clone_groups) > 0`
- **영향**: 클론 그룹이 하나라도 있으면 중복률과 무관하게 WARN 이다. `warn_pct = 5.0` 은
  "5% 넘으면 경고" 로 읽히지만 실제로는 **0% 초과면 경고**다. 설정값이 의미를 갖지 못한다.
- **실측**: `viewer/` 는 4.5%(임계값 5.0% 아래)인데 WARN. `diskmap` 이 PASS 였던 건
  중복률이 낮아서가 아니라 클론 그룹이 **정확히 0개**였기 때문이다.
- **부수 문제**: Type-2 탐지가 `parseArray`/`parseObject` 같은 **구조적 대칭**도 클론으로 잡는다.
  지표를 맞추려고 템플릿으로 억지 통합하면 가독성이 나빠진다 — 지표가 설계를 왜곡하는 사례.
- **수정 (PR #64)**: `or len(clone_groups) > 0` 제거. 정책 아래인 클론 그룹은 위치·크기·스니펫을
  그대로 담아 리포트에 남되 조치 대상으로 세지 않는다 — 엔진이 PASS 인데 자기 타깃은 WARN 이라고
  말하는 모순을 피한다.
- **함께 나온 것**: 중복률이 **100% 를 넘을 수 있었다.** 분모 `total_code_lines` 는 빈 줄·주석·import 를
  제외하는데 분자는 클론 구간의 **모든 물리 라인**을 셌다. 실측 145%. 비율이 아닌 값에는 임계값을
  걸 수 없으므로 함께 고쳤다.
- **실측 영향**: `viewer` 4.9%→PASS(4.7%), `loglens` 1.8%→PASS(1.7%), **`ici` 는 여전히 WARN**(15.2%).
  기준을 푼 게 아니라 정확해진 것이다.

### C-11. ✅ [수정됨 · ici PR #59] 모노repo 의 여러 리포트를 게시할 수 없었다
- **위치**: `src/ici/engines/publish.py` — `_resolve_target()`, `PUBLISH_MARKER`
- **현상**: 서브프로젝트를 여럿 검증하는 저장소는 리포트도 여럿 만드는데, `ici publish` 는 하나만 다뤘다.
  두 지점이 막혀 있었다.
  1. **self 모드 경로에 프로젝트 접두사가 없다.** `prefix` 는 hub 모드(`ICI_PUBLISH_REPO`)에서만
     `project_name` 으로 채워지고 self 모드는 `""` 다. 그래서 모든 프로젝트가 같은
     `pr/<N>/index.html` 에 써서 **마지막 것만 남는다.**
  2. **sticky 마커가 `<!-- ici-report -->` 하나로 고정.** 두 번째 publish 가 첫 번째 댓글을 덮어쓴다.
- **부수 함정**: 매트릭스 레그에서 각자 publish 하면 위 두 문제에 더해 **경쟁**까지 생긴다.
  Contents API 는 덮어쓰기에 현재 blob sha 를 요구하므로, 병렬로 같은 브랜치에 쓰면 하나가 유실된다.
- **수정 (PR #59)**: `--report-dir` 반복 지정. 각 디렉터리를 **라벨로 네임스페이스된 경로**에 게시하고
  프로젝트별 행·링크를 담은 **댓글 하나**를 남긴다. 업로드는 한 잡 안에서 순차 실행한다.
  `label=path` 형식은 저장소 루트(디렉터리 이름이 `.`)를 위해 필요하다.
- **적용**: 이 저장소는 매트릭스 레그가 아티팩트만 올리고 `report-pr` 잡이 전부 모아 한 번에 게시한다.
  그 잡은 **PR 소스를 체크아웃하지 않는다** — 실행물은 체크섬 검증된 릴리스 pyz, 게시물은
  verify 잡의 아티팩트뿐이라 PR 이 쓰기 토큰에 닿지 않는다.
- **남은 것**: ici 저장소의 `publish-main` 은 여전히 루트 리포트만 게시한다. main 에서 verify 를
  재실행해 인라인 게시하는 구조라, 뷰어까지 담으려면 C++ 게이트 재실행이나 아티팩트 소비로의
  전환이 필요하고 후자는 `test_purity.py` 의 토큰 격리 검증을 손대게 된다.
