# Candidate SHA selector와 Quality Zoo local acceptance 기록

## Overview

기존 `python.dead-private-function` Quality Zoo known-answer 시나리오에 ici
sanitizer-normalization candidate executable의 exact SHA-256 selector와 strict
expectation을 기록했다. 이번 변경은 인증된 로컬 archive intake와 Q0 runner 결과를
문서화하는 범위이며, ici-hosted candidate-to-Quality-Zoo workflow의 원격 수락을
완료했다고 주장하지 않는다.

## Context

Package version `0.10.2`만으로는 released executable과 candidate executable을
구분할 수 없으므로 expectation은 executable digest로 선택한다. 일반 toy CI는
계속 released ici `v0.10.2`를 고정한다. Candidate cross-repository acceptance는
ici workflow가 merge되고 dispatch된 뒤에 별도 원격 evidence로 확인해야 한다.

## Changes Made

### Exact candidate selector and expectation

`quality-zoo/scenarios/python/dead-private-function/scenario.json`에 다음 candidate
mapping을 추가했다.

| Item | Value |
| --- | --- |
| ici target | `9d470edca7ab037a24dcd6594531a822f116548b` |
| producer run | `33706057540` (success) |
| artifact | `9875319095` |
| raw archive SHA-256 | `4aec084b3a30ac01a1df5124fa3b42b7f51d23f66c12b490194a84549be9db27` |
| executable SHA-256 | `e7f1a2ce7147057538873a802715c7bf2b12e530a85070af862e02e378caceb8` |
| selected expectation | `expectations/candidate-9d470ed.json` |

The expectation keeps this candidate tied to the existing Python dead-code
known-answer: contract `PASS`, observed suite `WARN`, one expected `dead` finding at
`src/bad.py:1`, and no finding for the clean counterpart `src/clean.py`.

### Local evidence and status documentation

- Authenticated local candidate archive intake succeeded.
- The Q0 runner returned contract `PASS`, observed suite `WARN`, one matched finding,
  and zero errors.
- `quality-zoo/README.md`, `CHANGELOG.md`, the portfolio master plan, and the handover
  now distinguish released-artifact remote acceptance from candidate local evidence.

## Verification Results

```text
(cd quality-zoo && python3.10 -m unittest discover -s tests -v)
56 tests passed

python3 -m unittest <nine ci contract modules> -v
140 tests passed

python3 ci/check_manifest.py
validated 4 projects, 6 GUI matrix entries

python3.10 -m runner.run \
  --manifest manifest.json \
  --ici-bin <verified-candidate>/ici.pyz \
  --output-dir <fresh-output> \
  --timeout-seconds 300
contract PASS; observed WARN; one matched finding; zero errors

git diff --check
exit 0
```

No workflow, version, release, or remote publication was changed by this slice.

## Scope Boundary and Next Steps

This candidate evidence does not validate ASan/UBSan/LSan sanitizer findings and does
not establish remote cross-repository acceptance. After the ici-hosted workflow is
merged and dispatched with exact ici/toy SHAs, audit its evidence separately before
claiming candidate remote acceptance. Future Quality Zoo Q1–Q5 scenario expansion
remains pending.
