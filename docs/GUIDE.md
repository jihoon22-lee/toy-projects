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

## 공통 계약

### 종료 코드

| 코드 | 의미 | 예시 |
|------|------|------|
| 0 | clean — findings 없음 | 정상 스캔, 회귀 없는 `test diff` |
| 1 | findings 있음 | `test diff` 회귀/신규 실패, `abi diff` incompatible·uncertain(수동 검토 필요), `sys cycles` 사이클, `env check` 누락/충돌 의존성, `doctor`의 `--fail-on` 기준 이상, `disk scan` incomplete(권한 오류·절단), `trace analyze` fd 누수 |
| 2 | 사용·입력·런타임 오류 | 존재하지 않는 경로, 잘못된 플래그 값, 파싱 불가 입력 |

`doctor`는 `--fail-on warn|fail`(기본 `fail`)로 findings 기준을 조절한다.

### 출력 형식

대부분의 명령이 `--format text|json`을 지원한다. `text`는 사람이 읽는
요약(색인·집계·top-N)이고, `json`은 기존 결정적 JSON 스키마를 출력한다.
기본값은 명령별 기존 동작을 유지한다(예: `abi inspect`는 json,
`disk scan`은 text). `disk scan`/`net inspect`의 기존 `--json` 플래그는
`--format json`의 별칭으로 유지된다.

## 도메인별 사용법

### 1. disk — 파일시스템 분석

```bash
lens disk scan <path> [--json|--format json] [--parallel]
               [--max-depth N] [--exclude PATTERN]... [-x|--one-file-system]
               [--top N]
lens disk duplicates <path> [--min-size BYTES] [--format text|json]
lens disk trash <path>
```

- `scan`: 아레나 트리로 계층 구조 + 논리/할당 크기 집계. `--json`은
  `diskmap.snapshot/v2` 출력. 심볼링크 루프 감지, 깊이/항목 상한 도달 시
  `complete=false`로 표시되며 per-entry 오류는 stderr로 나옵니다.
- `--max-depth`/`--exclude`/`--one-file-system`: 스캔 범위 제한
  (`--exclude`는 반복 지정 가능, 이름 glob 패턴). `-x`는 마운트 경계를
  넘지 않는다(WSL의 `/mnt/c`, `/proc` 등 제외에 유용).
- `--top N`(기본 10): 텍스트 출력에 최상위에서 가장 큰 항목 N개를
  KiB/MiB/GiB 단위로 표시.
- `--parallel`: rayon으로 per-entry stat 병렬화. cold cache의 대형 트리에서
  유효하고, warm cache에서는 이득이 거의 없다.
- `duplicates`: 부분 해시 → 전체 SHA-256 2단계 판정. 하드링크(inode 공유)는
  회수 가능 용량에서 제외, `(dev=0,ino=0)` 파일은 경로 기반 폴백.
  해시 실패는 `errors`로 집계해 출력하고, 총 회수 가능 용량 요약을 표시.
  `--format json`은 `{groups, errors, total_reclaimable_bytes}`를 출력.
- `trash`: FreeDesktop Trash 규격으로 이동(`.trashinfo` 기록 포함,
  심볼링크는 타깃이 아닌 링크 자체가 이동됨, 복구 영수증 출력).

### 2. abi — ELF 바이너리 검사

```bash
lens abi inspect <binary> [--format text|json]
lens abi diff <baseline> <candidate> [--format text|json]
```

- 동적 심볼 표면(정의/미정의 구분), DT_NEEDED, RPATH/RUNPATH, SONAME,
  인터프리터, 심볼 버전 요구사항(VERNEED/VERDEF), `.debug_info` 타입명 수집.
- 스트립된 바이너리도 PT_DYNAMIC/PT_INTERP program header 폴백으로 파싱.
- `diff`는 `abilens.diff/v3`: 정의된(exported) 심볼만 제거 판정에 반영,
  미정의 import는 `imports`에 의존성 정보로 별도 집계(import만 바뀌면
  compatible), weak 심볼은 binding=`weak`로 표기. 버전 요구사항 변경,
  `Compatibility`(compatible/incompatible/uncertain) 3상태 판정.

### 3. log — 로그 인덱싱·필터

```bash
lens log inspect <path> [--format text|json]
lens log filter <path> [--query TEXT] [--min-level LEVEL] [--include-unknown] [--format text|json]
```

- mmap 라인 인덱서(memchr 기반)로 GB급 로그를 즉시 열람.
- `--min-level`: trace|debug|info|warn|error|fatal (오타 시 즉시 거부).
  레벨을 판별할 수 없는 줄은 제외되고 stderr에 제외 수를 보고한다.
  `--include-unknown`으로 복원 가능.
- 레벨 감지는 텍스트 레벨 + syslog PRI(`<33>`) + 커널 printk(`<3>`)
  접두사까지 인식. 메시지 추출은 무할당 슬라이딩 윈도우로 수행.

### 4. test — JUnit 리포트 파싱·회귀 diff

```bash
lens test parse <file|dir|glob> [--project NAME] [--format text|json]
lens test diff <baseline> <candidate> [--format text|json]
```

- 입력은 파일 1개, `*.xml` 디렉터리, 또는 `*`/`?` 파일명 glob — CI의
  모듈별 JUnit 파일들을 하나의 run으로 병합해 비교한다.
- `testlens.run/v1`: 테스트케이스 신원(`suite::classname::name`), 상태
  (passed/failed/error/skipped), 실패 본문, `system-out`/`system-err`,
  `<properties>`, suite 출력까지 수집.
- 잘린/깨진 XML은 가능한 범위까지 파싱하고 `complete=false`로 표시.
- `run_id`는 내용 기반 결정적 해시(재실행해도 동일).
- `diff`는 `testlens.diff/v2`: 구조화된 `regressions[]{id,before,after,
  message}`, `fixes`, `new_failures`(새로 등장한 실패/에러 — exit 1),
  `removed_tests`, `skipped_changes`. skipped→passed는 fix가 아니라
  skip 변경으로 분류. 중복 identity는 `diagnostics`로 보고.

### 5. trace — strace 분석

```bash
lens trace analyze <trace_file> [--format text|json]
lens trace diff <baseline> <candidate> [--format text|json]
```

- `tracelens.snapshot/v1`: syscall 집계, 에러, 지연, IO 처리량,
  프로세스별 fd 테이블 + `fd_leaks_by_process`.
- `+++ exited/killed/superseded` 계열 종결 처리, `strace -y` 주석
  (`3</etc/hosts>`) 무시, fork/clone 시 fd 상속, tid 재사용 generation 분리.
  `CLONE_FILES` 스레드는 fd 테이블을 공유, `O_CLOEXEC` fd는 `execve`에서
  해제, `close_range` 일괄 해제 처리. 에러는 `syscall:errno` 키로 집계.
- `diff`는 `tracelens.diff/v1`: 지연 변화와 신규 에러.

### 6. sys — systemd 정적 분석

```bash
lens sys inspect <path>          # 유닛 파일 또는 디렉터리
lens sys cycles <dir> [--format text|json]
lens sys diff <baseline> <candidate>   # 스냅샷 JSON 또는 유닛 디렉터리
```

- `.d/` drop-in 병합(인스턴스 유닛은 템플릿 `foo@.service.d/`도 병합),
  `Key=` 빈 값 리셋 의미론, `%u`/`%h` 등 specifier 확장.
- 전 suffix 지원(.service/.target/.socket/.timer/.mount/...).
- Tarjan SCC로 순환 의존 탐지. 사이클은 실제 방향 경로로 보고하고 각
  엣지의 기원(`--Wants(a.service:12)-->`)을 표시. `/dev/null` masked
  유닛은 엣지 대상에서 제외, alias/템플릿 이름도 해석.
- `diff`는 섹션의 모든 키를 비교(`User=` 추가 등). `servicelens.snapshot|
  diff/v1` 출력.

### 7. build — 컴파일 데이터베이스 분석

```bash
lens build inspect <compile_commands.json> [--format text|json]
lens build impact <compile_commands.json> --header <path> [--format text|json]
lens build diff <baseline.json> <candidate.json> [--format text|json]
```

- `inspect`는 `buildscope.snapshot/v4`: 파싱된 유닛 + 역방향 impact 그래프.
  `reverse_impact`는 직접 includer만, `transitive_impact`는 헤더 체인을
  거친 간접 includer까지 포함.
- `-I`/`-isystem`/`-iquote`/`-idirafter` 순서 보존(첫 일치 우선),
  `-include`/`-imacros` 강제 인클루드, 주석 내 `#include` 무시,
  `..`/`./` 경로 정규화.
- `impact`는 헤더의 전이 의존자(재컴파일 대상)를 계산. 온디스크 스캔 상한
  도달 시 `scan_truncated=true`.

### 8. env — Python 가상환경 감사

```bash
lens env inspect <venv_path> [--project <dir>] [--format text|json]
lens env check <venv_path> [--extras a,b] [--format text|json]
lens env diff <baseline.json> <candidate.json> [--format text|json]
```

- 인터프리터 실행 없이 정적 메타데이터 감사(dist-info/METADATA).
- `check`는 PEP 508 환경 마커(`python_version`, `sys_platform`,
  `extra`, `and`/`or`/괄호, 버전 비교)를 venv의 실제 Python 버전/
  플랫폼에 대해 평가한다. 거짓 마커의 요구는 스킵, `extra == "x"`는
  `--extras`로 활성화한 extra에서만 적용, 설치됐지만 범위를 벗어난
  버전은 `version_conflicts`로 보고, 평가 불가 마커는 `unevaluated`로
  missing과 구분해 보고. uv venv의 `version_info` 키도 인식.
- `--project`: 로컬 소스가 site-packages 모듈을 가리는 import 섀도잉 탐지.

### 9. net — 소켓/포트 포렌식

```bash
lens net inspect [--proc-dir PATH] [--json|--format json] [--no-unix]
lens net diff <baseline.json> <candidate.json> [--format text|json]
```

- `/proc/net/{tcp,tcp6,udp,udp6,unix}` 파싱 + `/proc/*/fd` inode→PID 상관.
- UDP 바인드 소켓도 리스너로 집계. unix 소켓은 Type/St 의미론 분리.
  `--no-unix`는 unix 소켓을 출력에서 제외.
- 소유자 없는 소켓은 `owner_state`로 구분: `orphan`(inode를 검사했으나
  보유 프로세스 없음) vs `owner_unknown`(`/proc/<pid>/fd` 접근 거부로
  확인 불가 — 텍스트에서 `<owner unknown>`으로 표시, sudo 권장).
  summary에 `owner_unknown_sockets`/`uninspectable_processes` 카운터.
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
lens doctor [--root PATH] [--procfs PATH] [--systemd-dir PATH]
            [--json|--format json] [--fail-on warn|fail]
```

스토리지 용량(statvfs), 네트워크, 서비스, 환경 검사를 PASS/WARN/FAIL로
집계하고 권장 조치를 출력합니다. `--fail-on`(기본 `fail`) 미만의
심각도는 exit 0을 유지한다 — 권한 부족으로 검사 불가한 소켓은 orphan
WARN이 아니라 PASS + "sudo로 재실행" 권고로 보고한다.

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
