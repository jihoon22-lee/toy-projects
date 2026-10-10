# Phase 2b — lens 워크플로우 깊이

브랜치 `feat/lens-phase2b-workflows`(base `feat/lens-phase2a-contracts` @ e9fe73c).
계획: `docs/plans/2026-10-09-lens-usability-review.md` §5 Phase 2의
잔여 항목 F16, F18, F20, F21, F22.

## 구현

### F16 — `build impact` 경로 해석 + 진단

- `--header` 상대 경로를 이제 **compile_commands.json이 있는 디렉터리**와
  각 엔트리의 `directory`(중복 제거) 기준으로 모두 시도한다. 마지막으로
  cwd도 후보에 포함해 기존 동작을 해치지 않는다(`ops.rs`).
- 그래프에 없는 헤더는 `ImpactReport.hint`(additive 필드)에 basename
  유사 후보(최대 5개)와 경로 전달 방법을 담고 stderr warning으로 출력한다.
  종료 코드는 0 — 그래프 미스는 findings가 아니라 경로 철자 문제로 간주
  (보수적 판단; 계획서는 "에러" 제안이었으나 핸드오프가 warning을 지시).
- `ImpactGraph`에 파일별 include 추출 캐시(`include_cache`) 추가 —
  공유 헤더가 TU마다 디스크에서 재읽히던 O(TU×헤더) I/O 제거.
- 읽기 실패한 소스는 `missing_sources`, 해석 실패 include는
  `unresolved_includes`로 리포트에 집계(기존에는 조용히 drop).
  `scan_truncated`와 함께 stderr 경고로 표면화.
- `buildscope.impact/v1` 스키마 유지 — 새 필드는 `#[serde(default)]` additive.

### F18 — `disk trash` 되돌릴 수 있는 정리

- `trash <PATH>...` 다중 입력 + `--dry-run`(이동 없이 예정 동작만 출력).
  개별 경로 실패는 나머지를 막지 않고 집계 후 마지막에 오류 반환(exit 2).
- `trash list`: `.trashinfo` 기준 항목 나열(이름/삭제 시각/원래 경로/
  파일 존재 여부/identity 사이드카 유무). `--format json` 지원.
- `trash restore <name>`: `.lens.json` identity 사이드카(device/inode/
  mode/size/mtime — rename으로 바뀌는 ctime은 제외)로 trashed 파일의
  교체 여부를 검증하고 원래 경로로 복원. 대상 점유·identity 불일치·
  `..`/빈/숨김 이름은 fail-closed로 거부. 복구 성공 시 `.trashinfo`와
  사이드카를 함께 제거.
- `list`/`restore`는 `--trash-dir`로 비-홈 trash(예: topdir 폴백 결과물)를
  지정 가능.
- EXDEV(파일이 다른 파일시스템) 시 `for_topdir_of`가 마운트 루트를
  st_dev 추적으로 찾아 `$topdir/.Trash-$uid`를 생성하고 mode 0700을
  강제(FreeDesktop 규격).
- `FileIdentity`에 `Deserialize` 추가(사이드카 영속화용).
- lens-disk 단위 테스트: roundtrip(다중 trash→list→restore),
  점유 대상 거부, 조작된 trashed 파일의 identity 불일치 거부,
  안전하지 않은 이름 거부.

### F20 — `log` 입력·출력 보강

- `LogIndexer`의 백엔드를 `enum Backing { Mapped(Mmap), Owned(Vec<u8>) }`로
  일반화. `.gz`는 flate2 + 상한(512 MiB — 번들 해제 상한과 같은 규약),
  `from_reader`로 stdin 스트림을 상한 있는 메모리 버퍼로 수용.
- `get_line_lossy` 추가: invalid UTF-8 줄을 버리지 않고 U+FFFD로 노출,
  `lossy_lines()` 카운트를 `log inspect` JSON 필드와 `log filter`
  stderr/JSON(`lossy_lines`)로 보고.
- `log filter`에 `--regex`(regex::Regex, `--query`와 상호배타),
  `--limit N`(매치 N개 후 중단 — 마지막 매치의 후행 컨텍스트까지 출력),
  `--context N`(매치 전후 N줄, 텍스트는 `[N]-`/`jsonl`은
  `"context": true` 구분), `--format jsonl`(줄당 JSON 오브젝트) 추가.
- `log` 명령의 `path`로 `-`를 받으면 stdin. 진단·요약은 json/jsonl
  모드에서도 stderr로 유지해 stdout이 깨끗한 JSON이 되도록 함.

### F21 — systemd 검색 경로 병합 로더

- `lens-sys`에 `SYSTEMD_SEARCH_DIRS`(/etc → /run → /usr/lib → /lib,
  우선순위 내림차순)와 `load_units_merged(dirs)` 추가.
- 우선순위: 상위 디렉터리의 유닛 파일이 하위를 가린다. `/etc`의
  mask(/dev/null symlink)·alias도 하위 유닛을 완전히 가림. drop-in은
  systemd 규약대로 **모든** 디렉터리에서 수집해 낮은 우선순위 dir부터
  적용(상위 dir의 conf가 키별로 승리). drop-in만 존재하는 유닛은
  `UNIT_STUB` 진단과 함께 스텁으로 합성.
- `doctor`(load_systemd_units)/TUI Services 탭/`sys inspect`·`sys cycles`
  경로 생략 시 모두 이 로더 사용. 명시 경로는 여전히 fail-closed
  (존재하지 않으면 오류).
- 단위 테스트: /etc 우선순위 + 교차 디렉터리 drop-in 병합 + /etc mask가
  벤더 유닛 가림 + drop-in-only 스텁.

### F22 — bundle show/extract + `--log` 원본

- `lens_core::bundle`에 `read_bundle_entry(path, name)`(이름 검증 +
  정규화 후 단일 엔트리 읽기)와 `extract_bundle(bundle, dest, force)` 추가.
  extract는 `verify_bundle_archive`를 먼저 통과해야 하고, 엔트리 이름을
  다시 검증(`..`/절대/역슬래시/NUL 거부, 정규화 후 dest 탈출 검사),
  `--force` 없이 기존 파일 덮어쓰기 거부.
- CLI: `bundle show <bundle> <entry>`, `bundle extract <bundle> <dest>
  [--force]`.
- `bundle create --log`는 요약 JSON 외에 원본 로그 내용을
  `logs/<basename>` 엔트리로 포함. 상한 8 MiB — 초과 시 꼬리만 남기고
  첫 줄 경계에서 시작해 `diagnostics`에 절단 사실을 기록(사건 조사에서
  최근 로그가 가장 가치 있으므로 tail 선택).
- "Bundle cryptographic verification failed" → "Bundle integrity check
  failed"로 정정(문서와 일치: 서명 없는 매니페스트는 무결성 확인).
- `bundle verify` 실패는 이제 Err(2)이 아니라 **findings(1)** — 검증
  자체는 정상 수행됐고 결과가 부정적이기 때문. 읽기 불가/손상 번들은
  여전히 2.

## 검증

- `cargo fmt --all -- --check`: clean.
- `cargo clippy --workspace --all-targets -- -D warnings`: clean.
- `cargo test --workspace --jobs 2`: 전부 통과(cli_regression 34개 포함).
- `python3 .github/scripts/check_docs.py`: 통과(11 Markdown).
- 라이브 재현:
  - F16: `/tmp/f16-demo/cwd`에서 `--header include/common.h` → 1 TU
    (db dir 기준 해석). 없는 헤더 → `similar: .../include/common.h`
    힌트 + exit 0. `<generated.h>` → `unresolved_includes=1` 경고.
  - F18: `disk trash a.txt b.txt` 다중 이동, `list` 나열,
    `restore a.txt` 복원, `--dry-run` 무동작, `../x` 거부(exit 2),
    `/dev/shm` 파일 → EXDEV → `/dev/shm/.Trash-1000/`(0700) 폴백,
    `--trash-dir`로 topdir trash list/restore 왕복.
  - F20: `app.log.gz` 필터, `printf | lens log filter -`, lossy 줄
    U+FFFD + "1 line(s) contain invalid UTF-8", `--regex`/`--context 1`/
    `--format jsonl` 출력.
  - F21: `lens sys inspect`(인자 없음) → 이 호스트에서 370 유닛 로드,
    `sys cycles` → 실제 사이클 1개를 방향 경로+기원으로 보고(exit 1),
    doctor가 같은 로더로 사이클 FAIL 보고.
  - F22: `--log` 번들에 `logs/app.log` 포함, `show`로 내용 출력,
    `extract` → 3 파일 + 검증 통과, 재추출 거부(exit 2) → `--force` 성공,
    python3 tarfile로 위변조 번들 → `verify` exit 1 + "integrity" 문구,
    `extract` 거부(exit 2).

## 편차 / 결정

- F16 헤더 미스는 계획서의 "에러" 대신 **warning + exit 0** — 핸드오프가
  "findings-free-but-warn" 판단을 위임했고, 그래프 미스는 도구 오류가
  아니라 입력 철자 문제라 findings 분류가 어색하다. `hint` 필드로
  JSON에도 남긴다.
- F20 gz/stdin은 계획서의 "스트리밍 경로" 대신 상한 있는 인메모리 버퍼 —
  mmap이 아닌 스트림은 어차피 재방문이 없고, 상한(512 MiB)이 메모리
  폭발을 막는다. `mmap` 기반 strict 검증(SafeInput)은 regular 파일에만
  적용되는 기존 의미론을 유지.
- `--since/--until`은 미구현 — 핸드오프의 필수 목록(`--limit`,
  `--context`, regex, JSON/JSONL)에는 없었고, JSONL ISO 타임스탬프
  파싱은 별도 정확도 작업이 필요.
- `bundle verify` 실패를 exit 2 → **1**로 변경(편차 아닌 규약 정정으로
  간주, GUIDE 표에 추가). Phase 2a에서 Err이던 경로를 findings로 재분류.
- `OutputFormat::Jsonl`은 `log filter` 외 명령에서는 json과 동일하게
  동작(fallback) — cli.rs 주석에 명시.
- `trash` 서브커맨드는 `args_conflicts_with_subcommands` +
  `subcommand_precedence_over_arg`로 구현 — `trash list`가 `list`라는
  이름의 파일을 가리는 모서리가 있음(`./list`로 우회 가능).

## 커밋

`feat/lens-phase2b-workflows`에 항목별 커밋(push 없음):
lens-sys 로더 → lens-build impact → lens-disk trash(+lens-core identity) →
lens-log 인덱서/필터 → lens-core bundle → lens-cli 배선+회귀 테스트 →
docs → 이 문서.
