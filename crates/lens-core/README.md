# lens-core

공통 시스템 진단 및 포렌식 플랫폼(`Lens Platform`)의 핵심 공통 기반 라이브러리.

모든 도메인 진단 엔진(`lens-disk`, `lens-abi`, `lens-log`, `lens-trace`, `lens-sys`, `lens-env`, `lens-test`, `lens-build`)이 공유하는 필수 불변식(Invariants)과 기본 데이터 구조를 제공합니다.

---

## 제공 모듈 및 핵심 기능

### 1. `identity` (파일 식별 및 TOCTOU 방어)
- **`FileIdentity`**: POSIX `st_dev`, `st_ino`, `st_mode`, `st_size`, `st_mtim_ns`, `st_ctim_ns`를 추적하여 하드링크 공유 및 변경 여부를 검증합니다.
- **`SafeInput`**: 파일을 열어 초기 메타데이터를 저장하고, 파일 검사 완료 후 `verify_unchanged()`를 통해 경쟁 상태(Time-of-Check to Time-of-Use)를 원천 차단합니다.
- **`mmap()`**: 안전성이 검증된 정규 파일에 대해 Zero-Copy 메모리 매핑(`memmap2::Mmap`)을 제공합니다.

### 2. `hash` (결정론적 암호화 해싱)
- **`digest_bytes` / `digest_file`**: 표준 SHA-256을 64자리 소문자 16진수 해시 문자열로 계산합니다.
- **`IncrementalHasher`**: 대용량 스트림 및 청크 버퍼를 위한 점진적 해시 빌더.

### 3. `diff` (3상태 보수적 호환성 판정)
- **`Compatibility`**:
  - `Compatible`: 명백하게 안전하고 하위 호환성이 보장됨.
  - `Incompatible`: 심볼 삭제, 아키텍처 불일치 등 명백한 호환성 파괴.
  - `Uncertain`: 메타데이터 누락, 불완전 스캔, 증거 부족 시 실패 폐쇄(Fail-Closed) 원칙에 따라 불확실성을 유지.
- **`SetDiff<T>`**: 베이스라인(Left)과 후보(Right) 간의 정렬된 집합 차분(`added`, `removed`)을 제공.

### 4. `evidence` (출처 보존 및 자원 제한)
- **`Evidence`**: 관측된 위치(파일 오프셋, 줄 번호, 길이)를 엄밀히 기록.
- **`Source`**: 분석 대상 파일의 해시, 크기, 스캔 범위, 수정 시각 메타데이터를 영구 보존.
- **`BoundedCollector<T>`**: 악의적이거나 비정상적인 입력으로 인한 메모리 고갈(OOM DoS)을 방지하기 위해 최대 수집 상한을 엄격히 제한하고 절삭(`truncated`) 플래그를 유지.

### 5. `json` (결정론적 직렬화)
- **`to_deterministic_string` / `to_deterministic_pretty`**: JSON 객체 내의 모든 키를 알파벳 순으로 재귀 정렬하여 바이트 수준의 재현성과 Git diff 안정성을 보장.

---

## 사용 예제

```rust
use lens_core::{SafeInput, Compatibility, SetDiff, to_deterministic_pretty};

// 1. 안전한 파일 검사 및 매핑
let safe_file = SafeInput::open("sample.bin")?;
let mmap = safe_file.mmap()?;

// 2. 3상태 호환성 결합
let sym_compat = Compatibility::Compatible;
let abi_compat = Compatibility::Uncertain;
let overall = sym_compat.combine(abi_compat);
assert_eq!(overall, Compatibility::Uncertain);

// 3. 결정론적 JSON 출력
let diff = SetDiff::compute(vec!["A", "B"], vec!["B", "C"]);
let json = to_deterministic_pretty(&diff)?;
```
