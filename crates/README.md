# Lens 크레이트 레퍼런스 (`crates/`)

Lens는 단일 Rust 워크스페이스다. 각 `lens-*` 크레이트가 하나의 진단 도메인을
담당하고, `lens`(lens-cli) 바이너리와 `lens-mcp` 서버가 공용 진입점이다.

공통 계약:
- **Fail-Closed**: 파싱 불가·증거 부족 시 안전한 것으로 간주하지 않는다.
- **Evidence-Bound**: 출력은 관측된 파일 오프셋/줄/경로와 해시에 근거한다.
- **Deterministic JSON**: `lens_core::to_deterministic_*`로 키를 정렬해 출력한다.
- **3상태 호환성**: `Compatibility::{Compatible, Incompatible, Uncertain}`.

---

## lens-core — 공통 기반

| 모듈 | 역할 |
|---|---|
| `identity` | `SafeInput`(open→fstat→verify)과 `FileIdentity`로 TOCTOU 방어, `mmap()` |
| `hash` | SHA-256 `digest_bytes`/`digest_file`, `IncrementalHasher` |
| `diff` | `Compatibility` 3상태 enum, `SetDiff<T>` |
| `evidence` | `Evidence`, `Source`, 상한 있는 `BoundedCollector` |
| `json` | 결정론적 JSON 직렬화(키 정렬) |
| `time` | UTC/로컬 ISO-8601 타임스탬프 유틸 |
| `bundle` | `lens.bundle/v2` — tar.gz + `manifest.json`(SHA-256, 크기·항목 상한) 생성/검사/검증 |

## lens-disk — 파일시스템 분석

- `ArenaTree`: `u32` 인덱스 연속 아레나. 자식 삽입 O(1)(`last_child`),
  크기 집계는 반복형 post-order(깊은 트리 스택 안전).
- `DiskScanner`: 최대 깊이/항목 수, 제외 패턴, 마운트 경계(`one_file_system`),
  심볼링크 루프 감지(링크 **타깃**의 dev/ino 사용). `ScanOptions::parallel`이
  실제 rayon 병렬 stat을 수행.
- `DuplicateFinder`: 크기→inode 묶음→4KB 부분 해시→전체 SHA-256 순으로 필터.
  같은 inode의 하드링크는 회수 가능 용량에서 중복 계산하지 않는다.
- `TrashManager`: FreeDesktop Trash v1.0 (`files/` + `.trashinfo` 영수증).
- 스키마: `diskmap.snapshot/v2`, `SnapshotDiff`.

```rust
use lens_disk::{DiskScanner, ScanOptions, DuplicateFinder, SnapshotV2};
let res = DiskScanner::new(ScanOptions::default()).scan("/path")?;
let groups = DuplicateFinder::new(1<<20).find_in_tree(&res.tree, "/path".as_ref())?;
let snap = SnapshotV2::from_tree(&res.tree, res.root_id, res.complete, res.truncated);
```

## lens-abi — ELF/ABI 분석

- `inspect_elf`: ELF 헤더, `.dynamic`(NEEDED/RPATH/RUNPATH/SONAME/PT_INTERP),
  `.gnu.version_r`/`.gnu.version_d`의 심볼 버전 요구사항, dynsym 증거
  (`defined` 여부, 디맹글링 포함)를 수집.
- `dwarf`: `.debug_info`가 있으면 gimli로 선언 타입명을 추출(최대 10,000건,
  stripped 바이너리는 진단 메모만 남김). 타입 그래프(멤버/레이아웃 비교)는 미구현.
- `diff_reports`: 심볼/vtable/타입 집합 차분 + 속성 변경(타입·바인딩·데이터 크기)
  + 헤더 변경을 종합해 3상태 호환성 판정. 경로/의존성 변경은 `Uncertain`.
- 스키마: `abilens.report/v2`, `abilens.diff/v3`(v3에서 `compatibility` 어휘가 `compatible|incompatible|uncertain`으로 통일되고 버전 요구사항 diff가 `abi` 필드에 채워짐).

## lens-log — 로그 분석

- `LogIndexer`: mmap + `memchr`로 라인 오프셋 테이블 구축.
- `parse_line`: JSONL(`level`/`msg`/`ts` 등, 키 대소문자 무시, 나머지 키는
  `fields`에 보존)과 `LEVEL ...` 휴리스틱. `detect_level`은 레벨만 빠르게 반환.
- `LogFilter`: min_level/query/source. `matches_line`은 no-op 필터와
  substring-only 거절을 파싱 없이 단락.
- 스키마: `loglens.session/v2`.

## lens-test — JUnit 파싱

- `quick-xml` 스트리밍 파서: 속성 엔티티 디코딩, testcase 상태
  (failure/error/skipped) 메타데이터, `<system-out>`/`<system-err>`와
  `<properties>` 수집, 열린 태그 잔존 시 `complete=false`(잘린 XML 감지).
- run id는 입력 해시 기반 결정적 생성.
- 스키마: `testlens.run/v1`, `testlens.diff/v1`.

## lens-trace — strace 분석

- strace 라인 파싱, `unfinished`/`resumed` 스티칭, 지연 시간 집계, errno 분포.
- fd 추적은 tid별 테이블: open/openat/socket/accept/dup/pipe2([3,4]) 등
  생성 시스템콜과 `+++ exited`에서 잔여 fd를 `fd_leaks_by_process`에 귀속.
- 스키마: `tracelens.snapshot/v1`, `tracelens.diff/v1`.

## lens-sys — systemd 정적 분석

- `load_units`(공용 로더): 단일 파일 또는 디렉터리의 unit을 로드하고
  `<unit>.d/*.conf`를 이름순으로 병합. 읽기 실패는 진단 스텁으로 보존.
- 파서는 할당 순서를 유지해 `ExecStart=` 등 빈 할당 리셋 의미론을 지원하고
  `%u`/`%h` 등 specifier를 `User=` 기준으로 확장.
- `OrderingGraph`: Before/After DAG + Tarjan SCC 사이클 탐지.
- 스키마: `servicelens.snapshot/v1`, `servicelens.diff/v1`.
- 지원 범위는 `systemd-255-subset-v1`로 명시 — 전체 systemd 의미론의 부분 집합.

## lens-build — 빌드 영향도

- `compile_commands.json` 파싱: `-I`/`-isystem`, `-D`, `-std`, `-o`,
  `-include`(강제 포함) 추출, 경로 정규화(`normalize_path`).
- `ImpactGraph::add_translation_unit`: 소스 파일의 `#include`를 디스크에서
  해석(`"…"`은 포함 파일 기준 우선, `<…>`은 검색 경로만)하고 헤더 간
  전이 클로저를 구축. `compute_impact`는 역방향 BFS로 영향받는 TU를 반환.
- 스키마: `buildscope.snapshot/v4`, `buildscope.diff/v1`, `buildscope.impact/v1`.

## lens-env — Python 환경 감사

- 인터프리터를 실행하지 않는 정적 분석: `pyvenv.cfg`, `*.dist-info/METADATA`
  (PEP 376/503), `Requires-Dist` 검증, 프로젝트 소스의 stdlib/서드파티
  모듈 섀도잉 탐지.
- 스키마: `envlens.snapshot/v1`, `envlens.diff/v1`.

## lens-net — 소켓 포렌식

- `/proc/net/{tcp,tcp6,udp,udp6,unix}` 파싱(주소는 커널의 호스트 엔디안
  표기 — `to_ne_bytes` 의도적 사용), `/proc/[pid]/fd`로 프로세스 상관.
- UDP는 LISTEN 상태가 없으므로 바인드된 wildcard 소켓(st=07, remote `*:0`)을
  리스너로 집계.
- diff는 리스너를 (kind, 주소, 포트)로, 연결을 5-tuple로 매칭(inode는
  스냅샷 간 불안정). `127.0.0.1→0.0.0.0` 확장을 감지.
- 스키마: `lens.net/v1`.

## lens-cli — `lens` 통합 CLI

- `src/cli.rs`: clap 커맨드 트리. `src/ops.rs`: 전 서브커맨드 디스패치
  (라이브러리로 공개 — lens-mcp가 재사용). `main.rs`는 얇은 래퍼.
- `doctor`: 스토리지/네트워크/서비스/환경 종합 헬스체크(`--json` 지원).
- `bundle create/inspect/verify`: `lens.bundle/v2` 아카이브.
- `tui`: `lens_tui::run`으로 진입. `completion`: 셸 자동완성 생성.

## lens-tui — 터미널 대시보드

- 4개 탭 실데이터: Storage(아레나 스캔), Services(`load_units` 결과),
  Logs(`LogIndexer`로 시스템 로그 tail + 레벨 컬러), Network(리스너 목록).
- `lens_tui::run(path)`가 렌더 루프를 소유해 `lens-tui` 바이너리와
  `lens tui`가 공유한다.

## lens-mcp — MCP 서버

- JSON-RPC 2.0 over stdio로 진단 툴을 노출:

| 툴 | 용도 |
|---|---|
| `lens_disk_scan`, `lens_disk_duplicates` | 스토리지 분석/중복 탐지 |
| `lens_abi_inspect` | ELF/심볼/버전/DWARF 검사 |
| `lens_log_filter` | 로그 필터 |
| `lens_trace_analyze` | strace 분석 |
| `lens_sys_cycles` | systemd 사이클 |
| `lens_build_impact` | 헤더 영향도 |
| `lens_env_check` | venv 의존성 검사 |
| `lens_net_inspect` | 소켓/리스너 검사 |
| `lens_bundle_verify` | 번들 무결성 검증 |
| `lens_doctor` | 종합 진단 |

```json
{ "mcpServers": { "lens": { "command": "/path/to/lens-mcp" } } }
```
