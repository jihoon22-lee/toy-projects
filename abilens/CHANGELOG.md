# Changelog

## Unreleased

- Add report/diff v2 with tri-state compatibility, rich symbol evidence, ordered
  loader paths, SONAME/interpreter/build ID, root-confined sysroot candidates and
  optional bounded libdw aggregate layout analysis.
- Fix unknown symbol policies, removed exports and ELF32 symbol offsets.
- Add CI failure policies and complete install layout.

## [0.3.0](https://github.com/jihoon22-lee/toy-projects/compare/abilens/v0.2.1...abilens/v0.3.0) (2026-10-03)


### Features

* **abilens:** diff the exported dynamic-symbol surface ([#98](https://github.com/jihoon22-lee/toy-projects/issues/98)) ([90b0055](https://github.com/jihoon22-lee/toy-projects/commit/90b00550c1fa637690590b5dec2bf438b341b365))
* **abilens:** qualify exported symbols with verdef versions ([#105](https://github.com/jihoon22-lee/toy-projects/issues/105)) ([7079fc0](https://github.com/jihoon22-lee/toy-projects/commit/7079fc01f6933872fe3ebd243e383bf73627e1d0))
* **abilens:** report exported vtables as their own diff axis ([#114](https://github.com/jihoon22-lee/toy-projects/issues/114)) ([d73b3a9](https://github.com/jihoon22-lee/toy-projects/commit/d73b3a941d22bd773fb9bc5c1f93e457a11a9190))
* **abilens:** symbol, rpath and stripped policy rules ([#109](https://github.com/jihoon22-lee/toy-projects/issues/109)) ([2389eae](https://github.com/jihoon22-lee/toy-projects/commit/2389eae1e31b79e326b110cb46171a17b422c429))
* expand eight independent diagnostic tools and verified releases ([#120](https://github.com/jihoon22-lee/toy-projects/issues/120)) ([3a01bb9](https://github.com/jihoon22-lee/toy-projects/commit/3a01bb93c3fee936a335bb9445400b5f469d6a87))
* **portfolio:** add ELF inspection and log investigation workflows ([#58](https://github.com/jihoon22-lee/toy-projects/issues/58)) ([413c75a](https://github.com/jihoon22-lee/toy-projects/commit/413c75a9fa13621fc78046a8e1baf4a9183ce869))


### Bug Fixes

* **abilens:** fail closed on unknown evidence and degrade bad version tables ([334d748](https://github.com/jihoon22-lee/toy-projects/commit/334d74894c021ce1c020b5f2649e01e8e0c8d97d))
* derive asserted versions from a single source of truth ([#93](https://github.com/jihoon22-lee/toy-projects/issues/93)) ([6c9517d](https://github.com/jihoon22-lee/toy-projects/commit/6c9517d3cd1179fd4f4f4bd53cae214dcbfb130f))
* remove DiskMap's swappable read-range params and pin AbiLens thresholds ([#64](https://github.com/jihoon22-lee/toy-projects/issues/64)) ([bd949c1](https://github.com/jihoon22-lee/toy-projects/commit/bd949c156df29fd6850604364a2126b0b2917df3))

## [0.2.1](https://github.com/jihoon22-lee/toy-projects/compare/abilens/v0.2.0...abilens/v0.2.1) (2026-10-03)


### Features

* **abilens:** diff the exported dynamic-symbol surface ([#98](https://github.com/jihoon22-lee/toy-projects/issues/98)) ([90b0055](https://github.com/jihoon22-lee/toy-projects/commit/90b00550c1fa637690590b5dec2bf438b341b365))
* **abilens:** qualify exported symbols with verdef versions ([#105](https://github.com/jihoon22-lee/toy-projects/issues/105)) ([7079fc0](https://github.com/jihoon22-lee/toy-projects/commit/7079fc01f6933872fe3ebd243e383bf73627e1d0))
* **abilens:** report exported vtables as their own diff axis ([#114](https://github.com/jihoon22-lee/toy-projects/issues/114)) ([d73b3a9](https://github.com/jihoon22-lee/toy-projects/commit/d73b3a941d22bd773fb9bc5c1f93e457a11a9190))
* **abilens:** symbol, rpath and stripped policy rules ([#109](https://github.com/jihoon22-lee/toy-projects/issues/109)) ([2389eae](https://github.com/jihoon22-lee/toy-projects/commit/2389eae1e31b79e326b110cb46171a17b422c429))
* expand eight independent diagnostic tools and verified releases ([#120](https://github.com/jihoon22-lee/toy-projects/issues/120)) ([3a01bb9](https://github.com/jihoon22-lee/toy-projects/commit/3a01bb93c3fee936a335bb9445400b5f469d6a87))
* **portfolio:** add ELF inspection and log investigation workflows ([#58](https://github.com/jihoon22-lee/toy-projects/issues/58)) ([413c75a](https://github.com/jihoon22-lee/toy-projects/commit/413c75a9fa13621fc78046a8e1baf4a9183ce869))


### Bug Fixes

* **abilens:** fail closed on unknown evidence and degrade bad version tables ([334d748](https://github.com/jihoon22-lee/toy-projects/commit/334d74894c021ce1c020b5f2649e01e8e0c8d97d))
* derive asserted versions from a single source of truth ([#93](https://github.com/jihoon22-lee/toy-projects/issues/93)) ([6c9517d](https://github.com/jihoon22-lee/toy-projects/commit/6c9517d3cd1179fd4f4f4bd53cae214dcbfb130f))
* remove DiskMap's swappable read-range params and pin AbiLens thresholds ([#64](https://github.com/jihoon22-lee/toy-projects/issues/64)) ([bd949c1](https://github.com/jihoon22-lee/toy-projects/commit/bd949c156df29fd6850604364a2126b0b2917df3))

## [0.2.0](https://github.com/jihoon22-lee/toy-projects/compare/abilens/v0.1.2...abilens/v0.2.0) (2026-10-03)


### Features

* expand eight independent diagnostic tools and verified releases ([#120](https://github.com/jihoon22-lee/toy-projects/issues/120)) ([3a01bb9](https://github.com/jihoon22-lee/toy-projects/commit/3a01bb93c3fee936a335bb9445400b5f469d6a87))

## [0.1.2](https://github.com/jihoon22-lee/toy-projects/compare/abilens/v0.1.1...abilens/v0.1.2) (2026-10-03)


### Bug Fixes

* **abilens:** fail closed on unknown evidence and degrade bad version tables ([334d748](https://github.com/jihoon22-lee/toy-projects/commit/334d74894c021ce1c020b5f2649e01e8e0c8d97d))

## [0.1.1](https://github.com/jihoon22-lee/toy-projects/compare/abilens/v0.1.0...abilens/v0.1.1) (2026-10-03)


### Features

* **abilens:** diff the exported dynamic-symbol surface ([#98](https://github.com/jihoon22-lee/toy-projects/issues/98)) ([90b0055](https://github.com/jihoon22-lee/toy-projects/commit/90b00550c1fa637690590b5dec2bf438b341b365))
* **abilens:** qualify exported symbols with verdef versions ([#105](https://github.com/jihoon22-lee/toy-projects/issues/105)) ([7079fc0](https://github.com/jihoon22-lee/toy-projects/commit/7079fc01f6933872fe3ebd243e383bf73627e1d0))
* **abilens:** report exported vtables as their own diff axis ([#114](https://github.com/jihoon22-lee/toy-projects/issues/114)) ([d73b3a9](https://github.com/jihoon22-lee/toy-projects/commit/d73b3a941d22bd773fb9bc5c1f93e457a11a9190))
* **abilens:** symbol, rpath and stripped policy rules ([#109](https://github.com/jihoon22-lee/toy-projects/issues/109)) ([2389eae](https://github.com/jihoon22-lee/toy-projects/commit/2389eae1e31b79e326b110cb46171a17b422c429))
* **portfolio:** add ELF inspection and log investigation workflows ([#58](https://github.com/jihoon22-lee/toy-projects/issues/58)) ([413c75a](https://github.com/jihoon22-lee/toy-projects/commit/413c75a9fa13621fc78046a8e1baf4a9183ce869))


### Bug Fixes

* derive asserted versions from a single source of truth ([#93](https://github.com/jihoon22-lee/toy-projects/issues/93)) ([6c9517d](https://github.com/jihoon22-lee/toy-projects/commit/6c9517d3cd1179fd4f4f4bd53cae214dcbfb130f))
* remove DiskMap's swappable read-range params and pin AbiLens thresholds ([#64](https://github.com/jihoon22-lee/toy-projects/issues/64)) ([bd949c1](https://github.com/jihoon22-lee/toy-projects/commit/bd949c156df29fd6850604364a2126b0b2917df3))

## [0.1.0](https://github.com/jihoon22-lee/toy-projects/releases/tag/abilens/v0.1.0) (2026-10-03)

Baseline: native build, test, and release pipeline established for abilens.
