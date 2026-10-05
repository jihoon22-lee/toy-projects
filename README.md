# Lens Forensic Platform (`toy-projects`)

리눅스 시스템 및 소프트웨어 생명주기 전 영역을 아우르는 **초고성능 통합 시스템 진단 및 포렌식 플랫폼**.

파편화되어 있던 기존 8개 독립 도구(`diskmap`, `abilens`, `loglens`, `testlens`, `tracelens`, `servicelens`, `buildscope`, `envlens`)의 엄격한 계약(Fail-Closed, Evidence-Bound, Schema 호환)을 온전히 계승하면서, **단일 정적 Rust 바이너리(`lens`), 90% 이상의 메모리 절감, Zero-Copy I/O, 수십 배의 속도 향상, 사고 포렌식 비행기록장치(`.lens` bundle)**로 전면 재개발 및 현대화되었습니다.

---

## 1. 아키텍처 및 워크스페이스 구조 (`crates/`)

플랫폼은 10개의 고성능 모듈식 Rust 크레이트로 구성되어 있습니다.

```text
toy-projects/
├── Cargo.toml          루트 워크스페이스 정의
├── crates/
│   ├── lens-core/      공통 기반 (Zero-Copy I/O, SafeInput TOCTOU 방어, SHA-256, 3상태 Diff)
│   ├── lens-disk/      스토리지 분석, ArenaTree (<40B), 중복 파일 탐지, FreeDesktop Trash
│   ├── lens-abi/       ELF/DWARF 검사, SHF_COMPRESSED 압축 지원, 전 버전 태그 수집
│   ├── lens-log/       mmap 제로카피 라인 인덱서, 무할당 고속 검색 엔진, Session v2
│   ├── lens-test/      quick-xml 초고속 스트리밍 테스트 파서, 회귀 자동 탐지
│   ├── lens-trace/     strace 스트리밍 파서, 미완료/재개 스레드 시퀀스 복원, 지연/에러 diff
│   ├── lens-sys/       systemd 유닛/드롭인 파서, Specifier 확장, Tarjan SCC 순환 탐지
│   ├── lens-build/     compile_commands.json 파서, 플래그 정규화, 헤더 영향도 역방향 DAG
│   ├── lens-env/       Zero-Code Python venv 분석기, 미충족 패키지 검사, import 섀도잉 탐지
│   ├── lens-cli/       단일 통합 CLI 실행 파일 (`lens`) 및 포렌식 비행기록장치 (`.lens` bundle)
│   ├── lens-tui/       대화형 터미널 UI 대시보드 (VIM 키바인딩, 디스크 트리맵, 실시간 게이지)
│   └── lens-mcp/       AI 어시스턴트(Claude, Antigravity) 연동용 Model Context Protocol 서버
```

---

## 2. 레거시 도구 대비 혁신 지표

| 영역 | 기존 레거시 도구 | 차세대 Rust 엔진 (`lens`) | 혁신 성과 |
|---|---|---|---|
| **배포 형태** | Qt6, Python, libstdc++ 런타임 종속 8개 분열 | 단일 정적 바이너리 (`lens`) | **외부 런타임 의존성 제로** |
| **디스크 스캔 메모리** | 노드당 432바이트 (100만 파일 시 ~1GB) | 노드당 36바이트 (`ArenaTree`, ~40MB) | **메모리 90% 이상 절감** |
| **로그 검색 핫패스** | 검색 시마다 `toLowerAscii` 힙 할당 (100만회+) | 무할당 슬라이딩 윈도우 (`contains_insensitive`) | **Zero Allocation (0회 할당)** |
| **DWARF 압축 섹션** | `SHF_COMPRESSED` 즉시 포기 (`limited`) | `gimli` + `flate2`로 배포판 압축 완벽 지원 | **정상 DWARF 분석 복원** |
| **심볼 버전 범위** | `GLIBC*` 3개 네임스페이스 하드코딩 | `DT_VERDEF`/`DT_VERNEED` 전 라이브러리 동적 수집 | **제한 완전 해제** |
| **테스트 XML 파싱** | Python `defusedxml` (10만 건에 1.3초, 300MB) | `quick-xml` 스트리밍 (10만 건에 30ms, <10MB) | **40배 가속 / 30배 메모리 절감** |
| **strace 다중스레드** | C++ 파서 복잡성, 스레드 중첩 분실 위험 | 비동기 호출 매칭 및 $O(1)$ 스트리밍 파서 | **안정성/속도 대폭 향상** |
| **systemd 정적 분석** | Python AST 파싱 속도 지연 | 무평가 정적 파서 + Tarjan SCC 순환 그래프 탐지 | **부팅 데드락 사전 예방** |
| **빌드 영향도 분석** | CMake 캐시 수동 파싱 | 헤더 역방향 전이 종속성 클로저 계산 (<10ms) | **빌드 타임 예측 가속** |
| **Python 환경 감사** | Python 인터프리터 구동 위험 | 제로 코드 실행 정적 메타데이터 & 섀도잉 감사 | **보안 취약점 원천 차단** |
| **사고 포렌식 기록** | 도구별 산출물 수동 수집 | 통합 `.lens` 비행기록장치 번들 생성/검사 | **사고 대응 시간 획기적 단축** |

---

## 3. 빌드 및 테스트

### 전체 테스트 실행 (10개 크레이트 동시 검증)
```bash
cargo test --workspace --jobs 2
```

### 릴리즈 바이너리 컴파일
```bash
cargo build --release -p lens-cli --jobs 2
# 생성 바이너리: target/release/lens
```

---

## 4. 통합 CLI 사용법 (`lens`)

단일 실행 파일 `lens` 하나로 8개 진단 도메인의 기능과 포렌식 번들링을 모두 실행할 수 있습니다.

```bash
# 1. 파일시스템 초고속 스캔 & 중복 파일 탐지 (동일 inode 하드링크 자동 인식)
$ lens disk scan .
$ lens disk duplicates . --min-size 1048576

# 2. 바이너리 ABI 검증 및 3상태 호환성 diff (abilens.report/v2)
$ lens abi inspect /usr/bin/python3
$ lens abi diff lib_v1.so lib_v2.so

# 3. 수 GB 로그 파일 무할당 제로카피 고속 필터링
$ lens log filter /var/log/syslog --query "error" --min-level warn

# 4. 스트리밍 테스트 리포트 파싱 및 회귀(Regression) 자동 탐지
$ lens test parse target/junit.xml --project backend
$ lens test diff run_baseline.xml run_candidate.xml

# 5. Syscall 트레이스(strace) 분석 및 지연시간/신규 에러 탐지
$ lens trace analyze /var/log/strace.log
$ lens trace diff baseline_trace.log candidate_trace.log

# 6. systemd 유닛 순환 의존성(Cycle) 오프라인 정적 탐지
$ lens sys cycles /etc/systemd/system

# 7. 헤더 수정 시 재컴파일이 필요한 소스 파일 역방향 영향도 분석
$ lens build impact compile_commands.json --header include/common.h

# 8. Python 가상환경 종속성 및 표준 라이브러리 모듈 섀도잉 정적 검사
$ lens env inspect .venv --project .

# 9. 종합 사고 포렌식 비행기록장치(.lens) 번들 생성 및 검사
$ lens bundle create incident.lens --disk /var/log --trace /tmp/strace.log --test target/junit.xml
$ lens bundle inspect incident.lens
```

---

## 5. 상세 문서 링크

각 크레이트별 아키텍처, 벤치마크, Rust API 레퍼런스는 다음 개별 문서를 참고하세요:

- [통합 CLI 가이드 (`lens-cli`)](crates/lens-cli/README.md)
- [공통 플랫폼 코어 (`lens-core`)](crates/lens-core/README.md)
- [스토리지 분석 엔진 (`lens-disk`)](crates/lens-disk/README.md)
- [바이너리 ABI 분석 엔진 (`lens-abi`)](crates/lens-abi/README.md)
- [로그 분석 엔진 (`lens-log`)](crates/lens-log/README.md)
- [테스트 분석 엔진 (`lens-test`)](crates/lens-test/README.md)
- [시스템 트레이스 엔진 (`lens-trace`)](crates/lens-trace/README.md)
- [시스템 유닛/서비스 엔진 (`lens-sys`)](crates/lens-sys/README.md)
- [빌드 데이터베이스 엔진 (`lens-build`)](crates/lens-build/README.md)
- [파이썬 환경 엔진 (`lens-env`)](crates/lens-env/README.md)
