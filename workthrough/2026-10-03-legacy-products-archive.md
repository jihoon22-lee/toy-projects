# 2026-10-03 — 레거시 제품 작업 기록(보관)

> 이 파일은 `abilens`, `buildscope`, `diskmap`, `envlens`, `loglens`,
> `servicelens`, `testlens`, `tracelens` — 현재 워크스페이스에 **존재하지 않는**
> 8개의 독립 레거시 제품(C++/Python)에 대한 2026-10-03 작업 기록을 통폐합한 것이다.
> 원본 문서 4건(`phase6-product-enhancements`, `portfolio-expansion`,
> `version-reset-and-feature-round-2`, `v0.1.1-review-fixes`)은 삭제되었고,
> 이 파일이 그 요약이다. 현재 코드와 무관하며 감사 추적 목적으로만 남는다.

## portfolio-expansion
- 코디네이터가 EnvLens/AbiLens와 공용 통합을 구현하고, BuildScope/LogLens/DiskMap은
  각 소유자가 완료. 베이스라인 `e40ae22`.
- 제품별 수용 기능: 빌드 DB 재생, 세션 타임라인, 중복 keeper, 비동기 UI 등.

## phase6-product-enhancements (기능 라운드 1)
- AbiLens 동적 심볼 서피스(#98), DiskMap 스냅샷 diff 필터(#99),
  LogLens `loglens.session/v1`(#100), BuildScope delayed include 분석(#101),
  EnvLens 스냅샷 v2(`3ff1037`).

## version-reset-and-feature-round-2
- 제품군 버전을 `v0.2.0`에서 `v0.1.0`으로 리셋(릴리스·태그 5개 삭제, 매니페스트·
  CHANGELOG 정렬, `bump-patch-for-minor-pre-major` 설정).
- 각 제품의 두 번째 기능 라운드 진행. release-please PR #107로 `0.1.1` 확인.

## v0.1.1-review-fixes (v0.1.2)
- 2·3차 라운드(#104–#115) 상세 리뷰에서 15건 지적 → 전부 수정.
  대표: loglens 정규식 SIGSEGV, envlens `Requires-External` 처리,
  buildscope BOM 거부, abilens `forbid_stripped` fail-closed 등.
- 제품별 커밋 분리로 release-please CHANGELOG 정확도 확보.

---

이후 이 제품군은 단일 Rust 워크스페이스(`lens-*` 크레이트)로 대체되었다.
현재 구조와 계약은 [저장소 README](../README.md)와
[crates/README.md](../crates/README.md)를 참고한다.
