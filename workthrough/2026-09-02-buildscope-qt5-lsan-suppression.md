# BuildScope Qt5 deep LeakSanitizer boundary

## Overview

The earlier `MainWindowTest::cleanup()` change removes the deferred test-owned Qt5 allocations,
but the hosted Qt 5.15.18 offscreen run still retains 688 bytes in 10 allocations rooted in
`libqoffscreen.so`/fontconfig platform-global state at process shutdown. That is outside
BuildScope's ownership and is not present in the Qt6 leg.

## Changes

- Added `ci/fixtures/outside/qt5-offscreen-platform.supp` with the single module-scoped
  `leak:libqoffscreen.so` rule.
- Applied `LSAN_OPTIONS=detect_leaks=1:suppressions=...` only inside the Qt5 branch of the
  `buildscope-ici-deep` verification step. The Qt6 branch is unchanged.
- Added `ci/fixtures/outside/project_leak.cpp`, intentionally outside `buildscope/ici.toml`
  `source_dirs`. The Qt5 job compiles and runs it with the exact same LSAN options and requires a
  nonzero exit plus a `LeakSanitizer: detected memory leaks` diagnostic.
- Added a temporary, source-pinned cross-repository candidate path for the Qt5/Qt6 deep jobs. It
  checks out exact ici commit `040e61e64df20d64923df80a6c7ea29993e5c3ac`, verifies that identity,
  builds its pyz reproducibly, and uses it only for deep BuildScope verification. Ordinary portfolio
  jobs remain on the checksummed public v0.10.1 release, and final integration returns to a released
  pin.

This keeps leak detection enabled and demonstrates that the narrow plugin suppression cannot hide a
project-owned leak.

## Local evidence

With the current Qt 5.15.18 toolchain, the instrumented `test_main_window` reports the known
688-byte plugin leak without the suppression and exits 1; with the suppression it exits 0 while all
14 QtTest lifecycle entries pass. The control binary exits 1 and reports its `project_leak.cpp`
allocation under the same `detect_leaks=1:suppressions=...` options.
