# Changelog

## Unreleased

- Fix venv execution, relative compilation roots and console return values.
- Add snapshot v3 origin/platform evidence, PEP standards evaluation, transitive
  extras, dependency paths, import overlap and CI policies.
- Bound compilation batches and protect report inputs from output aliases.

## [0.2.0](https://github.com/jihoon22-lee/toy-projects/compare/envlens/v0.1.2...envlens/v0.2.0) (2026-10-03)


### Features

* expand eight independent diagnostic tools and verified releases ([#120](https://github.com/jihoon22-lee/toy-projects/issues/120)) ([3a01bb9](https://github.com/jihoon22-lee/toy-projects/commit/3a01bb93c3fee936a335bb9445400b5f469d6a87))

## [0.1.2](https://github.com/jihoon22-lee/toy-projects/compare/envlens/v0.1.1...envlens/v0.1.2) (2026-10-03)


### Bug Fixes

* **envlens:** keep standing external requirements from failing diff and check ([02ed1e7](https://github.com/jihoon22-lee/toy-projects/commit/02ed1e709fea7b861be3135d4e238332bb3056ae))

## [0.1.1](https://github.com/jihoon22-lee/toy-projects/compare/envlens/v0.1.0...envlens/v0.1.1) (2026-10-03)


### Features

* **abilens:** diff the exported dynamic-symbol surface ([#98](https://github.com/jihoon22-lee/toy-projects/issues/98)) ([90b0055](https://github.com/jihoon22-lee/toy-projects/commit/90b00550c1fa637690590b5dec2bf438b341b365))
* **envlens:** add deterministic Python environment snapshots ([#50](https://github.com/jihoon22-lee/toy-projects/issues/50)) ([c307ac1](https://github.com/jihoon22-lee/toy-projects/commit/c307ac1ab01e12e4ac81a34623eb669da0e43641))
* **envlens:** check command for single-snapshot compatibility ([#112](https://github.com/jihoon22-lee/toy-projects/issues/112)) ([b51688c](https://github.com/jihoon22-lee/toy-projects/commit/b51688c8e47a526320b21f82c4c5845ff191cff2))
* **envlens:** measure wheel purity with ici instead of asserting it ([#68](https://github.com/jihoon22-lee/toy-projects/issues/68)) ([fc3342f](https://github.com/jihoon22-lee/toy-projects/commit/fc3342ffd4650cdf9436d186f63854ed005597ff))
* **envlens:** surface Requires-External metadata as unknown dependency evidence ([#115](https://github.com/jihoon22-lee/toy-projects/issues/115)) ([18cf9dc](https://github.com/jihoon22-lee/toy-projects/commit/18cf9dc1a0e5c258ff2bdcb6877e1f2130ba4b26))
* **tools:** make runtime, log, and storage evidence trustworthy ([#57](https://github.com/jihoon22-lee/toy-projects/issues/57)) ([6376bfc](https://github.com/jihoon22-lee/toy-projects/commit/6376bfc6c18e5dd74fc17387d4373b3688377d9b))


### Bug Fixes

* derive asserted versions from a single source of truth ([#93](https://github.com/jihoon22-lee/toy-projects/issues/93)) ([6c9517d](https://github.com/jihoon22-lee/toy-projects/commit/6c9517d3cd1179fd4f4f4bd53cae214dcbfb130f))


### Documentation

* **envlens:** record merged validation evidence ([#51](https://github.com/jihoon22-lee/toy-projects/issues/51)) ([371ebe4](https://github.com/jihoon22-lee/toy-projects/commit/371ebe499e628a6dbb786666b0943ddbdd369012))
* reset documentation around each product's own purpose ([#81](https://github.com/jihoon22-lee/toy-projects/issues/81)) ([e5de21a](https://github.com/jihoon22-lee/toy-projects/commit/e5de21a908a037a24fa98dc609e609c7519cad36))

## 0.1.0 (unreleased)

Baseline: native build, test, and release pipeline established for envlens.
The `envlens/v0.1.0` tag name is permanently unavailable after an earlier
immutable-release deletion, so the first publishable envlens release will be
0.1.1 or later.
