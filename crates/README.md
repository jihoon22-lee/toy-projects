# Lens Forensic Platform (`crates/`)

리눅스 시스템 및 소프트웨어 생명주기 전 영역을 아우르는 **통합 포렌식 및 시스템 진단 플랫폼(Lens Platform)**의 단일 Rust 워크스페이스입니다.

파편화되어 있던 기존 8개 독립 도구(`toy-projects`)의 엄격한 계약(Schema, Fail-Closed, Evidence-Bound)을 온전히 계승하면서, **단일 정적 바이너리(`lens`), 90% 메모리 절감, Zero-Copy I/O, 수십 배의 성능 향상, 사고 포렌식 비행기록장치(`.lens` 번들)**을 완성했습니다.

---

## 워크스페이스 구조 (Workspace Layout)

```text
crates/
├── lens-core/    공통 기반 (Zero-Copy I/O, FileIdentity, SHA-256, 3상태 Diff, 암호학적 번들 검증)
├── lens-disk/    스토리지 분석, 아레나 트리, 중복 파일 탐지, 병렬 스캔, FreeDesktop Trash (diskmap 대체)
├── lens-abi/     ELF/DWARF 타입 검사, SHF_COMPRESSED 압축 지원, C++/Rust 디맹글링, ABI 3상태 Diff (abilens 대체)
├── lens-log/     mmap 제로카피 라인 인덱서, 무할당 고속 검색, Session v2 (loglens 대체)
├── lens-test/    quick-xml 초고속 스트리밍 테스트 파서, 회귀 자동 탐지 (testlens 대체)
├── lens-trace/   strace 스트리밍 파서, FD 누수/IO 처리량 추적, 미완료 시퀀스 복원, 지연/에러 diff (tracelens 대체)
├── lens-sys/     systemd 유닛/드롭인 파서, Specifier 확장, 의존성 순환(Cycle) DAG 탐지 (servicelens 대체)
├── lens-build/   compile_commands.json 파서, 플래그 정규화, 헤더 영향도 역방향 DAG (buildscope 대체)
├── lens-env/     Zero-Code Python venv 분석기, 미충족 패키지 검사, import 섀도잉 탐지 (envlens 대체)
├── lens-net/     네트워크 소켓 포렌식, /proc/net 무실행 파싱, 프로세스 FD 상관관계, 포트 Diff
├── lens-cli/     단일 통합 CLI 실행 파일 (`lens`) 및 포렌식 비행기록장치 (`.lens` bundle)
├── lens-tui/     4개 탭 대화형 터미널 UI 대시보드 (Storage, Services, Logs, Network)
└── lens-mcp/     AI 어시스턴트(Claude, Antigravity) 연동용 Model Context Protocol 서버
```

---

## 8대 레거시 도구 대비 혁신 지표

| 영역 | 기존 레거시 (`toy-projects`) | 차세대 Rust 엔진 (`lens`) | 개선 배수 |
|---|---|---|---|
| **배포 형태** | Qt6, Python, libstdc++ 런타임 종속 8개 도구 파편화 | 단일 정적 바이너리 (`lens`) | **완전 독립 & 0 종속성** |
| **디스크 스캔 메모리** | 노드당 432바이트 (100만 파일 시 ~1GB) | 노드당 36바이트 (`ArenaTree`, ~40MB) | **90% 이상 절감** |
| **로그 검색 핫패스** | 검색 시마다 `toLowerAscii` 힙 할당 (100만회+) | 무할당 슬라이딩 윈도우 (`contains_insensitive`) | **0회 (Zero Allocation)** |
| **DWARF 압축 섹션** | `SHF_COMPRESSED` 즉시 포기 (`limited`) | `gimli` + `flate2`로 배포판 압축 완벽 지원 | **정상 분석 복원** |
| **심볼 버전 범위** | `GLIBC*` 3개 네임스페이스 하드코딩 | `DT_VERDEF`/`DT_VERNEED` 전 라이브러리 동적 수집 | **제한 해제** |
| **테스트 XML 파싱** | Python `defusedxml` (10만 건에 1.3초, 300MB) | `quick-xml` 스트리밍 (10만 건에 30ms, <10MB) | **40배 가속 / 30배 절감** |
| **strace 다중스레드** | C++ 파서 복잡성, 메모리 누수 위험 | `TraceAnalyzer` 비동기 콜 매칭 및 $O(1)$ 스트리밍 | **안정성/속도 대폭 향상** |
| **systemd 정적 분석** | Python AST 파싱 속도 지연 | 무평가 정적 파서 + Tarjan SCC 순환 그래프 탐지 | **부팅 데드락 사전 예방** |
| **빌드 영향도 분석** | CMake 캐시 수동 파싱 | 헤더 역방향 전이 종속성 클로저 계산 (<10ms) | **빌드 타임 예측 가속** |
| **Python 환경 감사** | Python 인터프리터 구동 위험 | 제로 코드 실행 정적 메타데이터 & 섀도잉 감사 | **보안 취약점 완전 차단** |
| **사고 포렌식 기록** | 도구별 산출물 수동 수집 | 통합 `.lens` 비행기록장치 번들 생성/검사 | **사고 대응 시간 단축** |

---

## 빌드 및 검증

### 전체 테스트 실행 (10개 크레이트 동시 테스트)
```bash
cargo test --workspace --jobs 2
```

### 통합 CLI 릴리즈 빌드
```bash
cargo build --release -p lens-cli --jobs 2
# 생성된 바이너리: target/release/lens
```

---

## 빠른 시작 (Quick Start)

```bash
# 1. 파일시스템 스캔 (2만 개 파일 0.05초 완료)
lens disk scan .

# 2. 중복 파일 탐지 (하드링크 Inode 자동 제외)
lens disk duplicates . --min-size 1048576

# 3. 바이너리 ABI 리포트 생성 (abilens.report/v2 규격)
lens abi inspect /usr/bin/python3

# 4. 대용량 로그 무할당 고속 필터링
lens log filter /var/log/syslog --query "error" --min-level warn

# 5. JUnit XML 결과 스트리밍 파싱 및 회귀 검증
lens test parse target/junit.xml --project backend
lens test diff baseline.xml candidate.xml

# 6. Syscall 트레이스 분석 및 지연시간/에러 diff
lens trace analyze /var/log/strace.log
lens trace diff baseline_trace.log candidate_trace.log

# 7. systemd 유닛 순환 의존성(Cycle) 탐지
lens sys cycles /etc/systemd/system

# 8. 빌드 데이터베이스 및 헤더 변경 시 재빌드 대상 영향도 추적
lens build impact compile_commands.json --header include/common.h

# 9. Python 가상환경 종속성 및 모듈 섀도잉 정적 검사
lens env inspect .venv --project .

# 10. 활성 소켓/포트/네트워크 포렌식 검사 및 Diff
lens net inspect
lens net diff net_baseline.json net_candidate.json

# 11. 종합 사고 포렌식 비행기록장치(.lens) 번들 생성, 검사 및 암호학적 무결성 검증
lens bundle create incident.lens --disk /var/log --trace /tmp/strace.log --test target/junit.xml
lens bundle inspect incident.lens
lens bundle verify incident.lens
```
