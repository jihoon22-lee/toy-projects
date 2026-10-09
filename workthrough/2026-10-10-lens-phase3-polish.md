# lens Phase 3 — MCP 확장 + TUI UX + 진단/메시지 정리

## Overview

Phase 3(`feat/lens-phase3-polish`, base `7743a61`)는 usability 리뷰의 나머지
항목 — MCP 응답 크기 제어와 diff 도구(F24), TUI 백그라운드 스캔·네비게이션·
검색(F23), 도움말/에러 메시지 정리(F25/F26), env shadowing 품질(F27),
systemd 파서 진단(F28) — 을 구현했다. 코드는 결정적·additive이며 CLI/MCP
계약(Outcome 0/1/2, `--format`, `result.isError`)은 유지된다.

## F24 — MCP 응답 제어 + diff 도구

### Size limits / pagination (`handler.rs`)

- `serialize_result`: 출력 크기 64KiB 클램프 + 배열 pagination.
  - `truncate_value(value, limit, offset)`: 재귀로 모든 배열을 자르고 마지막에
    `{"_truncated": true, "total_before_truncation": n, "omitted": k}` 마커 추가.
  - 클램프로까지 자른 경우 최상위에 `_response_clamped: true`.
  - **self-paginated 결과는 재자르지 않는다** — `limit`/`offset`/`tail`을
    이미 적용한 `Paginated { items, _paginated: true }`는 `{items, offset,
    has_more, _truncated, total_before_truncation}`로 직렬화.
- 모든 도구가 `limit`(기본 200) `offset` 인자를 받는다.
- `lens_log_filter`: 첫 1000줄 제한 폐기. `tail`(마지막 N줄 스캔,
  `paginate_tail` 재사용), `regex`(설정 시 `contains` 이중 조건),
  `include_unknown`, `limit`/`offset`(매치 페이지 + `has_more`) 추가.
- path base 명시: 모든 도구 설명에 "server process working directory" 기준
  문구 추가. `lens_build_impact`의 상대 `header`는 컴파일 DB 디렉터리와
  각 entry의 `directory` 기준으로 해석 — `resolve_build_header`를
  lens-cli에서 lens-build 공용으로 이동해 공유.
- `lens_doctor`: `root_path`/`proc_dir`/`systemd_dir`이 존재하지 않으면
  JSON-RPC error 대신 `isError: true` + 경로 메시지.
- `initialize`: 클라이언트 `protocolVersion`을 에코(없거나 알 수 없으면
  `2024-11-05`). 응답 capabilities에 `{"tools": {}}` 포함.

### diff 도구

| 도구 | 입력 |
|---|---|
| `lens_abi_diff` | `base`, `current` ELF 경로 |
| `lens_sys_diff` | 스냅샷 JSON, 또는 `path_a`/`path_b` 유닛 디렉터리(loader 사용) |
| `lens_test_diff` | `baseline`/`current` 파일 또는 디렉터리(merge) |
| `lens_net_diff` | 스냅샷 JSON 경로만(라이브 캡처는 lens-cli 소유라 MCP는 non-live) |

회귀/비호환 → `isError: true`(MCP 도구 결과가 "실패"를 표현하는 관례), 호환
결과는 정상 result.

## F23 — TUI (`app.rs`, `term.rs`, `main.rs`, ops.rs/cli.rs)

- `App`가 `path`/`path_id`/`node_id`/`arena`/`search_progress`/`scan_error`/
  `scan_in_flight`/`scan_started`/`log_path`/`log_override`를 보유.
- **백그라운드 스캔**: `mpsc::channel`로 `(PathBuf, NodeId, DirTree,
  Instant)`를 받아 도착 시에만 배치로 arena에 삽입(`scanned_this_cycle`
  상한으로 렌더/입력을 블록하지 않음). 진행은 `progress(running, scanned,
  total, total_bytes)` 타이틀 스피너 + `scan_animation()` 프레임으로 표시.
- **In-memory navigation**: 스캔된 트리 안에서는 `navigate_into`/`navigate_up`
  이 arena 노드만 이동(재스캔 없음). 미완료 child(`pending_marker`)/스캔
  루트 위로 나갈 때만 새 백그라운드 스캔 시작. `navigate_to`로 임의 경로
  점프 지원 — 나중에 `handle_pending_child_visited`로 자녀 매칭.
- **로그 검색**: `/` → 입력 모드(제목 `Filter: …`), Enter 적용 / Esc 해제.
  `filter_log_view`: `needs_scan`/`msg`/`level` 필터로 Indexed 로그를
  재구성. `--log PATH`로 시작 시 해당 파일을 자동 감지 대신 사용
  (`log_override`; `try_open_log`로 직접 열어 `LogIndexer`를 채움).
- 키: `PgUp/PgDn`(가시 행 기준 페이지), `g/G`, `?`(도움말 토글),
  `Ctrl+L`(로그 리로드). j/k는 가시 높이 클램프.
- `TuiApp::new`은 canonicalize + 최대 4레벨 얕은 스캔으로 즉시 진입 가능 —
  시작 경로 `.`에서 Backspace가 parent로 동작.
- `term::run`: 스캔 채널 + `tick`(스피너 프레임 갱신). 스캔 결과 수신 시
  `receive_scans`/`handle_pending_child_visited`로 UI 상태 갱신.
- 패닉 훅은 Phase 0에서 이미 추가됨(alt screen/leave raw mode 후 체인).

## F25/F26 — 메시지·도움말 정리

- `LensError::Usage` 신설(Display = 메시지 그대로). 사용 오류(필수 인자
  누락, 잘못된 플래그 값, `--force` 누락, 잘못된 포맷 등)가
  "Corrupt or invalid input format:" 접두사를 붙이지 않고 출력.
- `parse_json_file(path, ...)`: `serde_json::from_reader(File)` 결과 에러에
  파일 경로를 `InvalidInput(format!("{}: {}", path.display(), e))`로 포함.
- `cli.rs`: 모든 positional/optional 인자에 한국어 설명 추가, `--help`에
  다중 파일 스페이스 구분 표기, deprecated "flaky analysis" 문구 제거.
- GUIDE: `abilens.diff/v3` 표기 정정, 결정성에 타임스탬프(`created_at`) 예외
  명시, `log filter` 1000줄 주장을 tail/limit/offset 설명으로 교체, TUI에
  `--log`/새 키 추가.

## F27 — env shadowing 품질 (`shadowing.rs`, `model.rs`, `venv.rs`, `metadata.rs`)

- `stdlib_module_names()`: `sys.stdlib_module_names`에 준하는 확장 목록
  (`secrets`, `test`, `wsgiref`, `zoneinfo` 등 200+).
- `project_modules(root)`: 프로젝트 루트와 `src/`의 1레벨 `.py` 파일과
  `__init__.py` 패키지 디렉터리만 후보 — `pkg/json.py` 같은 깊은 파일은
  제외해 거짓양성 제거. 디스크에 직접 나열 + pkg dir은 `--package-dir`
  추적(2.3에서 이미).
- `PyPackage.top_level_modules: Vec<String>` — dist↔모듈 불일치를 위해
  `metadata::top_level_modules()`가 `top_level.txt` 파싱, 없으면 `RECORD`
  파일 최상위 파트 추정. 예: `typing_extensions`, `yaml` (`PyYAML`).
- `shadow_report`: 모듈명 비교를 dist 이름 소문자가 아닌 `top_level_modules`
  우선으로(비어 있으면 dist 이름 폴백) 비교 — `llm-usage`가 `llm_usage`를
  shadow하는 케이스 포착.
- uv `version_info`는 Phase 1에서 이미 처리됨을 확인.

## F28 — systemd 파서 진단 (`parser.rs`, `loader.rs`, `ops.rs`, model.rs 재사용)

- `unit_entries`가 `(HashMap, Vec<Diagnostic>)` 반환 — 호출자는 두 번째
  반환을 유닛/스냅샷 진단에 append.
- `=` 없는 비주석 라인: `severity=warning`, `code=SYNTAX_GARBAGE_LINE`,
  `file`/`line`/`message`에 원문.
- 닫히지 않은 `[Name` 라인: `code=SYNTAX_SECTION_HEADER`.
- `loader.rs`: 유닛 로드 후 의존 그래프의 엣지 타겟(`After`/`Before`/
  `Requires`/`Wants`/`Conflicts`/`BindsTo`/`PartOf`/`ConsistsOf`)을 수집,
  로드된 유닛·alias·masked(`/dev/null`)·템플릿(`foo@.service` ↔ `foo@inst.
  service`)에도 매칭되지 않으면 `code=UNIT_REF_MISSING` 경고 추가.
  `.device`/`.mount`/`swap`/`slice`/`scope`/`target`/`timer`/`path` 등
  생성형 유닛은 참조 매칭에서 제외.
- `ops.rs`: `sys_diagnostics(&snapshot)`이 모든 유닛의 진단을 평탄화해
  `s.diagnostics` 상위 배열에 채움 — 파일·라인·메시지·code를 그대로 유지.

## Code examples

```rust
// crates/lens-sys/src/loader.rs — missing referenced units (post-load)
const REF_KEYS: [(&str, RefKeys); 4] = [ ... ];
if !loaded.contains_key(&name) && !is_generated(name) && !is_template_instance(...) {
    unit.diags.push(Diagnostic{ code: "UNIT_REF_MISSING".into(), ... })
}
```

```rust
// crates/lens-mcp/src/handler.rs — structured truncation marker
fn truncated_marker(total: usize, omitted: usize) -> Value {
    json!({ "_truncated": true, "total_before_truncation": total, "omitted": omitted })
}
```

## Tests added

- `crates/lens-sys`: 파서 garbage/unclosed 테스트 + loader missing-ref 테스트
  (fixture 유닛 간 `Requires=missing.service` → UNIT_REF_MISSING).
- `crates/lens-env`: 패키지 디렉터리(`logging/__init__.py`) 탐지, nested
  `pkg/json.py` 오탐 없음, `top_level_modules` 매핑.
- `crates/lens-mcp`: ping, protocolVersion 에코, doctor bad-path isError,
  log tail/limit/offset, response clamp, self-paginated 중복 방지.
- `crates/lens-tui`: `parent_from_relative_start`(canonicalize + 부모),
  `log_filter_view`.
- `crates/lens-cli`: Usage exit 2가 "Corrupt" 접두사 없음, JSON parse error가
  파일 경로 포함 — 회귀 테스트.

## Verification

```text
cargo fmt --all -- --check            → clean
cargo clippy --workspace --all-targets -- -D warnings → 0 warnings
cargo test --workspace --jobs 2       → 모든 타깃 pass(163 unit + cli_regression 39 + mcp_stdio 14)
python3 .github/scripts/check_docs.py → 12 Markdown files OK
```

Live repro: `lens sys inspect` on this host → `units: 370 diags: 63`
(`UNIT_REF_MISSING:40, UNIT_ALIAS:13, UNIT_MASKED:6, UNIT_STUB:4`). MCP stdio:
initialize `protocolVersion` 에코, doctor bad-path `isError:true`, ABI imports
`_truncated` 마커, log `tail`이 파일 끝 라인만 반환. `lens tui --log` help는
비-TTY에서도 `--log` 플래그를 출력한다.
