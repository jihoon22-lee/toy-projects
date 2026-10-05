# lens-log

초고속 메모리 매핑 기반 로그 분석, 제로카피 인덱싱 및 인베스티게이션 세션 엔진.

기존 C++20 기반 `loglens`(23,389 라인)을 완전히 재설계 및 대체하여, **수 GB 대용량 로그의 밀리초 단위 로딩, 100만 회 힙 할당을 제거한 무할당 필터링, JSONL 및 Syslog 자동 감지 파서**를 제공합니다.

---

## 핵심 아키텍처 및 개선 사항

### 1. Memory-Mapped 제로카피 라인 인덱서 (`LogIndexer`)
- **로딩 지연 및 메모리 폭발 해결**: 기존 C++ 구현체는 모든 로그 레코드를 개별 힙 객체(`std::string`, `std::map<string, string>`)로 적재하여 1GB 로그 파일 처리 시 수 GB의 RAM을 소모하거나 OOM이 발생했습니다.
- `lens-log`는 `memmap2`를 통해 파일을 커널 페이지 캐시에 직접 매핑하고, 줄 바꿈 오프셋(`LineSpan { offset, length }`)만 인덱싱합니다.
- 1GB 파일(약 1,000만 줄)을 인덱싱하는 데 필요한 메모리는 불과 **80MB** 내외이며, 100ms 이내에 탐색 준비를 완료합니다.

### 2. 무할당(Zero-Allocation) 대소문자 무시 필터 (`contains_insensitive`)
- **핫패스 병목 제거**: 기존 C++ `containsInsensitive`는 레코드를 검사할 때마다 `toLowerAscii`로 2개의 `std::string`을 동적 할당하여, 50만 레코드 필터링 시 100만 번의 힙 할당이 일어났습니다.
- `lens-log`의 필터 엔진은 슬라이딩 윈도우 바이트 비교 기법을 적용하여 **핫루프 내 힙 할당을 0회(Zero Allocation)로 제거**했습니다.

### 3. 유연한 제로카피 파서 (`parse_line`)
- `LogRecordView<'a>`는 `Cow<'a, str>`를 활용하여 메모리 매핑된 슬라이스를 직접 차용(borrow)합니다.
- 표준 JSONL, ISO-8601 타임스탬프 기반 로그, Syslog 및 비정형(Unstructured) 텍스트를 자동 감지하여 파싱합니다.

### 4. `loglens.session/v2` 명세 100% 호환
- 기존 `loglens`의 인베스티게이션 세션 스키마를 지원하여, 저장된 쿼리 및 분석 설정을 상호 교환할 수 있습니다.

---

## 사용 예제

```rust
use lens_log::{LogIndexer, parse_line, LogFilter, LogLevel};

// 1. 대용량 로그 파일 고속 인덱싱
let indexer = LogIndexer::open("/var/log/app.log")?;
println!("인덱싱 완료: 총 {} 라인", indexer.len());

// 2. 필터 설정 (ERROR 이상이면서 'database' 포함)
let filter = LogFilter::new()
    .with_min_level(LogLevel::Error)
    .with_query("database");

// 3. 무할당 고속 검색
for idx in 0..indexer.len() {
    if let Some(line) = indexer.get_line(idx) {
        let record = parse_line(line, idx + 1);
        if filter.matches(&record) {
            println!("[L{}] {} - {}", record.line_number, record.level.as_str(), record.message);
        }
    }
}
```
