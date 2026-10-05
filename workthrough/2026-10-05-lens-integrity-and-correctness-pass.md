# Lens 워크스페이스 무결성·정확성 대수정

## Overview

`lens-*` Rust 워크스페이스(~8,600줄, 13 크레이트)에 대한 전면 코드 리뷰 결과를
바탕으로 **주장-구현 일치화** 작업을 수행했다. 전면 재작성 대신 4단계로 진행했다:

- Phase 0: 포렌식 무결성 복구(번들 포맷 통일, 버전/타임스탬프, TOCTOU, 릴리스)
- Phase 1: 약속됐으나 미구현이던 기능 구현(ABI/DWARF, include 해석, drop-in, TUI)
- Phase 2: 정확성 버그(디스크 O(n²), 네트워크 diff, strace FD 추적)
- Phase 3: 중복 제거, 데드 의존성, 문서 통폐합/재작성

모든 워크스페이스 테스트·clippy·fmt 통과. 커밋은 작업 단위별 Conventional
Commit으로 분리했다(아래 커밋 목록 참조).

## Changes Made

### 1. Phase 0 — 포렌식 무결성

**단일 번들 포맷 (`lens.bundle/v2`)**
- `crates/lens-core/src/bundle.rs` (신규): create/inspect/verify를 한 구현으로 통합.
  `tar.gz` + `manifest.json` + 아티팩트별 SHA-256 + 수집 진단 보존 + 압축해제
  폭탄 방어(항목 수/총 바이트 상한).
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
  일치, inode별 그룹핑으로 중복 해시 1회화, 하드링크는 회수 용량에서 제외.

**릴리스 파이프라인 수리**
- `.release-please-manifest.json`: 0.3.0 → 0.4.2. release-please 태그 로직을
  단일 패키지 `vX.Y.Z` 형식으로 교체(레거시 `product/v*` 기대로 SystemExit하던 것).
- `.github/workflows/release.yml`: `workflow_call` 추가, 불변 소스 커밋 해석,
  draft 자산 업로드→재다운로드+체크섬 검증→그 후에만 태그/publish 시퀀스로 재구성.
- `.github/scripts/test_release_flow.py`: fake GitHub API로 6개 시나리오 검증.
  `.github/workflows/ci.yml`에 연결.
- `.github/scripts/check_docs.py`: 삭제된 레거시 제품 버전 체크를 단일 워크스페이스
  버전 검사(Cargo.toml ↔ manifest ↔ CHANGELOG)로 교체.

### 2. Phase 1 — 미구현 기능 완성

**lens-abi** (`elf.rs`, `dwarf.rs` 신규, `model.rs`, `diff.rs`)
- `.dynamic`을 엔디안/클래스 인지 수동 파싱해 DT_SONAME/DT_RPATH/DT_RUNPATH,
  `.interp`, DT_VERDEF/VERNEED(→ `abi.versions`) 수집(기존: 전부 빈 벡터).
  개발 중 DT_STRSZ 상수를 DT_SYMTAB(6)으로 잘못 매핑한 버그를 E2E로 발견·수정.
- `SymbolEvidence.defined` 추가 — 정의/미정의(import) 심볼 구분.
- `dwarf.rs`: gimli로 `.debug_info`의 선언 타입명(struct/class/union/enum/typedef)
  추출, 상한 10,000. `abi.types` 실측 — lens 자체 바이너리에서 10,000 타입 도달.
- diff의 `compatibility`를 `lens_core::Compatibility` enum으로 통일
  (문자열 "unknown" → `Uncertain`, 직렬화값 `"uncertain"`).

**lens-build** (`impact.rs`, `compiler.rs`, `model.rs`)
- `ImpactGraph::add_translation_unit`: 소스 파일의 `#include`를 디스크에서 해석
  (`"…"`은 포함 파일 디렉터리 우선, `<…>`은 검색 경로만, `-include` 처리),
  `header_to_headers` 구축으로 전이 클로저가 실제로 작동
  (E2E: `config.h` 변경 시 `app.h` 경유 `main.cpp`와 직접 포함 `core.cpp` 모두 감지).
- `normalize_path` 공개로 `--header` 인자와 그래프 키 정규화 일치.

**lens-sys** (`loader.rs` 신규, `parser.rs`)
- `.d/` drop-in 디렉터리 스캔·사전순 병합, 할당 순서 유지 파서로 `Key=` 리셋
  의미론, `%u`/`%h`를 `User=` 기준으로 확장.
- `lens_sys::load_units` 공용 로더로 CLI/doctor/MCP의 3중 중복 제거.

**lens-log** (`parser.rs`, `indexer.rs`, `filter.rs`)
- `memchr::memchr_iter` 개행 인덱싱, `LogLevel::parse`/JSON 키 비교의
  `to_uppercase`/`to_lowercase` 할당 제거(`eq_ignore_ascii_case`),
  구조화 `fields`에 미인식 JSON 키 보존, `detect_level`/`matches_line` 단락 경로.

**lens-tui + CLI**
- `crates/lens-tui/src/term.rs`에 렌더/이벤트 루프 이동, `lens_tui::run` 공개로
  `lens-tui` 바이너리와 `lens tui`가 공유.
- `TuiApp`이 Services(`load_units` 실데이터)/Logs(LogIndexer tail) 탭을
  실데이터로 구동. 탭별 j/k 탐색.
- `lens tui [PATH]` 서브커맨드, `lens bundle --net` 캡처 추가.

**lens-cli 아키텍처 분리**
- `cli.rs`(clap 트리)/`ops.rs`(전 서브커맨드 디스패치)/`main.rs`(얇은 래퍼)로
  분리, `lens_cli` 라이브러리로 공개해 lens-mcp가 동일 디스패치 재사용.

### 3. Phase 2 — 정확성 버그

**lens-disk** (`arena.rs`, `scanner.rs`)
- `ArenaTree::add_child` O(n²) 형제 순회 → `last_child` tail 포인터로 O(1) 삽입.
- `aggregate_sizes` 재귀 → 반복형 post-order(깊은 트리 스택 오버플로 방지).
- rayon 기반 실제 병렬 stat(기존 `parallel` 옵션은 dead이던 것을 실구현).
- 심볼링크 마운트 경계: 링크가 아닌 **타깃** filesystem 기준으로 판정.

**lens-net** (`parser.rs`, `diff.rs`)
- `/proc/net` 주소가 호스트 엔디안 형식임을 주석으로 명문화.
- UDP 바인드 소켓(st=07, wildcard remote)을 리스너로 집계 — 기존엔 완전 누락.
- unix 파서의 `parts.len()<6` → `<7` (인덱스 범위 버그).
- diff: 리스너 매칭을 `(kind, address, port)`로 — `127.0.0.1→0.0.0.0` 확장을
  closed+new로 표면화. 연결은 inode가 아닌 5-tuple로 매칭.

**lens-trace** (`parser.rs`, `model.rs`)
- fd 테이블을 `BTreeMap<tid, BTreeSet>`으로 분리 — 프로세스 간 오염 해소.
  `+++ exited`에서 잔여 fd를 프로세스별 누수로 귀속하고 tid 재사용에 대비.
- fd 생성 시스템콜 확장(socket/accept/dup/pipe2 `[3,4]` 배열 등),
  `fd_leaks_by_process` 필드 추가(기존 `fd_leaks`는 union으로 유지).

**lens-test** (`junit.rs`, `model.rs`)
- JUnit 속성 엔티티 디코딩(`&amp;quot;` 등), 열린 태그 잔존 시 `complete=false`
  (잘린 XML 감지), 상태 메타데이터·`<system-out>`/`<system-err>` 수집,
  입력 해시 기반 결정적 run id.

### 4. Phase 3 — 구조/문서 정리

- 데드 의존성 제거: lens-abi의 `flate2`, lens-disk의 `walkdir`,
  lens-log의 `regex`, lens-cli의 `flate2`.
- 번들 변조 테스트를 비결정적 바이트 플립에서 아카이브 재구성 방식으로 교체
  (tar 패딩에 떨어지는 플립은 탐지 불가했던 문제).
- **문서 통폐합**: 12개 크레이트 README를 `crates/README.md` 단일 문서로 통합
  (각 크레이트의 스키마/공용 API/구현 범위/한계). 삭제된 레거시 제품군을 다루던
  workthrough 4건을 `2026-10-03-legacy-products-archive.md`로 통폐합.
- README 거짓 지표 정정("노드당 36바이트"는 실측 ~376B, "무할당"/"병렬"/
  "DWARF 지원" 주장을 실제 구현 기준으로 재기술).

## Commit Units (275bf09..HEAD)

1. `feat(core)` 번들 포맷 통일 + SafeInput/타임 유틸
2. `fix(test)` JUnit 엔티티/잘림/출력
3. `feat(sys)` drop-in 로더·리셋·specifier
4. `fix(disk)` 아레나/스캐너/중복·Trash
5. `fix(net)` UDP 리스너·diff 튜플 매칭
6. `fix(trace)` per-process fd 추적
7. `refactor(log)` 할당 제거·memchr
8. `feat(tui)` 실데이터 탭·run() 공유
9. `feat(build)` include 해석·전이 클로저
10. `refactor(cli)` cli/ops 분리·mcp 로더 통합
11. `feat(abi)` dynamic/DWARF/버전 수집
12. `fix(core)` 번들 변조 테스트 결정화
13. `docs` 크레이트 문서 통폐합·루트 문서 갱신
14. `ci` 릴리스 파이프라인 수리

## Verification Results

```bash
cargo fmt --all -- --check                    # clean
cargo clippy --all-targets -- -D warnings     # clean
cargo test --workspace                        # 29개 타깃 전부 ok
cargo build --release                         # ok
python3 .github/scripts/test_release_flow.py  # 6 tests OK
python3 .github/scripts/check_docs.py         # pass
```

E2E 확인:
- `lens bundle create`(disk) → `inspect` → `verify` 성공, 아티팩트 변조 시
  `tampered_files` 보고.
- `lens tui --help`/`lens doctor --help` 정상, `lens --version` → 0.4.2.
- systemd drop-in: `ExecStart=` 리셋 → `/bin/new`, `%u`→`nobody`, `%h`→`/home/nobody`.
- lens-net: UDP 바인드 소켓이 `listening_ports`에 집계, 바인드 주소 변경 diff 감지.
- lens-trace: per-process fd, pipe/accept/dup 추적 회귀 테스트 통과.
- lens-abi: 실 .so에서 SONAME/RUNPATH/interp/GLIBC 버전 요구사항 수집,
  DWARF 타입명 추출(lens 바이너리 10,000건 상한 도달).

## Known Limitations / Next Steps

- lens-abi DWARF: 타입명 표면만 추출 — 멤버/레이아웃 비교 타입 그래프는 미구현.
- lens-net diff는 바인드 변경을 closed+new로 표면화 — 명시적 "expanded" 이벤트
  타입은 향후 스키마 버전업에서 검토.
- 이후 다중 서브에이전트 코드 리뷰(보안/파서/성능/아키텍처/테스트·문서)를
  수행하고 결과를 통합 검토한다 — 결과는 별도 섹션에 추가 예정.
