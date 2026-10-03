# ROADMAP

각 제품은 자신의 사용자 문제에 맞는 독립적인 스택·버전·테스트를 유지한다.
현재 사용법과 지원 경계는 [README](README.md)와 각 제품 문서를 기준으로 한다.

| 제품 | 구현한 방향 | 이후 검토할 범위 |
|---|---|---|
| [DiskMap](diskmap/README.md) | Qt6 탐색·중복 정리, raw-byte snapshot, 취소 가능한 작업, Trash 기록 | 정확성을 보존하는 증분 재스캔 |
| [LogLens](loglens/README.md) | Qt6 조사, 근거에 묶인 triage/session, 전체 파일 검색, JSON 상관 분석 | 다중 소스 시계 보정과 대규모 이력 |
| [BuildScope](buildscope/README.md) | C++ producer, 제한·취소 가능한 include 분석, impact·relocation·GUI import | 빌드 시스템별 추가 근거 adapter |
| [EnvLens](envlens/README.md) | Python 표준 메타데이터, origin/extras/shadowing, runtime·CI 정책 | 더 다양한 인터프리터 ABI 증거 |
| [AbiLens](abilens/README.md) | 네이티브 ELF, 심볼 속성, 3상태 diff, sysroot 후보, 선택적 DWARF | 공개 API 도달성과 더 넓은 타입 그래프 |
| [TraceLens](tracelens/README.md) | 저장된 strace·분할 trace 분석, 근거 탐색·GUI·diff | 측정된 대규모 스트림 탐색 확대 |
| [TestLens](testlens/README.md) | JUnit/CTest, 명시적 retry/shard, diff/history·오프라인 HTML | 추가 runner 형식과 압축 CTest 출력 |
| [ServiceLens](servicelens/README.md) | rootfs unit/drop-in·설정 출처·graph·diff/check | 지원 systemd 의미론의 단계적 확대 |

공통 릴리스는 설치 결과·체크섬·provenance를 검증한 뒤 draft를 게시한다.
로컬 검증과 GitHub 호스팅 환경에서의 실행은 구분해서 기록한다.
