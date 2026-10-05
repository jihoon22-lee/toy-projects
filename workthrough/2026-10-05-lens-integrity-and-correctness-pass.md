# Lens 워크스페이스 무결성·정확성 대수정

## Overview

`lens-*` Rust 워크스페이스(~8,600줄, 13 크레이트)에 대한 전면 코드 리뷰 결과를
바탕으로 **주장-구현 일치화** 작업을 수행했다. 전면 재작성 대신 4단계로 진행했다:

- Phase 0: 포렌식 무결성 복구(번들 포맷 통일, 버전/타임스탬프, TOCTOU, 릴리스)
- Phase 1: 약속됐으나 미구현이던 기능 구현(ABI, include 해석, drop-in, TUI)
- Phase 2: 정확성 버그(디스크 O(n²), 네트워크 diff, strace FD 추적)
- Phase 3: 중복 제거, 데드 의존성, 문서 재작성

57개 파일, +2069/−1460. 모든 워크스페이스 테스트·clippy·fmt 통과.

## Changes Made

### 1. Phase 0 — 포렌식 무결성

**단일 번들 포맷 (`lens.bundle/v2`)**
- `crates/lens-core/src/bundle.rs` (신규): create/inspect/verify를 한 구현으로 통합.
  `tar.gz` + `manifest.json` + 아티팩트별 SHA-256 + 수집 진단 보존.
- 기존 문제: `bundle create`는 평문 JSON을 쓰고 `bundle verify`는 tar.gz를 기대해
  자기 번들을 검증 불가했던 치명적 불일치. 변조된 파일은 fail-closed로 거부 확인.

**버전·타임스탬프 정규화**
- 하드코딩된 `"0.3.0"`/`"2026-10-05T00:00:00Z"` 전량 제거 → `env!("CARGO_PKG_VERSION")`
  및 `lens_core::time::{utc_now_iso, local_now_iso}` (`crates/lens-core/src/time.rs` 신규).

**`SafeInput` TOCTOU 버그**
- `crates/lens-core/src/identity.rs`: `symlink_metadata`(링크 inode)와 `fstat`
  (타깃 inode)를 비교하던 버그 수정 — 심볼링크 경로 입력이 100% `InputChanged`로
  실패하던 문제 해소.
- `crates/lens-disk/src/duplicates.rs`: 해시 대상(타깃 내용)과 inode 비교 기준
  일치, inode별 그룹핑으로 중복 해시 1회화.

**릴리스 파이프라인 수리**
- `.release-please-manifest.json`: 0.3.0 → 0.4.2. release-please 태그 로직을
  단일 패키지 `vX.Y.Z` 형식으로 교체(레거시 `product/v*` 기대로 SystemExit하던 것).
- `.github/workflows/release.yml`: `workflow_call` 추가, 불변 소스 커밋 해석,
  draft 자산 업로드→재다운로드+체크섬 검증→그 후에만 태그/publish 시퀀스로 재구성.
- `.github/scripts/test_release_flow.py`: fake GitHub API로 6개 시나리오 검증 —
  실패한 태그 조회 stdout이 EXISTING으로 잡히던 버그 수정 포함. CI에 연결.

**문서 정정**
- `FsNode` "36바이트" 주장 → 실측 376바이트(힙 제외)로 정정.
- "무할당" 로그 파싱, 미구현 DWARF/병렬 스캔 주장 등 README·크레이트 README 전면 교정.

### 2. Phase 1 — 미구현 기능 완성

**lens-abi** (`elf.rs`, `model.rs`, `diff.rs`)
- `.dynamic`에서 DT_NEEDED/RPATH/RUNPATH/SONAME, `.interp`, DT_VERDEF/VERNEED를
  `object` 크레이트로 실제 파싱(기존: 전부 빈 벡터).
- `SymbolEvidence.defined` 추가 — 정의/미정의(import) 심볼 구분.
- diff를 `lens_core::Compatibility`(Compatible/Incompatible/Uncertain)로 통일,
  uncertain은 fail-closed.

**lens-build** (`impact.rs`, `compiler.rs`)
- `extract_includes`/`resolve_include`: `"quoted"`·`<angle>`·`-include` 해석기.
- `header_to_headers` 실제 구축 → 전이 헤더 클로저가 실제로 작동
  (E2E: `config.h` 변경 시 `app.h` 경유 `main.cpp`와 직접 포함 `core.cpp` 모두 감지).

**lens-sys** (`loader.rs` 신규, `parser.rs`)
- `.d/` drop-in 디렉터리 스캔·병합, `ExecStart=` 리셋 의미론, `%u`/`%h` 확장.
- `lens_sys::load_units` 공용 로더로 CLI/doctor/MCP의 3중 중복 제거.
- `lens sys diff` 서브커맨드 추가.

**lens-log** (`parser.rs`, `indexer.rs`, `filter.rs`)
- memchr 개행 인덱싱, `to_uppercase` 할당 제거(ASCII 수동 비교→`eq_ignore_ascii_case`),
  구조화 `fields` 채움, JSON 키 정규화 할당 감소, min_level 의미론 교정.

**lens-tui + CLI**
- `crates/lens-tui/src/term.rs`에 렌더/이벤트 루프 이동, `TuiApp`이 서비스·로그
  실데이터 보유(기존 Services/Logs는 정적 플레이스홀더).
- `lens tui [PATH]` 서브커맨드 추가.

### 3. Phase 2 — 정확성 버그

**lens-disk** (`arena.rs`, `scanner.rs`)
- `ArenaTree::add_child` O(n²) 형제 순회 → tail-child O(1) 삽입.
- `aggregate_sizes` 재귀 → 반복문(깊은 트리 스택 오버플로 방지).
- rayon 기반 실제 병렬 stat(기존 `parallel` 옵션은 dead).
- 심볼링크 마운트 경계: 타깃 filesystem 기준으로 판정하도록 수정.
- dead `min_size` 옵션 제거, `StatOutcome::Ok`를 Box로 감싸 enum 크기 경고 해소.

**lens-net** (`parser.rs`, `diff.rs`)
- `/proc/net` 주소가 호스트 엔디안 형식임을 주석으로 명문화
  (`to_ne_bytes`가 이 형식에서는 정확 — 커널이 호스트 워드 해석을 출력).
- UDP 바인드 소켓(st=07, wildcard remote)을 리스너로 집계 — 기존엔 LISTEN만
  인식해 UDP 포트가 완전 누락.
- unix 파서의 `parts.len()<6` → `<7` (인덱스 범위 버그).
- diff: 리스너 매칭을 `(kind, address, port)`로 — `127.0.0.1→0.0.0.0` 바인드
  확장이 closed+new로 표면화. 연결은 inode가 아닌 5-tuple로 매칭(inode는
  스냅샷 간 불안정).

**lens-trace** (`parser.rs`, `model.rs`)
- fd 테이블을 `BTreeMap<tid, BTreeSet>`으로 분리 — 프로세스 A의 close가 B의 fd를
  지우던 전역 오염 해소. `+++ exited`에서 잔여 fd를 프로세스별 누수로 귀속하고
  tid 재사용에 대비.
- fd 생성 시스템콜 확장: socket/accept/accept4/dup/dup2/dup3/epoll_create/
  eventfd/signalfd/timerfd_create/inotify_init/memfd_create/pidfd_open 등.
  `pipe`/`socketpair`는 인자 배열 `[3,4]`에서 양쪽 fd 추출(`parse_fd_array`).
- `fd_leaks_by_process: BTreeMap<String, Vec<u64>>` 필드 추가(v1 스키마 호환,
  `fd_leaks`는 union으로 유지).

### 4. Phase 3 — 구조 정리

- `lens_sys::load_units`로 systemd 로딩 통합(main.rs/doctor.rs/mcp handler).
- 데드 의존성 제거: lens-abi의 `gimli`·`flate2`, lens-disk의 `walkdir`,
  lens-log의 `regex`, lens-cli의 `flate2`.
- ROADMAP.md·CHANGELOG.md를 실제 `lens-*` 워크스페이스 구조로 재작성
  (기존엔 삭제된 멀티프로덕트 포트폴리오 기술).

## Verification Results

```bash
cargo fmt --all -- --check        # clean
cargo clippy --workspace --all-targets -- -D warnings   # clean
cargo test --workspace --jobs 2   # 29개 테스트 타깃 전부 통과
cargo build --release -p lens-cli # ok
python3 .github/scripts/test_release_flow.py  # 6 tests OK
```

E2E 확인 사항:
- `lens bundle create`(4개 아티팩트) → `inspect` → `verify` → 변조 시
  "Bundle verification FAILED / Tampered: reports/test_run.json" + exit 1.
- `lens tui --help`, `lens doctor --help` 정상.
- systemd drop-in: `ExecStart=` 리셋이 `/bin/new`로, `%u`→`nobody`, `%h`→`/home/nobody`.
- lens-net: UDP 바인드 소켓이 `listening_ports`에 집계, 바인드 주소 변경 diff 감지.
- lens-trace: per-process fd, pipe/accept/dup 추적 회귀 테스트 통과.

## Known Limitations / Next Steps

- lens-abi DWARF 타입 그래프: gimli 의존성은 미사용으로 제거. 재도입 시 실제
  파싱 구현과 함께.
- JUnit `<system-out>`/`properties` 본문 텍스트는 미수집(메시지 속성만).
- lens-net diff는 바인드 변경을 closed+new로 표면화 — 명시적 "expanded" 이벤트
  타입은 향후 스키마 버전업에서 검토.
- 모델과 프레젠테이션의 완전한 분리(main.rs 내 포맷팅 로직)는 잔여 리팩토링.
