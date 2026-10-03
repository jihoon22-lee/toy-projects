# 2026-10-03 — v0.1.0 리셋과 2차 제품 강화

> **역사적 작업 기록** — 아래 기능 범위, 계획, 테스트 수, 버전 및 Git/배포 상태는
> 해당 작업 단계에서 기록한 내용이다. 현재 사용법이나 최신 검증·배포 상태를 뜻하지 않는다.
> 현재 제품별 안내는 [저장소 README](../README.md), 변경 이력은
> [CHANGELOG](../CHANGELOG.md), 후속 계획은 [ROADMAP](../ROADMAP.md)을 참고한다.

## 배경

제품 라인을 `v0.1.0`부터 다시 시작한다는 결정에 따라 `v0.2.0` 릴리스·태그를
정리하고, 전 제품의 버전 표면을 `0.1.0`으로 정렬했다. 이어서 각 제품의 두 번째
독립 기능 라운드를 진행했다.

## 당시 버전 리셋 (PR #103, #108)

- `*/v0.2.0` 릴리스 5개와 태그 5개 삭제. 태그 보호 ruleset은 일시 비활성화 후
  include 패턴을 `*/v*`로 단순화하고 재활성화.
- 소스 버전·`.release-please-manifest.json`·CHANGELOG를 `0.1.0` 베이스라인으로
  정렬. 기존 `abilens/v0.1.0` 등 Phase 5 베이스라인 태그는 그대로 유지.
- `release-please-config.json`에 `bump-patch-for-minor-pre-major: true` 추가:
  1.0 이전에는 `feat`도 patch(0.1.x)로 범프해 소각된 `0.2.0` 이름 공간을
  영구히 회피하려는 당시 방침이었다. 처음에 `bump-minor-pre-major`로 잘못 설정했던 것을 교정.
- 검증: release-please PR #107이 전 제품 `0.1.1`을 제안 — 설정 의도와 일치.

이 버전 리셋·태그 삭제와 patch-only 방침은 과거 작업의 기록이다. 현재 반복할
운영 절차가 아니며, 현재 [release-please 설정](../release-please-config.json)은
`bump-patch-for-minor-pre-major: false`로 기능 변경을 minor로 구분한다.

## 2차 기능 라운드

| 제품 | 기능 | PR |
|------|------|-----|
| diskmap | `--load-snapshot A --compare-snapshot B` 오프라인 스냅샷 비교 | #104 |
| abilens | `DT_VERDEF`/`DT_VERSYM` 기반 `name@version` 심볼 한정 | #105 |
| loglens | `loglens.format/v1` 선언적 정규식 포맷 플러그인 (`--format-plugin`) | #106 |
| abilens | 정책 DSL 확장: 심볼 규칙, stripped, rpath/runpath 금지 | #109 |
| buildscope | compile_commands.json 스트리밍 파싱 (전체 DOM 제거) | #110 |
| diskmap | `--cleanup-plan` 중복 정리 드라이런 (읽기 전용) | #111 |
| envlens | `envlens check` 단일 스냅샷 호환성 서브커맨드 | #112 |

## 3차 라운드 (로드맵 잔여분)

| 제품 | 기능 | PR |
|------|------|-----|
| loglens | GUI 세션 열기/저장 (`loglens.session/v1` 재사용) | #113 |
| abilens | `_ZTV*` vtable 심볼을 report/diff 전용 축으로 분리 | #114 |
| envlens | `Requires-External` 메타데이터 → `external-requirement` unknown 증거 | #115 |

## 주요 교정 사항

- **BuildScope Qt 호환**: CI의 Qt6이 `QJsonValue::fromJson`을 제공하지 않아
  배열 래핑 파싱으로 교체 (`73cf96c`).
- **envlens ruff**: `test_cli_e2e.py` 포맷 위반 수정 (`25af7f4`).
- **AbiLens verdef명 검증**: `valid_version`이 숫자·점 형식만 허용해
  `ZLIB_1.2.0` 같은 verdef명을 거부 → 심볼릭 버전명용 완화 검증 분리.
- **base verdef 장식 제거**: versym 인덱스 1은 무장식 관례(`BASE`) —
  `deflate@libz.so.1` 같은 잘못된 한정을 막기 위해 인덱스 ≥2만 장식하고
  hidden 비트(0x8000)를 마스킹.
- **버전 스크립트 C++ 매칭**: `extern "C++"` 패턴은 demangled 이름에 매칭되므로
  픽스처 vtable 내보내기는 mangled 와일드카드 `_ZTV*` 사용.
- **`--cleanup-plan`은 절대 파일을 삭제하지 않는다** — GUI와 동일한 보수적
  의미론(보호 루트, 마운트 경계, 하드링크, 불완전 스캔, 심볼릭 링크)을 CLI에
  드라이런으로만 노출.

## 당시 검증

- abilens: parser/integration/clean + ASan/UBSan 전부 통과. libstdc++에서
  179개 버전 한정 vtable 확인.
- loglens: CTest 18/18, 플러그인 스위트 33/33.
- envlens: pytest 117 + ruff check/format 클린.
- buildscope: 네이티브 9/9, 대형 DB·구조 오류 E2E.
- diskmap: storage CLI 테스트 + 오프라인 비교/필터/플랜 E2E.

## 당시 변경에서의 설계 원칙

- 전부 additive 변경 — 기존 스키마의 필드는 바꾸지 않고 선택 필드만 추가.
- 불확실성 유지 — `external-requirement`는 unknown 증거로 보고하며 실패로
  간주하지 않음. `--cleanup-plan`은 어떤 경우에도 삭제하지 않음.
- 결정적 출력 — 정렬된 목록, 바이트 동등 재직렬화.
- 실패 폐쇄 — malformed 입력은 부분 결과 없이 거부.

## 당시 기록한 잔여 로드맵

- diskmap 증분 재스캔: 디렉터리 mtime이 파일 내용 변경에 전파되지 않아
  정확성을 훼손하므로 보류. 휴리스틱 표시 없는 구현은 계약 위반이 됨.
- loglens 세션에 triage 상태(북마크·주석) 번들: 별도 스키마 확장 필요.
- Release PR #107 (`0.1.1` 전 제품 제안): 사용자 판단에 맡김.


위 보류·제안은 해당 라운드 종료 시점의 기록이다. LogLens의 triage 세션 번들은
이후 session v2에 반영되었으며, 현재 사용법은 [LogLens 안내](../loglens/README.md)에
있다. Release PR #107에 대한 당시 제안도 현재 대기 중인 승인 요청을 뜻하지 않는다.
현재 게시 이력과 남은 계획은 상단의 CHANGELOG·ROADMAP 링크를 기준으로 확인한다.
