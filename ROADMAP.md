# ROADMAP

각 제품을 어느 방향으로, 왜 그 순서로 키울지 적는다. 제품 소개와 구조 규칙은
[README.md](README.md)에 있다.

원칙은 하나다: **각 제품의 본질과 기능만 기준으로 기술 스택과 우선순위를 정한다.**
공용 도구·공용 규격에 맞추기 위해 제품이 자기 스택을 억지로 선택하지 않는다.

## 공통 방향

- **스택 현대화**: 레거시 빌드·테스트 기반을 각 제품의 표준 스택으로 옮긴다.
- **독립 릴리스**: 제품마다 자체 버전, 자체 태그(`{product}/vX.Y.Z`), 자체
  아티팩트를 가진다.
- **기능 강화**: 견고성과 실사용 기능을 넓히는 것이 최우선이다.

## 제품별 방향

### diskmap — 디스크 사용량 탐색과 정리 workbench

- **qmake → CMake + Qt6 단일화**: qmake는 Qt6 시대의 레거시이며, CTest/Qt Test 표준
  경로로 옮긴다. 수제 `check` 하니스는 Qt Test로 대체한다.
- 스캔·스냅샷·중복 증거의 보수적 계약(identity revalidation, read-only load,
  advisory flock)은 그대로 유지한다.
- 완료: 스냅샷 diff 필터, 오프라인 스냅샷 비교, cleanup 드라이런 플랜.
- 이후: 대용량 트리의 증분 재스캔 — 디렉터리 mtime이 파일 내용 변경에
  전파되지 않아 정확성 훼손 위험이 있으므로 신중한 설계가 필요하다.

### loglens — 로그 조사 workbench

- **Qt6 단일화**: Qt5/Qt6 듀얼 빌드를 Qt6만으로 좁혀 복잡도를 줄인다.
- **수제 테스트 하니스 → Qt Test**: 표준 러너와 CI 친화적 출력으로 교체한다.
- 완료: 선언적 정규식 파서 플러그인, `loglens.session/v1` 세션 저장/복원
  (CLI + GUI).
- 이후: 세션에 triage 상태(북마크·주석) 번들 — 별도 스키마 확장이 필요하다.

### buildscope — compile database 탐색기

- **Python 백엔드 → C++ 통합**: 현재 Python producer + C++ consumer의 하이브리드는
  실행 시 Python 인터프리터 의존과 별도 패키징 부담을 만든다. `compile_commands.json`
  파싱과 정규화는 QJsonDocument/C++로 충분히 표현되므로 단일 바이너리로 통합하고,
  스냅샷 스키마 호환(v1/v2/v3 reader)은 유지한다.
- 완료: 스트리밍 compile DB 파싱, `--include-analysis delayed` +
  `--analysis-unit` 선택적 재생.

### envlens — Python 환경 인스펙터

- **순수 Python 유지**: Python 환경을 검사하는 도구이므로 언어 자체가 본질적이다.
  Hatchling/uv, pytest, ruff, mypy strict 게이트를 유지한다.
- 완료: venv/conda/system 감지(snapshot v2), 단일 스냅샷 `check`,
  `Requires-External` unknown 증거, v1/v2 스키마 병행 지원.

### abilens — ELF/ABI 아티팩트 인스펙터

- **`readelf` 의존 제거**: 현재 binutils `readelf` 출력을 파싱하는 외부 프로세스
  경계를, ELF header/section/symbol/dynamic을 직접 읽는 자체 파서로 대체한다.
  외부 도구 버전·locale에 출력이 흔들리는 근본 원인을 없앤다.
- 완료: `.dynsym` 네이티브 파싱, `name@version` 심볼 한정, vtable diff 축,
  정책 DSL(심볼/버전/rpath/stripped 규칙).
