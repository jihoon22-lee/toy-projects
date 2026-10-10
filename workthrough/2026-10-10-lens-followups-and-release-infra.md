# 2026-10-10 — 후속 수정·정리 및 v0.6.0 릴리스

## 요약

v0.5.0 이후 남은 검증 항목과 인프라 개선을 마무리하고 v0.6.0을
릴리스했다. 이어서 `paste` 의존성 제거와 수동 릴리스 자동화까지
반영했다.

## 변경 범위

- `feat/lens-remaining` → PR #140
  - `abi inspect`/`abi diff` 비ELF 입력: `status: "non-elf"` 리포트 +
    종료 0 → `error: ... is not an ELF file` + 종료 2 (fail-closed,
    라이브러리·MCP는 기존 리포트 유지).
  - `log filter --since/--until/--year`: RFC 3339·ISO 날짜·syslog
    타임스탐프 창 필터. 시각 미확정 줄은 제외하고 수를 stderr로 보고
    (`--include-unknown`으로 복원). `--min-level`과 같은 규약.
  - `.gz`/stdin 입력을 힙 버퍼 대신 삭제 예정 임시 파일로 스풀링해
    mmap. ~300 MiB 로그에서 익명 메모리 374.8 MB → 51.2 MB.
  - TUI 목록 뷰포트가 선택을 따라가지 않던 실제 버그 수정(tmux
    스모크 테스트로 발견).
- `fix/lens-followups` → PR #139
  - 전 테스트에 `tempfile::TempDir` 적용으로 `/tmp` 누수 제거.
  - `lens completion` 파이프 종료 시 패닉 대신 종료 0.
  - `doctor --root`가 호스트 `/etc/ld.so.preload`가 아니라
    `<root>/etc/ld.so.preload`를 검사.
  - README에 수동 릴리스 절차 문서화.
- `release/v0.6.0` → PR #141 → `v0.6.0` 태그·릴리스
  - 릴리스 워크플로가 아티팩트 빌드, `SHA256SUMS` 검증, 태그 발행.
  - 다운로드 바이너리가 `lens 0.6.0`이고 비ELF 입력이 종료 2임을 확인.
- `chore/lens-infra-fixes` → PR #142
  - ratatui 0.30( + crossterm 0.29)으로 올려 `paste`
    의존성(RUSTSEC-2024-0436, unmaintained) 제거. API 변경은
    `Backend::Error` 바운드 추가 하나뿐.
  - `release-please.yml`: 수동 `chore(release)` 머지 후 manifest
    버전과 태그를 비교해 없으면 Release 워크플로를 자동 호출.
    비-404 조회 실패는 태그 없음으로 오인하지 않고 명시적 실패로
    처리한다.

## 검증

- 게이트: `cargo fmt --all -- --check`, `cargo clippy --workspace
  --all-targets -- -D warnings`, `cargo test --workspace --jobs 2`
  (31 스위트 0 실패), `python3 .github/scripts/check_docs.py`,
  `python3 .github/scripts/test_release_flow.py`.
- 실 `strace` 캡처 검증(사전 미검증 항목 해소): `strace -f -o`로 만든
  실제 출력을 `lens trace analyze`에 입력해 373개 호출 파싱,
  `syscall:errno` 오류 집계, clone 부모-자식 관계, fd 9 누수 감지를
  확인했다.
- tmux 스모크 테스트로 ratatui 0.30 마이그레이션 후 TUI 렌더링을
  확인했다.

## 정리

- 병합된 작업 브랜치(`feat/lens-*`, `fix/lens-*`, `chore/lens-*`,
  `docs/*`, `release/*`)와 미병합 브랜치 6개를 삭제했다. 미병합
  브랜치는 `main`에 이미 포함되거나 제거된 멀티프로덕트 릴리스
  잔여물뿐이었다.
- 로컬·원격 모두 `main`만 남았다. 열린 PR 없음, 작업 트리 깨끗,
  `/tmp/lens*` 0개, `cargo clean`으로 `target/` 삭제.
