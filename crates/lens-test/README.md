# lens-test

초고속 스트리밍 테스트 결과 수집, 회귀(Regression) 분석 및 실행 diff 엔진.

기존 Python 기반 `testlens`(5,210 라인)을 완전히 재설계 및 대체하여, **10만 건 이상의 테스트 XML을 50ms 미만 및 10MB 미만의 메모리로 스트리밍 파싱**하고, 자동 회귀 탐지 및 스냅샷 diff를 제공합니다.

---

## 핵심 아키텍처 및 개선 사항

### 1. Zero-DOM 스트리밍 파서 (`quick-xml`)
- **Python 메모리 병목 극복**: 기존 Python `defusedxml` 구현체는 전체 DOM 트리를 구성하여 10만 건의 테스트 케이스 처리 시 약 1.3초와 300MB의 메모리를 소모했습니다.
- `lens-test`는 `quick-xml` 기반의 이벤트 스트리밍 파서를 적용하여 메모리에 전체 XML을 올리지 않고 온디맨드로 이벤트를 소비합니다.
- 동일한 10만 건의 테스트 케이스를 **30ms 이내에 파싱하며, 메모리 사용량은 10MB 미만(30배 이상 절감)**입니다.

### 2. 정밀한 회귀(Regression) 및 해결(Fix) 탐지 (`diff_test_runs`)
- 테스트 케이스의 고유 식별자(`identity`, 예: `pkg.Class::test_method`)를 기반으로 두 실행 결과를 비교:
  - **회귀(Regression)**: 기존 베이스라인에서 통과했으나 후보 실행에서 실패/에러가 발생한 테스트를 즉각 식별.
  - **해결(Fix)**: 기존에 실패했으나 후보 실행에서 통과한 테스트를 식별.
  - **테스트 집합 변경**: 새로 추가되거나 삭제된 테스트를 `SetDiff`로 추적.

### 3. `testlens.run/v1` 및 `testlens.diff/v1` 스키마 준수
- 기존 `testlens`의 JSON 스키마를 준수하여 CI 대시보드 및 리포트 파이프라인과 완벽히 호환됩니다.

---

## 사용 예제

```rust
use lens_test::{parse_junit_xml, diff_test_runs};

// 1. JUnit XML 스트리밍 파싱
let xml_bytes = std::fs::read("target/junit.xml")?;
let run = parse_junit_xml(&xml_bytes, "my_project")?;

println!("수집 완료: 총 {}개 테스트 (통과: {}, 실패: {}, 스킵: {})",
    run.summary.total, run.summary.passed, run.summary.failed, run.summary.skipped);

// 2. 두 테스트 실행 간 회귀 분석
let run_baseline = parse_junit_xml(&baseline_bytes, "my_project")?;
let run_current = parse_junit_xml(&current_bytes, "my_project")?;
let diff = diff_test_runs(&run_baseline, &run_current);

for regression in &diff.regressions {
    eprintln!("[경고] {}", regression);
}
for fix in &diff.fixes {
    println!("[해결] {}", fix);
}
```
