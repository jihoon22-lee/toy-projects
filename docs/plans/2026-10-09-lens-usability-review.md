# Lens 사용성 중심 코드리뷰 및 개선 계획 (2026-10-09)

대상: `toy-projects` 워크스페이스 `v0.4.3` (HEAD `66c5788`), 13개 크레이트 약 11.5k LOC.
관점: 단순 버그 검토가 아니라 **실제 사용 시나리오를 수행하면서 드러난 신뢰성·사용성 문제**를 중심으로 본다.

---

## 0. 요약

| 구분 | 건수 | 대표 항목 |
|---|---|---|
| P0 결과 신뢰성 | 9 | env check 오탐 100%, 없는 입력의 "정상" 통과, `--min-level` 무동작, fd 누수·ABI 비호환 오탐, systemd 사이클 오표시, `sys diff`의 변경 누락, bundle 부분 실패 시 전체 중단·무경고 덮어쓰기 |
| P1 사용 흐름 차단 | 15 | 파이프 시 패닉, 종료 코드 미반영, incomplete 은폐, 출력 형식 비일관, 스캔 옵션 미노출, test diff 의미론, TUI·MCP 결함 |
| P2 문서·품질 | 6 | 인자 도움말 부재, 문서 드리프트, shadowing 품질, CLI 통합 테스트 부재 |

- 기존 품질 게이트는 모두 녹색이다: `cargo fmt --check`, `clippy -D warnings`, 단위 테스트 74개 전부 통과, `git status` clean.
- 그런데도 위 문제들이 그대로 남아 있다. 단위 테스트가 **정상 경로의 파서 출력만** 검증하고, CLI 계약(종료 코드, stdout/stderr, 잘못된 입력 처리)이나 실데이터 시나리오는 검증하지 않기 때문이다.
- 성능은 대체로 양호하다. release 기준 538MB/600만 줄 로그 필터 0.7초(grep 0.31초), `/usr` 12만 항목 스캔 4.8초. 사용성 문제가 성능 문제보다 훨씬 크다.

---

## 1. 범위와 방법

- **환경**: WSL2 (Linux 6.18), 비root 사용자, rustc 1.97.1. 바이너리는 `target/{debug,release}/lens` 0.4.3
- **정적 리뷰**: 13개 크레이트 전 모듈
- **동적 검증**: 실제 시스템 데이터(`/var/log`, `/lib/systemd/system`, `/proc`, 실 venv, `/usr`)와 `/tmp/lens-review/` 샌드박스 픽스처 사용
  - systemd 유닛
  - C 미니 프로젝트 + `compile_commands.json`
  - gcc로 빌드한 `.so` v1/v2
  - JSONL·CRLF·non-UTF8·대용량 로그
  - JUnit XML
  - 합성 strace
  - 실 리스너 소켓
  - MCP stdio 세션
- **한계**
  - `strace`가 설치되어 있지 않아 trace는 합성 로그로만 검증했다.
  - TUI는 코드 리뷰와 경로 동작 확인으로 대체했다(대화형 조작은 하지 않음).
  - root 권한 시나리오는 수행하지 않았다.

---

## 2. 사용 시나리오와 결과

페르소나별로 "처음 쓰는 사람이 문서대로 했을 때" 무엇이 일어나는지 기록했다. 괄호 안 ID는 3장의 발견 사항이다.

| # | 시나리오 | 수행 | 결과 / 불편 |
|---|---|---|---|
| U1 | 운영자: 디스크 정리 | `disk scan /var/log` → `disk duplicates` → `disk trash` | 권한 오류가 있어도 "Scan completed successfully"(F12). 총합만 출력되고 어디가 큰지 알 수 없음(F14). 정리 대상이 `/tmp`(tmpfs)면 trash가 EXDEV로 실패하고, 실수로 버린 파일을 복구할 명령이 없음(F18) |
| U2 | 장애 대응: 증거 번들 | `bundle create incident.lens --disk … --trace … --env …` | 소스 하나만 실패해도 번들 전체가 생성되지 않음(F03). 같은 이름으로 다시 실행하면 6개 아티팩트 번들이 1개짜리로 경고 없이 덮어써짐(F03). `--log`는 줄 수만 담김(F22). 번들 안 리포트를 읽으려면 `tar` 필요(F22) |
| U3 | CI: 테스트 회귀 게이트 | `test diff base.xml cand.xml` | 회귀가 있어도 exit 0(F11). 새로 추가된 실패 테스트와 삭제된 테스트가 회귀로 드러나지 않음. skipped→passed를 "FIX"로 집계(F15) |
| U4 | 라이브러리 릴리스: ABI 확인 | `abi diff libv1.so libv2.so` | 내부 구현에서 `strlen`만 안 쓰게 바꿨는데 `incompatible`로 판정(F06). 진짜 비호환이어도 exit 0(F11) |
| U5 | systemd 변경 리뷰 | `sys inspect` ×2 → `sys diff` | `User=` 추가가 diff에 안 나옴(F08). `sys cycles /lib/systemd/system`이 43개 유닛짜리 "경로"를 출력하는데 실제 경로가 아님(F07) |
| U6 | C/C++: 헤더 영향도 | `build impact cc.json --header include/common.h` | 프로젝트 루트에서만 동작하고, 하위 디렉터리에서는 0건이 조용히 나옴(F16). `build inspect`의 `reverse_impact`는 전이 의존을 빠뜨림(F09) |
| U7 | Python 환경 감사 | `env check .venv` | 실제 venv에서 "missing" 21건이 전부 오탐(F01). 오타 경로도 "All dependencies satisfied"(F02) |
| U8 | 보안: 노출 포트 점검 | `doctor`, `net inspect` | 비root면 다른 사용자 소켓 58개가 "orphan"으로 분류되어 doctor가 항상 WARN(F17). `net inspect --json \| head`에서 패닉(F10) |
| U9 | 로그 조사 | `log filter /var/log/kern.log --min-level error` | 36,183줄 중 35,723줄 매칭, 즉 필터가 사실상 무동작(F04). `.gz` 로테이트 로그와 stdin 미지원(F20) |
| U10 | AI 어시스턴트(MCP) | `lens-mcp`에 initialize/ping/tools 호출 | `ping` 미지원, 도구 오류를 JSON-RPC error로 반환, libc 검사 응답 711KB, 로그는 앞 1000줄만 봄(F24) |

---

## 3. 발견 사항

각 항목 형식: **재현 → 실제 결과 → 원인 → 개선안**

### P0 — 결과를 믿을 수 없게 만드는 문제

#### F01. `env check`: 환경 마커 미평가로 오탐 100%
- 재현: `lens env check ~/projects/llm-usage-dashboard/.venv` (Python 3.14.4)
- 실제: "Found 21 missing dependenc(ies)"인데 전부 아래 둘 중 하나였다.
  - `extra == "testing"` 같은 extras
  - `python_version < '3.10'`
  - 예: `Package 'Flask' requires 'importlib-metadata>=3.6.0; python_version < '3.10''`
- 원인: `crates/lens-env/src/venv.rs:52-56`이 `;` 뒤의 마커를 잘라낼 뿐 평가하지 않는다. 버전 제약(`>=2.5`)도 검사하지 않고 설치 여부만 본다.
- 개선:
  1. PEP 508 마커 평가를 추가한다. 최소한 `extra`, `python_version`, `python_full_version`, `sys_platform`, `platform_system`, `implementation_name`을 지원한다.
  2. `extra == …` 의존성은 기본적으로 제외하고, `--extras a,b` 옵션으로만 포함한다.
  3. PEP 440 버전 비교로 `version_conflicts`를 별도로 보고한다.
  4. 평가할 수 없는 마커는 missing이 아니라 `unevaluated`로 분리한다(fail-closed 원칙 유지).

#### F02. 입력 검증 부재: 잘못된 입력이 "정상 결과"로 둔갑 (fail-open)
아키텍처 문서의 1번 불변조건(Fail-closed)을 CLI 경계에서 위반한다.

| 재현 | 실제 |
|---|---|
| `lens env check /nonexistent-venv` | "All dependencies satisfied", exit 0 |
| `lens env check /home/…/toy-projects` (venv 아님) | 동일 |
| `lens net inspect --proc-dir /nonexistent` | 소켓 0개, exit 0 |
| `lens doctor --systemd-dir /typo --procfs /typo` | Services PASS("non-systemd environment"), Network PASS |
| `lens test parse <(echo 'not xml')` | `complete: true`, total 0, exit 0 |
| `lens trace analyze README.md` | 95 events / 30 calls로 집계, 파싱 실패 줄 수는 보고 안 함 |
| `lens bundle create … --env /nonexistent` | 빈 env 스냅샷이 증거로 번들에 들어감 |
| `lens sys cycles <(echo '[Unit]')` | "0 units, no cycles" |

- 원인 위치
  - `lens-env/src/venv.rs:12`: pyvenv.cfg가 없어도 계속 진행
  - `lens-net/src/parser.rs:223-237`: 모든 read 실패를 무시
  - `lens-cli/src/doctor.rs:216,221`: `unwrap_or_default()`, 경로가 없으면 PASS
  - `lens-test/src/junit.rs:350`: 루트 요소를 검증하지 않음
  - `lens-trace/src/parser.rs`: unparsed 줄 카운터 없음
- 개선
  - 도메인별 입력 검증 헬퍼를 둔다.
    - venv: `pyvenv.cfg` 또는 `site-packages`가 반드시 있어야 함
    - procfs: `net/tcp` 읽기 성공이 필수
    - JUnit: 루트가 `testsuites`/`testsuite`여야 함
    - strace: 인식된 줄 비율이 0이면 에러
  - 리포트에 `unparsed_lines`, `skipped_inputs`를 추가한다.
  - doctor는 경로를 명시했는데 없으면 FAIL, 기본 경로가 없을 때만 SKIP으로 한다.

#### F03. `bundle create`: 부분 실패 시 전체 중단, 기존 번들을 무경고 덮어쓰기
- 재현 1: `lens bundle create p.lens --test junit.xml --trace /nope`
  - 실제: `error: I/O error at "/nope"`, 번들 미생성. 정상인 `--test`도 함께 버려진다.
- 원인 1: `lens-cli/src/ops.rs:414-421`의 `collect!` 매크로에 넘기는 블록 안 `?`(`:425,451,461,471`)가 블록이 아니라 **`dispatch` 함수에서 리턴**한다. 그래서 "수집 실패는 diagnostics에 보존한다"는 주석과 문서 의도(`:409-410`)가 실제로는 동작하지 않는다. `:503`의 "all failed" 분기는 사실상 도달할 수 없다.
- 재현 2: 6개 아티팩트 번들 `incident.lens`가 있는 상태에서 `lens bundle create incident.lens --test junit.xml`
  - 실제: 경고 없이 1개짜리 번들로 교체된다. 포렌식 도구로서 증거 유실이다.
  - 원인: `lens-core/src/bundle.rs:150`의 `rename`이 무조건 덮어쓴다.
- 개선
  - 각 소스 수집을 클로저 `(|| -> Result<_> { … })()`로 감싸 `?`가 소스 단위에서 멈추게 한다.
  - 대상이 이미 있으면 거부하고 `--force`로만 덮어쓰게 한다(`rename` 대신 `link`+`unlink` 또는 `O_EXCL` 체크).
  - 이 동작을 고정하는 통합 테스트를 추가한다.

#### F04. `log filter --min-level`이 레벨 미상 줄을 모두 통과
- 재현과 실제
  - `lens log filter /var/log/kern.log --min-level error` → "Matched 35723 of 36183"
  - `dpkg.log --min-level warn` → 437/437
- 원인
  - `lens-log/src/filter.rs:71`: `record.level != Unknown && …`이라 Unknown이면 통과한다.
  - `parser.rs:89`: 앞 5단어만 보는 휴리스틱이라 syslog/RFC5424 형식(`Main kernel: …`)은 대부분 Unknown이 된다.
- 개선
  - 기본값을 "Unknown 제외"로 바꾸고 `--include-unknown` 옵션을 둔다.
  - 매칭 요약에 "레벨 미상 N줄 제외"를 표시한다.
  - syslog `<PRI>`, journald export, RFC5424 레벨 추출을 추가한다.

#### F05. `trace`: fd 누수 오탐과 비실행 가능한 오류 집계
- 재현: 합성 trace
  - 스레드 101(`CLONE_FILES|CLONE_THREAD`)이 `close(3)`
  - PID 100이 `O_CLOEXEC`로 fd 5를 연 뒤 `execve`
- 실제: `fd_leaks_by_process: {"100": [3, 5]}`. 둘 다 오탐이다.
- 원인
  - `lens-trace/src/parser.rs:197`: 스레드·`CLONE_FILES` 자식에게 fd 테이블을 **복사**한다(공유해야 함).
  - `execve` 시 CLOEXEC 정리, `close_range`, `fcntl(F_DUPFD*)`를 처리하지 않는다.
  - 프로세스 종료 시 남은 fd를 "누수"로 분류하는데, 정상 종료 시 커널이 닫는 것까지 포함된다.
- 추가 문제: `errors`가 errno로만 집계된다(`:167`, 예: `{"ENOENT": 1}`). 어떤 syscall·경로인지 없어서 `trace diff`의 `new_errors: ["EACCES"]`만으로는 조치할 수 없다.
- 개선
  - fd 테이블을 `Rc`로 공유하는 "fd table id" 모델을 도입한다(clone flags 파싱).
  - execve CLOEXEC 처리를 추가한다.
  - `fd_leaks`를 `open_at_exit`로 개명하거나 의미를 문서화한다.
  - 오류 키를 `syscall:errno`로 바꾸고 대표 인자 샘플(경로)을 남긴다.

#### F06. `abi diff`: import 심볼 변화만으로 `incompatible` 판정
- 재현: gcc로 만든 `libv1.so`와 `libv2_importonly.so`. 공개 API는 동일하고, 내부에서 `strlen`/`puts` 호출만 제거했다.
- 실제: `incompatible`, "Exported symbols removed: 2" (`puts`, `strlen`)
- 원인
  - `lens-abi/src/elf.rs:524`: undefined 심볼까지 `abi.symbols`에 넣는다.
  - `diff.rs:133`: 이 목록의 제거를 "export 제거"로 판정한다.
- 추가 문제
  - `elf.rs:529`가 `SymbolScope::Linkage`를 "weak"로 매핑한다. 실제 weak 심볼(`nm -D`의 `w __cxa_finalize`)은 "global"로 표기된다. 따라서 global↔weak 변경을 탐지하지 못한다.
  - 비ELF 입력과 비교하면 `incompatible`로 나온다(`diff.rs:126`). 입력 오류는 판정이 아니라 에러여야 한다.
- 개선
  - `symbols`(defined export)와 `imports`를 분리하고, 호환성 판정에는 defined만 사용한다.
  - binding은 `sym.is_weak()`로 판별한다.
  - invalid 입력은 `uncertain` + 에러 종료로 처리한다.

#### F07. `sys cycles`: SCC를 경로처럼 출력하고 템플릿·alias·mask를 구분하지 않음
- 재현과 실제
  - `lens sys cycles /lib/systemd/system` → 유닛 43개를 `a -> b -> … -> z`로 출력
  - 픽스처(a After b, b After c, c After a) → `a.service -> b.service -> c.service`. 실제 의존 방향(c→b→a→c)과 다르다.
- 원인
  - `lens-sys/src/dag.rs:66,77`: SCC를 **알파벳 정렬**한 뒤 CLI(`ops.rs:234`)가 `" -> "`로 잇는다. 정렬된 집합은 경로가 아니다.
  - `loader.rs:103`의 `is_file()`이 심링크를 따라간다. 그래서 alias(`dbus-org…service -> systemd-hostnamed.service`)가 별도 노드로 중복되고, `/dev/null` 마스킹 유닛은 보고 없이 사라진다.
  - 템플릿(`foo@.service`)이 실재 노드로 들어간다.
- 개선
  - SCC 안에서 실제 단순 사이클 하나를 BFS로 추출해 출력한다.
    - 형식: `a.service --After(a.service:2)--> b.service`
    - 각 엣지에 출처 파일:라인과 지시어를 붙인다.
  - SCC 전체는 별도 필드로 둔다.
  - alias는 대상 유닛으로 합치고, masked는 진단으로 보고하고, 템플릿은 그래프에서 제외한다.
  - 장기적으로 enabled(`*.wants/`) 유닛만 대상으로 하는 `--enabled-only` 옵션을 둔다. 실제 systemd는 같은 트랜잭션 안의 유닛끼리만 사이클을 문제 삼는다.

#### F08. `sys diff`: 6개 필드 외 변경을 모두 놓침
- 재현: a.service에 `User=svc`를 추가한 뒤 `sys inspect` 두 번 → `sys diff`
- 실제: `modified_units: []`
- 원인: `lens-sys/src/diff.rs:27-61`이 ExecStart/Wants/Requires/Before/After/drop-in 경로만 비교한다. `User=`, `Environment=`, `ExecStartPre=`, `Restart=`, 보안 하드닝 키, `[Install]`은 보이지 않는다. drop-in **내용** 변경도 경로가 같으면 놓친다.
- 개선
  - `sections` 전체를 키 단위로 비교해 `{section, key, before, after}` 목록을 만든다.
  - 기존 필드는 요약으로 유지한다.
  - `sys diff`가 디렉터리를 직접 받게 한다(현재는 `Is a directory` 에러).

#### F09. `build inspect`의 `reverse_impact`는 직접 의존만 담음
- 재현: `a.c → mid.h → common.h`, `b.c → common.h`
- 실제
  - `build impact --header common.h`는 a.c와 b.c를 모두 반환한다(정상).
  - 반면 `build inspect`의 `reverse_impact["common.h"]`는 `[b.c]`뿐이다.
- 원인: `ops.rs:272-276`이 `header_to_units`(직접 include)를 그대로 직렬화한다.
- 영향: 스냅샷을 소비하는 사용자나 도구가 "a.c는 영향 없음"으로 오판한다.
- 개선: 필드를 `direct_includers`로 개명하고, 전이 폐포인 `transitive_impact`를 별도로 제공한다(또는 의미를 스키마에 명시).

### P1 — 일상 사용을 막는 문제

#### F10. stdout 파이프가 닫히면 패닉
- 재현: `lens net inspect --json | head`
- 실제: `panicked … failed printing to stdout: Broken pipe`
- 영향: `| head`, `| less`(q), `| grep -m1` 같은 가장 흔한 사용 패턴이 전 명령에서 해당한다.
- 개선: `main.rs`에서 `libc::signal(SIGPIPE, SIG_DFL)`(libc는 이미 의존성에 있음)을 설정하거나, 출력을 `writeln!` + `ErrorKind::BrokenPipe`이면 조용히 종료하도록 바꾼다.

#### F11. 종료 코드가 결과를 반영하지 않음
- `test diff`(회귀), `abi diff`(incompatible), `doctor`(FAIL/WARN), `sys cycles`(사이클 발견), `env check`(missing)가 모두 exit 0이다. CI에서 쓰려면 `jq` 후처리가 필수다.
- 개선(`diff(1)` 관례)
  - 0 = 문제 없음, 1 = findings 있음, 2 = 사용·입력·런타임 오류
  - doctor는 `--fail-on warn|fail`(기본 fail)
  - **호환성 변경**이므로 CHANGELOG에 명시한다.

#### F12. 텍스트 출력이 incomplete를 숨김
- 재현: `lens disk scan /var/log` (`/var/log/private` 권한 거부)
- 실제: stderr 경고 뒤에 "Scan completed successfully"
- 원인: `ops.rs:46`이 `result.complete`/`truncated`를 무시한다.
- 개선: incomplete면 "Scan INCOMPLETE: N errors, truncated=…" 배너를 출력하고 종료 코드에 반영한다(F11).

#### F13. 출력 형식 비일관과 요약 부재
- JSON만 있는 명령: abi/test/trace/sys/build inspect, env inspect
- 텍스트만 있는 명령: duplicates, cycles, env check, bundle, log
- 출력 크기
  - `abi inspect /usr/bin/ls` 2,963줄
  - `net inspect --json` 413KB(unix 소켓 454개 포함)
  - `trace analyze`는 이벤트 최대 10,000개를 덤프
- 개선
  - 전 명령에 `--format text|json`을 통일한다.
  - text 기본값은 사람이 읽을 요약으로 한다(예: abi는 SONAME, NEEDED, export N/import N, 최고 GLIBC 버전).
  - `--include-events`, `--no-unix` 같은 상세 옵트인을 둔다.

#### F14. `disk scan`: 옵션 미노출과 정보 부족
- `ScanOptions`에 `max_depth`/`exclude_patterns`/`one_file_system`이 있지만 CLI에는 없다(`cli.rs:92-99`).
  - WSL에서 `lens disk scan /`을 하면 `/mnt/c`(Windows 드라이브)와 `/proc`까지 들어간다.
- 텍스트 출력은 총합만 있고 가장 큰 하위 항목 top-N이 없다. 크기도 MB 고정이다.
- `--parallel`은 warm cache에서 이득이 없다(4.85초 vs 4.81초). cold에서만 8.0초 → 5.4초. 문서에 조건을 명시해야 한다.
- 개선: `--max-depth`, `--exclude`, `-x/--one-file-system`, `--top N`(기본 10), 사람 친화적 단위(KiB/MiB/GiB).

#### F15. `test diff` 의미론
- 재현 결과
  - skipped→passed가 "FIX"로 집계된다.
  - 새로 추가된 `error` 테스트는 `cases.added`에만 있고 회귀로 안 잡힌다.
  - passed였던 테스트가 삭제되어도 `cases.removed`에만 있다.
  - 실패 메시지가 빠져 있다.
  - 같은 identity(파라미터화 테스트)는 `HashMap`에서 덮어써진다(`diff.rs:7-17`).
  - identity에 suite명이 없다.
- 기능 공백
  - CI에서 흔한 "모듈별 JUnit 파일 N개"를 받지 못한다(파일 1개만 입력 가능).
  - help의 "flaky analysis"는 구현되어 있지 않다.
- 개선
  - 결과를 구조화된 목록으로 낸다: `regressions[]{id, before, after, message}`, `new_failures`, `removed_tests`, `skipped_changes`
  - 중복 identity는 진단으로 보고한다.
  - glob·디렉터리 입력을 지원한다.
  - help 문구를 정정한다.

#### F16. `build impact`: 조용한 0건과 기준 경로 혼동
- 재현과 실제
  - `src/`에서 `--header include/common.h` → 0건. cwd 기준으로 정규화하기 때문이다(`ops.rs:299-302`).
  - 그래프에 없는 헤더도 0건이고 힌트가 없다.
  - 누락 소스(`deleted.c`)와 미해결 include(`generated.h`)는 진단 없이 버려진다(`impact.rs:69,78`).
- 성능: TU마다 헤더를 디스크에서 다시 읽는다(캐시 없음). 대형 프로젝트에서 O(TU×헤더) I/O가 발생한다.
- 개선
  - 상대 헤더는 cwd와 각 `directory` 기준을 모두 시도한다.
  - 그래프에 없으면 "헤더를 찾지 못함" 에러와 basename 유사 후보를 보여준다.
  - 리포트에 `missing_sources`, `unresolved_includes` 카운트를 추가한다.
  - 파일별 include 추출 결과를 캐시한다.
  - 참고: `-iquote`가 `<>` include에도 적용되는 것(`compiler.rs:39`)은 사소한 정확도 문제다.

#### F17. `net`: 권한 부족을 "orphan"으로 오분류
- 실제: 비root에서 다른 사용자 프로세스 소켓 57~58개가 `<orphan>`으로 나오고, doctor가 매번 WARN을 낸다.
- 원인: `lens-net/src/parser.rs:177`이 `/proc/<pid>/fd` EACCES를 `continue`로 삼키고, 접근 불가 PID 수를 남기지 않는다.
- 개선
  - 소유자 상태를 `owner_unknown`(권한 없음)과 `orphan`으로 구분한다.
  - summary에 `uninspectable_processes`를 추가한다.
  - doctor 메시지를 "N개는 권한 부족으로 확인 불가 — sudo로 재실행"으로 바꾼다.
  - 리스너 표에 UID/사용자 열을 추가한다.

#### F18. `disk trash`: 되돌릴 수 없는 정리 도구
- 재현과 실제
  - `/dev/shm` 파일(다른 파일시스템)을 버리면 `Invalid cross-device link (os error 18)`만 나온다.
  - `lens disk trash a b`는 다중 경로가 안 된다(clap 에러).
- 원인: FreeDesktop 규격의 `$topdir/.Trash-$uid`를 지원하지 않는다(`trash.rs:166`).
- 기능 공백: 라이브러리의 `restore()`는 `TrashReceipt`(identity 포함)가 필요하지만 영수증을 저장하지 않는다. 그래서 CLI로는 복구할 방법이 없다(`restore`/`list`/`empty` 서브커맨드도 없음).
- 개선
  - `trash <PATH>...` 다중 입력과 `--dry-run`
  - topdir trash 폴백(최소한 EXDEV에 대한 안내 메시지)
  - `.trashinfo` 기반 `trash list`/`trash restore <name>`(identity는 info 파일 옆 메타로 저장)

#### F19. `disk duplicates`
- 해시 실패를 `if let Ok`로 조용히 버린다(`duplicates.rs:78,94`). 스캔 경고도 출력하지 않는다(`ops.rs:60-76`).
- JSON 출력이 없고, 경로를 Debug 포맷(`"…"`)으로 출력하며, 크기는 바이트 고정이다.
- 개선: 해시 실패를 `errors`로 집계하고, `--format json`, 사람 친화적 단위, 총 회수 가능 용량 요약을 제공한다.

#### F20. `log` 기능 공백
- non-UTF8 줄을 조용히 건너뛴다(`indexer.rs:65`). 재현: 3줄 중 `\xff` 포함 ERROR 줄이 누락됐는데 "Matched 1 of 3"으로만 나온다.
- `.gz` 미지원: `dpkg.log.2.gz`가 104줄로 인덱싱되고 0건 매칭된다.
- stdin 미지원: `-`는 파일 없음, `/dev/stdin`은 "not a regular file"이다. 그래서 `journalctl | lens log filter -`를 쓸 수 없다.
- `--limit`, `--context`, regex, `--json`, `--since/--until`이 없고, JSONL의 ISO 타임스탬프 문자열을 무시한다.
- 개선: `from_utf8_lossy` + 손실 줄 카운트, gz 스트리밍 경로, stdin 경로(인덱싱 없이 스트리밍), 위 옵션들.

#### F21. `doctor` 범위
- `--root`가 storage에만 적용되고 ld.so.preload는 항상 호스트 `/etc`를 읽는다.
- systemd는 `/etc/systemd/system` 최상위만 본다(대부분 심링크). `/usr/lib/systemd/system`과 `/run/systemd/system` 우선순위 병합이 없다.
- 개선: systemd 검색 경로 병합 로더를 `lens-sys`에 두고 doctor, TUI, CLI가 공유한다.

#### F22. bundle 활용성
- `--log`는 `{path, total_lines}`만 담는다(`ops.rs:441-444`). 원본 로그나 필터 결과가 없어 증거 가치가 없다.
- `bundle extract`, `bundle show <entry>`가 없다.
- 변조 시 메시지가 "Bundle cryptographic verification failed"인데, 문서는 "무결성 확인이지 출처 인증이 아님"이라고 한다. "integrity check failed"로 맞춰야 한다.
- 개선: `--log` 원본 포함 옵션(크기 상한·tail N), `show`/`extract`, 메시지 정정.

#### F23. TUI
- **버그**: 기본 실행(`lens tui`, path=".")에서 Backspace/h를 누르면 `Path::new(".").parent() == Some("")`(직접 확인함)가 되고, `canonicalize("")`가 ENOENT로 실패한다. 그 결과 목록이 비고 "Failed to scan """이 표시된다(`app.rs:303-307`).
- 시작할 때와 디렉터리 이동마다 동기식 전체 재귀 재스캔을 한다(`app.rs:187`). `lens tui ~`은 진행 표시 없이 오래 멈추고, 이미 만든 아레나 트리를 재사용하지 않는다.
- 패닉 훅이 없어 패닉 시 터미널이 raw mode로 남는다(`term.rs:24`).
- 로그 경로를 지정할 수 없고, 검색, PgUp/PgDn/g/G, `?` 도움말이 없다. 스캔 오류가 화면에 표시되지 않는다.
- 개선
  - 시작 시 경로를 canonicalize하고, 한 번 스캔한 트리 안에서 메모리 탐색한다.
  - 백그라운드 스캔 스레드와 스피너를 둔다.
  - panic hook으로 터미널을 복원한다.
  - `lens tui --log PATH`와 `/` 검색을 추가한다.

#### F24. MCP 서버
- `ping`이 `-32601`로 나온다. MCP 클라이언트의 연결 상태 확인이 실패한다.
- 도구 실행 오류가 JSON-RPC error(`-32000`)로 나온다(`main.rs:105`). MCP 스펙은 `result.isError: true`를 권장하며, 그래야 모델이 오류 내용을 보고 재시도할 수 있다. 알 수 없는 도구는 `-32602`가 적절하다.
- `lens_log_filter`가 **앞** 1000줄만 스캔한다(`handler.rs:95-97`). 최근 이벤트는 파일 끝에 있으므로 조사 용도로 부적합하다.
- 응답 크기 제한이 없다: `lens_abi_inspect`(libc) 711KB, `lens_net_inspect` 350KB. LLM 컨텍스트를 초과한다.
- `lens_build_impact`의 상대 header가 MCP 서버 프로세스의 cwd 기준이다.
- `protocolVersion`을 클라이언트 요청과 무관하게 `2024-11-05`로 고정한다.
- diff 계열 도구(`test diff`, `abi diff`, `sys diff`)가 없다. AI가 가장 유용하게 쓸 수 있는 기능이다.
- 개선: ping, `isError`, 응답 요약·`limit` 인자, 뒤에서부터 N 매칭, 절대경로 요구 또는 `cwd` 인자, diff 도구 추가.

### P2 — 문서·품질

#### F25. 메시지·도움말
- clap 인자 설명이 비어 있다(`<PATH>`, `--query`, `--min-level` 빈칸).
- 사용 오류도 "Corrupt or invalid input format:"으로 출력된다(`lens-core/src/error.rs:16`). 예: "at least one artifact source is required".
- JSON 파싱 오류에 파일 경로가 없다(`build impact`: "invalid type: map … at line 1 column 0").

#### F26. 문서 드리프트
- [사용 가이드](../GUIDE.md)는 `abilens.diff/v2`라고 하지만 실제 출력은 `abilens.diff/v3`이다.
- "같은 입력 → 같은 바이트 출력" 주장이 `collected_at`/`created_at` 타임스탬프와 충돌한다. 결정성 범위를 명시하거나 `--no-timestamps`를 제공해야 한다.
- `test` 도움말의 "flaky analysis"는 구현되지 않았다.

#### F27. env shadowing 품질
- `pkg/json.py`를 stdlib shadowing으로 오탐한다. `max_depth(2)`라서 하위 패키지까지 보기 때문이다(`shadowing.rs:81`).
- 놓치는 경우
  - 패키지 디렉터리(`logging/__init__.py`)
  - stdlib 목록이 58개뿐이라 `secrets.py`, `test.py` 등
- 배포명과 모듈명을 혼동한다(`pyyaml`↔`yaml`, `typing-extensions`↔`typing_extensions`). `top_level.txt`/`RECORD`를 써야 한다.
- uv venv의 `version_info =` 키를 읽지 못해 `python_version`이 빈 문자열이다.

#### F28. systemd 파서 진단
- 다음 경우에 진단이 없다.
  - `=` 없는 쓰레기 줄
  - 닫히지 않은 `[Unit`
  - `Requires=missing.service`(디렉터리에 없는 유닛)
- 스냅샷 최상위 `diagnostics`는 항상 `[]`이다(`ops.rs:221`).

#### F29. 테스트 구조
- 단위 테스트는 74개이고, 크레이트별로 lens-log 2개, lens-env 3개, lens-tui 1개다. 통합 테스트 타깃이 없다(`crates/lens-cli/tests` 없음).
- 위 P0/P1 대부분이 CLI 경계(종료 코드, 잘못된 입력, 출력 형식)에서 생기므로 CLI 통합 테스트가 가장 효과가 크다.

#### F30. 성능 메모 (문제 아님, 참고)
- `log filter`: 538MB/600만 줄, query 0.7초, min-level 1.2초, grep 0.31초. RSS 623MB는 mmap 페이지와 줄 인덱스(16B×줄 수)다. "최소 RAM" 문구는 "파일 크기 + 줄당 16B"로 구체화하는 것이 정확하다.
- `disk scan /usr`(12만 항목): 4.8초, 63MB.

---

## 4. 횡단 개선 제안

1. **출력 계층 공통화**: `lens-cli`에 `Output` 헬퍼를 둔다(`--format`, BrokenPipe 처리, incomplete 배너, 사람 친화적 단위). 명령 구현은 리포트만 반환하고 렌더링은 공통 계층이 맡는다.
2. **종료 코드 규약**: `Outcome { Clean, Findings, Error }` → 0/1/2. 각 명령의 findings 정의를 문서 표로 고정한다.
3. **입력 검증 헬퍼**: 도메인별 `validate_*`가 의미 있는 에러를 반환한다. 예: "`/x` is not a Python virtualenv (no pyvenv.cfg or site-packages)".
4. **오류 타입**: `LensError::Usage`를 추가해 사용 오류와 입력 손상을 구분한다.
5. **CLI 통합 테스트**: `assert_cmd` + `predicates`(dev-dependency, 7일 이상 지난 버전 고정)로 종료 코드, stdout/stderr 계약, 잘못된 입력, 파이프 종료를 고정한다.

---

## 5. 개선 실행 계획

각 단계는 독립 PR로 진행할 수 있다. 각 항목의 완료 기준은 해당 재현 시나리오를 통합 테스트로 고정하는 것이다.

### Phase 0 — 빠른 안전성 확보 (작은 변경, 높은 효과)
| 작업 | 대상 | 완료 기준 |
|---|---|---|
| SIGPIPE/BrokenPipe 처리 | F10 | `lens net inspect --json \| head -1` 패닉 없이 exit 0 |
| bundle 수집 단위 오류 격리 + `--force` | F03 | `--test ok --trace /nope` → 번들 생성, diagnostics에 trace 오류. 기존 파일은 `--force` 없이 거부 |
| TUI 경로 canonicalize + panic hook | F23 | "."에서 parent 이동 시 상위 디렉터리 표시 |
| MCP `ping`, `isError` | F24 | ping → `{}`, 도구 실패 → `result.isError=true` |
| 텍스트 incomplete 배너 | F12 | 권한 오류 디렉터리 스캔 시 "INCOMPLETE" 출력 |
| 입력 검증 헬퍼(env/net/doctor/test/trace) | F02 | 2장 F02 표의 모든 재현이 에러 종료 |
| CLI 통합 테스트 골격 | F29 | `crates/lens-cli/tests/` 생성, 위 항목 회귀 테스트 포함 |

### Phase 1 — 판정 정확도
| 작업 | 대상 | 완료 기준 |
|---|---|---|
| PEP 508 마커 평가 + extras + 버전 충돌 | F01 | llm-usage-dashboard venv에서 오탐 0건, `--extras testing` 시 실제 미설치만 보고 |
| min-level Unknown 제외 + syslog 레벨 추출 | F04 | kern.log `--min-level error` 결과가 레벨 판별된 줄로 한정, 제외 수 표시 |
| fd 테이블 공유 모델 + CLOEXEC/close_range | F05 | 합성 trace의 fd 3/5 오탐 제거, 오류 키 `syscall:errno` |
| ABI defined/import 분리 + weak 판별 | F06 | import-only 변경 → compatible, weak 심볼 정확 표기 |
| 사이클 경로 추출 + 엣지 출처 + alias/mask/template 처리 | F07 | 픽스처에서 실제 방향 경로와 파일:라인 출력 |
| `sys diff` 섹션·키 전체 비교 + 디렉터리 입력 | F08 | `User=` 추가가 diff에 표시 |
| `reverse_impact` 의미 정정 | F09 | `transitive_impact`에 a.c 포함 |

### Phase 2 — 사용성 확장
| 작업 | 대상 |
|---|---|
| 종료 코드 규약 적용(호환성 변경, CHANGELOG 명시) | F11 |
| `--format text\|json` 통일 + 요약 모드 | F13, F19 |
| disk scan 옵션·top-N·단위 | F14 |
| test diff 구조화 결과 + 다중 파일 | F15 |
| build impact 기준 경로·힌트·누락 보고·캐시 | F16 |
| net owner_unknown 구분 | F17 |
| trash 다중 경로·list/restore·topdir 폴백 | F18 |
| log lossy UTF-8·gz·stdin·limit/context/json | F20 |
| systemd 검색 경로 병합 로더(doctor/TUI 공유) | F21 |
| bundle show/extract, `--log` 원본 포함 | F22 |

### Phase 3 — 확장·정리
- MCP 응답 크기 제어, diff 도구, 경로 기준 명시 (F24)
- TUI 백그라운드 스캔·메모리 탐색·로그 검색 (F23)
- 문서·도움말·에러 메시지 정비 (F25, F26), shadowing·파서 진단 보강 (F27, F28)

---

## 부록 A. 핵심 재현 명령

```bash
# F01: 실제 venv에서 오탐
lens env check ~/projects/llm-usage-dashboard/.venv
# F02: fail-open
lens env check /nonexistent-venv; echo $?
lens test parse <(echo 'not xml'); echo $?
# F03: 부분 실패 시 전체 중단
lens bundle create p.lens --test junit.xml --trace /nope
# F04: min-level 무동작
lens log filter /var/log/kern.log --min-level error 2>&1 | tail -1
# F05: 스레드 close를 누수로 오탐
lens trace analyze <(printf '%s\n' \
  '100 openat(AT_FDCWD, "/etc/hosts", O_RDONLY) = 3' \
  '100 clone(child_stack=0x7f, flags=CLONE_VM|CLONE_FILES|CLONE_THREAD) = 101' \
  '101 close(3) = 0' '101 +++ exited with 0 +++' '100 +++ exited with 0 +++')
# F06: import만 바뀐 라이브러리 → incompatible
gcc -shared -fPIC -o v1.so v1.c && gcc -shared -fPIC -o v2.so v2_importonly.c
lens abi diff v1.so v2.so
# F07: SCC를 경로처럼 출력
lens sys cycles /lib/systemd/system
# F10: 파이프 패닉
lens net inspect --json | head -1
# F23: TUI 경로 버그의 원인
#   Path::new(".").parent() == Some("") → canonicalize("") = ENOENT
```

## 부록 B. 검증 게이트 현황 (리뷰 시점)

| 단계 | 결과 |
|---|---|
| `cargo fmt --all -- --check` | 통과 |
| `cargo clippy --workspace --all-targets -- -D warnings` | 통과(경고 0) |
| `cargo test --workspace` | 74/74 통과, 통합 테스트 타깃 없음 |
| `cargo build --release -p lens-cli -p lens-mcp` | 통과 |
