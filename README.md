# toy-projects

C++17 / Qt 5.15 및 Qt 6 환경을 대상으로 만드는 실사용 데스크톱 도구 모음.
동시에 [ici](https://github.com/jihoon22-lee/ici) 품질 게이트의 **외부 C++ 검증 대상**으로 쓰인다.

앞으로의 방향과 그 순서를 정한 이유는 [ROADMAP.md](ROADMAP.md) 에, 이 과정에서 발견한
ici 결함은 [ICI-GAPS.md](ICI-GAPS.md) 에 있다.

## 릴리스 버전 규율

이 저장소의 버전은 포트폴리오 진행률이나 검증 인프라의 변경 횟수를 세는 숫자가 아니다.
`loglens`, `diskmap`, `buildscope` 등 각 toy 제품은 서로 독립적으로 버전을 결정한다.

- 포트폴리오 방향, B-stage 진행, ici pin, CI/runner-only 변경은 toy 버전을 자동으로 올리지 않는다.
- `patch`는 이미 공개된 stable 제품의 defect, security, compatibility regression을 고칠 때만 사용한다.
- `minor`는 하나의 응집된 사용자 가치가 실제로 쓸 수 있는 제품 checkpoint가 된 뒤에만 올린다.
  이때 native tests, released-ici verification, PR 및 exact-main CI/Pages, 문서와 limitations,
  재현 가능한 release assets를 모두 확인하고 기록한다.
- candidate, pre-release, unreleased 상태는 stable release로 간주하지 않는다.
- BuildScope `0.5.0`은 B3/B4/B5를 묶은 첫 usable release boundary이며 2026-09-02 KST에
  [불변 GitHub Release](https://github.com/jihoon22-lee/toy-projects/releases/tag/buildscope-v0.5.0)로
  공개됐다. 최종 publication evidence는 아래 절과 [BuildScope 문서](buildscope/README.md)에
  기록하며, 그 이후의 작업은 같은 수준의 checkpoint가 마련될 때까지 `Unreleased`에 쌓는다.
- `diskmap`은 `0.1.0`/`Unreleased`를 유지하는 explorer workbench다. D1 identity-safe scan,
  D2 cancellation/rescan, D3 explorer UX의 구현과 PR/exact-main evidence는 완료됐고, D4~D7
  cleanup/trash/snapshot/release 범위는 아직 pending이다. D3의 exact PR/main artifact·Pages
  표는 [workthrough](workthrough/2026-09-02-diskmap-explorer-workbench.md)에 모아 둔다.

## 프로젝트

| 이름 | 설명 | 상태 |
|---|---|---|
| [diskmap](diskmap/) | 디스크 사용량 트리맵 뷰어 | Qt5/Qt6 GUI · D1/D2 complete · D3 explorer workbench merged (PR #46; exact-main green) · `0.1.0`/`Unreleased` |
| [loglens](loglens/) | 로그 뷰어 / 분석기 | Qt5/Qt6 GUI · bounded background loader · L2 1 GiB benchmark merged in PR #26 · default capacity 8192 |
| [buildscope](buildscope/) | compile DB explorer | 0.5.0 stable · published 2026-09-02 · immutable GitHub Release |
| [envlens](envlens/) | Python 환경 snapshot CLI/library | pure Python 3.10+ · deterministic snapshot core · PR #50 merged · exact-main green · release pending · `0.1.0`/`Unreleased` |
| [quality-zoo](quality-zoo/) | ici known-answer expected-finding corpus | Q0 contract/candidate intake/PR/exact-main complete · Q1~Q5 pending · no product release |

### envlens deterministic snapshot — merged evidence

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

### Quality Zoo known-answer corpus and candidate consumer

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
closeout are captured in the [Quality Zoo workthrough](docs/workthroughs/2026-09-03-quality-zoo-contract.md);
the post-merge four-project evidence is in the [EnvLens workthrough](workthrough/2026-09-03-envlens-snapshot.md).

### buildscope B0 hybrid skeleton — release-backed evidence

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

### buildscope B2 normalized Qt explorer — 0.3.0

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

### BuildScope 0.4.0 — historical include-resolution implementation and evidence

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

### BuildScope 0.5.0 release evidence — reproducible packaging and guarded publication

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

## 공통 구조 규칙

모든 프로젝트는 아래 배치를 따른다. 공용 파서·모델은 `core`에 두고, Qt 셸은 그 계약을
사용하는 별도 계층으로 둔다. Qt 사용 자체를 core에서 금지하지는 않지만, CLI와 GUI가 같은
핵심 의미론을 공유하도록 한다.

```
<project>/
├── ici.toml              project.source_dirs = ["src"]
├── <빌드 정의>           loglens 는 CMakeLists.txt, diskmap 은 diskmap.pro
├── include/<project>/    헤더는 전부 여기. 코어와 GUI 모두
│   └── gui/              Qt5/Qt6 셸의 헤더
├── src/                  구현. 전부 ici 검증 대상이다
│   ├── main.cpp          CLI 드라이버
│   └── gui/              Qt5/Qt6 셸의 .cpp. 라이브러리 + 실행 파일로 나뉜다
└── tests/                각각 자체 실행 파일이 되는 테스트
```

**헤더는 예외 없이 `include/<project>/` 아래에 둔다.** GUI 헤더를 `.cpp` 옆에 두던
시절도 있었지만, 그러면 같은 저장소에 배치 규칙이 둘이 된다. GUI 를 라이브러리로 분리한
뒤로는 테스트가 그 헤더를 직접 include 하므로 더더욱 그렇다 — `src/` 밖에서 쓰이는
헤더는 공개 헤더다.

`gui/` 하위를 두되 접두사(`loglens/`, `diskmap/`)는 유지한다. `-Iinclude` 하나로
`#include "loglens/gui/log_model.hpp"` 와 `#include "loglens/log_parser.hpp"` 가 같은
모양이 된다.

빌드 정의에는 **GUI 헤더를 명시적으로 나열해야 한다.** CMake 의 `AUTOMOC` 은 `.cpp` 와
같은 디렉터리에 같은 이름의 헤더가 있을 때만 알아서 찾고, qmake 는 `HEADERS` 에 적힌
것만 moc 에 넘긴다. 헤더가 `include/` 로 가면 둘 다 자동 탐지가 안 되므로, 적어두지
않으면 `Q_OBJECT` 클래스가 조용히 vtable 미해결로 링크에 실패한다.

### 빌드 시스템이 프로젝트마다 다른 것은 의도다

| 프로젝트 | 빌드 | ici 가 쓰는 것 |
|---|---|---|
| `loglens` | CMake | CMake 어댑터 + CTest |
| `diskmap` | qmake | qmake 어댑터 + `make check` |

ici 0.6.0 은 어댑터를 둘 갖는다. **각각 실물 프로젝트 하나씩으로 검증하지 않으면 한쪽은
픽스처만으로 설계한 것이 된다** — ici 저장소의 `examples/cpp-fixtures/` 는 단위 테스트용이지
실측 근거가 아니다. 실제로 이 배치가 아니었으면 놓쳤을 결함이 나왔다: qmake 는 Qt 링크
테스트를 `target_wrapper.sh` 로 실행하는데, ici 가 그 줄을 못 읽어 **`diskmap` 의 테스트
6개 중 5개만 세고 있었다.** 픽스처는 Qt 테스트만 있어서 다른 경로로 구제되는 바람에 이걸
드러내지 못했다.

### diskmap D1 identity-safe scan — Slice 2

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

### diskmap D2 cancellable/latest-generation scan — complete

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

### diskmap D3 Qt-free view projection — core slice merged

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

### diskmap D3 explorer workbench — merged GUI evidence (2026-09-02)

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
DiskMap은 계속 `0.1.0`/`Unreleased`이며 D1~D3 구현·PR/exact-main evidence는 완료됐다.
D4~D7 cleanup/trash/snapshot/release는 pending 범위다.

### GUI 는 빌드되고 테스트된다

Qt 셸을 `src/` 밖에 두면 "검증할 필요 없는 코드" 라는 뜻이 되어버리므로 `src/gui/` 에 둔다.
0.5.x 까지는 `cpp_external_build_dirs` 로 GUI 를 링크에서 빼야 했다 — ici 가 moc 를 돌리지
못해 `Q_OBJECT` 클래스가 vtable 미해결로 링크되지 않았기 때문이다. **0.6.0 의 빌드 어댑터가
그 전제를 없앴다.** 이제 GUI 는 다른 소스와 똑같이 빌드·단위 테스트·sanitize 된다.

그래서 GUI 프로젝트는 모두 GUI 를 **라이브러리와 실행 파일로 나눈다.** 실행 파일 하나뿐이면
테스트가 링크할 대상이 없다.

```toml
cpp_pkg_config = ["Qt6Widgets", "Qt5Widgets"]   # 설치된 Qt 헤더를 lint가 찾게
```

`cpp_external_build_dirs` 는 더 이상 쓰지 않는다. 루트에 빌드 디스크립터가 없는 프로젝트
(g++ 경로)에서는 여전히 유효하다.

### Qt 환경과 현재 실측

개발 환경에는 Qt 5.15.18과 Qt 6.10.2가 함께 설치돼 있다. 버전은 다음 명령으로 확인한다.

```text
$ qmake -v
Using Qt version 5.15.18 in /usr/lib/x86_64-linux-gnu
$ qmake6 -v
Using Qt version 6.10.2 in /usr/lib/x86_64-linux-gnu
$ pkg-config --modversion Qt5Core Qt5Widgets Qt5Concurrent Qt5Test
5.15.18
5.15.18
5.15.18
5.15.18
$ pkg-config --modversion Qt6Core Qt6Widgets Qt6Concurrent Qt6Test
6.10.2
6.10.2
6.10.2
6.10.2
```

2026-08-31 Qt6 기본 환경에서 ici 0.6.0 release asset으로 수행한 T0/D1 기준 snapshot은
다음과 같이 통과했다. 이는 D2 `b7218c6` 이후 local candidate의 full post-refactor
`ici verify` 결과가 아니다.

```bash
for p in loglens diskmap; do
  (cd "$p" && QT_QPA_PLATFORM=offscreen ../../ici/dist/ici.pyz verify \
    --report --html verify_report.html --github-summary)
done
```

결과는 `loglens: Suite PASS, TEM 4.84 (10 passed, 2 skipped)`와 `diskmap: Suite PASS,
TEM 4.85 (10 passed, 2 skipped)`다. 이 historical snapshot과 별개로 D2 candidate의 full
post-refactor local `ici verify`는 위 current-ici evidence로 완료됐다. D2의 toy remote PR/CI,
sticky comment/Pages evidence와 병합된 main full benchmark는 위 원격 증거와 아래 benchmark
기록으로 완료됐다. Qt5 강제 CMake build는 loglens에서
`CMAKE_DISABLE_FIND_PACKAGE_Qt6=ON`으로, diskmap의 Qt5 qmake build/test는
`/usr/bin/qmake`로 각각 별도 실측했다. T0-5에서는 이 선택을 매 PR의 명시적 CI matrix로
강제한다. 현재 로컬에서는 CMake Qt6/Qt5와 qmake6/Qt5 네 조합 모두 native test와
`QT_QPA_PLATFORM=offscreen` 실제 GUI smoke가 통과했다.

### Qt major matrix 계약

[`ci/check_manifest.py`](ci/check_manifest.py)의 `SUPPORTED_QT_MAJORS = (5, 6)`가
`gui.enabled = true`인 각 manifest 항목을 자동으로 두 개의 discovery 항목으로 확장한다.
따라서 현재 `diskmap`, `loglens`, `buildscope`는 다음 여섯 job이 되고, 새 GUI 프로젝트도
manifest에 한 번만 추가하면 같은 양쪽 검증을 받는다.

| 프로젝트 | Qt5 | Qt6 |
|---|---|---|
| `diskmap` | `/usr/bin/qmake` · `make check` · headless smoke | `/usr/bin/qmake6` · `make check` · headless smoke |
| `loglens` | CMake + Qt6 disable guard · CTest · headless smoke | CMake + Qt5 disable guard · CTest · headless smoke |
| `buildscope` | CMake + Qt6 disable guard · CTest · headless smoke | CMake + Qt5 disable guard · CTest · headless smoke |

각 matrix leg는 선택 major의 Core/Gui/Widgets/Concurrent/Test pkg-config 버전을 로그에
출력하고 major prefix를 검증한다. CMake leg는 반대 major의 `CMAKE_DISABLE_FIND_PACKAGE_*`
guard와 configure output, 최종 `ldd`의 `libQt${major}Widgets`를 확인한다. qmake leg는
고정 경로와 `-query QT_VERSION`을 확인한 뒤 같은 binary를 테스트와 smoke에 사용한다.
CMake GUI descriptor는 `-- <project>: using Qt<major> <version>` status line을 출력하는
공통 convention을 따른다.

### loglens 스트림 계약

`loglens`의 CLI와 GUI는 같은 `RecordAssembler`를 사용한다. `FileTailer::pollChunk`가
poll에서 읽은 raw bytes와 source generation을 전달하고, assembler가 다음 상태를 한 곳에서
보존한다.

POSIX에서 `FileTailer`는 경로의 timestamp가 아니라 열린 파일 handle의 device/inode를
비교한다. 따라서 같은 경로가 더 작거나, 같은 크기이거나, 더 큰 파일로 원자적 rename
교체되어도 `Replaced`로 감지하며, 같은 inode에 쓰는 in-place 축소는 별도의 `Truncated`로
구분한다. source 계층은 missing, permission denied, open/stat/read failure와 unsupported
file type을 typed error로 보존한다.

GUI는 follow 중 발생한 source 오류를 retryable/fatal로 나눈다. missing, permission, open,
stat, read 계열의 retryable 오류에서는 마지막으로 정상적으로 읽은 행을 유지하고 follow
checkbox와 poll timer를 켠 채 `Follow waiting (attempt N)` 상태를 표시한다. 경로에 새 파일이
다시 나타나 새 identity가 확인되면 이전 generation의 assembler와 모델을 비우고 새 파일의
첫 행부터 표시한다. 사용자가 대기 중 Follow를 끄면 retry timer도 멈추며, 다시 켜면 같은
경로를 명시적으로 재개한다. 디렉터리 같은 fatal unsupported file type은 follow를 중지하지만
마지막으로 읽은 화면은 유지하여 사용자가 원인을 확인하거나 다른 파일을 선택할 수 있게
한다. 최초 open 자체가 실패한 경우에는 기존 source를 비우고 Follow를 끄는 기존 계약을
유지한다.

- newline을 기준으로 한 physical line 번호
- 다음 poll에서 이어 붙일 partial bytes
- stack-trace continuation을 확장할 pending record
- truncation/restart 뒤 초기화되는 generation
- 선택된 format과 byte-preserving encoding/error policy

새 record는 `Append`, 기존 record의 continuation은 `Extend` delta로 구분된다. 따라서 GUI는
이미 표시한 행을 갱신하고 CLI는 같은 결과 벡터를 갱신한다. newline 없는 마지막 조각은
follow 모드에서는 보류하며, one-shot CLI의 명시적 EOF `flush()`에서만 record가 된다.

### loglens bounded storage

GUI와 CLI는 기본 8,192개(최대 1,000,000개)의 같은 bounded record store를 사용한다.
GUI status는 visible/retained/seen/dropped, oldest-newest physical line과 capacity를 표시하고,
CLI는 `--capacity N`으로 보존량을 정하며 일반 출력과 `--stats` 모두 같은 요약을 출력한다.
오래된 record가 제거돼도 assembler의 absolute ID는 바뀌지 않아 continuation update가 다른
행에 적용되지 않는다.

source read는 한 poll당 기본 1 MiB(최대 16 MiB)로 제한된다. GUI는 초기 backlog를 이벤트
루프에 나눠 처리하고, one-shot CLI는 최초 file-size snapshot까지만 읽는다. newline 없는
거대한 line이나 continuation은 기본 64 KiB(최대 1 MiB)에서 잘리며, UI/CLI에 정확한
`omitted_bytes`가 표시된다. 이 slice는 이벤트 루프를 독점하는 전체 파일 read를 없앤 기반
단계다.

초기 GUI 로드는 `Latest records`(Tail N)와 `From start` 중에서 선택한다. Tail N은
continuation line을 포함한 logical record의 시작 offset을 bounded byte scan으로 찾고,
선택된 suffix를 실제 `FileTailer`와 `RecordAssembler`로 읽어 원래 physical line number를
유지한다. From start는 첫 poll의 file-size snapshot까지만 읽는다. 두 경로 모두 source
identity를 다시 확인해 선택 시점과 로드 시점이 다른 파일이면 섞어 표시하지 않고 retryable
오류로 끝낸다.

초기 I/O와 parsing은 `LogLoadWorker`가 전용 `QThread`에서 소유하고, `LogModel`과 모든
위젯 변경은 GUI thread에서만 수행한다. worker는 한 번에 최대 512개의 `RecordDelta`만
`LoadBatch`로 발행하며, GUI가 같은 `job_id`와 `sequence`를 acknowledge하기 전에는 다음
batch를 읽거나 발행하지 않는다. 새 파일을 열면 thread-safe job selector가 이전 작업을
취소하고, GUI는 stale job 또는 sequence가 어긋난 batch를 적용하지 않는다. 따라서 느린
파일 I/O가 event loop를 막지 않으면서도 queued signal이 무한히 쌓이지 않는다.

`Follow`는 초기 backlog가 drain되는 동안에도 명시적으로 켜고 끌 수 있다. 취소 또는 Follow
중지는 pending follow poll을 버리고, 재개할 때 현재 source generation부터 다시 읽는다.
구조화된 filter와 대소문자 구분 없는 raw-text search는 worker가 계속 batch를 보내는 중에도
GUI thread에서 안전하게 바꿀 수 있으며, timeline 갱신은 debounce된다.

### loglens filter contract

구조화된 filter는 `level >= WARN`, `source == api`, `source ~ gateway`,
`message ~ "request timeout"`, `message !~ "health"`와 `AND`/`OR`/`NOT`/괄호 조합을
지원한다. `~`와 `!~`는 정규식이 아닌 대소문자 구분 없는 literal substring 연산이다.
따옴표 안에서는 `\"`와 `\\`만 escape로 해석하며, 나머지 UTF-8 바이트는 그대로 보존한다.

한 query는 입력 4,096 bytes, AST 256 nodes, decoded literal 1,024 bytes, nesting depth 64로
제한된다. malformed 또는 제한 초과 query는 `ParseError::position`과 새
`ParseError::end`가 가리키는 UTF-8 입력의 half-open byte range 및 deterministic message로
거부된다. GUI filter 상태도 이 byte range를 표시하며, 이전에 적용된 정상 filter 화면은
오류가 나도 유지한다. CLI도 같은 `[begin,end)` byte range를 stderr에 출력한다.

CLI의 --level shorthand와 --filter는 각각 독립적으로 parse한 뒤 결합하므로 오류 range가
생성된 conjunction의 prefix/괄호 때문에 이동하지 않고 사용자가 입력한 argument에 매핑된다.
GUI도 입력을 trim해서 버리지 않고 untrimmed UTF-8 bytes를 parser에 전달한다. depth 제한 오류는
허용 한도를 넘긴 추가 NOT/괄호 nesting token을 가리키며, unsupported escape가 multibyte UTF-8
scalar를 시작하면 backslash부터 scalar 전체를 range에 포함한다. 실패한 apply는 이전에 적용된
정상 filter를 계속 유지한다. parser의 TokenRange/PredicateTokens 구조화로 새 clang-tidy
swapped-parameter 경고도 제거했다.

이 slice의 Qt5/Qt6 native suite는 각각 12/12 pass이고, public ici `v0.10.2` `ici.pyz`로 수행한
uncached deep 검증의 artifact SHA-256은
`2af5198d1348a64c39f4f37d12657aa9a2c4bf3ddf034a9099909c41e86e30e7`이다. 전체 suite는 clazy가
사용 불가하고 기존 lint finding이 남아 `WARN`이지만 다른 실패는 없다. 변경된
`filter_expr.cpp`, `main.cpp`, `main_window.cpp`는 actionable lint target 0건이다. 전체 lint는
26개 target으로 보이며, 그중 clang-tidy `note:` 16줄은 ici가 별도 target으로 부풀려 세고 있다.
이는 ici 엔진의 알려진 후속 보완 과제로 기록한다. `compile_db`는 40개 configuration의 production
unit 14/14 `PASS`, `test`는 12/12 `PASS`이며 line/function/branch coverage는
`93.3% / 96.7% / 82.4%`, `complexity`는 218개 대상에서 max 15 `PASS`, `sanitize`는 `PASS`다.
HTML은 484,899 bytes이며 exact title `ici Verification Report — loglens`와 Zero-CDN을 확인했다.
LogLens product version/release는 아직 pending이고, 더 넓은 L3 parser-pipeline 완료를 의미하지 않는다.

2026-08-31에 canonical 1 GiB synthetic log(정확히 1,073,741,824 bytes, 1,000,000 records,
SHA-256 `11186d3021e558c8ed5e33473198a6f9f281ca0605ae79739a928a87156435bb`)의 전체 sweep을
완료했다. capacity `8192, 16384, 32768, 65536, 131072, 262144`를 각 3회, process timeout
180초로 실행했으며, 두 Qt major에서 `8192..65536`이 모든 correctness·성능·RSS budget을
만족했다. `131072`은 core RSS, `262144`는 core와 GUI RSS budget을 넘겼다. PR #26은
`c45176ce25f2efd66ea9b0ed9b48690e34cc8679`로 squash merge됐고, [main 대용량 workflow
run](https://github.com/jihoon22-lee/toy-projects/actions/runs/33355312096)의 Qt5/Qt6
benchmark·combine·verdict가 모두 green이었다.

고정한 budget은 first result `≤ 5000 ms`, first paint `≤ 5000 ms`, 전체 load `≤ 60000 ms`,
throughput `≥ 25 MiB/s`, records `≥ 25000 records/s`, core peak RSS `≤ 256 MiB`, GUI peak
RSS `≤ 512 MiB`다. 두 Qt 결과에서 best median load time 대비 10% 이내인 가장 작은 적격
capacity를 선택하는 규칙으로 기본 capacity를 `8192`로 결정했다. 대표적인 capacity 8192
median은 다음과 같다.

| Qt | component | first result | first paint | load | throughput | records/s | peak RSS |
|---|---|---:|---:|---:|---:|---:|---:|
| 5 | core | 3.041 ms | — | 1510.632 ms | 677.862 MiB/s | 661974.809 | 24.465 MiB |
| 5 | GUI | 18.031 ms | 19.616 ms | 17717.171 ms | 57.797 MiB/s | 56442.421 | 53.336 MiB |
| 6 | core | 3.049 ms | — | 1480.219 ms | 691.790 MiB/s | 675575.761 | 24.469 MiB |
| 6 | GUI | 18.055 ms | 18.843 ms | 18490.615 ms | 55.379 MiB/s | 54081.488 | 55.980 MiB |

#### 1 GiB benchmark 재현 (opt-in)

benchmark target은 기본 빌드에 포함하지 않는다. Qt 6의 local 실행은 다음과 같다.

```bash
cd loglens
cmake -S . -B build/benchmark-qt6 -DCMAKE_BUILD_TYPE=Release \
  -DLOGLENS_BUILD_BENCHMARKS=ON \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=ON
cmake --build build/benchmark-qt6 --parallel \
  --target loglens-bench-generate loglens-bench-core loglens-bench-gui
QT_QPA_PLATFORM=offscreen python3.10 benchmarks/run_benchmark.py \
  --build-dir build/benchmark-qt6 \
  --scratch /tmp/loglens-benchmark-qt6 \
  --artifact-dir /tmp/loglens-benchmark-artifacts/qt6 \
  --qt-major 6 \
  --bytes 1073741824 --records 1000000 \
  --capacities 8192,16384,32768,65536,131072,262144 \
  --repetitions 3 --timeout-seconds 180
```

Qt 5는 `build/benchmark-qt5`를 사용하고 `CMAKE_DISABLE_FIND_PACKAGE_Qt6=ON`,
`--qt-major 5`로 바꾼다. runner는 generator 결과의 정확한 byte/record 수와 SHA-256을
검증한 뒤 core/GUI raw sample을 집계한다. `summary.json`, `summary.md`, `toolchain.json`,
`toolchain.txt`, `samples/*.json`만 artifact로 남기며 1 GiB input과 process log는 scratch에
둔다. `.github/workflows/loglens-benchmark.yml`의 Qt5/Qt6 matrix는 `workflow_dispatch`와
주간 schedule에서만 실행되고 일반 PR/merge gate에는 포함하지 않는다. 이 benchmark는
[PR #26](https://github.com/jihoon22-lee/toy-projects/pull/26)으로
`c45176ce25f2efd66ea9b0ed9b48690e34cc8679`에 squash merge됐다. 최종 PR gate인
[workflow run `33355058919`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33355058919)은
모든 checks가 green이었고, 기존 [sticky comment](https://github.com/jihoon22-lee/toy-projects/pull/26#issuecomment-5473343910)는
`diskmap: PASS · TEM 4.90`, `loglens: PASS · TEM 4.80`, warn 0과 HTML 링크를 유지한다.
Pages `diskmap/pr/26/`와 `loglens/pr/26/`는 각각 HTTP 200·`text/html`·external refs 0개
(180160/334215 bytes)였다. main 대용량 workflow의 combined summary SHA-256은
`5e3292950958a4c678a0c54bf75e7b2546ad1528f43529b6cce1c3dff4e150a8`이다.

일반 PR에는 별도로 `.github/workflows/ci.yml`의 `benchmark-smoke`가 포함된다. 이것은
1 MiB/1,000 records, capacity `64,256`, 1회, 30초 timeout의 Qt6 harness correctness run이며
budget을 건너뛰고 결과 artifact만 업로드한다. `Merge Gate`가 이 smoke 성공을 required check로
요구하므로 benchmark harness 자체의 회귀는 PR에서 막지만, 비용이 큰 1 GiB budget sweep은
opt-in/nightly workflow에 남긴다.

PR [#24](https://github.com/jihoon22-lee/toy-projects/pull/24)의 구현 head `fa4fd1a`는
workflow [`33348597272`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33348597272)에서
공개 ici 검증, 두 프로젝트 Qt5·Qt6 GUI, report publish와 Merge Gate를 모두 통과했다.
[sticky comment](https://github.com/jihoon22-lee/toy-projects/pull/24#issuecomment-5472700934)에
두 PASS 결과와 HTML 링크가 게시됐고, 두 Pages 문서는 HTTP 200·`text/html`·외부 참조 0개로
직접 확인했다.

위 원격 기록은 bounded foundation에 대한 과거 증거다. background/Tail N 변경의 이전 local
ici deep no-cache 결과는 구현 head `e19fea9`에서 Suite PASS, 11 pass / 0 warn / 0 fail / 0
error / 2 skip, TEM 4.83, line/function/branch 93.4%/96.6%/81.6%, maximum complexity
15(0 issues), duplication 1.72%, sanitizer PASS, HTML 428,025 bytes·external refs 0개였다.
최신 background/Tail N 구현 head `ce2a7cd91ff0a47c4f153b60f7fb7984de406ce9`는
[PR #25](https://github.com/jihoon22-lee/toy-projects/pull/25)에서
[workflow `33351033448`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33351033448)의
모든 checks를 통과했고 merge commit은
`69db15966ca0c032026aeb7b742c4eed6335910d`다. [sticky comment](https://github.com/jihoon22-lee/toy-projects/pull/25#issuecomment-5472960253)는
두 프로젝트 PASS와 HTML 링크를 담았고, Pages `diskmap/pr/25/`와 `loglens/pr/25/`는 각각
HTTP 200·`text/html`·external refs 0개(180160/327074 bytes)였다. 이 원격 증거는
background/Tail N 변경에 대한 것이며, 1 GiB benchmark는 PR26 병합과 main workflow
검증까지 완료됐다. L2 이후의 parser/filter와 release 조건은 별도 stream으로 유지한다.

### Qt 셸 테스트 현황

GUI는 헤드리스 QtTest와 실제 fixture 파일을 사용해 상태 전이를 검증한다. `loglens`는
Qt5/Qt6 CMake/CTest에서 같은 12개 CTest target을 실행했고, `diskmap`의 explorer workbench는
현재 native qmake target `11/11`을 통과했다. MainWindow는 29개, TreemapWidget은 12개,
NodeTableModel은 11개의 focused QtTest case를 가진다. `buildscope` B2는 Qt5/Qt6 CMake/CTest에서
각각 6/6을 통과했다. T0-5의
CI matrix는 GUI 프로젝트를 Qt5와 Qt6로 각각 빌드·native test·실제 headless smoke까지
실행한다.

| 파일 | 상태 |
|---|---|
| `loglens/src/gui/log_model.cpp` | `QAbstractItemModelTester` 로 검증 |
| `loglens/src/gui/log_load_worker.cpp` | `test_log_load_worker` — 전용 QThread, 512-record ACK backpressure, stale job/sequence, Tail N line number, invalid source와 rotation |
| `diskmap/src/gui/treemap_widget.cpp` | `QSignalSpy` 로 검증 — treemap activation/hover/uncertainty (12 tests) |
| `loglens/src/gui/main_window.cpp` | `test_main_window` — Tail N/From start, open/growth/truncation, retryable missing/reappear, follow stop/resume, search/filter during load, stale sequence와 status |
| `loglens/src/gui/timeline_widget.cpp` | `test_main_window` — empty/populated paint branch |
| `diskmap/src/gui/main_window.cpp` | `test_main_window` — scan, filters, metric/largest-files, navigation, rescan/selection/generation/freeze (29 tests) |
| `diskmap/src/gui/node_table_model.cpp` | `test_node_table_model` — shared document, keyed rows, sorting, filters, knownness and largest-files (11 tests) |
| `buildscope/src/core/compilation_model.cpp` | `test_compilation_model` — normalized grouping, roles, v1 projection, status aggregation, entry view, JSON argv rendering |
| `buildscope/src/gui/main_window.cpp` | `test_main_window` — v2 tree/detail population, raw-vs-JSON command view, status/source/define filters, malformed-input location |

`loglens` bounded foundation의 ici 0.6.0 실측은 line 93.9% / function 96.9% /
branch 83.1% / TEM 4.85이다.
`loglens/ici.toml`은 이 실측 아래에 slack을 둔 branch 75.0 / function 92.0을 게이트로
사용한다. QPainter/Qt 내부 예외 경로와 C++ type-check 미지원은 남은 명시적 한계다.

L1 Slice 2의 GUI 회귀 테스트는 `QTemporaryDir`로 실제 파일을 만들고
`QMetaObject::invokeMethod(..., Qt::DirectConnection)`으로 poll 경계를 결정적으로 구동한다.
`test_main_window`는 retryable missing 상태에서 기존 행을 보존하는지, 동일 경로의 replacement를
새 generation으로 재개하는지, 사용자의 Follow 중지/재개와 fatal unsupported source를 각각
확인한다. 현재 L2 benchmark의 native CMake/CTest는 Qt 6과 Qt 5 각각 12/12 PASS이고,
Qt 6 strict `-Wall -Wextra -Wpedantic -Werror` benchmark build도 통과했다. ici 0.6.0 deep
no-cache는 Suite PASS, 12/12 tests, TEM 4.83, line/function/branch
93.6%/96.6%/81.8%, maximum complexity 15, duplication 1.71%, sanitizer PASS였으며 HTML은
433,351 bytes·external refs 0개였다. background/Tail N의 원격 검증은 PR #25에서
완료됐고, L2 benchmark도 PR #26의 squash merge, green PR gate, sticky comment, Pages와
main Qt5/Qt6 full sweep 검증을 완료했다.

## CI 리포트

프로젝트마다 `ici verify` 가 따로 돌아 HTML 리포트가 여러 개 나온다. 이걸 PR 에 남기는 방식은
매트릭스 레그가 각자 게시하는 게 아니라 **집계 잡 하나가 전부 모아서** 게시한다.

레그별 게시가 안 되는 이유가 둘 있다. ici 의 sticky 마커는 고정 문자열 하나라 두 번째 게시가
첫 번째 댓글을 덮어쓰고, 병렬 레그가 같은 gh-pages 브랜치에 동시에 쓰면 Contents API 가
요구하는 blob sha 를 두고 경쟁하다 하나가 유실된다.

그래서 레그는 아티팩트만 올리고, `report-pr` 잡이 전부 내려받아
`ici publish --report-dir` 로 **순차 업로드 후 댓글 하나**를 남긴다. 프로젝트별로 행과 링크가
하나씩 붙는다. 그 잡은 PR 소스를 체크아웃하지 않는다 — 실행물은 체크섬 검증된 릴리스 pyz,
게시물은 verify 잡의 아티팩트뿐이라 PR 이 쓰기 토큰에 닿지 않는다.

## CI 품질 게이트 계약

현재 프로젝트 목록과 각 Qt GUI의 빌드·smoke 입력은
[`ci/projects.json`](ci/projects.json)이 유일한 기준이다. `discover` 잡은 이 manifest를
검증하고 `ici.toml`을 가진 프로젝트가 하나라도 목록에서 빠지면 실패한다. `ici verify`
matrix와 Qt GUI build/smoke matrix는 모두 같은 manifest 출력에서 생성되며, GUI 항목은
discovery 단계에서 자동으로 Qt5·Qt6 두 major로 확장된다. 따라서 새 프로젝트를 추가할 때
한쪽 matrix만 수정하는 실수를 허용하지 않는다. GUI가 없는 순수 CLI 프로젝트는
`gui.enabled = false`를 명시해야 하며, 그 경우에도 ici verify에는 포함된다.

discovery contract 자체는 의존성 없는 Python unit suite로 manifest expansion, 공개 HTML의
exact-title/Zero-CDN 규칙, PR/main publisher event split과 exact-SHA/digest/path/byte 검증 불변식을
검사한다. `gui-build`를 비롯해 PR 소스를 체크아웃하는 품질 job은 repository-level
`contents: read`만 상속한다. write 권한이 필요한 `report-pr`는 소스를 체크아웃하지 않고
체크섬을 검증한 ici release asset과 verify artifact만 처리한다. 별도 `publish-main`은 모든
품질 job이 성공한 push에서만 exact `main` 소스를 체크아웃한다.

PR의 `report-pr`는 verify·GUI matrix가 성공 또는 실패로 끝난 뒤 항상 평가된다. 성공한
실행에서는 checksum을 확인한 ici `v0.10.2` release asset으로 모든 report artifact를
`gh-pages`에 순차 게시하고, repository 단위 concurrency로 Pages 쓰기 경합을 막는다. 이어서
실제 sticky 댓글을 API로 읽어 manifest 프로젝트 수만큼의 HTML 링크가 정확히 있는지 확인하고,
각 링크가 Pages에서 `text/html` 응답을 반환할 때까지 기다린다.

`Merge Gate`는 branch protection에서 required check로 설정해야 한다. 이 stable check가
manifest discovery, 모든 ici verify·GUI matrix leg, benchmark smoke, BuildScope deep Qt5/Qt6,
Python quality, release contract와 event별 게시 경계를 함께 요구한다. PR에서는 sticky 댓글과
`<project>/pr/<number>/` Pages가 필수이고 `publish-main`은 건너뛴다. `main` push에서는 반대로
PR publisher를 건너뛰고, exact current SHA와 public ici checksum을 다시 확인한 뒤
`<project>/main/`에 명시적 label로 게시해야 한다. 공개 응답은 local artifact와 byte-identical,
exact-title, Zero-CDN HTML이어야 `Merge Gate`가 성공한다.

## 검증

```bash
cd <project>
QT_QPA_PLATFORM=offscreen ../../ici/dist/ici.pyz verify --report
```

`loglens`의 셸 상태 테스트만 빠르게 돌리려면 CTest를 쓴다. 테스트는 프로젝트 루트에서
실행되며, 실제 follow timer는 `QTRY_COMPARE`로 기다리고 truncation/read error는 Qt
meta-object로 기존 `pollSource` slot을 동기 호출해 구동한다.
전용 loader 테스트는 `QSignalSpy`/`QTRY`와 queued 또는 blocking meta-object 경계를 사용해
고정 sleep 없이 batch ACK, stale job, Tail N line number와 Follow/cancel 계약을 검증한다.

```bash
# manifest/discovery
python3 -m unittest \
  ci/test_check_manifest.py \
  ci/test_check_published_html.py \
  ci/test_ci_workflow_contract.py \
  -v

# loglens — Qt 6 and Qt 5 CMake legs
cd loglens
cmake -S . -B build/gui -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=ON
cmake --build build/gui --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir build/gui --output-on-failure
cmake -S . -B build/gui-qt5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
cmake --build build/gui-qt5 --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir build/gui-qt5 --output-on-failure

# diskmap — explicit qmake6 and Qt 5 legs
cd ../diskmap
mkdir -p build/gui-qt6 && cd build/gui-qt6
/usr/bin/qmake6 ../../diskmap.pro && make -j"$(nproc)"
QT_QPA_PLATFORM=offscreen make check
mkdir -p ../gui-qt5 && cd ../gui-qt5
/usr/bin/qmake ../../diskmap.pro && make -j"$(nproc)"
QT_QPA_PLATFORM=offscreen make check
```

위 명령은 `ici`와 `toy-projects`를 같은 프로젝트 디렉터리 아래의 형제 저장소로 둔 배치를
기준으로 한다. `QT_QPA_PLATFORM` 이 필요한 이유는 `diskmap` 의 위젯 테스트가 `QWidget` 을
만들기 때문이다.

ici 0.6.0 이상이 필요하다. 그 아래 버전에는 빌드 어댑터가 없어 루트의 빌드 디스크립터를
거부한다.

## GUI 빌드

```bash
# loglens (CMake)
cd loglens
cmake -S . -B build/gui -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=ON
cmake --build build/gui --parallel
./build/gui/src/gui/loglens-gui [경로]

# Qt5를 명시적으로 검증할 때
cmake -S . -B build/qt5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
cmake --build build/qt5 --parallel

# diskmap (qmake)
cd diskmap
mkdir -p build/gui && cd build/gui
/usr/bin/qmake6 -query QT_VERSION
/usr/bin/qmake6 ../../diskmap.pro && make -j
./src/gui/diskmap-gui [경로]

# diskmap (qmake, Qt 5.15)
mkdir -p ../gui-qt5 && cd ../gui-qt5
/usr/bin/qmake -query QT_VERSION
/usr/bin/qmake ../../diskmap.pro && make -j
./src/gui/diskmap-gui [경로]
```

경로를 주면 폴더 선택 대화상자를 건너뛰고 바로 스캔한다. 덕분에
`QT_QPA_PLATFORM=offscreen` 으로 헤드리스 스모크 실행이 가능하고, CI 가 그렇게 쓴다.

## CLI root scan

CLI는 디렉터리뿐 아니라 regular file도 직접 받을 수 있다. 파일 root는 JSON에서
`"is_dir":false`인 한 노드로 출력되며, logical size는 실행 시 파일 metadata에서 읽는다.
root symlink는 항상 target을 확인하고, descendant symlink follow 여부는 scanner 옵션의
의미를 따른다. 실제 파일 root smoke는 다음처럼 실행한다.

```bash
cd diskmap
./build/gui/src/diskmap path/to/main.cpp --json
```

파일 크기는 source에 따라 달라지므로 예시 출력의 `size` 숫자를 고정된 fixture로 취급하지
않는다. 깨진 root symlink는 JSON 출력과 함께 stderr 및 `ScanResult.errors`에 오류를 남긴다.

### DiskMap generated-source benchmark (opt-in/nightly)

`diskmap/benchmarks/run_benchmark.py`는 실제 파일을 만들지 않고 deterministic fake source에서
full scan과 cooperative cancellation을 각각 실행한다. 아래는 Qt6 local 재현 명령이다. Qt5는
두 qmake 호출의 `/usr/bin/qmake6`를 `/usr/bin/qmake`로 바꾸고 별도 `benchmark_root`를 사용한다.

```bash
cd diskmap
repo_root="$(pwd)"
benchmark_root="$(mktemp -d /tmp/diskmap-benchmark.XXXXXX)"
artifact_dir="$benchmark_root/artifact"
mkdir -p "$benchmark_root/src" "$benchmark_root/benchmarks" "$artifact_dir"
(
  cd "$benchmark_root/src"
  /usr/bin/qmake6 "$repo_root/src/src.pro"
  make -j"$(nproc)"
)
(
  cd "$benchmark_root/benchmarks"
  /usr/bin/qmake6 "$repo_root/benchmarks/scan_benchmark.pro"
  make -j"$(nproc)"
)
python3.10 benchmarks/run_benchmark.py \
  --binary "$benchmark_root/benchmarks/diskmap-scan-benchmark" \
  --entries 1000000 --cancel-after 10000 --timeout-seconds 60 \
  --output-dir "$artifact_dir"
sha256sum "$artifact_dir/summary.json"
```

runner의 기본 budget은 full throughput `≥ 100000 entries/s`, full peak RSS `≤ 1536 MiB`,
full elapsed `≤ 30000 ms`, cancellation elapsed `≤ 2000 ms`이며 각 process에는 60초 hard
timeout을 둔다. PR smoke처럼 correctness만 확인할 때는 `--skip-budgets`를 명시한다.

2026-08-31에 기록한 local summary는 다음과 같다.

| 시나리오 | 입력/생성 | elapsed | throughput | peak RSS | correctness |
|---|---:|---:|---:|---:|:---:|
| full | 1,000,000 / 1,000,000 | 4820.934 ms | 207428.692 entries/s | 1063.496 MiB | PASS |
| cancellation | 1,000,000 / 10,000 | 2.676 ms | 3737316.017 entries/s | 15.414 MiB | PASS |

full sample은 `nodes_retained=1,000,001`을 확인했고, summary JSON의 SHA-256은
`743d5c5409101cfd9ef889da2da421e94cc205f585770ab19bb611472926246d`다. 이 수치는 단일
local candidate artifact의 evidence이며, 실행마다 scheduler와 host RSS에 따라 시간·메모리는
달라질 수 있다.

PR #28 병합 후 head `ec075e5`에서 수행한 [main full benchmark run `33369288586`](https://github.com/jihoon22-lee/toy-projects/actions/runs/33369288586)은
Qt5/Qt6 full, combine, verdict job을 모두 성공시켰다. 설정은 1,000,000 entries,
`cancel-after 10000`, process timeout 60초였고, 두 Qt 결과 모두 correctness `true`, failures
0, budgets enforced `PASS`였다.

| Qt | full elapsed | full throughput | full peak RSS | cancellation elapsed |
|---|---:|---:|---:|---:|
| 5 | 2131.069 ms | 469248.020 entries/s | 1064.094 MiB | 1.479 ms |
| 6 | 3120.463 ms | 320465.315 entries/s | 1064.137 MiB | 1.580 ms |

combined `summary.json`은 3567 bytes이며 SHA-256은
`26391797763aed17fedb04e2a4aeb5cf8238ec4d5b5d040d473d32a513369251`이다.

PR 경로의 `.github/workflows/ci.yml` `diskmap-benchmark-smoke`는 Qt5/Qt6 matrix에서
10,000 entries와 1,000-entry cancellation, 30초 timeout, `--skip-budgets`로 harness
correctness만 확인하고 `Merge Gate` required check에 포함한다. 전체 1,000,000-entry 측정은
`.github/workflows/diskmap-benchmark.yml`의 `workflow_dispatch`와 주간(일요일 03:37 UTC)
schedule에서만 Qt5/Qt6 matrix로 실행한다. 이 workflow는 기본값 1,000,000/10,000/60초를
runner에 전달하고 Qt별 summary를 aggregate/verdict job에서 합치며, artifact에는 JSON·MD·TXT
report만 남기고 generated input과 process log는 올리지 않는다. 별도 workflow threshold를
추가하지 않고 runner 기본 budget을 단일 정책으로 사용한다.
