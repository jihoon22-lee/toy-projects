# ICI-GAPS

`ici` 를 실제 C++/Qt 프로젝트에 적용하면서 발견한 갭 목록.
Phase 5(Qt/CMake 어댑터) 설계의 입력이다. 각 항목은 **코드 위치 + 재현 조건 + 영향**을 남긴다.

버전 기준: `ici 0.5.0` (`dist/ici.pyz`)

---

## A. Qt/빌드시스템 지원 부재

### A-1. C++ include 경로를 설정으로 주입할 수 없다
- **위치**: `src/ici/core/project.py` — `get_all_cpp_includes(base_path, config)`
- **현상**: `config` 파라미터를 받지만 **본문에서 전혀 사용하지 않는다**. `-I` 는 `include/`,
  `<source_dir>/include`, 그리고 NAS 경로에서만 수집된다.
- **영향**: Qt 시스템 헤더(`/usr/include/x86_64-linux-gnu/qt6/...`) 를 추가할 방법이 없어
  `src/` 안의 Qt 코드는 `lint`(`-fsyntax-only`)와 `test`(컴파일) 양쪽에서 무조건 실패한다.
- **파생**: `src/ici/engines/lint.py` 는 `get_all_cpp_includes(self.project_root)` 로 **config 인자 없이**
  호출한다. A-1 을 고쳐도 lint 는 여전히 설정을 못 읽는다. 두 곳 다 고쳐야 한다.

### A-2. 루트에 빌드 디스크립터가 있으면 build 엔진이 거부한다
- **위치**: `src/ici/engines/build.py` — `_has_build_descriptor()`, `_compile_cpp()`
- **현상**: 프로젝트 루트에 `CMakeLists.txt` / `Makefile` / `*.pro` 중 하나라도 있으면
  `"C++ build descriptor requires an adapter; generic g++ was not invoked"` ERROR 로 끝난다.
- **영향**: 정상적인 CMake/qmake 프로젝트는 `ici build` 를 아예 쓸 수 없다.
- **참고**: 검사 범위가 **루트 한 단계뿐**이라, 빌드 디스크립터를 하위 디렉터리(`gui/`)에 두면 회피된다.
  이번 토이 프로젝트들이 쓰는 우회책이다.

### A-3. 테스트 컴파일이 plain g++ 고정
- **위치**: `src/ici/engines/test.py` — `_run_cpp_tests()`, `_run_cpp_test_case()`
- **현상**: `tests/**/*.cpp` 를 각각 `g++ --coverage -std=c++17` 로 컴파일하고 `main.cpp` 를 제외한
  모든 src cpp 를 링크한다. moc 실행 없음, gtest/Catch2 링크 없음, 사용자 플래그 주입 지점 없음.
- **영향**:
  - `Q_OBJECT` 를 가진 클래스는 vtable 미해결로 링크 실패 → Qt 클래스는 단위 테스트 불가
  - gtest/Catch2 등 표준 프레임워크 사용 불가. 각 테스트 파일이 자체 `main()` 을 가져야 한다
  - `-std=c++17` 고정 → C++20/23 프로젝트 검증 불가

---

## B. 엔진 간 스코프 규칙 불일치

### B-1. `cycle` 엔진만 `project.source_dirs` 를 무시한다
- **위치**: `src/ici/engines/cycle.py` — `_iter_cpp_and_headers()`
- **현상**: `os.walk(project_root)` 로 **프로젝트 전체**를 훑는다. 제외 대상은 `.venv`/`venv`/`build`/`.git` 뿐.
  다른 C++ 엔진(`lint`/`dup`/`complexity`/`exception`)은 전부 `get_all_cpp_sources()` = `source_dirs` 기준이다.
- **영향**: `source_dirs = ["src"]` 로 스코프를 좁혀도 `gui/`, `tests/`, `third_party/` 가 순환 검사에 들어온다.
  스코프 규칙이 엔진마다 다르다는 것을 사용자가 알 방법이 없다.

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

### C-1. 🔴 순수 C++ 프로젝트는 기본 설정으로 절대 초록불이 될 수 없다
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

### C-2. 스위트 상태와 콘솔 요약 카운트가 서로 모순된다
- **현상**: 위와 같은 상황에서 콘솔 요약은 `Total Engines: 12 (Pass: 7, Warn: 3, Fail: 1, Error: 0)`
  을 출력하는데 실제 `suite_status` 는 `ERROR` 다. **`Error: 0` 인데 스위트는 ERROR.**
- **영향**: 사용자가 요약 줄만 보면 왜 실패했는지 영원히 알 수 없다. 카운트는 엔진 상태를 세고,
  스위트 상태는 다른 규칙(required+SKIP 승격)으로 정해지는데 그 규칙이 출력 어디에도 없다.
- **제안**: 요약에 스위트 상태와 그 **결정 사유**를 한 줄로 출력한다.
  (예: `Suite: ERROR — required engine 'dead' was SKIPPED`)

### C-3. test 엔진이 실패 사유를 어디에도 남기지 않는다
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

### C-5. C++ 프로젝트에서 `type` 엔진이 항상 WARN 을 만든다
- **현상**: C++ 타입 검사는 미구현이라 소스 파일마다 `[SKIP] C++ type checking is not implemented`
  타깃을 발행하고, 엔진 상태는 `WARN`(mode `pass_warn`) 이 된다.
- **영향**: 모든 C++ 프로젝트가 영구적으로 WARN 1건을 달고 산다. 의도된 동작(README 명시)이지만
  "고칠 수 없는 경고" 는 경고 피로를 유발하고 진짜 WARN 을 묻는다.
- **제안**: 미지원 언어는 WARN 이 아니라 N/A 로 표기 (리포트에 이미 N/A 회색 행 개념이 있다).

