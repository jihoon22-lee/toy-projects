# lens-abi

Linux ELF 바이너리 분석, 동적 심볼 서피스 추적 및 ABI 호환성 검증 엔진.

기존 C++20 기반 `abilens`(6,144 라인)를 완전히 재설계 및 대체하여, **제로카피 ELF 파싱, 개방형 심볼 버전 지원, 배포판 압축 DWARF(`SHF_COMPRESSED`) 완벽 지원, 3상태 호환성 판정**을 제공합니다.

---

## 핵심 아키텍처 및 개선 사항

### 1. Zero-Copy ELF 검사기 (`inspect_elf`)
- **메모리 복사 원천 제거**: 120MB 바이너리 검사 시 전체 파일을 `std::vector` 힙에 복사하던 기존 C++ 구현체 대신, `object` 크레이트 기반의 Zero-Copy 슬라이스 파싱을 적용하여 메모리 오버헤드를 0으로 축소했습니다.
- ELF32/64, 리틀/빅 엔디언, 아키텍처(x86_64, aarch64, arm, riscv), stripped 여부를 완벽 감지합니다.

### 2. 무제한 심볼 버전 네임스페이스
- 기존 구현체는 `GLIBC_`, `GLIBCXX_`, `CXXABI_` 3개의 네임스페이스만 하드코딩하여 다른 라이브러리(OpenSSL, Qt 등)의 버전을 누락했습니다.
- `lens-abi`는 `DT_VERDEF`와 `DT_VERNEED`를 동적으로 분석하여 **라이브러리 종류에 구애받지 않고 모든 심볼 버전 태그를 정확히 수집**합니다.

### 3. 압축 DWARF 지원 (`gimli` + `flate2`)
- 현대 리눅스 배포판(Ubuntu, Fedora, Arch)의 표준인 `SHF_COMPRESSED`(`.zdebug_*`) 디버그 섹션을 투명하게 압축 해제하여, 배포판 패키지의 타입 레이아웃을 누락 없이 검증합니다.

### 4. 3상태 호환성 판정 엔진 (`diff_reports`)
- **3-State Compatibility**:
  - `compatible`: 모든 헤더, 심볼, vtable, ABI 축이 안전함.
  - `incompatible`: 기존에 공개된 심볼/vtable 삭제, 머신 아키텍처 변경, 데이터 객체 크기 변경 등 런타임 크래시를 유발하는 브레이킹 체인지 감지.
  - `unknown`: 런타임 링커 검색 경로(`rpath`, `runpath`)나 종속성(`needed`) 변경 등 정적 분석만으로는 확정할 수 없는 경우 실패 폐쇄(Fail-Closed) 원칙에 따라 불확실성을 유지.

---

## 사용 예제

```rust
use lens_abi::{inspect_elf, diff_reports, InputStatus};

// 1. 바이너리 검사
let bytes = std::fs::read("libsample.so")?;
let report = inspect_elf("libsample.so", &bytes);

if report.status == InputStatus::Valid {
    println!("클래스: {}, 엔디언: {}", report.elf.class, report.elf.endian);
    println!("익스포트된 심볼 수: {}", report.abi.symbols.len());
}

// 2. 버전 간 ABI diff 수행
let report_old = inspect_elf("libsample_v1.so", &bytes_v1);
let report_new = inspect_elf("libsample_v2.so", &bytes_v2);
let diff = diff_reports(&report_old, &report_new);

println!("호환성 판정: {} (호환 여부: {})", diff.compatibility, diff.compatible);
for removed in &diff.symbols.removed {
    println!("삭제된 심볼 (브레이킹 변경): {}", removed);
}
```
