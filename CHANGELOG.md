# Changelog

Lens는 워크스페이스 단일 버전으로 릴리스된다. 태그는 `vX.Y.Z` 형식이다.

## Unreleased

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

## 0.4.2

마지막 태그된 릴리스. 이후 변경은 위 Unreleased 항목 참고.
