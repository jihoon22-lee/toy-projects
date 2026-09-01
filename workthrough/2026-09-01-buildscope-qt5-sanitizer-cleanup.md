# BuildScope Qt5 sanitizer cleanup

## Overview

BuildScope B5 PR #38의 첫 deep matrix에서 Qt6 sanitizer는 통과했지만 Qt5의
`test_main_window`만 CTest return code 8로 실패했다. report artifact에는 12개 test function과
QtTest lifecycle을 합친 14개 결과 entry의 원시 출력이 없어서 동일 Qt5.15.18, offscreen,
ASan/UBSan 환경으로 직접 재현했다.

실패는 assertion, crash, undefined behavior가 아니었다. 12개 test function과 QtTest
lifecycle 결과 entry 14개가 모두 통과한 뒤 Qt5
offscreen/fontconfig와 QLineEdit animation/deferred object가 process 종료 때 남아 LSan이
1,288 bytes / 18 allocations를 보고한 것이었다.

## Changes Made

- `MainWindowTest`에 QtTest가 각 case 뒤 자동 호출하는 `cleanup()` slot을 추가했다.
- cleanup은 queued `DeferredDelete` event를 보내고 pending GUI event를 처리한 뒤 deferred-delete
  queue를 한 번 더 비운다.
- leak detection을 끄거나 suppression을 추가하지 않았다. ASan/UBSan/LSan 정책과 모든 test
  assertion은 그대로 유지된다.

## Verification Results

수정 전 instrumented binary:

```text
Totals: 14 passed, 0 failed, 0 skipped
ERROR: LeakSanitizer: detected memory leaks
SUMMARY: AddressSanitizer: 1288 byte(s) leaked in 18 allocation(s)
exit 1
```

수정 후 같은 Qt5.15.18 toolchain/configuration으로 다시 빌드한 offscreen binary와 원래 LSan
정책:

```text
QT_QPA_PLATFORM=offscreen \
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
/tmp/buildscope-qt5-check.DI35vX/test_main_window

Totals: 14 passed, 0 failed, 0 skipped
exit 0
```

전체 sanitizer CTest tree:

```text
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
ctest --test-dir /tmp/buildscope-qt5-check.DI35vX --output-on-failure

100% tests passed, 0 tests failed out of 9
```

## Design Notes

- Qt5 전체나 GUI test의 leak detection을 끄면 product-owned leak도 숨길 수 있으므로 허용하지
  않았다. 실제 object lifecycle을 완료해 원인을 제거했다.
- cleanup은 test-only lifecycle 계약이다. production event handling이나 widget ownership을
  변경하지 않는다.
- Qt6에는 실패가 없었지만 동일 cleanup contract를 컴파일·실행해 두 major의 test lifecycle이
  갈라지지 않게 한다.

## Next Steps

- [ ] 공개 ici v0.10.2를 checksum pin한 PR rerun에서 Qt5/Qt6 sanitizer와 clazy exact evidence를 확인한다.
- [ ] PR #38 전체 Merge Gate와 sticky comment/three Pages를 감사한 뒤에만 병합한다.
