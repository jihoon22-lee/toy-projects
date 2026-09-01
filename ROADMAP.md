# ROADMAP

이 저장소의 프로젝트들을 **어느 방향으로, 왜 그 순서로** 키울지 적는다.
발견한 ici 결함은 [ICI-GAPS.md](ICI-GAPS.md) 에, 프로젝트 소개와 구조 규칙은
[README.md](README.md) 에 있다.

> **확장된 실행 계획 (2026-08-31 기준):** 기존 두 앱의 제품 완성, 신규 `buildscope`·`envlens`·
> `abilens`, `quality-zoo`와 ici 교차 개발 순서는
> [제품 포트폴리오 마스터 계획](docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md)이
> 기준이다. 이 계획은 [PR #12](https://github.com/jihoon22-lee/toy-projects/pull/12)로
> `main`에 병합됐다. 아래의 배경과 문제 발견 이력은 마스터 계획을 보충하는 역사적 기록으로
> 유지한다.

## 마스터 계획 stream 연결

세부 기능의 우선순위와 완료 조건은 아래 링크의 마스터 계획을 기준으로 한다. 이 문서의
과거 단계 설명과 마스터 계획의 현재 stream이 충돌하면 마스터 계획이 우선한다.

| Stream | 역할 | 마스터 계획 |
|---|---|---|
| T0 | 현재 Qt 셸 보정, parser state와 Qt5/Qt6 안전망 | [T0](docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md) |
| L | loglens incident explorer | [L stream](docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md) |
| D | diskmap storage workbench | [D stream](docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md) |
| B | buildscope hybrid build explorer | [B stream](docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md) |
| E | envlens Python environment explorer | [E stream](docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md) |
| A | abilens Make/ELF/ABI explorer | [A stream](docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md) |
| Q | quality-zoo expected-finding corpus | [Q stream](docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md) |

## 릴리스 버전 규율

포트폴리오의 단계와 제품의 버전은 같은 축이 아니다. `loglens`, `diskmap`, `buildscope` 등
각 toy 제품은 독립적으로 버전을 올린다.

- 포트폴리오 방향 전환, B-stage 진행, ici pin, CI/runner-only 변경은 toy 버전을 자동으로 bump하지 않는다.
- `patch`는 이미 공개된 stable 제품의 defect/security/compatibility regression 수정에만 사용한다.
- `minor`는 응집된 사용자 기능이 실제로 쓸 수 있는 제품 checkpoint가 된 경우에만 사용한다.
  native tests, released-ici verification, PR 및 exact-main CI/Pages, docs/limitations, 재현 가능한
  release assets를 모두 통과·기록해야 한다.
- candidate, pre-release, unreleased는 stable로 세지 않는다.
- BuildScope `0.5.0`은 B3/B4/B5를 함께 묶은 첫 usable release boundary로 유지한다. 그 이후 작업은
  이에 상응하는 checkpoint가 생길 때까지 `Unreleased`에 누적한다.

PR 제목과 요약은 plan code가 아니라 제품/기술 결과를 설명해야 한다. `T0`, `B1`, `D2` 같은 plan
code는 본문이나 label의 보조 메타데이터로만 쓰며, 제목·요약의 유일하거나 주된 식별자로 삼지 않는다.
이미 남아 있는 historical PR 제목과 문서는 증거이므로 이름을 바꾸지 않는다.

## 완료: BuildScope B0 hybrid skeleton (historical baseline, 2026-09-01)

BuildScope는 `compile_commands.json`을 shell 실행 없이 bounded read해 deterministic
`buildscope.snapshot/v1` JSON으로 내보내는 Python 3.10 backend와, 그 계약을 검증해 표시하는
C++20/Qt CLI·GUI를 첫 slice로 구현했다. CMake는 `AUTOMOC`·`AUTOUIC`·`AUTORCC`와 compile DB
export를 모두 실제로 사용한다.

> **B0 당시의 과거 pre-hardening local evidence (현재 B1 결과가 아님; superseded):** Qt 5.15.18과 Qt 6.10.2에서
> 각각 4/4 CTest가 통과했고, 공개 ici v0.7.1 asset의 당시 cold isolated 기록은 suite WARN,
> `12 PASS / 1 WARN / 0 FAIL / 0 ERROR / 0 SKIP`, `9/9` tests, TEM 5.00,
> line/function/branch `96.3% / 100.0% / 86.8%`, complexity 14 PASS, compile DB `4/4`
> production units·13 configurations, 총 63.37초였다. 이 수치는 hardening 이전의 역사 기록으로
> 현재 최종 결과가 아니며, BuildScope PR/remote CI/sticky report/Pages 완료를 의미하지 않는다.

공개 ici v0.7.1 release asset의 checksum과 cold verification 최종 수치는 아래 B1 evidence에
기록한다. BuildScope PR #31의 remote integration evidence도 완료됐다.

## 완료: BuildScope B1 compile database normalization (implementation + PR #31 remote evidence, 2026-09-01)

`feat/buildscope-compile-db`의 Python 3.10 core가 `buildscope.snapshot/v2`와 BuildScope `0.2.0`
metadata를 구현했다. Python 입력 compile DB는 64 MiB/100,000-entry로 제한되고, serialized
snapshot과 native reader 입력은 별도로 256 MiB로 제한된다. B0 raw
`arguments`/`command`/`directory`/`file`/`output`는 보존하면서, `arguments`를 우선해 bounded
POSIX/Windows tokenization을 수행하고 compiler, language, standard, ordered defines/includes,
sysroot, target hint, configuration identity, duplicate/missing/stale state, diagnostics를
deterministic하게 내보낸다. shell·환경·glob·command substitution과 response-file expansion은
하지 않는다.

`--project-root`를 기준으로 project/vendor/system scope를 분류하며, native host에서만
exists/mtime을 확인하고 foreign-platform path 상태는 unknown으로 둔다. v2는 v1 raw keys를
유지하므로 additive-field를 허용하는 v1 consumer가 raw view를 계속 사용할 수 있다. CLI는
`--schema-version v1|v2`를 명시적으로 지원하며 v1은 raw compatibility projection이다.
metadata/output option scan은 `--`에서 중단되고, POSIX `-o`는 separated form만, Windows
`/Fo`는 separated/joined form을 지원한다. drive/UNC/backslash compiler path도 Windows로
판정하므로 `C:\\MinGW\\bin\\g++.exe` 같은 GCC 경로를 놓치지 않는다. MSVC option matching은
case-sensitive하며 `/Fo`와 `/Fo:`의 separated/joined form만 인식해 유사 switch false positive를
막는다.

foreign Windows `project_root`는 host filesystem을 probe하지 않고 lexical Windows form을
보존해 scope를 분류하며, dedicated scope test가 이 경계를 검증한다.

입력은 final-name `lstat`와 지원 플랫폼의 no-follow regular-file descriptor를 함께 확인하고
device/inode/type, size, mtime, ctime의 read 전후 identity를 비교하며 final symlink를 거부한다. `--output`은 DB 자체와
self/hardlink/symlink alias를 거부한다. POSIX에서는 no-follow parent directory fd에 anchored한
exclusive mode-0600 temp 생성, flush/fsync, fd-relative rename으로 atomic race 경계를 제공한다.
그 primitive를 쓸 수 없는 환경의 portable fallback은 resolved real parent를 pin한 뒤 그 parent에서
temp/cleanup/replace와 parent identity/alias 검사를 수행한다. 생성한 temp는 교체 전에 `lstat`로
생성 당시 identity와 regular-file을 재검증하고 symlink resolve를 하지 않는다. POSIX dir-fd
경로와 같은 atomic race guarantee를 주장하지 않는다.

중복은 source+configuration 범위이고 `source_configuration_count`는 source별 unique
configuration 수다. configuration digest는 normalized source와 짝지은 동일 source의 recorded
invocation identity일 뿐 relocation-stable semantic diff가 아니다. semantic configuration
comparison은 B4 소유다. Source aggregation key는 `command_style`+normalized path로 Python과
C++ native reader가 일치한다. native contract reader에서 “strict”는 legacy v1 core validation과 v2
bounded/core/cross-entry validation을 뜻한다. v1은 exactly-one invocation과 legacy empty argv
compatibility 및 extension-key tolerance를 유지하고, v2는 duplicate JSON key rejection,
required/unknown fields, field/item bounds, enum/core shapes, invocation-source/argv, include
order, `entry_index`/duplicate/source-configuration-count cross-entry consistency를 검증한다.
command-only normalized argv를 raw command와 재토큰화해 full semantic attestation하지는 않는다.
native reader는 snapshot final symlink도 읽기 전에 거부한다. 공개
`buildscope-snapshot-v1.schema.json` 및 `buildscope-snapshot-v2.schema.json`은 source tree와
pure wheel에 함께 패키징된다. v2 `producer.version`의 schema `maxLength` 1 MiB와 native
reader의 문자열 bound도 서로 일치한다.
strict 외부 v1 consumer는 v1 raw document가 필요하다. B2는 이 계약을 받아 normalized C++
model/UI로 전환하는 범위다. B1 구현 범위는 완료로 표시한다. B3 include explanation은 구현과
원격 증거까지 완료되어 당시 `main`의 `0.4.0` candidate로 기록됐다. 별도 `0.4.0` stable
release 대신 이후 B5 release-boundary 작업을 `0.5.0`에 포함했으며, public release readiness와
남은 tag/release audit은 아래에 기록한다. **B1 checkpoint transition (historical):** 당시에는
B4 configuration diff가 다음 toy-project stage였고, ici I3 target-by-target 외부 대조도 완료됐다.

**B1 final local/public evidence (2026-09-01):** public
`ici v0.7.1` cold verification은 표준 `sha256sum --check ici.pyz.sha256`가 통과했고 suite
`WARN`을 기록했다. `13 engines = 11 PASS / 2 WARN / 0 FAIL / 0 ERROR / 0 SKIP`, total
`71.09s` (raw `71.08794903755188s`), tests `45/45`, line/function/branch
`95.2% / 100.0% / 84.3%`, TEM `5.00`이었다.
`compile_db`는 `7/7` production units·`16` configurations·`0` failures/warnings, complexity는
max `13`/`140` functions·`0` issues, exception은 `PASS`·`0`이었다. Duplication은 `WARN`
`8.8%` (raw `8.77914951989026`)/`25` groups/`56` findings였고, type WARN은 unsupported
analysis인 `7` C++ sources뿐이며
external dependencies는 `0`이다. HTML은 `489,978` bytes, SHA-256
`538bdde8fae8cc769d212799e80ffeae1e39069662b214b19efb3d35a66f3257`, title은
`ici Verification Report — buildscope`였다. Line inventory는 `2,798` total, `2,453` code,
`345` blank lines across `19` files다. 현재 source line counts는 `contract.cpp` 111,
`contract_json_guard.cpp` 125, `contract_parser.cpp` 382, `contract_parser_v2.cpp` 333이고,
Python tests는 `41` (CTest aggregate `45`)이다. Local cold HTML은 `489,978` bytes,
SHA-256 `538bdde8fae8cc769d212799e80ffeae1e39069662b214b19efb3d35a66f3257`이며 hosted HTML과
별개다.

**B1 PR #31 remote integration evidence:** [PR #31](https://github.com/jihoon22-lee/toy-projects/pull/31)의
initial implementation/docs head는 `1ff08fe5d2accddc0e9107113eb83dd86bd6d50a`이고,
[workflow run 33439733990](https://github.com/jihoon22-lee/toy-projects/actions/runs/33439733990)의
동적 matrix 15 checks가 모두 SUCCESS였다: 3 ici verify, Qt5/Qt6 GUI 6, manifest, benchmark
smoke 3, `Publish Reports & Sticky Comment`, `Merge Gate`. [Sticky comment](https://github.com/jihoon22-lee/toy-projects/pull/31#issuecomment-5484640868)는
marker 1개와 정확히 3개 project link를 포함하고 BuildScope `11 PASS / 2 WARN`, TEM `5.00`,
`45/45`, `7/7`, `16` configurations, complexity max `13`/`140`을 기록했다.

독립 Pages audit는 세 hosted report 모두 HTTP 200, `text/html`, external dependency `0`을
확인했다.

| Project | URL | Bytes | SHA-256 | Title |
|---|---|---:|---|---|
| BuildScope | [buildscope/pr/31](https://jihoon22-lee.github.io/toy-projects/buildscope/pr/31/) | 493,453 | `643a3e9e5c45a1512244cc90940146192399471621eac1a2dcb581cc534089c2` | `ici Verification Report — buildscope` |
| diskmap | [diskmap/pr/31](https://jihoon22-lee.github.io/toy-projects/diskmap/pr/31/) | 311,846 | `a8806808638c584312943d2551c1668a407c45830311de07cb0eed30d15e6924` | `ici Verification Report — diskmap` |
| loglens | [loglens/pr/31](https://jihoon22-lee.github.io/toy-projects/loglens/pr/31/) | 446,796 | `56f3b2d54ed2a05ebf100313b4d9447553e9c6fb9c85f7e7adce8eccc838dc4f` | `ici Verification Report — loglens` |

B1 implementation과 PR #31 remote integration evidence는 complete다. B2 implementation/local 및
remote verification도 complete다. B3 implementation과 원격 evidence도 아래에 기록한다. **B1-era
roadmap transition (historical):** 당시 B4 configuration diff는 next toy stage였고, B5 hybrid
release integration은 pending이었다.

## BuildScope B2 Qt compilation explorer (implementation + local/remote evidence, complete)

BuildScope `0.3.0`은 v2 snapshot을 소스별로 묶는 `QAbstractItemModel`과 가상 configuration
child index를 제공한다. snapshot entry를 다시 객체 트리로 복제하지 않고 index identity로
참조해 10만 entry에서도 bounded하게 동작한다. source/target/compiler/standard/configuration
tree, structured JSON argv와 별도의 raw command, define/include/diagnostic detail, recursive
case-insensitive 검색, missing/stale/present/unknown 로컬 SVG 상태 표시를 Qt `.ui`/`.qrc`에
연결했다. legacy v1은 raw compatibility view를 유지하며, malformed v2는 정확한 validation
location을 GUI에 표시한다.

`QAbstractItemModelTester`, MainWindow shell test와 파일 삭제·교체·변경·descriptor 종료를
결정적으로 재현하는 contract race test를 추가했다. Qt5 5.15.18과 Qt6 6.10.2에서 benchmark를
포함한 전체 CTest가 각각 6/6 PASS였다. Qt6 Release 100,000-entry/25,000-source benchmark는
model build 45 ms, filter 1,071 ms, peak RSS 132,612 KiB로 각 10초 budget을 통과했다.

public `ici v0.8.0` release asset 결과는 suite WARN(type/dup only), `46/46` tests,
line/function/branch `94.5% / 99.5% / 83.9%`, TEM `4.98`, compile DB `8/8`
production units·`19` configurations, complexity max `14`/`196` functions·0 issues다.
HTML은 558,384 bytes, SHA-256
`cdaefa06c52de696e0340b698e37b88dde199bc5a7bd2bbba27421618f44e444`, title은
`ici Verification Report — buildscope`, external resource reference는 0개다. 구현과 local
verification은 complete다. PR CI, ici sticky comment와 BuildScope/diskmap/loglens Pages 독립
검증도 아래 PR 및 main 원격 증거로 완료되어 B2 remote integration까지 complete다.

**B2 remote integration evidence (2026-09-01):** PR #32 head
`41472a66e69477fde7a71fe78c3ae9e47ba7f292`는 main에
`51a3480677a740475857dd92dd5a5a9373a287a4`로 squash-merge됐다. [PR run
`33454143021`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33454143021)의 16개 check가
모두 SUCCESS였고, [sticky comment #5486637533](https://github.com/jihoon22-lee/toy-projects/pull/32#issuecomment-5486637533)는
marker 1개와 link 3개를 포함한다. PR BuildScope는 `46/46`, branch `84.2%`, TEM `4.98`,
compile DB `8/8` production units·`19` configurations, complexity max `14`/`196` functions를
기록했다. PR benchmark는 model `53 ms`, filter `1,518 ms`, summary JSON SHA-256
`af7162b7603d558da6e7bc49d7bf5a80f546f412b7076992ded5e15739024db7`이며, exact-main run
`33454634202`도 SUCCESS였다(Report job expected skipped). main benchmark는 model `58 ms`,
filter `1,527 ms`, summary JSON SHA-256
`247c0b33095e0a09e97a289af556eae30f47f4f5c4136c530e3d6ca0018ae2d2`다.

세 hosted report는 HTTP 200·`text/html`, expected title, external resource reference 0개로
독립 확인됐다.

| Project | URL | Bytes | SHA-256 | Title |
|---|---|---:|---|---|
| BuildScope | [buildscope/pr/32](https://jihoon22-lee.github.io/toy-projects/buildscope/pr/32/) | 562,234 | `f15d18fe42ac172385e682ceb49e4b6d6f1d9bbfcc0ead301c11d1ee049c4c82` | `ici Verification Report — buildscope` |
| diskmap | [diskmap/pr/32](https://jihoon22-lee.github.io/toy-projects/diskmap/pr/32/) | 311,846 | `752f07251bc38285ea1633f5df879985131963e4b99f90532722eaedc9be1802` | `ici Verification Report — diskmap` |
| loglens | [loglens/pr/32](https://jihoon22-lee.github.io/toy-projects/loglens/pr/32/) | 446,791 | `7b2669fb7de82ada30bfdf28a2d82533f5566ad92779ea08c90528e188ea582b` | `ici Verification Report — loglens` |

B3 implementation과 원격 evidence는 아래에 기록한다. **B2-era roadmap transition (historical):**
당시 B4 configuration diff는 next toy stage였고, B5 hybrid release integration은 pending이었다.
ici I3 target-by-target same-basename comparison은 완료됐다.

## BuildScope include explanation (implementation + remote evidence complete; historical candidate)

**브랜치:** `feat/buildscope-include-explain`

BuildScope `0.4.0` main candidate에 optional include explanation을 추가했다. compile DB 입력과 v1/v2
projection은 그대로 유지하며, `--include-analysis estimate|compiler`를 사용하면
`buildscope.snapshot/v3`를 생성한다. `--schema-version v3`만 지정하면 `estimate`가 선택되고,
v1/v2와 analysis 조합은 거부된다. v3 schema는 root/entry/analysis/edge/search/diagnostic을
strict하게 검증하는 self-contained contract이며, unavailable entry도 warning과 함께 같은
shape으로 기록한다.

- [x] normalized compile entry에서 bounded include explanation을 만든다. `estimate`는
  source scan으로 lexical 후보를 계산하고, `compiler`는 compiler `-H` trace를 수집한다.
- [x] quote include의 current/quote roots 뒤에 include/framework, system, after roots를
  recorded order로 탐색하고, selected path·ordered candidates·same-basename alternatives를
  기록한다.
- [x] edge를 project/vendor/generated/system/missing/unresolved로 분류하고, compiler
  resolution과 source-scan/compiler-diagnostic location evidence를 분리한다.
- [x] shell 없는 argv replay boundary를 적용한다. 직접 승인된 system GCC/Clang driver만
  허용하고 positive allowlist, response-file/stdin/extra-input/plugin/linker escape 거부,
  argv/trace/edge/source/unit/time bounds를 적용한다.
- [x] v3 contract를 C++ native reader와 Qt UI에 연결한다. Include Edges에서 provenance,
  search order와 alternatives를 펼쳐 보고, edge 선택 상세와 parent source 위치 열기,
  Compilation Command 이동을 제공한다.

**B3 historical local candidate evidence (2026-09-01; before PR #34):**

- [x] Python 3.10 pytest `57/57` PASS, Ruff check + format `14 files` PASS, mypy `11 source
  files` PASS (replay policy, estimate/compiler, v3 projection, bounded failures 포함).
- [x] Qt 6.10.2 Release CMake/CTest `6/6` PASS와 Qt 5.15.18 Release CMake/CTest `6/6` PASS
  (v3 contract, GUI edge navigation, hybrid include contract 포함).
- [x] checksum-verified ici v0.8.0 release asset의 최종 no-cache local validation은 `Suite
  WARN`(검증 통과), engines `11 PASS / 2 WARN / 0 FAIL / 0 ERROR / 0 SKIP`, `63/63` tests,
  line/function/branch `92.6% / 98.9% / 79.1%`, TEM `4.94`, compile_db `8/8` production
  units·`19` configurations·`0` failures/warnings, complexity `PASS` max `14`/`251` functions·0
  issues, total `34.71s`였다. WARN은 C++ type unsupported와 dup뿐이며 `/tmp` HTML `851,656`
  bytes, SHA-256 `07d25971e04ed6a4aece36724ce8cf5e3c0548b7c382941a810454d8521c3e34`, exact title,
  external refs `0`로 확인했다. 이는 B3 PR/remote Pages evidence와 구분되는 local evidence다.
- [x] 100,000 entries/25,000 sources benchmark는 model `61 ms`, filter `1,126 ms`, budget
  `10,000 ms`, correctness `true`였고, pure wheel에 `compiler_replay.py`와 v3 schema가 포함됐다.
- [x] B3 PR/remote CI와 hosted report. [PR #34](https://github.com/jihoon22-lee/toy-projects/pull/34)의
  feature head `c3835cd4b0c859c38ae0f4afbdb20aae970515dc`에 대한 [run
  `33459294092`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33459294092)은 `Merge Gate`와
  `Publish Reports & Sticky Comment`를 포함한 16개 check 모두 SUCCESS였다. [sticky comment
  `#5487386460`](https://github.com/jihoon22-lee/toy-projects/pull/34#issuecomment-5487386460)는 marker
  정확히 1개와 project link 정확히 3개를 포함한다. BuildScope report는 `WARN`, `11 PASS / 2 WARN /
  0 FAIL / 0 ERROR / 0 SKIP`, TEM `4.94`, tests `63/63`, line/function/branch `92.7% / 98.9% /
  79.5%`였고, diskmap/loglens는 `PASS`, TEM `4.92`/`4.80`이었다.
- [x] PR benchmark는 `100,000` entries/`25,000` sources, model `118 ms`, filter `1,424 ms`,
  budget `10,000 ms`, correctness `true`였다. 세 PR Pages report는 HTTP 200 `text/html`, exact
  title, external attributes/CSS references `0`으로 독립 확인됐다.

| Project | URL | Bytes | SHA-256 | Title |
|---|---|---:|---|---|
| BuildScope | [buildscope/pr/34](https://jihoon22-lee.github.io/toy-projects/buildscope/pr/34/) | 858,143 | `e0b9c9ece1fb7268aa519bd0a4c62fd3da7c44a52b2efe6121393474d3ad36d4` | `ici Verification Report — buildscope` |
| diskmap | [diskmap/pr/34](https://jihoon22-lee.github.io/toy-projects/diskmap/pr/34/) | 311,847 | `8a6b01544b99eee6f0c2b95758f81395032f2b67a7c1a600879447cf7fb5f3bf` | `ici Verification Report — diskmap` |
| loglens | [loglens/pr/34](https://jihoon22-lee.github.io/toy-projects/loglens/pr/34/) | 446,786 | `1144759ef7e1b83ef7bd23f7bcfe9d02b05430a37af372350ec3b6e26d6c7ac7` | `ici Verification Report — loglens` |

- [x] PR #34는 `main`에 `9cce2699606e58ed67c3dac46f60dc7bf113bb60`으로 squash-merge됐고 branch는
  삭제됐다. 같은 head의 exact-main [CI run `33459591250`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33459591250)와
  [Dependency Graph run `33459594605`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33459594605)가
  성공했다. Push에서는 PR publish가 올바르게 skip됐고 applicable jobs와 `Merge Gate`는 성공했다.

B3 implementation과 remote evidence는 complete이고 code는 `main`에 shipped됐다. `0.4.0`은
별도 stable release로 발행하지 않고 include explanation을 `0.5.0` release boundary에 포함했다.
ici I3 cross-repository comparison은 완료됐으며 B4 configuration diff 구현과
PR/remote/hosted/merged-main evidence도 `main`에서 완료됐다.

## 완료: BuildScope semantic configuration diff (implementation + remote + merged-main evidence, 2026-09-01)

`feat/buildscope-config-diff`에서 개발된 B4 implementation은 두 raw `compile_commands.json`을 비교하는
strict `buildscope.diff/v1` contract, native C++/Qt consumption, and Python-to-C++ hybrid fixture를
제공한다. B4는 [PR #36](https://github.com/jihoon22-lee/toy-projects/pull/36)의 head
`ce64613263f0c4358579012aab135e0b23341a0e`에서 remote/hosted evidence까지 완료됐다.

**B4 remote evidence:** [run `33485837830`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33485837830)은 ici
`v0.9.1`로 `16/16` checks를 모두 성공시켰다. BuildScope report는 `WARN` (`10 PASS / 3 WARN /
0 FAIL / 0 ERROR / 0 SKIP`), lint WARN (49 warnings), `92/92` tests, line/function/branch
`93.5% / 99.0% / 77.3%`, sanitizer PASS, compile DB `12/12` production units·`27`
configurations, TEM `4.95/5.0`이다. 100,000 entries / 25,000 sources benchmark는 model `65 ms`,
filter `1,602 ms`, filtered sources `1`, budget `10,000 ms`, correctness `true`를 기록했다.
[Sticky comment #5489976814](https://github.com/jihoon22-lee/toy-projects/pull/36#issuecomment-5489976814)는
marker 1개와 hosted report link 3개를 포함한다.

| Project | Hosted report | Bytes | SHA-256 |
|---|---|---:|---|
| BuildScope | [buildscope/pr/36](https://jihoon22-lee.github.io/toy-projects/buildscope/pr/36/) | 1,319,378 | `5dc517d3ec8324cb1aedb6e611120ac9ae2951e27851cbc6b0e28303d02c5d43` |
| diskmap | [diskmap/pr/36](https://jihoon22-lee.github.io/toy-projects/diskmap/pr/36/) | 337,554 | `39a52d1e3d5b9eed6bbc1ec5253f1bf837deb4a7bb33ed2f2ef3b32df0f90e0e` |
| loglens | [loglens/pr/36](https://jihoon22-lee.github.io/toy-projects/loglens/pr/36/) | 492,746 | `0480f07aea266c0777403ac37ebf90d4acd10f84b04e49aa2a9ccf99a6153007` |

세 Pages report는 HTTP 200, exact title, external resource reference 0개였다. B4는 implementation과
PR/remote/hosted evidence까지 완료됐다. PR #36는 `main`에
`590899a0a9430e9ce35162b301bfef5d7dfc78a4`로 squash-merge됐고 feature branch는 삭제됐다. Exact-main
[CI run `33488169769`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33488169769)는 14개
선행 job과 `Merge Gate`가 모두 성공했으며 PR-only publisher는 expected skip이었다.
[Dependency Graph run `33488174425`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33488174425)도
같은 head에서 성공했다. 이 문단이 기록하는 B4 merge 시점에는 `0.5.0` product release artifact와
B5 hybrid release integration이 pending/not started였다. 이후의 local B5 작업은 다음 절에 기록한다.

## BuildScope 0.5.0 release readiness (remote acceptance complete; public release pending)

로드맵에서 B5로 추적한 release-boundary 작업은 B4 producer/consumer contract를 실제 배포
경계까지 연결한다. standalone packaging, install layout, examples/tutorial, release-gate와
공개 ici `v0.10.2` pin은 구현됐고, PR #38의 원격 검증 및 PR #39의 trusted `main` Pages
검증까지 완료됐다. 따라서 `0.5.0`은 release-ready 상태지만 annotated tag, GitHub Release,
9개 공개 asset의 사후 digest audit 전까지는 stable release로 주장하지 않는다.

- [x] `buildscope/tools/build_standalone.py`가 Python/pyproject/CMake/ici version surface를
  일치시키고 fixed zip metadata와 정렬된 package/schema payload로 `buildscope.pyz`를 생성한다.
  direct tests는 두 산출물의 byte identity, 실행, schema inventory와 symlink refusal을 확인한다.
- [x] `buildscope/CMakeLists.txt` install rule이 native CLI/GUI, `share/doc/buildscope/`,
  `share/buildscope/examples/`, `share/buildscope/schemas/` layout을 제공한다.
- [x] `buildscope/examples/cmake`, `buildscope/examples/qmake` sample compile database와
  [quickstart](buildscope/docs/quickstart.md)를 제공한다. quickstart는 pyz/wheel → JSON →
  native CLI/GUI 흐름과 qmake가 compile DB를 자동 생성하지 않는 제한을 명시한다.
- [x] local ici report에서 CMake compile DB `12/12` production units·`27` configurations·`0`
  issues와 Qt codegen exact inputs `3`, MOC `1`, UIC `1`, RCC `1`, Qt6 compile units `12`를
  확인한다.
- [x] PR #38의 hosted deep legs에서 `clang-tidy`/`clazy`를 포함한 tool-backed 검증이 실행됐다.
  local tool env에서 두 도구가 unavailable였던 기록은 별도의 local limitation으로 유지한다.
- [x] PR #38 CI preflight에서 Python `3.10/3.14` 및 Qt `5/6` Release/CTest matrix,
  generated MOC/UIC/RCC path와 offscreen GUI smoke가 실행됐다. Tag-only release workflow의
  실제 실행은 public release 단계에 남아 있다.
- [x] PR #38 release contract가 wheel/pyz가 같은 snapshot을 만들고 native CLI가 두 결과를
  소비하는 handoff와 Linux x86_64 bundle build를 검증했다. 실제 공개 asset 게시만 release
  audit 단계에 남아 있다.
- [ ] annotated `buildscope-v0.5.0` tag, exact-main/green Merge Gate provenance를 가진
  GitHub Release, 정확히 9개 asset 업로드와 사후 digest audit.

### Historical local ici evidence

public ici `v0.10.0` asset의 literal SHA-256 pin
`6d5f8c008b3b5393a61b2c1a418124eb66393c9eaab0abbb7d1c7922162bed9b`과
`ICI_PYTHON=/tmp/toy-b5-py310/bin/python`으로 deep/no-cache를 실행했다. 결과는 `Suite WARN`,
14 engines = `11 PASS / 3 WARN / 0 FAIL / 0 ERROR / 0 SKIP`, tests `96/96`,
line/function/branch `93.4% / 99.0% / 76.7%`, TEM `4.95`다. JSON은 2,873,207 bytes /
SHA-256 `ea5fce118e6edad8fa5af24c821663e4290805570377e9b7c190de8da1029612`, Zero-CDN HTML은
1,264,867 bytes / SHA-256 `4e12e77ee11f98b2d5bb146bd3d08252b088083726e6f11235e25a449471a565`이며,
exact title은 `ici Verification Report — buildscope`, external refs는 `0`이다. `clang-tidy`와
`clazy`는 local에서 실행되지 않아 tool-backed evidence가 아니라 unavailable로 기록됐다.

`.github/workflows/buildscope-release.yml`은 annotated tag가 exact `origin/main`과 green Merge Gate를
가리키는 provenance를 먼저 확인하고, Python `3.10/3.14`, Qt `5/6`, pure wheel/sdist, reproducible
pyz, Qt6 Linux x86_64 bundle, native handoff, ici v0.10.2 sidecar/download/API digest와
`SHA256SUMS`를 검사하도록 정의돼 있다. PR #38 CI의 동등한 preflight contract는 원격
acceptance를 완료했지만 tag-only workflow 자체는 아직 실행되지 않았다. 예정된 asset 이름은
`buildscope.pyz`, `buildscope.pyz.sha256`,
`buildscope-<version>-py3-none-any.whl`, `buildscope-<version>.tar.gz`,
`buildscope-ici-deep.{json,html}`, `buildscope-provenance.json`,
`buildscope-<version>-linux-x86_64.tar.gz`, `SHA256SUMS`다.

### Release-boundary remote evidence (PR #38)

[PR #38](https://github.com/jihoon22-lee/toy-projects/pull/38)의 final head
`3ba645eae5181698e1272729dddaa8a72189b067`에 대한 [CI run
`33545168957`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33545168957)은 PR
검증, Python/native handoff, Python `3.10/3.14`, Qt `5/6` matrix, deep legs, release contract,
`Publish Reports & Sticky Comment`, `Merge Gate`를 포함한 21개 check를 모두 성공시켰다. [sticky
comment `#5494648837`](https://github.com/jihoon22-lee/toy-projects/pull/38#issuecomment-5494648837)은
해당 exact run의 ici 검증 결과와 세 프로젝트 HTML report 링크를 기록한다. deep legs는 공개 ici
`v0.10.2`를 literal SHA-256
`8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4`로 검증한 뒤 실행했고,
Qt5/Qt6 hosted tool-backed evidence와 release handoff가 원격 환경에서 확인됐다.

PR #38은 `main`에 `069a3a86c0164a1d2a88710f9c3c48a398c8087e`로 squash-merge됐고 branch는
삭제됐다. 같은 exact head의 [main CI run
`33546046566`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33546046566)에서도
applicable jobs와 `Merge Gate`가 성공했다.

### Trusted main Pages evidence (PR #39)

PR #38 merge 뒤 main report가 독립적으로 안정적으로 게시되는지 검증하기 위해 [PR
#39](https://github.com/jihoon22-lee/toy-projects/pull/39)에서 trusted `main` publisher를
추가했다. final head `b861ff5b4cc0314aae5ec9f6dab905648233216d`의 [CI run
`33548626203`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33548626203)은 성공했고,
[sticky comment `#5499184834`](https://github.com/jihoon22-lee/toy-projects/pull/39#issuecomment-5499184834)은
그 run의 ici report 링크와 결과를 기록한다. PR #39는 `main`에
`c80e922f0d0911019cfa8b5c67a8b654c556a68c`로 squash-merge됐고 branch는 삭제됐다. 이후 [exact-main
run `33549475034`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33549475034)도
성공했다.

Exact-main run `33549475034` 시점의 세 main report는 HTTP 200, `text/html`, exact title
`ici Verification Report — <project>`, Zero-CDN을 만족했고 그 실행의 산출물과 byte-match됐다.

| Project | Main report | Bytes | SHA-256 |
|---|---|---:|---|
| BuildScope | [buildscope/main](https://jihoon22-lee.github.io/toy-projects/buildscope/main/) | 1,349,088 | `dd7ec9b49281875f812b9a9c6b4e18a028051936a37441f19d907098c05dcc65` |
| diskmap | [diskmap/main](https://jihoon22-lee.github.io/toy-projects/diskmap/main/) | 339,929 | `1bd6ebdce2206e4538fb28c20c17f2d504f1a160e15f5d8d4e2923b15b399e65` |
| loglens | [loglens/main](https://jihoon22-lee.github.io/toy-projects/loglens/main/) | 495,237 | `11e4c78eed957e237bcea8ec5ea64c3894751eafbe639c454e47ab0acd70df96` |

`0.5.0`은 include explanation, semantic configuration diff, hybrid packaging/integration을 묶은
첫 usable BuildScope release boundary로 남긴다. 현재 remote integration, matrices, handoff,
release contract, trusted main Pages는 complete다. annotated tag와 GitHub Release의 정확히 9개
asset 및 사후 digest audit이 끝나기 전까지는 public stable release를 주장하지 않으며, 이후
기능과 CI/ici pin 작업은 다음 comparable checkpoint까지 `Unreleased`에 쌓는다.

## 배경

이 프로젝트들은 재미와 검증을 동시에 노리고 시작했다. 실제로 쓸 C++/Qt 앱을 만들고,
그걸 [ici](https://github.com/jihoon22-lee/ici) 로 검증한다. ici 는 그 전까지 **자기 자신
(순수 Python 프로젝트) 하나만** 도그푸딩하고 있어서 C++ 경로에 E2E 회귀 커버리지가 없었다.

결과는 기록이 말한다. 초기 실물 개발에서 **ici 결함 17건을 찾았고 그중 13건이 수정됐다.**
어댑터 작업에서 9건이 더 발견돼 현재 추적 항목은 26건이며, 그중 22건이 수정됐다. 전부
만들면서 나왔고, 코드를 읽어서 찾은 것은 하나도 없다. 남은 항목은
[ICI-GAPS.md](ICI-GAPS.md)의 A-2, B-2, B-3, C-7이다.

## 완료: 어댑터와 loglens stream state (2026-08-31)

1단계와 그에 딸린 전환이 끝났다. **ici 0.6.0 이 CMake/CTest 와 qmake/Make 어댑터를 갖췄고**,
A-3 은 닫혔다. `Q_OBJECT` 클래스의 단위 테스트가 `tests/` 안에서 통과한다. 이어서
loglens의 poll 경계 parser state와 CLI/GUI 공통 delta 계약도
[#14](https://github.com/jihoon22-lee/toy-projects/pull/14)로 병합됐다. 이것은 T0-2 완료이며,
Qt 셸 테스트와 양 Qt major matrix까지 끝나야 T0 전체가 완료된다. T0-5 구현의 원격 PR
gate에서는 아래 네 leg가 같은 계약을 다시 실행한다.

| 프로젝트 | 빌드 | 검증 | Qt 테스트 |
|---|---|---|---|
| `loglens` | CMake · Qt5/Qt6 | L2 benchmark PR #26 merged · main Qt5/Qt6 sweep green · default 8192 | `QAbstractItemModelTester` + MainWindow QtTest |
| `diskmap` | qmake · Qt5/Qt6 | D1 Slice 2 merged · D2 fully complete (PR #28 remote evidence + main benchmark green) · D3 next | `QSignalSpy` + 9 native test targets + MainWindow QtTest |
| `ici/viewer` | CMake · Qt5/Qt6 | PASS · TEM 4.86 | MainWindow QtTest 4/4 |

T0-5의 `discover`는 GUI 프로젝트 한 항목을 Qt5·Qt6 두 항목으로 확장한다. 따라서 현재
`gui-build`는 `diskmap/Qt5`, `diskmap/Qt6`, `loglens/Qt5`, `loglens/Qt6` 네 leg이며,
job 이름에도 실제 major가 표시된다. Qt5는 `qtbase5-dev`/`qt5-qmake`, Qt6는
`qt6-base-dev`/`qmake6`를 조건부 설치하고, 두 leg 모두 선택된 pkg-config Core/Gui/Widgets/
Concurrent/Test 버전, native tests, 실제 headless GUI smoke를 기록한다. report-pr는 기존처럼
PR 소스를 실행하지 않는 별도 집계 job으로 유지한다.

### 완료 기준이 실제로는 바뀌었다

원래 1단계의 완료 기준은 **"ici 가 어디서 막히는지 `ICI-GAPS.md` 에 기록됐다"** 였다.
실제로는 **"ici 를 고쳐서 통과시켰다"** 가 됐다.

이유는 단순하다. 기록만 남기려면 실패하는 Qt 테스트를 저장소에 커밋해야 하는데, 그러면
게이트가 영구히 빨간불이 되거나 테스트를 ici 가 보지 않는 곳으로 옮겨야 한다. **후자는 우회를
없애려다 새 우회를 만드는 것이다.** 그래서 측정과 구현을 같은 루프에 넣었다 — 실패하는
테스트를 먼저 쓰고, 그것이 ici 구현을 끌고 가게 했다. 요구사항이 실측에서 나온다는 원칙은
그대로 지켜졌고, 빨간 상태만 커밋되지 않았다.

### 픽스처로는 안 나왔을 결함들

어댑터 작업에서 새 결함 9건이 나왔고 **전부 만들다 나왔다.** 그중 여럿은 실물 프로젝트라야
드러났다 — 목록과 근거는 [ICI-GAPS.md](ICI-GAPS.md) 에 있다.

가장 좋은 예가 D-7 이다. qmake 는 Qt 링크 테스트를 `target_wrapper.sh` 로 실행하는데 ici 가
그 줄을 못 읽어 **`diskmap` 의 테스트 6개 중 5개만 세고 있었다.** ici 의 qmake 픽스처는 Qt
테스트 하나뿐이라 다른 경로로 구제됐고, 그래서 픽스처는 통과했다. QtTest 와 수제 `main()`
테스트가 섞인 실물 프로젝트라야 나오는 결함이었다.

**`diskmap` 을 qmake 로 옮긴 것이 이 때문이다.** 어댑터가 둘인데 실물 프로젝트가 하나뿐이면
나머지 하나는 픽스처만으로 설계한 것이 된다.

## 기존 로드맵에서 남은 것 (마스터 계획에 흡수됨)

### 2단계 — diskmap 정리 작업대 (①)

**아직 남아 있다.** 이번 작업은 `diskmap` 을 qmake 로 옮기고 기존 위젯에 시그널 테스트를
붙였을 뿐, 정리 작업대 기능은 만들지 않았다.

디스크 사용량을 보는 이유는 대개 **지우려고** 인데 지금은 보고 끝이다. 다중 선택으로 삭제
후보를 담고, 회수 용량을 보여주고, 실행 전 검토한다. 휴지통으로 옮기므로 되돌릴 수 있다.

- 순수 core: 회수 용량 계산, 중첩 선택 정규화, 안전 규칙(심볼릭 링크·시스템 경로 거부)
- Q_OBJECT: 스테이징 모델 + `QUndoCommand` 스택

**이제 이것을 단위 테스트할 수 있다.** 1단계 전에는 불가능했고, 그게 이 단계를 미룬 이유였다.

### 과거 3단계 — diff/merge 도구: 근거를 다시 판단할 것

이 역사적 단계의 `A` 표기는 현재 마스터 계획의 `abilens` A stream과 다르다.

**원래 목적은 이미 달성됐다.** 이 단계는 A-2·A-3 을 실측하기 위한 것이었는데 둘 다 처리됐다.
이 문장은 당시 T0의 역사적 상태다. 그 이후 BuildScope CMake에는 `.qrc`/`.ui`와
`AUTOMOC`/`AUTOUIC`/`AUTORCC`가 들어갔고, B5 local ici report에서 exact input `3`, MOC `1`,
UIC `1`, RCC `1`과 Qt6 compile units `12`를 실측했다. 다른 프로젝트의 generated-asset 범위는
이 BuildScope B5 evidence와 별도다.

그 조건 하나를 위해 새 프로젝트를 만들 가치가 있는지는 다시 판단해야 한다. 더 싼 방법이
있다 — 기존 프로젝트에 아이콘 `.qrc` 하나를 넣으면 같은 것이 실측된다. 도구 자체가 갖고
싶다면 그건 별개의 이유이고, 그렇다면 그 이유로 정당화해야 한다.

### Qt 셸 테스트 현황

`loglens`는 `test_main_window`에서 실제 임시 파일의 open/growth/truncation/read error와
follow checkbox/timer/status 상태를 검증한다. `TimelineWidget`도 빈 상태와 색상 막대
렌더링 분기를 헤드리스 렌더링으로 확인한다. Qt5.15와 Qt6.10에서 동일한 CMake/CTest
12개 target이 통과했다.
`diskmap`도 MainWindow 내비게이션을 `test_main_window`로 검증하며 T0-4를 완료했다.

T0-5 로컬 실측은 Qt6 CMake와 Qt5 CMake 각각 `10/10 CTest PASS`, qmake6와 `/usr/bin/qmake`
각각 `make check PASS`, 네 GUI binary 모두 해당 `libQt{5,6}Widgets` 링크와 2초 이상
headless smoke 생존을 확인했다. CMake는 반대 major disable guard와 configure output을,
qmake는 `-query QT_VERSION`을 검증한다. `ci/test_check_manifest.py`의 stdlib 테스트는
새 GUI manifest 항목도 `(5,6)` 두 leg로 확장되는지 확인한다.

ici 0.6.0 실측은 line 93.2% / function 96.9% / branch 81.8% / TEM 4.84이다.
`loglens/ici.toml`은 실측값에 6.8%p/4.9%p slack을 둔 branch 75.0 / function 92.0을
게이트로 사용한다. 남은 것은 QPainter/Qt 내부 예외 경로와 ici의 C++ type-check 미지원이다.
`diskmap/src/gui/main_window.cpp`는 T0-4에서 scan, breadcrumb, descend/up, leaf no-op를
검증했다.

D1 Slice 2에서는 이 셸 검증에 identity-safe scanner 계약을 추가했다. `std::filesystem::path`
경계, followed-directory cycle 방지, hard-link allocated/reclaimable deduplication,
symlink alias 비소유, identityless target의 incomplete 전파와 aggregate overflow를 fake
source 및 실제 POSIX filesystem으로 확인한다. Qt5/qmake와 Qt6/qmake6 모두 fresh full build와
`make check` 9/9, GUI offscreen smoke 8초 생존을 통과했으며 public ici 0.6.0 verify는 TEM
4.90, line/function/branch 96.9%/97.9%/85.2%였다. PR #23의 전체 CI와 sticky report
comment, Pages HTML 응답까지 확인한 뒤 `039052f9`로 병합됐다.

**diskmap D2 complete (2026-08-31; local + remote evidence).** cooperative cancellation과 progress callback을
GUI에 연결하고, rescan마다 generation을 증가시켜 최신 scan만 treemap/breadcrumb에 반영한다.
늦게 끝난 이전 worker와 그 progress는 무시하며, 취소된 partial result는 폐기하고 기존에
보이던 결과를 유지한다. root metadata/listing의 fatal 오류와 중간 subtree의 partial/non-fatal
오류를 구분하고, mount boundary·exclude glob·min size·max depth 옵션을 추가했다. CLI에서는
실제 traversal bound인 `--max-depth`와 legacy 출력 전용 `--depth`, repeatable `--exclude`를
구분한다. scanner와 aggregate/sort/count/top-files/layout은 iterative helper를 사용하며,
effective structural depth는 `kMaxTreeDepth=512`에서 안전하게 clamp한다.

로컬에서는 Qt 5.15.18(`/usr/bin/qmake`)과 Qt 6.10.2(`/usr/bin/qmake6`)의 full build 및
`make check`가 각각 PASS했고, `test_main_window`는 두 major에서 각각 10/10 PASS를 기록했다.
CLI integration smoke도 PASS했으며, 1,000,000-entry full correctness는 4820.934 ms,
207428.692 entries/s, peak RSS 1063.496 MiB를 기록했다. 10,000/1,000,000 cancellation은
2.676 ms였고 local summary JSON SHA-256은
`743d5c5409101cfd9ef889da2da421e94cc205f585770ab19bb611472926246d`다. ici complexity-only
gate의 최초 scan complexity/nesting FAIL은 `b7218c6` refactor로 해소되어 maximum cyclomatic
14 (limit 15), 129 functions, 0 issues로 PASS했다. benchmark command/workflow와 budget은
[README의 DiskMap benchmark 절](README.md#diskmap-generated-source-benchmark-opt-innightly)에
기록했다. current ici main commit `6a0eadb`의 candidate pyz SHA256
`8cd2d4b128ab2d181e708660c4c4f38bcc9d50f9ad91e3aa5670f557e6077fed`로 수행한 full
post-refactor local `ici verify`는 `Suite PASS`, 10 pass / 0 warn / 0 fail / 0 error /
2 skip, test 9/9, TEM 4.92, line95.7% / function98.5% / branch84.4%, complexity max14
across129 functions / 0 issues, dup3.11%, sanitize clean, 30 tools/21 ready/0 incomplete/
9 unavailable, total82.29s, cache hits0이었다. HTML은 299034 bytes, SHA256
`cf75f9d6f28179d95645d0e1582022008804078d5e3844de503a8c1a130c64a0`, external resource tags0이다.
이 local evidence는 candidate artifact에 대한 기록이며, 아래 D2 원격 완료 증거가 toy-projects
`main` 병합과 PR/CI·sticky comment·Pages·merged-main benchmark gate를 닫는다.

loglens L2의 첫 slice는 GUI/CLI에 absolute-ID bounded store를 연결하고 source polling과
pathological record 크기를 제한했다. 이어진 background/Tail N slice는 Qt5·Qt6 및 엄격 경고
build의 CTest 12/12와 ici Suite PASS(TEM 4.83)를 확인했다. 구현 head `fa4fd1a`의 PR #24는
이전 bounded foundation에 대한 ici, Qt5·Qt6, sticky report/Pages와 Merge Gate 증거다.

background/Tail N 최신 구현 head `ce2a7cd91ff0a47c4f153b60f7fb7984de406ce9`는
[PR #25](https://github.com/jihoon22-lee/toy-projects/pull/25)의
[workflow `33351033448`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33351033448)에서
모든 checks를 통과했고 merge commit `69db15966ca0c032026aeb7b742c4eed6335910d`로
병합됐다. [sticky comment](https://github.com/jihoon22-lee/toy-projects/pull/25#issuecomment-5472960253)는
두 프로젝트 PASS와 HTML 링크를 포함하며, Pages `diskmap/pr/25/`와 `loglens/pr/25/`는 각각
HTTP 200·`text/html`·external refs 0개(180160/327074 bytes)였다. 이 원격 증거는
background/Tail N 변경에 대한 것이고 1 GiB benchmark의 원격 검증과 혼동하지 않는다.

#### L2 대용량 benchmark 결정 (2026-08-31)

canonical 1 GiB input(정확히 1,073,741,824 bytes, 1,000,000 records, SHA-256
`11186d3021e558c8ed5e33473198a6f9f281ca0605ae79739a928a87156435bb`)을 capacity
`8192, 16384, 32768, 65536, 131072, 262144` 각각 3회, 180초 timeout으로 측정했다.
두 Qt major에서 `8192..65536`이 correctness와 성능/RSS budget을 만족했고, `131072`은
core RSS, `262144`는 core와 GUI RSS budget에서 탈락했다. budget은 first result/paint
각 `≤ 5000 ms`, load `≤ 60000 ms`, throughput `≥ 25 MiB/s`, records `≥ 25000/s`,
core RSS `≤ 256 MiB`, GUI RSS `≤ 512 MiB`다. best median load time 대비 10% 이내의 가장
작은 적격 capacity를 고르는 규칙으로 기본값을 `8192`로 고정했다. PR #26은
`c45176ce25f2efd66ea9b0ed9b48690e34cc8679`로 squash merge됐다.

runner 재현 명령, Qt5/Qt6 guard, opt-in/nightly workflow와 `summary.json`·`summary.md`·
`toolchain.*`·`samples/*.json` artifact allowlist는 [README의 benchmark 절](README.md#1-gib-benchmark-재현-opt-in)에 기록했다. 일반 PR에는 `.github/workflows/ci.yml`의
1 MiB/1,000-record `benchmark-smoke`(capacity `64,256`, 1회, 30초, budget skip)가
`Merge Gate` required check로 포함된다. 최종 PR gate인
[workflow run `33355058919`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33355058919)은
모든 checks가 green이었고, 기존 [sticky comment](https://github.com/jihoon22-lee/toy-projects/pull/26#issuecomment-5473343910)는
`diskmap: PASS · TEM 4.90`, `loglens: PASS · TEM 4.80`, warn 0과 HTML 링크를 포함한다.
Pages `diskmap/pr/26/`와 `loglens/pr/26/`는 각각 HTTP 200·`text/html`·external refs 0개
(180160/334215 bytes)였다. 병합된 main의 [대용량 workflow run `33355312096`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33355312096)은
Qt5/Qt6 benchmark, combine, verdict를 모두 green으로 완료했고, combined summary SHA-256은
`5e3292950958a4c678a0c54bf75e7b2546ad1528f43529b6cce1c3dff4e150a8`이다.
병합 후 main의 ici 0.6.0 deep no-cache도 12/12 tests, TEM 4.83,
line/function/branch 93.6%/96.6%/81.8%, sanitizer PASS, HTML 433,351 bytes·external refs
0개로 통과했다.

**diskmap D2 원격 완료 증거 (2026-08-31).** [PR #28](https://github.com/jihoon22-lee/toy-projects/pull/28)은
squash merge commit `ec075e57874d20654f7cbfbc604ad8aaee8401a6`으로 toy-projects `main`에
병합됐다. [PR CI run `33368958698`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33368958698)의
12개 check가 모두 green이었고, DiskMap benchmark smoke Qt5/Qt6, 네 GUI matrix job, 두 ici
verification job, publish와 Merge Gate를 포함한다. [sticky comment](https://github.com/jihoon22-lee/toy-projects/pull/28#issuecomment-5475254935)에는
`diskmap: PASS · TEM 4.92 · 10 pass / 2 skip · tests 9/9`와
`loglens: PASS · TEM 4.80 · 10 pass / 2 skip · tests 12/12`, 양쪽 report link가 게시됐다.
Pages `diskmap/pr/28/`와 `loglens/pr/28/`는 각각 HTTP/2 `200`, content-type `text/html`,
외부 `script`/`link`/`img`/`iframe` resource 0개로 확인됐다.

| Pages 경로 | bytes | SHA-256 |
|---|---:|---|
| `diskmap/pr/28/` | 199843 | `c8a0d8009e1c19cd2d9df041969396f6abce95275713fe7ead6a499ac0b33b72` |
| `loglens/pr/28/` | 334215 | `acda3bfb29bf5f3534256f614719e678ec89ed21b3420ee2b282ec55e2107830` |

PR #28 병합 후 head `ec075e5`에서 수행한 [main full benchmark run `33369288586`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33369288586)은
Qt5/Qt6 full, combine, verdict job을 모두 성공시켰다. 설정은 1,000,000 entries,
`cancel-after 10000`, process timeout 60초이며, 양쪽 correctness는 `true`, failures는 0,
budgets enforced는 `PASS`였다.

| Qt | full elapsed | full throughput | full peak RSS | cancellation elapsed |
|---|---:|---:|---:|---:|
| 5 | 2131.069 ms | 469248.020 entries/s | 1064.094 MiB | 1.479 ms |
| 6 | 3120.463 ms | 320465.315 entries/s | 1064.137 MiB | 1.580 ms |

combined `summary.json`은 3567 bytes이며 SHA-256은
`26391797763aed17fedb04e2a4aeb5cf8238ec4d5b5d040d473d32a513369251`이다. 이는 toy-projects
`main`의 기능 병합 및 검증 기록이며, 별도 제품 버전 release를 의미하지 않는다. 따라서 D2는
local/native/ici, PR CI, sticky report, Pages와 merged-main benchmark까지 모두 완료됐고 다음
단계는 D3 explorer UX다.

### 4단계 — 여유가 되면

- loglens 하이라이트 룰 편집기 (④) — 룰이 코드에만 있다. UI 로 정의·저장·드래그 재정렬
- diskmap 스냅샷 비교 (②) — "지난주 대비 뭐가 늘었나". 트리 diff 는 좋은 core 소재

## 원칙

**완료 기준을 "기능이 동작한다" 가 아니라 "요구사항이 실측됐다" 로 둔다.** 기능은 수단이다.
각 단계는 PR 하나 크기로 끊고, 매번 ici 가 어디서 어떻게 깨지는지를 재현 조건과 함께 남긴다.

수정·미수정을 합친 26건의 결함이 전부 그렇게 나왔다 — 만들다 이상해서 파고든 결과다.

여기에 한 줄을 덧붙인다. **픽스처는 실물 프로젝트를 대신하지 못한다.** ici 저장소의 qmake
픽스처는 통과하는데 `diskmap` 은 테스트 하나를 통째로 잃고 있었다(D-7). 픽스처는 만든 사람이
상상한 모양만 담고, 실물은 그렇지 않은 모양을 갖고 있다.
