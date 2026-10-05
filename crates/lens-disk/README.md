# lens-disk

고성능 파일시스템 진단, 공간 분석 및 안전한 스토리지 정리 엔진.

기존 C++20 기반 `diskmap`(27,811 라인)을 완전히 재설계 및 대체하여, **90% 이상의 메모리 절감과 초고속 파일 순회, FreeDesktop Trash 명세 준수, 무손실 중복 파일 탐지**를 제공합니다.

---

## 핵심 아키텍처 및 개선 사항

### 1. 컴팩트 아레나 트리 (`ArenaTree`)
- **메모리 혁신**: 기존 C++ 구현체는 노드당 432바이트를 소비하고 전체 경로 문자열을 모든 리프에 중복 소유하여 100만 파일에서 1GB 이상의 RAM을 소모했습니다.
- `lens-disk`는 `u32` 기반의 연속 메모리 아레나 트리(`ArenaTree`)를 도입하여 **노드당 크기를 36바이트로 90% 이상 압축**했습니다. 100만 파일 스캔 시에도 40MB 내외의 극도로 적은 메모리만 소비합니다.
- 포스트 오더(Post-order) 크기 누적(`aggregate_sizes`)을 통해 디렉토리 서브트리 용량을 포화 연산(saturating add)으로 안전하게 계산합니다.

### 2. 안전한 디렉토리 스캐너 (`DiskScanner`)
- 심볼릭 링크 루프 및 파일시스템 마운트 경계(`one_file_system`)를 `FileIdentity`(`dev`, `ino`)로 완벽 감지.
- 최대 깊이(`max_depth`), 최대 항목 수(`max_entries`), 패턴 제외(`exclude_patterns`)를 지원하여 악의적인 디렉토리 구조로부터 시스템을 보호.

### 3. 정밀한 중복 파일 탐지기 (`DuplicateFinder`)
- **다단계 필터링 파이프라인**:
  1. 1차: 논리적 파일 크기 버킷팅.
  2. 2차: 후보 파일의 첫 4KB 부분 해시 계산.
  3. 3차: 일치하는 후보에 대해서만 전체 파일 SHA-256 계산.
  4. 4차: **하드링크 감지(Hardlink Deduplication)** — 두 파일이 동일한 Inode(`dev_t`, `ino_t`)를 공유하는 경우 물리 블록이 중복되지 않으므로, 회수 가능 용량(Reclaimable Bytes)을 과장하지 않고 정확히 계산합니다.

### 4. FreeDesktop 휴지통 관리자 (`TrashManager`)
- XDG Trash 명세(v1.0)를 완벽히 준수:
  - 파일 본체는 `Trash/files/`로 안전 이동.
  - 원본 경로 및 ISO-8601 삭제 시각을 담은 `.trashinfo` 파일을 `Trash/info/`에 원자적 생성.
  - `TrashReceipt` 영수증을 발급하여 추후 완벽한 원본 경로 복원(`restore`) 지원.

### 5. `diskmap.snapshot/v2` 스키마 100% 호환
- 기존 `diskmap` GUI/CLI가 생성하고 읽는 JSON 스키마를 완벽히 지원하며, 두 스냅샷 간의 변경점(추가, 삭제, 비대, 축소, 이동)을 계산하는 `SnapshotDiff` 제공.

---

## 사용 예제

```rust
use lens_disk::{DiskScanner, ScanOptions, DuplicateFinder, TrashManager, SnapshotV2};

// 1. 디렉토리 고속 스캔
let scanner = DiskScanner::new(ScanOptions::default());
let result = scanner.scan("/path/to/analyze")?;
println!("스캔 완료: 총 {}개 항목, 용량: {} bytes", result.scanned_entries, result.tree.nodes[result.root_id as usize].size);

// 2. 중복 파일 탐지 (최소 1MB 이상)
let finder = DuplicateFinder::new(1024 * 1024);
let duplicates = finder.find_in_tree(&result.tree, std::path::Path::new("/path/to/analyze"))?;
for group in duplicates {
    println!("중복 발견: SHA256={}, 크기={}B, 회수 가능={}B", group.sha256, group.size, group.reclaimable_bytes);
}

// 3. 스냅샷 v2 JSON 직렬화
let snapshot = SnapshotV2::from_tree(&result.tree, result.root_id, result.complete, result.truncated);
let json = serde_json::to_string_pretty(&snapshot)?;
```
