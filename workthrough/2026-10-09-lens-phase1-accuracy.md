# Lens 사용성 리뷰 Phase 1 — 정확성 수정

## Overview

`docs/plans/2026-10-09-lens-usability-review.md`의 Phase 1 항목
(F01, F04–F09)을 `fix/lens-phase1-accuracy` 브랜치에 구현했다.
기반은 Phase 0 브랜치 `fix/lens-phase0-safety`(e258ed0). 커밋은 항목별로
분리했으며 push하지 않았다.

## Changes Made

### F01 — lens-env: PEP 508 마커 평가
- `crates/lens-env/src/markers.rs` (신규): 마커 토크나이저/파서/평가자.
  `and`/`or`/`not`, 괄호, `==`/`!=`/`<`/`<=`/`>`/`>=`/`~=`/`in`/`not in`,
  PEP 440-lite 버전 비교, `python_version`/`sys_platform`/`platform_system`/
  `os_name`/`extra` 등의 변수 지원. 평가 불가는 `Unknown`(3상태).
- `PyVenv`에 `unevaluated_dependencies`·`version_conflicts`·활성 extra
  필드 추가. 거짓 마커 요구는 스킵, `extra == "x"`는 `--extras`로 활성화한
  extra에서만 적용, 설치됐지만 범위 불일치는 `version_conflicts`로 보고.
- uv venv의 `pyvenv.cfg` `version_info` 키 인식(기존에는 `version`만 읽어
  `python_version`이 비어 모든 마커가 평가 불가였음).
- `lens env check --extras a,b`와 MCP environment checker에 extra 전달.

실제 검증: `/home/jihoon/projects/llm-usage-dashboard/.venv`에서
`lens env check` → 거짓 missing 0건("All dependencies satisfied").
`--extras testing` → 10건 미충족을 보고했고 site-packages에 실제로 없음을
확인(gunicorn의 coverage/gevent/h2/httpx/inotify/packaging/pytest-*
/uvloop).

### F04 — lens-log: --min-level이 Unknown 제외
- `--min-level` 지정 시 레벨을 알 수 없는 줄을 제외하고 stderr에 제외
  수를 보고. `--include-unknown`으로 복원.
- syslog PRI(`<33>`)와 커널 printk(`<3>`) 우선순위 토큰에서 레벨 추출.

실제 검증: `/var/log/kern.log --min-level error` → level 식별된 2줄만
매칭(Fatal), Unknown 39,902줄 제외 보고.

### F05 — lens-trace: fd 테이블 공유·CLOEXEC·close_range
- `CLONE_FILES` clone/fork는 fd 테이블을 공유(Rc<RefCell>)해 한 스레드의
  close가 공유 fd를 해제. 일반 fork는 사본 상속.
- `O_CLOEXEC` fd 추적 → `execve`에서 해제. `close_range` 구간 해제.
- 에러 집계 키를 `syscall:errno`로 변경.
- 테스트의 합성 리프로(fd 3/5 거짓 leak)가 더 이상 leak으로 보고되지 않음.

### F06 — lens-abi: export/import 분리, weak 표기
- `SymbolEvidence.defined` 추가. 미정의 동적 심볼은 `abi.imports`로 분리하고
  diff는 `imports` SetDiff로 별도 비교 — 제거된 export가 아니므로
  import만의 변경은 compatible.
- weak 심볼은 `binding = "weak"`로 직렬화.
- 스키마는 `abilens.diff/v3` 유지(신규 필드는 `#[serde(default)]` 추가형).

### F07/F08 — lens-sys: 실 방향 사이클 + 완전 diff
- 사이클은 SCC 멤버의 알파벳 나열 대신 실제 방향 경로를 재구성하고 각
  엣지의 기원(유닛 파일:라인, 디렉티브)을 `--Wants(a.service:12)-->` 형태로
  표시. `/dev/null` masked 유닛은 엣지 대상에서 제외, alias/템플릿 해석.
- `sys diff`는 유닛의 모든 섹션·키를 비교(`User=` 추가 검출)하고 스냅샷
  JSON 외에 유닛 디렉터리도 입력으로 허용.

### F09 — lens-build: transitive_impact
- `reverse_impact`는 기존 의미(직접 includer) 유지. 신규
  `transitive_impact`는 헤더 체인을 거친 간접 includer까지 포함
  (`buildscope.snapshot/v4` 유지, 신규 필드만 추가).

## 검증

- `cargo fmt --all -- --check` ✅
- `cargo clippy --workspace --all-targets -- -D warnings` ✅ (0 warning)
- `cargo test --workspace --jobs 2` ✅ 전체 통과(119 tests, 0 failed)
- `python3 .github/scripts/check_docs.py` ✅
- CLI 회귀 테스트 7개 추가(`crates/lens-cli/tests/cli_regression.rs`,
  CARGO_BIN_EXE_lens + 임시 fixture; ABI 테스트는 cc 없으면 self-skip)

## 커밋

- 7184456 fix(env): PEP 508 마커, extras, version conflicts
- ab92e05 fix(log): --min-level Unknown 제외
- 800db6f fix(trace): 공유 fd 테이블, CLOEXEC, close_range
- b9a644c fix(abi): defined 심볼만 diff, weak binding
- a35ce65 fix(sys): 방향 사이클 경로 + 완전 diff
- 6e5e581 fix(cli): 사이클 기원 출력 + 디렉터리 diff 입력 배선
- 0eb3e0d fix(build): transitive_impact
- 89b4e4b test(cli): phase 1 회귀 테스트
- 735f443 docs: changelog/guide/architecture
