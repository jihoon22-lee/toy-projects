# Changelog

Lens는 워크스페이스 단일 버전으로 릴리스된다. 태그는 `vX.Y.Z` 형식이다.

## Unreleased

### 안전성 (Phase 0)
- stdout 파이프가 닫혀도 패닉하지 않고 exit 0으로 종료
  (`lens net inspect --json | head -1` 등).
- `lens bundle create`: 소스별 수집 실패를 전체 중단 대신 매니페스트
  `diagnostics`에 기록하고 번들을 계속 생성한다. 출력 파일이 이미 있으면
  거부하며 새 `--force` 플래그로만 덮어쓴다.
- 잘못된 입력이 조용히 성공하던 경로들을 0이 아닌 종료 코드의 명확한 에러로
  변경: venv가 아닌 경로(`env check`/`env inspect`), 읽을 수 없는 `net/tcp`
  (`net inspect --proc-dir`), 존재하지 않는 `doctor` `--root`/`--procfs`/
  `--systemd-dir`, JUnit 루트가 아닌 XML(`test parse`/`test diff`), 대부분
  파싱되지 않는 `trace analyze` 입력.
- `lens disk scan` 텍스트 출력: 권한 오류나 절단이 있으면 "Scan completed
  successfully" 대신 "Scan INCOMPLETE" 배너와 오류 수를 출력.
- `lens-mcp`: `ping` 메서드 지원. 알려진 도구의 실행 실패는 JSON-RPC error
  대신 `result.isError: true` + 텍스트 콘텐츠로 반환(MCP 권장 계약). 알 수
  없는 도구는 `-32602`, 알 수 없는 메서드·JSON 파싱 오류는 기존처럼
  JSON-RPC error.
- TUI: 시작 경로를 canonicalize하여 `lens tui`(경로 ".")에서 Backspace로
  상위 디렉터리 이동이 동작. 패닉 시 터미널(raw mode/alt screen)을 복원하는
  panic hook 추가.

## 0.4.3

무결성·정확성 대수정 릴리스. 상세 내역은 아래 섹션 참고.

### 무결성
- `lens bundle create/inspect/verify`가 단일 `tar.gz` + `manifest.json` 포맷
  (`lens.bundle/v2`)을 공유하도록 통일. 변조된 아티팩트를 fail-closed로 거부.
- 런타임 버전을 `CARGO_PKG_VERSION`에서, 타임스탬프를 `lens_core::time` 유틸에서
  생성하도록 정리. 하드코딩된 버전/시각 상수 제거.
- `SafeInput` 재검증이 심볼릭 링크의 링크 inode가 아닌 실제 타깃 메타데이터를
  비교하도록 수정(TOCTOU 버그).

### 정확성
- lens-abi: 동적 의존성·RPATH/RUNPATH·SONAME·인터프리터·심볼 버전 요구사항을
  실제로 수집. 정의/미정의 심볼 구분, `Compatibility` 어휘 통일,
  `.debug_info`의 선언 타입명 추출(상한 10k).
- lens-build: include 디렉티브 해석(`"`/`<>`/`-include`)과 전이 헤더 클로저 구현.
- lens-sys: `.d/` drop-in 스캔·병합과 `Key=` 리셋 의미론, `%u`/`%h` 확장 추가.
  `lens sys diff` 제공.
- lens-log: memchr 인덱싱, 레벨 파싱 할당 제거, 구조화 `fields` 채움.
- lens-disk: 형제 노드 삽입 O(1)화, 집계 반복문 전환, rayon 병렬 stat 구현.
- lens-net: `/proc/net` 호스트 엔디안 형식 문서화, UDP 바인드 소켓을 리스너로
  집계, diff가 (kind, address, port)/5-tuple로 매칭하도록 수정.
- lens-trace: fd 추적을 프로세스별 테이블로 분리하고 socket/accept/dup/pipe 등
  fd 생성 시스템콜 추적. `fd_leaks_by_process` 추가.
- lens-test: JUnit 속성 엔티티 디코딩, 상태 구분, 불완전 입력 감지.
- TUI: Services/Logs 탭에 실데이터 연결, `lens tui` 서브커맨드 추가.

### 릴리스/CI
- release-please 매니페스트를 0.4.2로 수정하고 `vX.Y.Z` 태그로 전환.
- release.yml에 `workflow_call` 추가, 불변 소스 커밋 해석, 다운로드 검증 후에만
  태그/퍼블리시하도록 시퀀스 재구성. 릴리스 플로우 테스트 6종 CI 연결.

### 정리
- 사용되지 않는 의존성 제거(lens-abi의 flate2, lens-disk의 walkdir,
  lens-log의 regex, lens-cli의 flate2).
- systemd unit 로딩을 `lens_sys::load_units`로 통합(CLI/doctor/MCP 중복 제거).
- README 성능 주장을 실측값으로 정정.

### 2차 다중 리뷰 기반 강화
- 번들 검증: 매니페스트를 `manifest.json` 정확 경로로만 해석(중첩 스푸핑 차단),
  중복·미등재 아카이브 엔트리 거부, 실제 해제 바이트 상한 적용, 번들 생성은
  임시 파일+원자적 rename으로 부분 쓰기·심볼링크 덮어쓰기 방지.
- lens-disk: 심볼링크 별칭이 visited_dirs를 오염시켜 하위 트리를 누락하던
  문제 수정, 깊이 상한 도달 시 `complete=false`, trash가 링크 타깃 대신 링크
  자체를 이동, `(dev=0,ino=0)` 파일은 경로 기반 중복 판정 폴백.
- lens-build: `-I` 검색 순서 보존 dedup, `..`/`./` include 경로 정규화,
  `-include`를 컴파일 디렉터리 기준 해석, 주석 내 `#include` 무시,
  스캔 상한 도달을 `scan_truncated`로 표면화.
- lens-trace: `+++ exited/killed/superseded` 계열 종결행 전부 처리,
  `strace -y` fd 주석(`3</etc/hosts>`) 제거, fork/clone 시 fd 테이블 상속,
  tid 재사용 generation 분리.
- lens-abi: 스트립된 바이너리용 program header(PT_DYNAMIC/PT_INTERP) 폴백,
  SHF_ALLOC 섹션 필터, 압축 DWARF 해제, `abi.versions` diff 비교.
- lens-sys: 유닛 suffix 전면 지원, 템플릿(`foo@.service`) drop-in 병합.
- lens-net: unix 소켓 Type/St 열 의미론 수정, diff 스키마 필드 추가,
  inode=0 고아 소켓 노이즈 제거.
- lens-test: `<properties>` 수집, suite 레벨 출력 보존, 인코딩 인지 디코딩,
  malformed 종료 태그 관대한 처리, 잘린 문서 `complete=false`.
- CLI/MCP/TUI: 표준에러 출력을 Display로, `--min-level`/`min_level` 검증,
  스캔 오류 표면화, `build inspect`가 `buildscope.snapshot/v4` 출력,
  `bundle create`에 최소 1개 소스 요구, TUI 리로드 실패 시 stale 상태 제거.
- CI: release.yml 태그 해석을 `git/ref/tags/<tag>`로 교체(이전 `/commits`
  리스트 엔드포인트는 항상 성공해 비교가 무의미했음), check_docs를 CI에 연결,
  assemble_pages가 `crates/README.md`를 `crates/index.md`로 발행.

## 0.4.2

Lens 통합 워크스페이스의 첫 태그 릴리스.

---

## 이전 이력 (Lens 통합 이전 — 레거시 제품별 버전 시대)

아래 항목들은 8개 독립 도구(diskmap, abilens, loglens, testlens, tracelens,
servicelens, buildscope, envlens) 시절의 기록으로, `{product}/vX.Y.Z` 태그로
버전이 관리됐다.

### 2026-10-03 — portfolio expansion published

- Add TraceLens: saved strace analysis, source evidence, snapshot/diff, Qt GUI.
- Add TestLens: JUnit/CTest collection, diff/history, CI policies and offline HTML.
- Add ServiceLens: offline systemd unit/drop-in provenance, graph, snapshot/diff.
- Correct BuildScope replay/source bounds, LogLens session input protection,
  DiskMap duplicate cleanup evidence, EnvLens runtime/PEP evaluation and
  AbiLens conservative compatibility.
- Extend existing products with versioned evidence/session/snapshot contracts,
  asynchronous desktop workflows and richer inspection controls.
- Gate all eight products independently; package installed layouts, connect
  release creation directly to verified draft assets, and repair Pages links.

Detailed behavior and release history live in each product's README and CHANGELOG.
The expansion was published as 0.2.1 for the five existing products and 0.1.0 for
TraceLens, TestLens and ServiceLens. The planned 0.2.0 tags were not published.

### 2026-10-03 — final review corrections

- Preserve EnvLens runtime input files and bounded pipe cleanup; incomplete
  snapshot collection remains unknown in check/diff.
- Resolve BuildScope vendor include roots correctly and auto-open saved diffs.
- Save/search with LogLens's applied filter and align AbiLens report I/O budgets.
- Validate ServiceLens scalar values and CTest observation completeness.
- Reconcile all product documentation, historical notes, examples and release
  links; add a required documentation consistency gate.

Product versions are owned by their package/build metadata. A local build or a
change recorded here does not itself publish a release.
