# Lens 사용 가이드

단일 바이너리 `lens`가 9개 진단 도메인(disk, abi, log, test, trace, sys, build,
env, net)과 포렌식 번들(`bundle`), 종합 점검(`doctor`), TUI 대시보드를 제공합니다.
모든 JSON 출력은 `to_deterministic_pretty`를 통해 결정적으로 직렬화되며,
각 스키마는 `*.schema` 식별자로 버전이 명시됩니다.

## 빌드 및 설치

```bash
cargo build --release -p lens-cli
# target/release/lens

# 셸 자동완성
source <(lens completion bash)   # zsh, fish도 지원
```

## 도메인별 사용법

### 1. disk — 파일시스템 분석

```bash
lens disk scan <path> [--json] [--parallel]
lens disk duplicates <path> [--min-size BYTES]
lens disk trash <path>
```

- `scan`: 아레나 트리로 계층 구조 + 논리/할당 크기 집계. `--json`은
  `diskmap.snapshot/v2` 출력. 심볼링크 루프 감지, 깊이/항목 상한 도달 시
  `complete=false`로 표시되며 per-entry 오류는 stderr로 나옵니다.
- `--parallel`: rayon으로 per-entry stat 병렬화(대형 트리에서 유효).
- `duplicates`: 부분 해시 → 전체 SHA-256 2단계 판정. 하드링크(inode 공유)는
  회수 가능 용량에서 제외, `(dev=0,ino=0)` 파일은 경로 기반 폴백.
- `trash`: FreeDesktop Trash 규격으로 이동(`.trashinfo` 기록 포함,
  심볼링크는 타깃이 아닌 링크 자체가 이동됨, 복구 영수증 출력).

### 2. abi — ELF 바이너리 검사

```bash
lens abi inspect <binary>
lens abi diff <baseline> <candidate>
```

- 동적 심볼 표면(정의/미정의 구분), DT_NEEDED, RPATH/RUNPATH, SONAME,
  인터프리터, 심볼 버전 요구사항(VERNEED/VERDEF), `.debug_info` 타입명 수집.
- 스트립된 바이너리도 PT_DYNAMIC/PT_INTERP program header 폴백으로 파싱.
- `diff`는 `abilens.diff/v2`: 추가/제거 심볼, 버전 요구사항 변경,
  `Compatibility`(compatible/incompatible/unknown) 3상태 판정.

### 3. log — 로그 인덱싱·필터

```bash
lens log inspect <path>
lens log filter <path> [--query TEXT] [--min-level LEVEL]
```

- mmap 라인 인덱서(memchr 기반)로 GB급 로그를 즉시 열람.
- `--min-level`: trace|debug|info|warn|error|fatal (오타 시 즉시 거부).
- 레벨 감지·메시지 추출은 무할당 슬라이딩 윈도우로 수행.

### 4. test — JUnit 리포트 파싱·회귀 diff

```bash
lens test parse <file> [--project NAME]
lens test diff <baseline> <candidate>
```

- `testlens.run/v1`: 테스트케이스 신원(`classname::name`), 상태
  (passed/failed/error/skipped), 실패 본문, `system-out`/`system-err`,
  `<properties>`, suite 출력까지 수집.
- 잘린/깨진 XML은 가능한 범위까지 파싱하고 `complete=false`로 표시.
- `run_id`는 내용 기반 결정적 해시(재실행해도 동일).
- `diff`는 `testlens.diff/v1`: REGRESSION/FIX 목록.

### 5. trace — strace 분석

```bash
lens trace analyze <trace_file>
lens trace diff <baseline> <candidate>
```

- `tracelens.snapshot/v1`: syscall 집계, 에러, 지연, IO 처리량,
  프로세스별 fd 테이블 + `fd_leaks_by_process`.
- `+++ exited/killed/superseded` 계열 종결 처리, `strace -y` 주석
  (`3</etc/hosts>`) 무시, fork/clone 시 fd 상속, tid 재사용 generation 분리.
- `diff`는 `tracelens.diff/v1`: 지연 변화와 신규 에러.

### 6. sys — systemd 정적 분석

```bash
lens sys inspect <path>          # 유닛 파일 또는 디렉터리
lens sys cycles <dir>
lens sys diff <baseline.json> <candidate.json>
```

- `.d/` drop-in 병합(인스턴스 유닛은 템플릿 `foo@.service.d/`도 병합),
  `Key=` 빈 값 리셋 의미론, `%u`/`%h` 등 specifier 확장.
- 전 suffix 지원(.service/.target/.socket/.timer/.mount/...).
- Tarjan SCC로 순환 의존 탐지. `servicelens.snapshot|diff/v1` 출력.

### 7. build — 컴파일 데이터베이스 분석

```bash
lens build inspect <compile_commands.json>
lens build impact <compile_commands.json> --header <path>
lens build diff <baseline.json> <candidate.json>
```

- `inspect`는 `buildscope.snapshot/v4`: 파싱된 유닛 + 역방향 impact 그래프.
- `-I`/`-isystem`/`-iquote`/`-idirafter` 순서 보존(첫 일치 우선),
  `-include`/`-imacros` 강제 인클루드, 주석 내 `#include` 무시,
  `..`/`./` 경로 정규화.
- `impact`는 헤더의 전이 의존자(재컴파일 대상)를 계산. 온디스크 스캔 상한
  도달 시 `scan_truncated=true`.

### 8. env — Python 가상환경 감사

```bash
lens env inspect <venv_path> [--project <dir>]
lens env check <venv_path>
lens env diff <baseline.json> <candidate.json>
```

- 인터프리터 실행 없이 정적 메타데이터 감사(dist-info/METADATA).
- `--project`: 로컬 소스가 site-packages 모듈을 가리는 import 섀도잉 탐지.

### 9. net — 소켓/포트 포렌식

```bash
lens net inspect [--proc-dir PATH] [--json]
lens net diff <baseline.json> <candidate.json>
```

- `/proc/net/{tcp,tcp6,udp,udp6,unix}` 파싱 + `/proc/*/fd` inode→PID 상관.
- UDP 바인드 소켓도 리스너로 집계. unix 소켓은 Type/St 의미론 분리.
- `lens.net/v1` 스키마. `diff`는 (kind, address, port)/5-tuple 매칭으로
  바인드 주소 변경도 closed+new로 표면화.

## 포렌식 번들 (`bundle`)

```bash
lens bundle create incident.lens \
  --disk /var/log --trace strace.log --test junit.xml \
  --sys /etc/systemd/system --env .venv --net
lens bundle inspect incident.lens
lens bundle verify incident.lens
```

- `tar.gz` + `manifest.json`(`lens.bundle/v2`): 아티팩트별 SHA-256, 크기,
  수집 진단(diagnostics) 보존. 최소 1개 소스 필수.
- `verify`: 정확 경로의 매니페스트만 신뢰(중첩 스푸핑 차단), 중복/미등재
  엔트리 거부, 해제 바이트 상한 적용. 매니페스트는 서명되지 않으므로
  **무결성 확인이지 출처 인증이 아닙니다**.
- 생성은 임시 파일 + 원자적 rename(부분 쓰기·심볼링크 덮어쓰기 방지).

## 시스템 종합 점검 (`doctor`)

```bash
lens doctor [--root PATH] [--procfs PATH] [--systemd-dir PATH] [--json]
```

스토리지 용량(statvfs), 네트워크, 서비스, 환경 검사를 PASS/WARN/FAIL로
집계하고 권장 조치를 출력합니다.

## TUI 대시보드

```bash
lens tui [path]
```

4개 탭(Storage/Services/Logs/Network) 모두 실데이터. 단축키:
`Tab`/`1-4` 탭 전환, `j/k` 이동, `Enter` 디렉터리 진입,
`Backspace`/`h` 상위, `r` 리로드, `q`/`Esc` 종료.

## MCP 서버 (`lens-mcp`)

AI 어시스턴트 연동용 stdio JSON-RPC 서버. 도구: `lens_disk_scan`,
`lens_disk_duplicates`, `lens_abi_inspect`, `lens_log_filter`,
`lens_trace_analyze`, `lens_sys_cycles`, `lens_build_impact`,
`lens_env_check`, `lens_net_inspect`, `lens_bundle_verify`, `lens_doctor`.

```bash
cargo build --release -p lens-mcp
# Claude Desktop 등에서 command로 target/release/lens-mcp 등록
```

`lens_log_filter`는 최대 1000라인만 스캔하며 응답에 `truncated`로 명시됩니다.

## 출력 계약 (공통)

- **결정적**: 같은 입력 → 같은 바이트 출력(BTreeMap 기반 정렬 직렬화).
- **Fail-closed**: 수집 실패는 조용히 삼키지 않고 `complete=false`,
  `errors`, `diagnostics`, `scan_truncated` 등의 필드로 표면화.
- **스키마 버전**: 모든 스냅샷/리포트에 `schema` 문자열 필드.
- **버전 정합**: 런타임 버전은 `env!("CARGO_PKG_VERSION")`에서만 취함.
