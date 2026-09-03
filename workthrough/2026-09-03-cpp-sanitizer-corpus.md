# C++ sanitizer known-answer scenarios

## Overview

Quality Zoo now contains four stable C++ sanitizer scenarios covering
AddressSanitizer heap use-after-free, LeakSanitizer memory leak,
UndefinedBehaviorSanitizer signed-integer overflow, and a sanitizer-clean
counterpart. Each scenario has separate strict expectations for the released
ici `v0.10.2` executable and the local candidate executable, selected by exact
executable SHA-256.

This is a local known-answer corpus slice. It does not claim broader corpus
completion, candidate cross-repository acceptance, PR CI, a merge, or a release.

## Context

The existing Quality Zoo runner distinguishes ici's observed suite status from
the contract verdict. The new defect fixtures intentionally observe `FAIL`,
while the clean fixture observes `PASS`; a contract pass means that the status,
sanitizer evidence, location, and clean-finding constraints all match the
selected expectation.

## Changes Made

### C++ sanitizer fixtures and expectations

The core change adds these stable scenarios under
`quality-zoo/scenarios/cpp/`:

| Scenario | Known answer |
|---|---|
| `cpp.asan-use-after-free` | AddressSanitizer heap use-after-free; clean counterpart must remain finding-free |
| `cpp.lsan-memory-leak` | LeakSanitizer memory leak; clean counterpart must remain finding-free |
| `cpp.ubsan-signed-overflow` | UndefinedBehaviorSanitizer signed-integer overflow; clean counterpart must remain finding-free |
| `cpp.sanitizer-clean` | sanitizer completes with zero issues, an informational completion record, and no active non-info sanitizer defect finding |

Each scenario maps both exact executable identities to full strict expectations:

```text
released ici v0.10.2: 8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4
ici candidate:         e7f1a2ce7147057538873a802715c7bf2b12e530a85070af862e02e378caceb8
```

The manifest now contains the existing Python scenario plus these four C++
scenarios. Package version alone is not used as an expectation selector.

### Documentation synchronization

Updated the following documentation to describe the local sanitizer evidence and
keep the completion boundary explicit:

- `CHANGELOG.md`
- `quality-zoo/README.md`
- `docs/superpowers/plans/2026-08-30-product-portfolio-master-plan.md`
- `docs/superpowers/2026-08-30-handover.md`
- `workthrough/2026-09-03-cpp-sanitizer-corpus.md`

Only the specific ASan/UBSan/LSan plus clean-counterpart subitem is marked
complete in the portfolio plan. The broader C++ corpus and all other corpus
families remain open.

## Verification Results

The local all-scenario result sets were audited for both exact executable
digests:

```text
released digest 8e623730...: contract PASS, 5/5 scenarios, 0 errors
candidate digest e7f1a2ce...: contract PASS, 5/5 scenarios, 0 errors
```

In both runs, `cpp.asan-use-after-free`, `cpp.lsan-memory-leak`, and
`cpp.ubsan-signed-overflow` had the expected observed `FAIL`;
`cpp.sanitizer-clean` had the expected observed `PASS`; and
`python.dead-private-function` had the expected observed `WARN`. All five
contract verdicts passed for each executable.

Documentation hygiene was also checked:

```bash
git diff --check
# exit 0
```

No version, release, workflow, PR, or remote acceptance state was changed.

## Scope Boundary and Next Steps

The completed local slice is limited to the four C++ sanitizer scenarios and
their clean constraints under the two exact executable digests. Candidate
cross-repository acceptance still requires a successful exact-revision dispatch
through the merged ici-hosted workflow. The remaining Python/C++, Qt,
build/binary, and hybrid corpus families remain future work.
