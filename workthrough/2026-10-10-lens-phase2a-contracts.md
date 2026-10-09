# Phase 2a — lens CLI 계약 반쪽

브랜치 `feat/lens-phase2a-contracts`(base `fix/lens-phase1-accuracy` @ 48cb5f3).
계획: `docs/plans/2026-10-09-lens-usability-review.md` §4/§5 Phase 2의
F11, F13/F19, F14, F15, F17.

## 구현

### F11 — 종료 코드 규약 0/1/2

- `ops::dispatch`가 `Result<Outcome>`을 반환하도록 변경
  (`Outcome::{Clean,Findings}`). `main.rs`가 `Clean→0`, `Findings→1`,
  `Err→2`로 매핑한다. dispatch 내부의 `std::process::exit` 호출은 모두
  제거됐다.
- findings로 분류되는 것: `test diff` regressions/new_failures,
  `abi diff` `compatible == false`(incompatible **와** uncertain —
  uncertain은 수동 검토가 필요하므로 findings; 문서 표에 명시),
  `sys cycles` 사이클, `env check` missing/version_conflicts
  (unevaluated만이면 0), `doctor`의 `--fail-on` 기준 이상,
  `disk scan` incomplete/truncated, `trace analyze` fd 누수.
- Phase 0의 "잘못된 입력 → exit 1"은 전부 **exit 2**로 재조정됐고,
  기존 회귀 테스트의 기대값도 갱신했다. `doctor --fail-on warn|fail`
  (기본 `fail`) 추가.
- **호환성 변경**: CHANGELOG "Changed (breaking)"에 명시.

### F13 — `--format text|json` 통일

- `cli.rs`에 `OutputFormat` value enum 추가. `wants_json(format,
  json_flag, default_json)` 헬퍼로 `--format` 플래그 > 기존 `--json`
  별칭 > 명령별 기본값 순으로 해석한다.
- 기본값은 기존 동작 그대로: JSON 기본이었던 명령(abi/test/trace/sys/
  build/env inspect·diff 류)은 유지, 텍스트 기본(duplicates, cycles,
  env check, bundle, log)도 유지.
- text 모드는 사람이 읽는 요약으로 재작성: `abi inspect`는 SONAME/
  NEEDED/export·import 수, `trace analyze`는 이벤트/콜/에러 + top-5
  syscall, 나머지 diff류는 +/- 건수 요약.
- `net inspect --no-unix` 추가. `disk scan`/`net inspect`/`doctor`의
  `--json`은 `--format json` 별칭으로 유지.
- 계획서가 예시로 든 `--include-events`는 별도 플래그로 두지 않음 —
  `--format json`이 전체 이벤트를 그대로 출력하므로 동일 역할(편차 기록).

### F19 — `disk duplicates`

- `DuplicateFinder::find_in_tree`가 `Vec<DuplicateGroup>` 대신
  `DuplicateReport{groups, errors, total_reclaimable_bytes}`를 반환.
  부분/전체 해시 실패를 `if let Ok`로 삼키지 않고 `errors`에 경로+에러로
  수집한다. `lens_disk` 단위 테스트 + MCP 핸들러 호출부 갱신.
- 텍스트: KiB/MiB/GiB 단위, 그룹별 파일 경로, `Total reclaimable` 요약,
  해시 오류 수. 스캔 경고(`result.errors`)도 stderr로 출력.
- `--format json`은 리포트 전체를 결정적으로 직렬화.

### F14 — `disk scan` 옵션

- `--max-depth`, `--exclude`(반복, 이름 glob), `-x`/`--one-file-system`,
  `--top N`(기본 10)를 `ScanOptions`에 연결.
- 텍스트 출력은 바이트 수 + KiB/MiB/GiB 병기, 최상위 항목 top-N을
  크기 내림차순으로 표시.

### F15 — `test diff` 구조화 + 다중 파일

- `TestDiff`가 `testlens.diff/v2`로 범프: `regressions`/`fixes`가 문자열
  → `CaseChange{id,before,after,message}`로 변경(스키마 상 필드 타입
  변경이라 범프 불가피). `new_failures`/`removed_tests`/
  `skipped_changes`/`diagnostics` 필드 추가.
- 의미론: skipped→passed는 fix가 아닌 `skipped_changes`, 새로 등장한
  failed/error는 `new_failures`(exit 1 findings), baseline에만 있는
  케이스는 `removed_tests`, 중복 identity는 덮어쓰지 않고
  `diagnostics`로 보고.
- 케이스 신원이 `suite::classname::name`을 포함(junit 파서에
  `<testsuite name>` 스택 추적 추가).
- `test parse`/`test diff` 입력이 파일 1개 / `*.xml` 디렉터리 /
  `*`/`?` glob을 받아 `merge_test_runs`로 병합(결정적 정렬).
- 도움말의 "flaky analysis" 언급 제거.

### F17 — net owner_unknown

- `SocketEntry.owner_state: Option<OwnerState>`(`orphan`|
  `owner_unknown`, serde default + skip_if_none — additive).
- `scan_process_socket_inodes`가 `/proc/<pid>/fd` 읽기 실패 PID 수
  (`uninspectable_processes`)와 해당 프로세스의 uid 집합을 수집.
  미상관 소켓은 uid가 uninspectable 집합에 속하면 `owner_unknown`,
  아니면 `orphan`. inode=0 커널 소켓과 unix 소켓은 분류 제외.
- `NetSummary`에 `owner_unknown_sockets`/`uninspectable_processes`
  추가(serde default, `lens.net/v1` 유지).
- 텍스트 표는 `<owner unknown>`/`<orphan>` 구분. doctor는
  owner_unknown만 있으면 PASS + "sudo로 재실행" 권고(진짜 orphan만
  WARN).

## 검증

- `cargo fmt --all -- --check` — clean
- `cargo clippy --workspace --all-targets -- -D warnings` — clean
- `cargo test --workspace --jobs 2` — 전부 통과(31개 테스트 타깃,
  0 failed). `cli_regression` 26개 중 신규 Phase-2 테스트 7개 추가
  (doctor --fail-on 양방향, format text/json, disk --top/--exclude/
  --max-depth, duplicates JSON/text, multi-file diff + 구조 의미론,
  net owner_unknown/orphan/--no-unix).
- `python3 .github/scripts/check_docs.py` — 통과(10 files).
- 라이브 재현(WSL 호스트):
  - `test diff` regression→1 / clean→0 / bad input→2
  - `disk scan --top 2` — "2.9 KiB big.bin" 식 top-N + 사람 단위
  - `disk duplicates` — 그룹 + `Total reclaimable: 2.9 KiB`
  - `net inspect` — `Owner-Unknown Sockets: 56`, `Orphan Sockets: 0`,
    `Uninspectable Procs: 358`
  - `doctor` — orphan 체크가 PASS + sudo 권고, `--fail-on warn` → exit 1
  - `sys cycles` fixture → exit 1, `abi diff /usr/bin/true /usr/bin/false`
    → incompatible + exit 1, `env check` missing→1 / invalid→2,
    `trace analyze` fd 누수 → exit 1

## 결정/편차

- `abi diff`의 `uncertain`은 findings(exit 1)로 분류 — 계획서가 별도
  기준을 주지 않아 보수적으로 "사람 검토 필요"로 처리하고 GUIDE 표에
  명시.
- `trace analyze` fd 누수는 계획서 F11 예시 목록에 없지만 findings로
  분류(같은 규약의 정신에 부합; GUIDE 표에 명시).
- `--include-events` 미구현 — `--format json`이 이미 전체 이벤트를 출력.
- `test diff` 스키마는 필드 타입 변경(문자열 → 구조체)이라 `v1`→`v2`
  범프. 나머지 스키마는 additive 필드 + serde default로 유지.
- owner_unknown 분류는 uid 매칭 휴리스틱 — 읽지 못한 프로세스의 uid와
  소켓 uid가 같을 때만 unknown으로 간주(더 보수적인 orphan 분류 유지).
- `--max-depth 0`은 truncated → incomplete 스캔 → exit 1(의도된 동작,
  회귀 테스트에 고정).

## 잔여

- Phase 2의 나머지 항목(F16 build impact, F18 trash, F20 log 옵션,
  F21 doctor 범위, F22 bundle, 등)은 이번 범위 밖.
