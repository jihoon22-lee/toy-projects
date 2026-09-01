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
  checks out exact ici commit `e5096e10e9ce0069d5cea951dbdb28f87ee60e14`, verifies that identity,
  builds its pyz reproducibly, and uses it only for deep BuildScope verification. Ordinary portfolio
  jobs remain on the checksummed public v0.10.1 release, and final integration returns to a released
  pin.

This keeps leak detection enabled and demonstrates that the narrow plugin suppression cannot hide a
project-owned leak.

The first source-candidate run (`33531285208`, commit
`040e61e64df20d64923df80a6c7ea29993e5c3ac`) kept the report publisher and all ordinary ici jobs
green but failed both deep legs. The runner compiled with GCC 13 while clazy selected the separately
installed GCC 14 libstdc++ headers. The refreshed candidate above probes the exact selected GCC and
projects its ordered C++-only include roots into clazy/clang-tidy before rerunning this matrix.

## Local evidence

With the current Qt 5.15.18 toolchain, the instrumented `test_main_window` reports the known
688-byte plugin leak without the suppression and exits 1; with the suppression it exits 0 while all
14 QtTest lifecycle entries pass. The control binary exits 1 and reports its `project_leak.cpp`
allocation under the same `detect_leaks=1:suppressions=...` options.
