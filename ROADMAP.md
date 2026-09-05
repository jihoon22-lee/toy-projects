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
- BuildScope `0.5.0`은 B3/B4/B5를 함께 묶은 첫 usable release boundary이며 2026-09-02 KST에
  stable release로 공개됐다. 그 이후 작업은 이에 상응하는 checkpoint가 생길 때까지
  `Unreleased`에 누적한다.

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

## BuildScope 0.5.0 release evidence (published 2026-09-02; final audit complete)

로드맵에서 B5로 추적한 release-boundary 작업은 B4 producer/consumer contract를 실제 배포
경계까지 연결한다. standalone packaging, install layout, examples/tutorial, release-gate와
공개 ici `v0.10.2` pin은 구현됐고, PR #38의 원격 검증 및 PR #39의 trusted `main` Pages
검증까지 완료됐다. BuildScope `0.5.0`은 2026-09-02 KST (`published_at`
`2026-09-01T22:36:42Z`)에 [GitHub Release](https://github.com/jihoon22-lee/toy-projects/releases/tag/buildscope-v0.5.0)
ID `380863869`으로 공개됐고, `immutable=true`, `draft=false`, `prerelease=false`다.

- [x] `buildscope/tools/build_standalone.py`가 Python/pyproject/CMake/ici version surface를
  일치시키고 fixed zip metadata와 정렬된 package/schema payload로 `buildscope.pyz`를 생성한다.
  direct tests는 두 산출물의 byte identity, 실행, schema inventory, symlink refusal과
  database-free `buildscope.pyz --version`을 확인한다.
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
  실제 실행도 [run `33566464110`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33566464110)에서
  exact `main` `fda8b5fb068b68c04c8c40e297812fbe79cee3da`로 성공했다.
- [x] PR #38 release contract가 wheel/pyz가 같은 snapshot을 만들고 native CLI가 두 결과를
  소비하는 handoff와 Linux x86_64 bundle build를 검증했다. 실제 공개 asset 게시와 final
  byte/digest audit도 완료됐다.
- [x] release payload checker가 exact nine-file inventory, exact-shebang/version pyz, pure wheel, sdist,
  Linux bundle, archive traversal/link/special-file 거부, schema-byte agreement, provenance,
  B5 deep JSON, Zero-CDN HTML을 함께 검증한다. ZIP은 `ZipFile` 이전에 bounded EOCD와
  central-directory preflight를 거친다. Python 3.10/3.14 in-memory/temporary archive regression
  fixture가 metadata/version/schema, embedded artifact/ELF, provenance, CLI 거부 경로와 두 유효한
  self-extracting ZIP offset layout을 포함한다.
- [x] release-state checker와 workflow가 authenticated paginated slot을 fail-closed로 검사한다.
  빈 슬롯만 direct private draft를 만들고, 기존 final은 mutation 없는 audit-only 경로로 보내며,
  기존 draft·중복·모호한 슬롯은 중단한다. 새 draft는 current-run owner marker를 갖고, 모호한
  생성 응답의 recovery는 exact owner-marked zero-asset private draft 중 expected body digest까지
  일치하는 하나에만 허용한다. 새 draft의
  fixed release ID, bounded binary upload/no-clobber, prepublish 재감사, ambiguous PATCH
  reconciliation, final fresh-byte download, post-download metadata/tag/assets re-fetch를 계약으로
  고정한다. 실패한 owned draft는 자동 삭제하지 않고 명시적 수동 검토를 위해 보존한다.
- [x] GitHub immutable releases를 활성화하고, active tag ruleset
  [`buildscope-release-tags`](https://github.com/jihoon22-lee/toy-projects/rules/22049711)가
  `refs/tags/buildscope-v*`의 최초 생성은 허용하되 update/deletion을 empty bypass로 차단한다.
- [x] annotated `buildscope-v0.5.0` tag at exact green main, GitHub Release, 정확히 9개 asset의
  공개 업로드 및 독립적인 final byte/digest audit.

**Final publication evidence (2026-09-02 KST):** annotated tag object
`dcaaf83a5842f6d7fc6c47e3b212e26b9528c342`는 exact `main`
`fda8b5fb068b68c04c8c40e297812fbe79cee3da`로 peel됐다. `Merge Gate`는 [job
`100050176790`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33565542193/job/100050176790)으로
[run `33565542193`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33565542193)에서 성공했다.
최종 release body SHA-256은
`9e58639c280655bf50b510ef676bb3e5f458cf2021c3c6c6b24c3b625945dd3b`이며, fresh download 기준
정확히 `9`개 asset을 audit했다. public ici `v0.10.2`의 SHA-256 pin은
`8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4`다.

최종 deep report는 `WARN` (`11 PASS / 3 WARN / 0 FAIL / 0 ERROR / 0 SKIP`), tests `97/97`,
line/function/branch coverage `93.5% / 99.0% / 77.2%`, compile DB `12/12` units·`27`
configurations, Qt6 exact codegen (`MOC 1`, `UIC 1`, `RCC 1`), `clang-tidy`/`clazy` 각각
exact `12` sources/configurations, TEM `4.95`였다. HTML은 `1,344,843` bytes, SHA-256
`0a0b50f8e056ad561427fd2141dbd8649dd43fdf111b2d6e187c220b0a610ee9`, exact title
`ici Verification Report — buildscope`,
Zero-CDN이다. 구현 및 contract detail은 [BuildScope README](buildscope/README.md)에 둔다.

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

`.github/workflows/buildscope-release.yml`은 고정 annotated tag의 peeled commit이 exact
`origin/main`과 green `Merge Gate`를 가리키는 provenance를 먼저 확인하고, Python `3.10/3.14`,
Qt `5/6`, pure wheel/sdist, reproducible pyz, Qt6 Linux x86_64 bundle, native handoff, ici
v0.10.2 sidecar/download/API digest와 `SHA256SUMS`를 검사하도록 정의돼 있다. 최신
`ci/check_buildscope_merge_gate.py`는 exact SHA의 newest `Merge Gate` check-run을 positive ID로
선택하고 GitHub Actions app 및 완료/성공 상태를 요구한 뒤, 독립적으로 조회한 Actions run의
ID/repository/head repository/SHA/workflow name/path/event/status/conclusion/canonical URL을
검증한다. Release API의 `target_commitish` equality는 사용하지 않고 annotated tag peel을
authoritative proof로 삼는다. softprops의 기존 release 갱신은 제거됐다. authenticated paginated
slot audit은 빈 슬롯에서만 direct private draft를 만들고, 기존 final은 mutation 없이 audit-only로
처리한다. 새 draft는 `RELEASE_NOTES.md`를 정규화해 만든 owner-marker body와 fixed numeric release ID를
확인하고, 별도 final/draft body 파일의 정확한 UTF-8 SHA-256을 계산한다. draft digest는
create/upload/prepublish/failure-report에서, final digest는 publish/final audit에서 재검증한 뒤 정확한 9개 asset을 20초
connect/300초 transfer bound의 binary upload/no-clobber로 전송한다. 모호한 생성
응답의 recovery는 exact owner-marked zero-asset private draft 중 expected body digest까지 일치하는
하나에만 허용된다. bounded downloader로
fresh directory에 내려받은 manifest/sidecar, payload/archive, provenance, B5/HTML, pyz를
검사하며 ZIP은 `ZipFile` 이전에 bounded EOCD/central-directory preflight를 거친다. 공개 전 draft를
재감사하고, PATCH가 모호하면 동일 ID를 재조회해 exact final은 성공, exact private draft는 재시도하며,
그 외 상태는 실패한다. 공개 뒤에는 9개 final asset을 fresh directory로 다시 내려받아 current `dist`와
모두 byte-compare하고 ID/tag별 metadata와 assets 및 peeled tag를 다시 fetch한다. write-token 단계는
다운로드한 원격 BuildScope pyz를 실행하지 않는다. 실패한 owned draft는 자동 삭제하지 않고 수동 검토를
위해 보존한다. empty-slot failure-report 단계는 create output ID가 유실돼도 paginated listing에서
exact current-run-owned zero-asset draft 중 expected body digest까지 일치하는 항목을 복구해
보고·보존만 수행한다. PR #38 CI의 동등한
preflight contract는 원격 acceptance를 완료했고, tag-only workflow도 [run `33566464110`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33566464110)에서
성공해 공개 final audit까지 마쳤다. 최종 asset 이름은
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
첫 usable BuildScope release boundary이며, 위 GitHub Release로 2026-09-02 KST stable published됐다.
annotated tag provenance, exact-main workflow, Merge Gate, 9개 asset final audit과 deep report
수치는 위 final publication evidence에 고정했다. 이후 기능과 CI/ici pin 작업은 다음 comparable
checkpoint까지 `Unreleased`에 쌓는다. 상세 contract는 [BuildScope README](buildscope/README.md)에서
확인한다.

## 배경

이 프로젝트들은 재미와 검증을 동시에 노리고 시작했다. 실제로 쓸 C++/Qt 앱을 만들고,
그걸 [ici](https://github.com/jihoon22-lee/ici) 로 검증한다. ici 는 그 전까지 **자기 자신
(순수 Python 프로젝트) 하나만** 도그푸딩하고 있어서 C++ 경로에 E2E 회귀 커버리지가 없었다.

결과는 기록이 말한다. 초기 실물 개발에서 **ici 결함 17건을 찾았고 그중 13건이 수정됐다.**
어댑터 작업에서 9건이 더 발견돼 현재 추적 항목은 26건이며, 그중 22건이 수정됐다. 전부
만들면서 나왔고, 코드를 읽어서 찾은 것은 하나도 없다. 아래 문장은 당시 기록의 남은 목록을
보존한 historical snapshot이다. 현재 상태는 [ICI-GAPS.md](ICI-GAPS.md)의 현황과 ici
마스터 계획이 기준이다. 손으로 쓴 `Makefile`만 있는 프로젝트를 거부하는 A-2는 여전히
실제 ici I7/AbiLens A0 입력이다. B-2의 BuildScope 외부 대조는 완료됐다. B-3의 언어 지원
범위 표기는 ici I1-2 support/capability matrix가 소유하지만, C++에서 dead/resource 및
관련 maintainability 결과를 제공하는 capability gap은 남아 있다. 그 구현은 toy 기능이
아니라 ici I4-3/I4-4가 소유한다. C-7의 예전 aggregate-status 제안은 현재 evidence taxonomy가
원래의 inapplicable/실행불가 모호성을 줄인 historical 입력이며, 향후 gate 정책까지 닫혔다고
간주하지 않는다.

## 완료: 어댑터와 loglens stream state (2026-08-31)

1단계와 그에 딸린 전환이 끝났다. **ici 0.6.0 이 CMake/CTest 와 qmake/Make 어댑터를 갖췄고**,
A-3 은 닫혔다. `Q_OBJECT` 클래스의 단위 테스트가 `tests/` 안에서 통과한다. 이어서
loglens의 poll 경계 parser state와 CLI/GUI 공통 delta 계약도
[#14](https://github.com/jihoon22-lee/toy-projects/pull/14)로 병합됐다. 이것은 T0-2 완료였고,
후속 T0-3~T0-5의 Qt 셸 테스트와 양 Qt major matrix도 구현·native/ici 실측·PR 및
exact-main/Merge Gate evidence까지 완료됐다. 따라서 T0 checkpoint는 닫혔으며 아래 표는
완료된 계약의 evidence를 요약한다.

| 프로젝트 | 빌드 | 검증 | Qt 테스트 |
|---|---|---|---|
| `loglens` | CMake · Qt5/Qt6 | L2 benchmark PR #26 merged · main Qt5/Qt6 sweep green · default 8192 | `QAbstractItemModelTester` + MainWindow QtTest |
| `diskmap` | qmake · Qt5/Qt6 | D1/D2 complete · D3 explorer workbench merged by PR #46 and exact-main run green · `0.1.0`/`Unreleased` | `QSignalSpy` + 11 native qmake targets + MainWindow/treemap/table QtTest |
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
`toolchain.*`·`samples/*.json` artifact allowlist는 [LogLens 문서의 benchmark 절](loglens/README.md#1-gib-benchmark-재현-opt-in)에 기록했다. 일반 PR에는 `.github/workflows/ci.yml`의
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
local/native/ici, PR CI, sticky report, Pages와 merged-main benchmark까지 모두 완료됐다. D3의
Qt-free core projection은 PR #45로, GUI workbench는 PR #46으로 병합됐고 PR/exact-main
verification도 완료됐다. DiskMap product version은 여전히 `0.1.0`/`Unreleased`다. exact
PR/main artifact·Pages 표는 [D3 explorer workthrough](workthrough/2026-09-02-diskmap-explorer-workbench.md)에
중앙화했다.

### 완료: DiskMap D3 explorer UX — merged GUI evidence (2026-09-02)

PR #45는 merge commit `0688e44fa99d1ec69aba0c9bf9995a4a857fea9e`로 `main`에 병합됐다. PR
workflow [`33607634973`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33607634973)와
exact-main workflow [`33608884643`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33608884643)는
required checks, Qt5/Qt6, ici, benchmark, report publication과 Merge Gate를 확인했다. 이
PR #45 material은 D3 Qt-free core projection의 historical evidence다. 그 core slice는
metric projection, deterministic node key/issue, conjunctive filter, visible
children/largest-files ordering과 incomplete·cycle·depth·mount·scanner-filtered provenance를
제공하며, PR #45의 merge/remote/main evidence는 해당 시점의 기록으로 보존한다.

역사적 `feat/diskmap-explorer-ui` 구현은 그 projection을 실제 GUI workbench로 연결했다.
`MainWindow`, treemap, sortable table은 하나의 shared immutable scan document를 읽고, 모든
활성화·breadcrumb 이동은 `NodeKey`로 라우팅된다. 두 view는 current root, filters, metric을
공유하고 table은 recursive largest-files projection도 제공한다.

[PR #46](https://github.com/jihoon22-lee/toy-projects/pull/46)은 이 GUI 구현을
`0cdd63953179a1dc885ed660e955b399d54243b7`로 `main`에 병합했다. PR workflow
[`33627322683`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33627322683)와 exact-main
workflow [`33628585439`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33628585439)는
모두 green이며, PR sticky marker/links와 PR/main artifact·Pages byte-identical 검증은
[canonical D3 workthrough](workthrough/2026-09-02-diskmap-explorer-workbench.md)에 기록했다.

GUI는 name/path search, type/size/age/state filters, logical/allocated/reclaimable metric 선택,
largest-files view를 제공한다. metric 설명은 logical entry bytes, identity별 allocated bytes,
known hard-link ownership이 있을 때만 확정되는 reclaimable bytes를 구분하고, sparse file,
hard-link overlap, unreadable/incomplete subtree, cycle/depth/mount boundary와 scanner filtering의
불확실성을 보존한다. unknown/non-additive physical value를 확정값처럼 합산하지 않는다.

Rescan은 가장 깊은 유효 `NodeKey` trail과 selection을 복원하고, 사라지거나 identity가 바뀐
entry는 유효한 ancestor로 fallback한다. 이전 generation의 result/progress와 generation
metadata mismatch는 폐기한다. scan-time interaction은 freeze되며, atomic progress state의
수명은 late worker callback이 안전하게 소진될 때까지 유지된다.

로컬 native evidence는 Qt 5.15.18과 Qt 6.10.2의 clean full qmake build(`-Werror`) 및
`make check` `11/11` PASS다. Focused QtTest는 MainWindow `29`, TreemapWidget `12`,
NodeTableModel `11`이다. public ici `v0.10.2` asset SHA-256은
`8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4`이며, deep no-cache는
`WARN` (`10 PASS / 2 WARN / 0 FAIL / 0 ERROR / 2 SKIP`), TEM `4.95`, compile DB `16/16`
production units across `30` configurations, line/function/branch `96.1% / 99.1% / 83.4%`,
complexity max `14`, sanitizer `PASS`를 기록했다. HTML은 정확히 `499,265` bytes,
SHA-256 `9b624303b6191c6ead73079aa42636f318b495e807699601cd403a960cf059c3`이며 exact-title /
Zero-CDN checker가 통과했다.

로컬 `clang-tidy`와 `clazy`는 unavailable이고 ici의 C++ type 및 exact dead-symbol 분석은 아직
지원되지 않는다. heuristic duplicate WARN은 `6.42%`/`34` groups로 ici I4-3의 robust
duplicate backlog에 연결된다.
false-positive clone shape를 없애려고 product code를 contort하지 않는다. DiskMap은 `0.1.0` /
`Unreleased`를 유지하며, D1~D3 구현과 PR/exact-main evidence는 완료됐다. D4~D7
cleanup/trash/snapshot/release는 pending 범위다.

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

## 부록: 제품별 slice 증거 (README에서 이관)

아래는 이전에 `README.md`에 누적돼 있던 제품별 slice 구현·검증 기록이다. README는 지금
무엇을 하는 저장소이고 어떻게 쓰는지를 설명하고, 언제 무엇이 어떤 근거로 끝났는지는
이 문서와 `CHANGELOG.md`, `workthrough/`가 맡는다. 내용은 옮기면서 바꾸지 않았다.

### 제품 slice

#### envlens deterministic snapshot — merged evidence

[envlens 문서](envlens/README.md)의 현재 slice는 하나의 명시적으로 선택한 Python interpreter를
네트워크 없이 조사해 `envlens.snapshot/v1` JSON으로 남기는 pure-Python CLI/library다. 고정된
`python -c` probe를 `shell=False`로 실행하고 implementation/version, executable/prefix,
platform/compiler, `sysconfig`, environment와 설치 distribution metadata를 수집한다.
`Requires-Python`, `Requires-Dist`, entry point, location과 distribution별 metadata error도
보존하며, 오류가 있는 distribution이 있어도 `collection.status = partial`로 나머지 결과를
확인할 수 있다.

Object와 unordered collection은 정렬하고, `captured_at`은 source identity와 분리한 명시적
UTC timestamp로 정규화한다. strict schema는 top-level source/environment/distribution/
collection 구조와 10,000 distribution, 4,096 mapping field, 100,000 nested-item, 65,536-character
string bound를 정의한다. 기본 redaction은 target/host home path를 `<USER_HOME>`으로 바꾸고,
token/password/API/access/private-key/auth/cookie/credential/secret/registry/repository 계열과
`_URL`/`_URI` 환경변수 값을 `<REDACTED>`로 바꾼다. URL userinfo와 token·API key 등 secret query
value도 distribution 문자열과 metadata error를 포함해 전부 scrub한다. CLI에는 unredacted 옵션이
없고, library의 명시적 `redact=False`는 통제된 환경에서만 사용할 수 있다.

Probe stdout은 8 MiB, stderr 보존은 64 KiB, 기본 timeout은 10초다. POSIX process group 또는
Windows process group으로 descendant cleanup을 bounded하게 수행해 상속된 pipe 때문에 무한히
기다리지 않으며, malformed/duplicate/non-finite protocol JSON, nonzero/timeout/oversized probe,
잘못된 interpreter와 출력 대상 오류는 traceback 없이 exit `2`로 실패한다. Snapshot file은 같은
directory에 atomic replacement하고 POSIX mode `0600`을 사용하며 symlink/special file과 선택한
interpreter(기존 hardlink alias 포함) 덮어쓰기를 거부한다. 이는 sandbox가 아니므로 대상 interpreter는
현재 사용자 권한으로 실행된다.

2026-09-03 현재 local Python 3.10에서 CLI/I/O/probe·process/redaction/normalization-schema
`50/50` tests, Ruff check/format, strict mypy(6 source modules)가 통과했다. released ici
`v0.10.2` local deep 검증도 14 total engines에서 `13 PASS / 0 WARN / 0 FAIL / 0 ERROR /
1 compile_db SKIP`로 PASS했으며, TEM `5.00`, line/function/branch
`93.0% / 100.0% / 84.6%`, complexity max `13`, cycle/sanitize `PASS`였다. `envlens/ici.toml`은
test/coverage, TEM `≥ 4.0`, branch `≥ 80%`, function `≥ 90%`의 intended quality contract를
기록한다. Path-aware manifest와 Python 3.10/latest quality matrix가 연결됐고, [PR #50](https://github.com/jihoon22-lee/toy-projects/pull/50)은
`c307ac1ab01e12e4ac81a34623eb669da0e43641`로 병합됐다. Exact-main
[run `33698248293`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33698248293)도 성공했으며,
EnvLens report와 package/Page byte evidence는 [EnvLens workthrough](workthrough/2026-09-03-envlens-snapshot.md)에
중앙화했다. 제품 버전은 `0.1.0`/`Unreleased`로 유지한다. 두 snapshot diff,
dependency/wheel compatibility, project import와 runtime smoke 및 release boundary는 E2~E4 후속 범위다.

#### Quality Zoo known-answer corpus and candidate consumer

Quality Zoo keeps intentionally defective, small scenarios outside the user-facing products and
checks the complete ici report contract: schema, observed status, evidence, locations, expected
findings, and forbidden findings in nearby clean code. Its registry is the dependency-free
[`quality-zoo/manifest.json`](quality-zoo/manifest.json), deliberately JSON rather than a TOML
parser so the runner stays standard-library-only and works on Python 3.10. Scenario projects may
still contain `ici.toml`; ici reads that file, while the runner only validates its location and
invokes a fixed argv contract.

Local runs use an explicit executable path through `ICI_BIN` (or `--ici-bin`). Candidate intake
accepts a checksum- and provenance-bound ZIP and, when authenticated provenance is being recorded,
an optional directory containing exactly five API snapshots: `artifact.json`, `candidate-run.json`,
`gate-check.json`, `gate-job.json`, and `gate-run.json`. The intake rejects unsafe archive members,
path escapes, symlinks, digest/version mismatches, and identity mismatches; the detailed threat
model and command examples are in the [Quality Zoo README](quality-zoo/README.md).

The first local candidate consumer run used ici candidate workflow [33689056008](https://github.com/jihoon22-lee/ici/actions/runs/33689056008),
source `7872a7b80899cbd3d40d92d18e7920cd7e2283e7`, artifact `9869395069`, ZIP SHA-256
`640e50ecf5b099174c16f1ef5d2b5b87945329711e96f926d94c3cc04109081e`, and candidate `ici.pyz`
SHA-256 `53fc75f0a073a74689babfe9ef8a4b2378995002d7d563bdc52da548fdbb9ee8` (version `0.10.2`).
Authenticated API evidence validation passed. `python.dead-private-function` had contract `PASS`
with observed suite `WARN`, exactly one matched finding, and no clean-counterpart false positive.
PR #49 and its exact-main run completed Q0 remote acceptance: [PR run
`33693241255`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33693241255)
passed, exactly one sticky comment contained exactly one marker and three product
HTML links at that time, the PR merged as
`ed5fea2e881da77ac95482cf665e4e40bfe172f1`, and [exact-main run
`33694452357`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33694452357)
passed. The later EnvLens merge exact-main [run `33698248293`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33698248293)
also passed all jobs, the main publisher, and `Merge Gate`; the PR-only publisher was
skipped as expected, and the Quality Zoo artifact was `9872561713`. Package version alone is not an expectation selector: released ici `v0.10.2` digest
`8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4` reports legacy `MEASURED`/`high`,
while candidate digest `53fc75f0a073a74689babfe9ef8a4b2378995002d7d563bdc52da548fdbb9ee8` reports
provenance-aware `ESTIMATED`/`medium`, despite the same package version. Schema-2 `scenario.json`
therefore selects a full strict schema-1 expectation by exact executable SHA-256; unknown digests
fail closed. Ordinary CI uses the released expectation, and candidate validation uses the candidate
expectation. This candidate validation does not bump a toy version or create a release. Q0 remote
acceptance is complete; Q1~Q5 corpus work remains pending. The exact local evidence and Q0 remote
closeout are captured in the [Quality Zoo workthrough](workthrough/2026-09-03-quality-zoo-contract.md);
the post-merge four-project evidence is in the [EnvLens workthrough](workthrough/2026-09-03-envlens-snapshot.md).

현재 consolidated worktree의 candidate registry는 released six scenarios에 ThreadSanitizer
2개, C++ build/quality 3개, Python depth 3개, security/resource/correctness 1개,
Make-to-ELF/integration 1개를 더한 16개다. accepted non-stable candidate digest를 사용한
로컬 contract run은 `15/16 PASS`였고, 유일한 미통과는 이 호스트에 `clazy`가 없어 Qt lifetime
scenario를 실행하지 못한 경우다. 새 C++ family와 Python family는 각각 focused `3/3 PASS`다.
이 실행은 ici `ccb2067c656492c549dae8f4abc198a69ea013c2`의 non-stable candidate
`23d9922b94b2ba34ab8884cd2d39c8eda358ccb32d0925af5c0a3d52a7ddc893`를 사용했고,
나머지 15개 contract에는 error가 없었다.
이 확장은 candidate-only corpus이며, 해당 worktree에 대한 remote PR/Pages acceptance와
version/release 변경은 아직 주장하지 않는다.

#### LogLens investigation workbench — local implementation

LogLens의 현재 로컬 slice는 로그를 읽고 끝나는 뷰어가 아니라, 한 행의 원본 증거와 분석
결과를 함께 보존하는 investigation workbench다. 제품 버전은 `0.1.0`/`Unreleased`를
유지하며, 이 절은 아직 consolidated toy PR의 원격 CI·Pages 결과나 release를 의미하지
않는다.

- `loglens.triage/v1`에 highlight rule, bookmark, source-line annotation을 저장한다.
  규칙은 literal/whole-row highlight, priority, 안전한 색상 표현을 가지며 CRUD·reorder와
  legacy `loglens.triage/v0`의 명시적 migration을 지원한다. 저장은 bounded strict JSON과
  atomic replacement를 사용한다.
- Investigation dock의 Record 탭은 선택한 행의 parsed fields, parse diagnostics, source
  위치, input/omitted bytes와 raw evidence를 함께 보여준다. 선택 행 export는
  `loglens.selection/v1` JSON으로 스트리밍하며 raw/message/source의 base64 필드로
  malformed/비 UTF-8 바이트도 손실 없이 보존한다. 출력은 누적 16 MiB로 제한되고 atomic
  save를 거치며, 현재 연 source 로그를 대상으로 선택할 수 없다.
- Timeline은 bucket을 half-open `[begin, end)` 범위로 선택한다. 선택 범위는 기존 filter와
  raw search에 추가로 적용되며, 두 범위를 baseline/comparison으로 지정하면 level/source/
  normalized-pattern의 new-pattern·rate-spike 신호와 request/thread 같은 raw correlation을
  deterministic하게 계산한다. 결과의 first/last source line을 선택하면 원래 표 행으로
  이동한다.
- GUI는 Qt5/Qt6에서 같은 core 계약을 사용한다. focused investigation suite는 timeline
  mouse interaction, UTF-8 highlight paint, triage persistence, byte-preserving export, diagnostics,
  window comparison/navigation, empty/error paths를 함께 확인한다. 로컬 실행에서 두 Qt
  major의 focused CTest는 각각 `18/18`로 통과했다. 최종 exact ici candidate deep local run은
  test engine `18/18 PASS`, line/function/branch `90.5% / 96.1% / 78.0%`, TEM `4.81`을
  기록했고, native TSan partition은 `41/41 PASS`였다. 원격 PR acceptance는 별도 gate이며,
  이 수치는 remote PR/Pages 또는
  stable release evidence가 아니다.

#### AbiLens ELF/ABI inspector — local implementation

`abilens`는 Linux build artifact를 실행하지 않고 직접 검사하는 dependency-free C++20 CLI다.
제품 버전은 `0.1.0`/`Unreleased`이며, 현재 구현 범위는 다음과 같다.

- ELF identification/header와 table bound를 bounded integer arithmetic으로 먼저 확인하고,
  shell 없는 GNU `readelf` capability probe 및 C-locale evidence 수집을 수행한다. timeout,
  signal, stream bound, 비 GNU/파싱 불가 Binutils는 incomplete/tool error로 닫힌다.
- `DT_NEEDED`, `DT_RPATH`, `DT_RUNPATH`, GLIBC/GLIBCXX/CXXABI 요구 버전을 정규화하고,
  ELF class/endian/type/machine, dynamic/static, stripped 상태를 포함한 deterministic
  `abilens.report/v1`을 생성한다. 두 ELF 또는 두 report는 `abilens.diff/v1`로 비교할 수
  있고, class/machine/ABI floor/RPATH/forbidden dependency policy를 적용할 수 있다.
- strict hand-written JSON reader는 schema/version/status/namespace/tool, duplicate key와
  ABI tuple/maximum 일관성을 fail closed로 검증한다. Make output은 ownership marker가 있는
  tree만 clean 대상이 되며, release·coverage·ASan/UBSan·TSan output은 서로 격리된다.
- ELF header와 GNU `readelf` evidence는 한 번 연 descriptor를 공유한다. 수집 전후
  device/inode/mode/size/mtime/ctime과 원래 경로 identity가 달라진 일반적인 path replacement와
  in-place mutation은 report를 버리고 `tool-error`로 닫힌다. Linux `/proc/self/fd`가 필요하며,
  권한 있는 공격자가 내용을 바꿨다가 동일 metadata와 함께 복원하는 경우까지 보장하는
  cryptographic snapshot 계약은 아니다.

정확한 ici candidate deep 실행은 exit `0`, suite `WARN`, `11 PASS / 3 WARN / 0 FAIL /
0 ERROR / 5 SKIP`, TEM `4.75`, complexity maximum `14`/194 functions, duplication `3.71%`였고
sanitizer·ThreadSanitizer·build·binary compatibility·integration은 모두 measured `PASS`였다.
테스트 2/2는 통과했지만 coverage는 이 실행에서 estimated evidence이므로 threshold 근거로
사용하지 않는다.

현재 AbiLens 설명과 local verification 기록은 [AbiLens README](abilens/README.md)와
[hardening workthrough](abilens/workthrough/2026-09-04-abilens-hardening.md)에 있다. 이
문서들은 remote PR/Pages acceptance와 stable release를 아직 주장하지 않는다.

#### buildscope B0 hybrid skeleton — release-backed evidence

[BuildScope 문서](buildscope/README.md)에 producer/consumer contract와 repository 밖 scratch
빌드 명령을 정리했다. Python backend는 `compile_commands.json`을 shell 실행 없이 64 MiB 및
100,000-entry bound 안에서 읽고, raw `arguments`/`command`를 보존한 deterministic
`buildscope.snapshot/v1` JSON을 만든다. C++20/Qt CLI와 GUI는 이 versioned contract를
검증하고 소비하며, CMake는 `AUTOMOC`·`AUTOUIC`·`AUTORCC`와 compile DB export를 실제로
활성화한다.

2026-09-01 로컬 실측에서 Qt 5.15.18과 Qt 6.10.2 build가 각각 CTest 4/4를 통과했고,
Python producer → C++ consumer hybrid test도 포함됐다. 같은 Python 3.10 interpreter의
`python -m` probe에서 `pytest`/`coverage`/`mypy` capability가 READY였고, mypy 실제 argv는
C++ roots를 제외한 `python` root만 받아 rc0이었다. 공개 ici v0.7.1 release asset의 cold
isolated verify는 suite WARN, `12 PASS / 1 WARN / 0 FAIL / 0 ERROR / 0 SKIP` 및 `9/9` tests,
TEM `5.00`, line/function/branch `96.3% / 100.0% / 86.8%`, complexity `14 PASS`, compile DB
`4/4` production units·`13` configurations, 총 `63.37s`였다. WARN은 C++ type 미지원 하나뿐이다.
D11/I5 interpreter/tool capability 경로도 release에서 재검증되어 고정됐다.

ici [PR #109](https://github.com/jihoon22-lee/ici/pull/109)의 sticky report와 두 Pages 검증이
완료됐고, exact `main` `b87afba`의
[CI run `33419851128`](https://github.com/jihoon22-lee/ici/actions/runs/33419851128)과
[v0.7.1 release run `33420348698`](https://github.com/jihoon22-lee/ici/actions/runs/33420348698)이
성공했다. [공개 v0.7.1 release asset](https://github.com/jihoon22-lee/ici/releases/tag/v0.7.1)은
9개 asset을 제공하며 `sha256sum --check ici.pyz.sha256`가 통과했다. B1의
compiler/configuration normalization과 B2 Qt explorer는 아래에 별도로 기록한다. ici I3 target
comparison은 B3 cross-repository comparison으로 완료됐다.

#### buildscope B2 normalized Qt explorer — 0.3.0

[BuildScope 문서](buildscope/README.md)의 B2는 `buildscope.snapshot/v2`를 native Qt model과
explorer UI로 연결한다. normalized source 아래에 configuration을 묶는 tree를 제공하고,
`missing > stale > present > unknown` 우선순위로 source 상태를 집계한다. 검색은 source /
status / target / compiler / standard / configuration / define / include를 대소문자 구분 없이
재귀적으로 찾으며, 선택 즉시 overview와 define/include/diagnostic 상세 표를 채운다.

Command 탭은 structured argv를 compact JSON array로 렌더링하고 공백·따옴표·빈 인자를
보존한다. 원본 `command` 문자열은 별도 raw 영역에 남기므로 두 표현의 의미를 혼동하지 않는다.
v1 raw projection도 계속 읽을 수 있다. 상태 표시는 네 개의 local SVG resource를 Qt resource에
내장해 CDN이나 외부 네트워크에 의존하지 않는다.

`BUILDSCOPE_BUILD_BENCHMARKS=ON` opt-in benchmark는 100,000 entries / 25,000 source groups를
검증한다. Qt6 측정은 model build `45 ms`, `unit_024999` filter `1,071 ms`, peak RSS
`132,612 KiB`, budget `10,000 ms`로 PASS했다. 로컬 Qt 5.15.18과 Qt 6.10.2 전체 CTest는
각각 `6/6` PASS였다. 공개 `ici v0.8.0` 검증은 `46/46` tests, line/function/branch
`94.5% / 99.5% / 83.9%`, TEM `4.98`, compile DB `8/8` production units·`19` configurations를
기록했다.

B2 remote integration도 완료됐다. PR #32 head `41472a66e69477fde7a71fe78c3ae9e47ba7f292`는
main에 `51a3480677a740475857dd92dd5a5a9373a287a4`로 squash-merge됐고, [PR run
`33454143021`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33454143021)의 16개
check가 모두 성공했다. [sticky comment #5486637533](https://github.com/jihoon22-lee/toy-projects/pull/32#issuecomment-5486637533)는
marker 1개와 project link 3개를 포함한다. PR BuildScope report는 `46/46`, branch `84.2%`,
TEM `4.98`, compile DB `8/8` production units·`19` configurations, complexity max `14`/`196`
functions였다. PR 100k benchmark는 model `53 ms`, filter `1,518 ms`, summary JSON SHA-256
`af7162b7603d558da6e7bc49d7bf5a80f546f412b7076992ded5e15739024db7`였고, exact-main run
`33454634202`도 성공했다(Report job은 expected skipped). main benchmark는 model `58 ms`, filter
`1,527 ms`, summary JSON SHA-256
`247c0b33095e0a09e97a289af556eae30f47f4f5c4136c530e3d6ca0018ae2d2`였다.

세 hosted report는 모두 HTTP 200·`text/html`, 올바른 title, 외부 resource reference 0개였다.

| Project | Bytes | SHA-256 | Title |
|---|---:|---|---|
| [BuildScope](https://jihoon22-lee.github.io/toy-projects/buildscope/pr/32/) | 562,234 | `f15d18fe42ac172385e682ceb49e4b6d6f1d9bbfcc0ead301c11d1ee049c4c82` | `ici Verification Report — buildscope` |
| [diskmap](https://jihoon22-lee.github.io/toy-projects/diskmap/pr/32/) | 311,846 | `752f07251bc38285ea1633f5df879985131963e4b99f90532722eaedc9be1802` | `ici Verification Report — diskmap` |
| [loglens](https://jihoon22-lee.github.io/toy-projects/loglens/pr/32/) | 446,791 | `7b2669fb7de82ada30bfdf28a2d82533f5566ad92779ea08c90528e188ea582b` | `ici Verification Report — loglens` |

#### BuildScope 0.4.0 — historical include-resolution implementation and evidence

BuildScope B3 구현에 대한 historical record다. PR #34에서 검증되어 `main`에 들어간 `0.4.0`
main candidate였으며, 당시 제품 버전 `0.4.0`은 B5 hybrid release integration 전까지 release하지
않았다. 옵션을 생략하면
기존처럼 normalized `buildscope.snapshot/v2`를 만들고, `--schema-version v1`은 raw compatibility
projection을 유지한다. `--include-analysis estimate|compiler`는 명시적으로 `v3`를 만들며,
`--schema-version v3`만 쓰면 `estimate`가 기본이다. v3는 root/entry/analysis/edge/search/
diagnostic 필드를 모두 담는 strict self-contained schema이며, 분석이 불가능한 entry도
`evidence: unavailable` warning으로 같은 계약 안에 남긴다.

`estimate`는 bounded source scan만 수행한다. `compiler`는 shell 없이 직접 실행되는 승인된
system GCC/Clang driver에 `-E -H`를 적용하고, positive option allowlist·argv/trace/edge/source
limits·unit/time budget을 지키며 response file, stdin, extra input, plugin/linker escape를
거부한다. compiler trace가 실제 resolved edge를 결정하고 source scan이 parent:line 위치를
보강하므로 `compiler-measured`와 `source-scan` location evidence가 별도로 보존된다. 각 edge는
current/quote → include/framework → system → after 순서의 후보와 selected path, same-basename
alternatives, project/vendor/generated/system/missing/unresolved classification을 기록한다. strict
v3 consumer는 `resolved`가 선택된 단 하나의 후보와 일치하는지, `alternatives`가 distinct
existing unselected 후보와 일치하는지 검증하며, 중복 search path에서는 최초 후보만 `selected`로
표시한다.

Qt **Include Edges** 탭에서는 provenance, ordered candidates, collision alternatives, directive
위치와 replay command를 확인할 수 있다. Edge를 선택하면 상세가 열리고, 더블클릭 또는 **Open
Source Location**으로 parent source 위치를 열며, **Compilation Command**로 원래 command 탭으로
돌아간다. PR 전 historical local candidate 검증에서 Python 3.10 pytest `57/57`, Ruff check+format `14 files`,
mypy `11 source files`, Qt5 5.15.18과 Qt6 6.10.2 Release CMake/CTest 각각 `6/6` PASS를
확인했다. 현재 compiler execution/sanitization/process bounds는 새
`buildscope/python/buildscope/compiler_replay.py`로 분리하고, `include_analysis.py`는 source
scan/edge assembly/trace interpretation을 담당한다. checksum이 확인된 ici v0.8.0 release
asset을 사용한 no-cache local public-release validation은 `Suite WARN`(검증 통과), engines
`11 PASS / 2 WARN / 0 FAIL / 0 ERROR / 0 SKIP`, line `PASS`(`5,151` total / `4,591` code /
`3` comment / `557` blank across `25` files), lint `PASS`, compile_db `8/8` production units·`19`
configurations·`0` failures/warnings, tests `63/63`, line/function/branch `92.6% / 98.9% / 79.1%`,
TEM `4.94`, complexity `PASS` (max `14` / `251` functions / `0` issues),
sanitize/security/resource/cycle/dead/exception `PASS`, duplication `11.65%` (raw display `11.7%`,
`78` groups, `179` findings), total `34.71s`를 기록했다. WARN은 type(C++ unsupported)와 dup뿐이다.
`/tmp` HTML은 `851,656` bytes, SHA-256
`07d25971e04ed6a4aece36724ce8cf5e3c0548b7c382941a810454d8521c3e34`, 정확한 title
`ici Verification Report — buildscope`, external refs `0`이었다. B3 PR/remote Pages evidence와도
별개다. 최종 benchmark는 `100,000` entries/`25,000` sources, model `61 ms`, filter `1,126 ms`,
budget `10,000 ms`, correctness `true`였고, `buildscope-0.4.0-py3-none-any.whl`에
`compiler_replay.py`와 v3 schema가 패키징되어 schema validation `PASS`였다. 이 local evidence는
아래 B3 원격 evidence와 별개다.

**B3 remote evidence (2026-09-01):** [PR #34](https://github.com/jihoon22-lee/toy-projects/pull/34)의
feature head `c3835cd4b0c859c38ae0f4afbdb20aae970515dc`에 대한 PR CI
[run 33459294092](https://github.com/jihoon22-lee/toy-projects/actions/runs/33459294092)은 `Merge Gate`와
`Publish Reports & Sticky Comment`를 포함한 16개 check를 모두 성공시켰다. [sticky comment
#5487386460](https://github.com/jihoon22-lee/toy-projects/pull/34#issuecomment-5487386460)는 marker
정확히 1개와 project link 정확히 3개를 포함한다. BuildScope report는 `WARN`(`11 PASS / 2 WARN /
0 FAIL / 0 ERROR / 0 SKIP`), TEM `4.94`, tests `63/63`, line/function/branch `92.7% / 98.9% /
79.5%`를 기록했고, diskmap과 loglens는 각각 `PASS`, TEM `4.92`와 `4.80`이었다.

PR BuildScope benchmark는 `100,000` entries / `25,000` sources, model `118 ms`, filter `1,424 ms`,
budget `10,000 ms`, correctness `true`였다. 세 PR Pages report는 모두 HTTP 200 `text/html`, exact
title, external attributes/CSS references `0`으로 독립 확인됐다.

| Project | URL | Bytes | SHA-256 | Title |
|---|---|---:|---|---|
| BuildScope | [buildscope/pr/34](https://jihoon22-lee.github.io/toy-projects/buildscope/pr/34/) | 858,143 | `e0b9c9ece1fb7268aa519bd0a4c62fd3da7c44a52b2efe6121393474d3ad36d4` | `ici Verification Report — buildscope` |
| diskmap | [diskmap/pr/34](https://jihoon22-lee.github.io/toy-projects/diskmap/pr/34/) | 311,847 | `8a6b01544b99eee6f0c2b95758f81395032f2b67a7c1a600879447cf7fb5f3bf` | `ici Verification Report — diskmap` |
| loglens | [loglens/pr/34](https://jihoon22-lee.github.io/toy-projects/loglens/pr/34/) | 446,786 | `1144759ef7e1b83ef7bd23f7bcfe9d02b05430a37af372350ec3b6e26d6c7ac7` | `ici Verification Report — loglens` |

PR #34 was squash-merged to `main` as
`9cce2699606e58ed67c3dac46f60dc7bf113bb60`, and its branch was deleted. Exact-main CI
[run 33459591250](https://github.com/jihoon22-lee/toy-projects/actions/runs/33459591250) and Dependency
Graph [run 33459594605](https://github.com/jihoon22-lee/toy-projects/actions/runs/33459594605) both succeeded
on that head; on push, PR publish was correctly skipped while applicable jobs and `Merge Gate` passed.

B3 implementation and remote evidence are complete, ici I3 cross-repository comparison is complete,
and B4 configuration diff implementation/local evidence is now on `main` after PR #36. B4 compares
raw `compile_commands.json` pairs without executing commands, normalizes relocation noise while
retaining ordered defines/includes and compiler/standard/language/target drift, and exports strict
`buildscope.diff/v1` JSON. Exit `0` means no visible drift, `1` means visible drift, and `2` means
invalid/untrusted input, policy, security, or export failure. Native C++/Qt diff consumption and the
Python-to-C++ hybrid fixture are covered by the local Release CTest evidence. Python is `83/83`,
Ruff check/format covers `19` files, mypy covers `15` source files, and the default Qt5 5.15.18
plus Qt6 6.10.2 Release CMake/CTest matrices are each `9/9` (`10/10` with the benchmark); the pure
`buildscope-0.5.0-py3-none-any` wheel contains snapshot v1/v2/v3 and
diff v1 schemas with no native extension. The public ici v0.9.0 uncached deep suite is historical
local evidence: `WARN` (11 PASS / 3 WARN, no FAIL/ERROR/SKIP) with `92/92` tests,
`93.5% / 99.0% / 76.7%` line/function/branch coverage, sanitizer PASS, compile DB `12/12`
production units and `27` configurations, and TEM `4.95/5.0`.

B4 remote evidence is complete on [PR #36](https://github.com/jihoon22-lee/toy-projects/pull/36): head
`ce64613263f0c4358579012aab135e0b23341a0e`, [run `33485837830`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33485837830),
and ici `v0.9.1` completed all `16/16` checks successfully. The BuildScope report was `WARN`
(`10 PASS / 3 WARN / 0 FAIL / 0 ERROR / 0 SKIP`), with lint WARN (49 warnings), `92/92` tests,
`93.5% / 99.0% / 77.3%` line/function/branch coverage, sanitizer PASS, compile DB `12/12`
production units and `27` configurations, and TEM `4.95/5.0`. The remote 100,000-entry /
25,000-source benchmark recorded model `65 ms`, filter `1,602 ms`, filtered sources `1`,
budget `10,000 ms`, and correctness `true`. [Sticky comment #5489976814](https://github.com/jihoon22-lee/toy-projects/pull/36#issuecomment-5489976814)
has exactly one marker and three hosted-report links.

| Project | Hosted report | Bytes | SHA-256 |
|---|---|---:|---|
| BuildScope | [buildscope/pr/36](https://jihoon22-lee.github.io/toy-projects/buildscope/pr/36/) | 1,319,378 | `5dc517d3ec8324cb1aedb6e611120ac9ae2951e27851cbc6b0e28303d02c5d43` |
| diskmap | [diskmap/pr/36](https://jihoon22-lee.github.io/toy-projects/diskmap/pr/36/) | 337,554 | `39a52d1e3d5b9eed6bbc1ec5253f1bf837deb4a7bb33ed2f2ef3b32df0f90e0e` |
| loglens | [loglens/pr/36](https://jihoon22-lee.github.io/toy-projects/loglens/pr/36/) | 492,746 | `0480f07aea266c0777403ac37ebf90d4acd10f84b04e49aa2a9ccf99a6153007` |

All three Pages responses were HTTP 200 with the exact `ici Verification Report — <project>` title
and zero external resource references. Historical boundary note from PR #36: B4 implementation plus
remote/hosted/merged-main evidence was complete on `main`, while the `0.5.0` product release artifact
and B5 hybrid release integration were pending/not started. The later release-boundary evidence is
recorded in the current section below.

PR #36 was squash-merged to `main` as
`590899a0a9430e9ce35162b301bfef5d7dfc78a4`, and its feature branch was deleted. Exact-main CI
[run `33488169769`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33488169769) completed
with all 14 prerequisite jobs and `Merge Gate` successful; the PR-only publisher was skipped as expected.
[Dependency Graph run `33488174425`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33488174425)
also succeeded on the same head.

#### BuildScope 0.5.0 release evidence — reproducible packaging and guarded publication

이 절은 구현된 B4를 배포 가능한 경계까지 연결한 BuildScope `0.5.0`의 release evidence를
기록한다. 2026-09-02 KST (`published_at` `2026-09-01T22:36:42Z`)에
[GitHub Release](https://github.com/jihoon22-lee/toy-projects/releases/tag/buildscope-v0.5.0) ID
`380863869`로 공개됐고 `immutable=true`, `draft=false`, `prerelease=false`다. 구현과 계약의
상세는 [BuildScope README](buildscope/README.md)를 참조한다.
`tools/build_standalone.py`는
Python/CMake/ici 버전 surface를 일치시키고 고정 zip metadata·정렬된 payload·schema inventory로
재현 가능한 `buildscope.pyz`를 만들며, direct test는 두 번의 산출물이 byte-identical인지와
`buildscope.pyz --version`이 `buildscope 0.5.0`을 보고하는지 확인한다.
`CMakeLists.txt`의 install rule은 native `buildscope-cli`/`buildscope-gui`, 문서, examples, schemas를
하나의 Linux bundle layout으로 설치한다. [B5 quickstart](buildscope/docs/quickstart.md)는
`buildscope.pyz` 또는 `buildscope-0.5.0-py3-none-any.whl`로 compile DB를 JSON으로 만들고
native CLI/GUI에서 소비하는 흐름, CMake/qmake examples와 qmake capture 제한을 설명한다.

2026-09-01의 historical local B5 실측은 public ici `v0.10.0` asset을 literal
`ICI_PYZ_SHA256=6d5f8c008b3b5393a61b2c1a418124eb66393c9eaab0abbb7d1c7922162bed9b`로 고정하고,
`ICI_PYTHON=/tmp/toy-b5-py310/bin/python` tool environment에서 deep/no-cache로 실행했다. 결과는
`Suite WARN`, 14 engines = `11 PASS / 3 WARN / 0 FAIL / 0 ERROR / 0 SKIP`, tests `96/96`,
line/function/branch `93.4% / 99.0% / 76.7%`, TEM `4.95`, compile DB `12/12` production units·`27`
configurations·`0` issues였다. Qt codegen은 exact input `3`, MOC `1`, UIC `1`, RCC `1`, Qt6
compile units `12`를 기록했다. HTML은 1,264,867 bytes / SHA-256
`4e12e77ee11f98b2d5bb146bd3d08252b088083726e6f11235e25a449471a565`, JSON은 2,873,207 bytes /
SHA-256 `ea5fce118e6edad8fa5af24c821663e4290805570377e9b7c190de8da1029612`이며 title은
`ici Verification Report — buildscope`, external refs는 `0`이다. 당시 로컬에는 `clang-tidy`와
`clazy`가 설치되지 않아 unavailable로 남았지만, 이 제한은 아래 원격 PR gate에서 보완됐다.

`.github/workflows/buildscope-release.yml`은 고정 annotated `buildscope-vX.Y.Z` tag의 peeled
commit이 exact `origin/main`과 green `Merge Gate`를 가리키는지 확인하고, Python `3.10/3.14`와
Qt `5/6` Release/CTest matrix, MOC/UIC/RCC generated-path 검사, Qt6 native bundle,
wheel/pyz-to-native-CLI handoff, ici v0.10.2 literal SHA/API digest 검증을 정의한다. 최신
`ci/check_buildscope_merge_gate.py`는 exact SHA의 `Merge Gate` 중 가장 최신 check-run ID를
고르고 GitHub Actions app, 완료/성공 상태를 요구한 뒤 Actions run의 ID, repository/head repository,
SHA, workflow name/path/event/status/conclusion과 canonical URL을 독립 검증한다. GitHub Release API의
`target_commitish`는 비교하지 않으며 annotated tag의 peeled SHA가 authoritative proof다. 예정된 top-level release assets는
`buildscope.pyz`, `buildscope.pyz.sha256`, pure `buildscope-<version>-py3-none-any.whl`, sdist,
`buildscope-ici-deep.{json,html}`, `buildscope-provenance.json`,
`buildscope-<version>-linux-x86_64.tar.gz`, `SHA256SUMS`의 정확히 9개다.

게시 단계는 softprops의 기존 release 갱신 경로를 사용하지 않는다. 인증된 paginated release-slot
검사가 fail-closed로 동작해 빈 슬롯에서만 direct private draft를 만들고, 기존 final release는
변경 없이 audit-only로 처리하며, 기존 draft·중복·모호한 슬롯은 중단한다. 새 draft는 고정 numeric
release ID를 끝까지 사용하고, 정확한 9개 경로를 `--clobber` 없이 fixed upload endpoint에
업로드한다. 생성 draft의 body는 repository/run/target SHA를 담은 terminal current-run owner marker로
끝나야 하며, POST가 모호할 때의 recovery는 그 marker와 exact tag/version, expected body digest를
가진 zero-asset private draft 하나에만 허용된다. workflow는 `RELEASE_NOTES.md`를 UTF-8 기준으로 한 번 normalize한 뒤
expected final body와 owner-marker가 붙은 expected draft body를 각각 고정 파일로 만들고, 두 body의
정확한 SHA-256을 계산한다. draft digest는 create/upload/prepublish/failure-report에서, final digest는
publish/final audit에서 반복 확인된다. 업로드는 numeric release ID, binary body, HTTPS/TLS, 20초 connect 및
300초 transfer bound, HTTP 201과 uploaded/id/size/digest 응답을 요구한다. bounded downloader가
draft asset을 fresh directory로 내려받은 뒤 manifest/sidecar, payload/archive와 schema bytes,
provenance, B5 JSON, HTML Zero-CDN, pyz version을 검사하고, ZIP은 `ZipFile` 이전에 bounded
EOCD/central-directory preflight를 거친다. 공개 직전 같은 draft를 재감사한다.
PATCH가 모호하면 동일 release ID를 재조회해 exact final은 성공, exact private draft는 재시도,
그 외 상태는 실패로 판정한다. write-token publish 단계에서는 다운로드한 원격 BuildScope pyz를
실행하지 않고 payload data로만 검사한다. 공개 후에도 9개 final asset을 다시 내려받아 모든 파일을
현재 `dist`와 byte-compare하며, 기존 final audit-only 경로에서도 이 비교를 수행한다. 다운로드
후에는 ID/tag별 release metadata와 asset records 및 peeled tag를 다시 fetch해 안정성을 확인한다.
실패한 current-run-owned draft는 명시적 수동 검토를 위해 보존하며 remote draft를 자동 삭제하지
않는다. empty-slot failure-report 단계는 create output ID가 유실돼도 paginated listing에서 exact
current-run-owned zero-asset draft 중 expected body digest까지 일치하는 항목만 복구해 보고·보존한다.
tag-only release workflow [run `33566464110`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33566464110)은
exact `main` `fda8b5fb068b68c04c8c40e297812fbe79cee3da`에서 성공했다. annotated tag object
`dcaaf83a5842f6d7fc6c47e3b212e26b9528c342`는 같은 exact `main` SHA로 peel됐고,
[Merge Gate job `100050176790`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33565542193/job/100050176790)은
[run `33565542193`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33565542193)에서 성공했다.
최종 release body SHA-256은
`9e58639c280655bf50b510ef676bb3e5f458cf2021c3c6c6b24c3b625945dd3b`이고, fresh download 기준
정확히 `9`개 asset을 audit했다.

현재 dependency-free CI helper discovery suite는 Python `3.10`/`3.14` 각각 `145/145` PASS이며,
`actionlint`, Ruff check/format, mypy도 통과했다. 이는 현재 구현의 사전 검증 기록이며 새 PR, tag,
release를 의미하지 않는다.

#### Release-boundary verification

현재 deep 검증은 public ici `v0.10.2`를 literal SHA-256
`8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4`로 고정한다.
[PR #38](https://github.com/jihoon22-lee/toy-projects/pull/38)의 head
`3ba645eae5181698e1272729dddaa8a72189b067`는 [run `33545168957`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33545168957)에서
통과했고, [sticky comment #5494648837](https://github.com/jihoon22-lee/toy-projects/pull/38#issuecomment-5494648837)에
세 HTML report link가 게시됐다. PR은 `069a3a86c0164a1d2a88710f9c3c48a398c8087e`로
squash-merge됐으며, exact-main [run `33546046566`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33546046566)도
통과했다.

정확한 `main` Pages publisher는 [PR #39](https://github.com/jihoon22-lee/toy-projects/pull/39)의 head
`b861ff5b4cc0314aae5ec9f6dab905648233216d`에서 [run `33548626203`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33548626203)와
[sticky comment #5499184834](https://github.com/jihoon22-lee/toy-projects/pull/39#issuecomment-5499184834)를
통해 검증됐다. PR은 `c80e922f0d0911019cfa8b5c67a8b654c556a68c`로 squash-merge됐고,
exact-main [run `33549475034`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33549475034)도
통과했다.

Exact-main run `33549475034` 시점의 stable `main` Pages 응답은 모두 HTTP 200, exact title,
Zero-CDN이며 그 실행의 local artifact와 byte-match됐다.

| Project | Hosted report | Bytes | SHA-256 |
|---|---|---:|---|
| BuildScope | [buildscope/main](https://jihoon22-lee.github.io/toy-projects/buildscope/main/) | 1,349,088 | `dd7ec9b49281875f812b9a9c6b4e18a028051936a37441f19d907098c05dcc65` |
| diskmap | [diskmap/main](https://jihoon22-lee.github.io/toy-projects/diskmap/main/) | 339,929 | `1bd6ebdce2206e4538fb28c20c17f2d504f1a160e15f5d8d4e2923b15b399e65` |
| loglens | [loglens/main](https://jihoon22-lee.github.io/toy-projects/loglens/main/) | 495,237 | `11e4c78eed957e237bcea8ec5ea64c3894751eafbe639c454e47ab0acd70df96` |

`0.5.0`은 B3 include explanation, B4 semantic configuration diff, B5 hybrid packaging/integration을
한데 묶은 첫 usable BuildScope release boundary로, 위 GitHub Release를 통해 stable published됐다.
최종 deep report와 9개 asset audit의 수치는 [CHANGELOG.md](CHANGELOG.md)와
[ROADMAP.md](ROADMAP.md)에 고정했으며, B5의 historical local candidate 기록이나 ici pin 변경만으로
다음 버전을 bump하지 않는다. `0.5.0` 이후의 기능은 다음 comparable checkpoint가 마련될 때까지
`Unreleased`에 기록한다.

### diskmap slice

#### diskmap D1 identity-safe scan — Slice 2

diskmap은 파일·디렉터리·심볼릭 링크와 링크 대상, 하드 링크를 서로 혼동하지 않도록
물리적 identity와 메타데이터를 보존한다. 사용자는 이후 탐색·정리 기능에서 “링크 자체”와
“대상 파일”을 구분하고, 권한 오류로 불완전한 디렉터리를 확정된 용량처럼 보지 않게 된다.

개발자 관점에서는 `RealFsSource`가 POSIX `lstat`/`stat` 결과와 `*_known` 플래그를
`DirEntry`에 담고, scanner가 `std::filesystem::path`와 함께 `FsNode`로 전달한다.
`follow_symlinks=true`인 디렉터리는 물리적 `FileIdentity` 방문 집합으로 cycle을 차단하며,
이미 방문한 target은 노드로 남기되 다시 확장하지 않는다. target identity를 확인할 수 없는
followed directory는 안전하게 `complete=false`와 오류를 남긴다. listing/iterator 오류도 같은
방식으로 보존하고, stat의 `metadata.logical_size`와 자식 합계인 `FsNode::size`는 별도 값이다.

명시적으로 선택한 root가 regular file이면 디렉터리로 열려고 실패하지 않고, complete한 한
노드 scan으로 반환한다. 이 노드는 logical/allocated/reclaimable facts와 `files_scanned=1`을
그대로 가진다. 선택한 root가 symlink이면 `follow_symlinks`와 무관하게 target을 dereference
하며, target이 file이면 leaf로 남긴다. 이 옵션은 root 아래 descendants에만 적용된다. 깨진
root symlink target은 root를 incomplete로 남기고 오류를 `ScanResult.errors`에도 포함한다.

`FsNode::size`는 directory entry마다 logical bytes를 세고, `allocated_size`는 non-directory
entry data를 유효한 물리 identity별로 한 번만 합산한다. directory 자체의 filesystem metadata
block은 이 값의 범위가 아니다. `reclaimable_size`는 해당 subtree가 known hard-link reference를
모두 소유할 때만 known으로 계산하며, symlink target alias는 소유 reference로 세지 않는다.
불완전한 subtree·unknown allocation/link-count는 조용히 0으로 바꾸지 않고 aggregate의
`*_known=false`로 전파한다. 유한한 `max_depth`로 잘린 directory도
`complete=false`, `scan depth limit reached`로 남기므로 allocated/reclaimable total을
확정값처럼 보이지 않게 한다. logical aggregate도 `FsNode::logical_size_known`으로 정확성을
보존하며, `uint64_t` overflow에서는 최댓값으로 포화(saturate)하고 해당 flag를 false로
설정한다. 따라서 cleanup 기능은 확정된 값과 추정할 수 없는 값을 구분할 수 있다.

D1 Slice 2의 검증은 fixed-identity fake source와 실제 POSIX temporary filesystem을 함께
사용한다. 경로 component의 공백, cycle/back-edge, hard-link 중복·reclaimability, symlink
alias, identity 없는 followed directory, allocation/link-count unknown 및 aggregate overflow를
검증하는 9개 qmake test binary가 등록되어 있다. qmake의 static consumer에
`PRE_TARGETDEPS`를 연결해 테스트가 최신 archive를 다시 링크하도록 했고, stale `.gcda`/`.gcno`
혼입으로 coverage가 낮게 보이는 재현도 제거했다.

최종 D1 Slice 2는 Qt5·Qt6 qmake `make check` 9/9와 8초 headless smoke를 통과했고,
ici 0.6.0 qmake-clean 검증은 Suite PASS, TEM 4.90, line/function/branch
96.6%/98.0%/85.0%, complexity 14, duplication 2.0, sanitizer clean이었다. PR
[#23](https://github.com/jihoon22-lee/toy-projects/pull/23)의 workflow
[`33338809225`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33338809225)는 모든
checks를 통과했고 merge commit `039052f9f30e355e12f3c812065657e3be4576f2`로 병합됐다.
[sticky comment](https://github.com/jihoon22-lee/toy-projects/pull/23#issuecomment-5471613383)와
Pages HTML도 HTTP 200·`text/html`·외부 참조 0개로 확인했다.

#### diskmap D2 cancellable/latest-generation scan — complete

D2는 스캔을 취소할 수 있고, 최신 요청의 결과만 화면에 반영하는 계약을 추가했다.
core scanner는 `ScanCancellationToken`을 atomic flag로 공유하고 directory listing 전·중·후에
협력적으로 checkpoint를 둔다. GUI의 `ProgressFn`은 queued callback으로 상태 라벨에
`dirs/files` 진행을 전달하며, 새 `scanPath()`는 이전 token을 취소하고 generation을 증가시킨다.
진행·완료 callback의 generation이 현재 값과 다르면 stale result로 폐기하므로, A가 늦게 끝나도
그 결과가 나중에 시작한 B의 treemap이나 breadcrumb를 덮지 않는다.

취소 정책은 명시적으로 **partial result를 폐기**하는 쪽이다. 취소된 `ScanResult`는
`cancelled=true`와 incomplete root를 남기지만, GUI는 이를 `Scan cancelled — partial result
discarded`로 표시하고 기존에 보이던 완전한 tree를 그대로 유지한다. 아직 표시된 결과가 없으면
빈 화면을 유지한다. root metadata/open 실패와 비어 있는 root listing 실패는 fatal로 분류해
CLI가 `fatal:` 한 줄을 내고 exit 1을 반환한다. 중간 subtree의 stat/list/iterator 실패와 entries를
일부 반환한 listing 실패는 해당 node를 `complete=false`로 남기고 `error_count`와 오류 목록을
보존하면서 형제 작업을 계속하는 partial/non-fatal 결과다. 의도적인 mount 경계 skip은 별도
카운터로 표시하며 오류로 위장하지 않는다.

scanner와 `aggregateSizes`, `aggregateStorage`, `sortBySizeDesc`, `countNodes`, `topFiles`,
treemap layout은 explicit stack을 사용해 call stack recursion에 의존하지 않는다. scan의
effective structural depth는 기본값과 512 초과 요청을 모두 `kMaxTreeDepth=512`로 제한하며,
잘린 directory는 화면에 남기되 `complete=false`와 `scan depth limit reached`를 기록해
physical total을 확정값처럼 보이지 않게 한다. CLI의 legacy `--depth`도 출력 깊이만 최대 512로
제한한다 — `--depth`는 scan traversal을 줄이지 않고, 실제 traversal bound는 `--max-depth`다.

CLI는 다음 옵션을 제공한다.

| 옵션 | 의미 |
|---|---|
| `--max-depth N` | scanner traversal bound. `0`은 root만 list하며, 기본값/512 초과는 구조 안전 한계 512로 clamp한다. |
| `--depth N` | legacy 출력(tree text/JSON) 깊이 제한. scan 자체는 계속 진행하며 출력 cap도 512다. |
| `--follow-symlinks` | descendant directory symlink를 target identity 방문 집합으로 cycle-safe하게 확장한다. 명시적으로 선택한 root symlink는 이 flag와 무관하게 dereference한다. |
| `--min-size BYTES` | regular file 중 지정 크기보다 작은 entry만 제외한다. directory와 symlink entry는 보존한다. |
| `--one-file-system` | root와 device가 다른 directory를 visible incomplete node로 남기고 확장하지 않는다. identity를 확인할 수 없으면 안전하게 오류를 남긴다. |
| `--exclude GLOB` | basename과 root-relative generic path에 `*`/`?` wildcard를 적용한다. repeatable이며 매칭된 entry는 aggregate에서 제외된다. |

2026-08-31 local candidate 구현에서는 `/usr/bin/qmake` Qt 5.15.18과 `/usr/bin/qmake6` Qt 6.10.2의
full build 및 `make check`가 모두 통과했고, 두 `test_main_window`가 각각 `10/10 PASS`를
보고했다. CLI integration smoke도 fixture에 `--max-depth`, legacy `--depth`, `--min-size`와
`--exclude`를 함께 적용한 JSON 검증으로 PASS했다. ici complexity-only gate에서 처음
발견한 scan complexity/nesting FAIL은 `b7218c6`의 상태 전이 분리 refactor로 해소되어
maximum cyclomatic 14 (limit 15), 129 functions, 0 issues로 PASS했다. 이 candidate의 local
ici main commit
`6a0eadb`의 candidate pyz SHA256 `8cd2d4b128ab2d181e708660c4c4f38bcc9d50f9ad91e3aa5670f557e6077fed`로
수행한 full post-refactor local `ici verify`는 `Suite PASS`, 10 pass / 0 warn / 0 fail /
0 error / 2 skip, test 9/9, TEM 4.92, line95.7% / function98.5% / branch84.4%, complexity
max14 across129 functions / 0 issues, dup3.11%, sanitize clean, 30 tools/21 ready/0 incomplete/
9 unavailable, total82.29s, cache hits0이었다. HTML은 299034 bytes, SHA256
`cf75f9d6f28179d95645d0e1582022008804078d5e3844de503a8c1a130c64a0`, external resource tags0이다.
이 local evidence는 candidate artifact에 대한 기록이며, 아래 원격 증거가 toy-projects `main`의
D2 병합과 검증을 닫는다.

#### D2 원격 완료 증거

2026-08-31 [PR #28](https://github.com/jihoon22-lee/toy-projects/pull/28)은 squash merge
commit `ec075e57874d20654f7cbfbc604ad8aaee8401a6`으로 toy-projects `main`에 병합됐다.
[PR CI run `33368958698`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33368958698)의
12개 check가 모두 green이었다. 여기에는 DiskMap benchmark smoke Qt5/Qt6, 네 GUI matrix
job, 두 ici verification job, publish와 Merge Gate가 포함된다.
[sticky comment](https://github.com/jihoon22-lee/toy-projects/pull/28#issuecomment-5475254935)에는
`diskmap: PASS · TEM 4.92 · 10 pass / 2 skip · tests 9/9`와
`loglens: PASS · TEM 4.80 · 10 pass / 2 skip · tests 12/12`, 양쪽 report link가 게시됐다.

Pages도 HTTP/2 `200`, content-type `text/html`, 외부 `script`/`link`/`img`/`iframe`
resource 0개로 각각 확인했다.

| Pages 경로 | bytes | SHA-256 |
|---|---:|---|
| `diskmap/pr/28/` | 199843 | `c8a0d8009e1c19cd2d9df041969396f6abce95275713fe7ead6a499ac0b33b72` |
| `loglens/pr/28/` | 334215 | `acda3bfb29bf5f3534256f614719e678ec89ed21b3420ee2b282ec55e2107830` |

이것은 toy-projects `main`의 기능 병합 및 검증 기록이며, 별도 제품 버전 release를 의미하지 않는다.

#### diskmap D3 Qt-free view projection — core slice merged

D3의 첫 core slice는 Qt에 의존하지 않는 view projection API로 병합됐다. [PR #45](https://github.com/jihoon22-lee/toy-projects/pull/45)의
merge commit은 `0688e44fa99d1ec69aba0c9bf9995a4a857fea9e`이며, PR workflow
[`33607634973`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33607634973)와 exact-main
workflow [`33608884643`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33608884643)가
모두 성공했다. 두 실행에서 required checks, Qt5/Qt6 native GUI matrix, ici 검증, benchmark,
report publication과 Merge Gate를 확인했다.

core는 logical/allocated/reclaimable metric, 불확실성·cycle·depth·mount·scanner-filter
provenance, deterministic node key/issue, conjunctive search/type/size/age filter, visible
children와 largest-files의 안정적인 정렬을 제공한다. hard-link이 겹칠 수 있는 physical 값은
non-additive 범위를 보존하므로 형제 subtree를 합산하지 않는다.

이 문단은 PR #45 core-only 병합 시점의 historical snapshot이다. 이후 GUI 범위도
PR #46으로 병합됐고, 상세한 core native/ici 수치는
[기존 workthrough 기록](workthrough/2026-09-02-diskmap-view-projection.md)에 보존한다.

#### diskmap D3 explorer workbench — merged GUI evidence (2026-09-02)

역사적 `feat/diskmap-explorer-ui` 구현은 shared immutable scan document와 `NodeKey`를 기준으로 treemap,
sortable table, accessible breadcrumb를 연결했다. 두 화면은 current root, filter, metric을
공유하며 table은 recursive largest-files projection도 제공한다. Search와 type/size/age/state
filter, logical/allocated/reclaimable 설명, uncertainty 표시, rescan 시 path/selection 복원,
stale generation 차단과 worker lifetime 보호까지 포함한다.

[PR #46](https://github.com/jihoon22-lee/toy-projects/pull/46)은 이 D3 GUI 구현을
`0cdd63953179a1dc885ed660e955b399d54243b7`로 `main`에 병합했다. PR run
[`33627322683`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33627322683)와 exact-main
run [`33628585439`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33628585439)는
모두 green이며, sticky marker/link 수와 PR/main artifact·Pages byte-identical 결과를 포함한
정확한 HTML 표는 [D3 explorer workthrough](workthrough/2026-09-02-diskmap-explorer-workbench.md)에
중앙화했다.

Qt 5.15.18/6.10.2 clean qmake(`-Werror`)와 native target `11/11`이 통과했다. Public ici
`v0.10.2` deep no-cache 결과는 `10 PASS / 2 WARN / 0 FAIL / 0 ERROR / 2 SKIP`, TEM `4.95`,
coverage `96.1% / 99.1% / 83.4%`다. 알려진 WARN과 정확한 asset/HTML provenance는
[workthrough 기록](workthrough/2026-09-02-diskmap-explorer-workbench.md)에 보존한다.
이 문단의 D3 historical snapshot에서 DiskMap은 `0.1.0`/`Unreleased`였고 D1~D3
구현·PR/exact-main evidence만 완료된 상태였다. 당시 D4~D7 cleanup/trash/snapshot/release는
pending 범위였으며, 이후 local storage integration은 다음 절에 별도로 기록한다.

#### diskmap storage evidence workbench — local integration (2026-09-03)

Storage workbench의 local integration은 기존 cleanup/Trash safety path 위에 bounded snapshot과
duplicate evidence를 연결했다. GUI의 snapshot save/load/compare와 duplicate progress/cancel,
CLI의 `diskmap.snapshot/v1`·snapshot-diff·duplicate versioned schema는 상세 workthrough에
정리돼 있다. Loaded snapshot은 read-only이며 certain reclaimable copy만 cleanup dry run에
staging되고, 실제 변경은 confirmation·identity revalidation·recoverable Trash를 거친다.

Qt5/Qt6 clean qmake aggregate는 `test_storage_cli`와 cleanup/Trash를 포함한 17개 leaf를
각각 `17/17 PASS`로 실행하고 실제 `diskmap_core` static library를 링크한다. 현재 DiskMap
focused test slot은 `MainWindow=29`, `StorageWorkbench=3`이며, QTest 출력은 lifecycle
hook을 포함해 각각 `31 PASS`와 `5 PASS`로 보인다. 자세한 구현·검증·metric provenance는
[canonical storage workthrough](workthrough/2026-09-03-diskmap-storage-workbench.md)에 둔다.

Safety boundary는 유지된다. Relative `CleanupTarget.path`와 unsupported kind은 mutation 전에
`InvalidRequest`로 거부되고 false `trashed_path`/`restore_token`을 발행하지 않는다. Linux의
no-follow/nonblocking regular-file I/O와 identity revalidation은 race를 incomplete/uncertain으로
전파하며, advisory `flock`은 같은 경로를 쓰는 협력적 mutation만 직렬화한다. 비협조적 동일 UID
프로세스는 보장 범위 밖이다. Move/restore source split은 각 translation unit의 line gate를
회복했다.

이 통합 트리에 released ici `v0.10.2`를 사용한 deep no-cache 결과는 `WARN`
(`10 PASS / 2 WARN / 0 FAIL / 0 ERROR / 2 SKIP`)이다. `test`는 `17/17`, compile DB는
`37/37`(58 configurations), line/function/branch는 `94.4% / 99.2% / 81.0%`, TEM은 `4.96`,
sanitizer는 `PASS`였다. Line은 `13,041` total / `11,500` code lines across `60` files,
complexity는 max `15` across `594` functions(`0 issues`)이며 WARN은 lint와 duplication뿐이다.
이는 WARN/skip 경계를 포함한 local deep evidence이며 release 완료를 의미하지
않는다. DiskMap 버전은 계속 `0.1.0`/`Unreleased`다. 자세한 변경·검증 범위는 [storage workbench
workthrough](workthrough/2026-09-03-diskmap-storage-workbench.md)에 보존한다.
