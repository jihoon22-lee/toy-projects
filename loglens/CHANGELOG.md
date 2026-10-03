# Changelog

## [0.1.2](https://github.com/jihoon22-lee/toy-projects/compare/loglens/v0.1.1...loglens/v0.1.2) (2026-10-03)


### Bug Fixes

* **loglens:** keep GUI sessions faithful to the saved filter and the open log ([660c99b](https://github.com/jihoon22-lee/toy-projects/commit/660c99b4d704034d3665fd69ad5e739713e2477a))
* **loglens:** make format plugins safe on long lines and persist them in sessions ([2521146](https://github.com/jihoon22-lee/toy-projects/commit/2521146c95442fe1b7afa5be1140aa35b672be52))

## [0.1.1](https://github.com/jihoon22-lee/toy-projects/compare/loglens/v0.1.0...loglens/v0.1.1) (2026-10-03)


### Features

* **cli:** let the diskmap and loglens binaries state their version ([#71](https://github.com/jihoon22-lee/toy-projects/issues/71)) ([a31ec70](https://github.com/jihoon22-lee/toy-projects/commit/a31ec70c708965768488d10adbff37404609b3c3))
* loglens on CMake, diskmap on qmake, with Qt tests that pass ([#10](https://github.com/jihoon22-lee/toy-projects/issues/10)) ([3919ba4](https://github.com/jihoon22-lee/toy-projects/commit/3919ba430c166961330d458fb7b4cf8c61847cf0))
* **loglens:** add bounded filter diagnostics and safe query errors ([#44](https://github.com/jihoon22-lee/toy-projects/issues/44)) ([f926233](https://github.com/jihoon22-lee/toy-projects/commit/f92623334e051a28ede808eeeb31ea515ce88ac3))
* **loglens:** add measured large-file benchmark gate ([#26](https://github.com/jihoon22-lee/toy-projects/issues/26)) ([c45176c](https://github.com/jihoon22-lee/toy-projects/commit/c45176ce25f2efd66ea9b0ed9b48690e34cc8679))
* **loglens:** add reliable file identity ([#20](https://github.com/jihoon22-lee/toy-projects/issues/20)) ([8f06b74](https://github.com/jihoon22-lee/toy-projects/commit/8f06b74b7987fd3599bdbe6093ebcc1d274d40a5))
* **loglens:** bound record storage and source polling ([#24](https://github.com/jihoon22-lee/toy-projects/issues/24)) ([71bc8e3](https://github.com/jihoon22-lee/toy-projects/commit/71bc8e3d1c8a36399809471c4b48e02656394a07))
* **loglens:** declarative format plugins for custom line shapes ([#106](https://github.com/jihoon22-lee/toy-projects/issues/106)) ([f2d1956](https://github.com/jihoon22-lee/toy-projects/commit/f2d1956382c7d0a7d11bf1b0345aee56f4d6b964))
* **loglens:** load large logs off the UI thread ([#25](https://github.com/jihoon22-lee/toy-projects/issues/25)) ([69db159](https://github.com/jihoon22-lee/toy-projects/commit/69db15966ca0c032026aeb7b742c4eed6335910d))
* **loglens:** log viewer core, CLI and test suite ([#2](https://github.com/jihoon22-lee/toy-projects/issues/2)) ([06843dc](https://github.com/jihoon22-lee/toy-projects/commit/06843dc2206601fede7589d980fe2cfe883fba0c))
* **loglens:** open and save sessions from the GUI ([#113](https://github.com/jihoon22-lee/toy-projects/issues/113)) ([4a18622](https://github.com/jihoon22-lee/toy-projects/commit/4a1862257b48c83788f7e4597de904d3f1ae8941))
* **loglens:** Qt6 log viewer GUI ([#4](https://github.com/jihoon22-lee/toy-projects/issues/4)) ([1da46ba](https://github.com/jihoon22-lee/toy-projects/commit/1da46baf2ee378fbb354b7e1c6e1eef6f1251eed))
* **loglens:** recover follow after transient source errors ([#22](https://github.com/jihoon22-lee/toy-projects/issues/22)) ([7d0d992](https://github.com/jihoon22-lee/toy-projects/commit/7d0d992ae7f5563382b954a0772b165a14bf9025))
* **loglens:** save and reload investigation sessions ([#100](https://github.com/jihoon22-lee/toy-projects/issues/100)) ([a66f42f](https://github.com/jihoon22-lee/toy-projects/commit/a66f42fcc20d2156fb8ffbffb380085a67354be7))
* **portfolio:** add ELF inspection and log investigation workflows ([#58](https://github.com/jihoon22-lee/toy-projects/issues/58)) ([413c75a](https://github.com/jihoon22-lee/toy-projects/commit/413c75a9fa13621fc78046a8e1baf4a9183ce869))
* **tools:** make runtime, log, and storage evidence trustworthy ([#57](https://github.com/jihoon22-lee/toy-projects/issues/57)) ([6376bfc](https://github.com/jihoon22-lee/toy-projects/commit/6376bfc6c18e5dd74fc17387d4373b3688377d9b))


### Bug Fixes

* **loglens:** preserve filtered append indexes ([#19](https://github.com/jihoon22-lee/toy-projects/issues/19)) ([bd1b6cd](https://github.com/jihoon22-lee/toy-projects/commit/bd1b6cdec89c45e0f4283fae84630002271b933e))
* **loglens:** preserve parser state across file polls ([#14](https://github.com/jihoon22-lee/toy-projects/issues/14)) ([87927b4](https://github.com/jihoon22-lee/toy-projects/commit/87927b4135536d80e5b255816912af7f7c0e5e9b))

## [0.1.0](https://github.com/jihoon22-lee/toy-projects/releases/tag/loglens/v0.1.0) (2026-10-03)

Baseline: native build, test, and release pipeline established for loglens.
