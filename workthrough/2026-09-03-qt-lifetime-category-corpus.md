# Qt Lifetime Category Candidate Contract

## Overview

This workthrough records the new Qt 6 Quality Zoo known-answer fixture and its
digest-bound released/candidate expectations. It establishes the exact
rule/status/evidence/confidence/path/line contract and the intentional category
change from released ici `v0.10.2` to the candidate `tool-rule-v1` taxonomy.
The fixture has local candidate evidence only; Qt remote acceptance, PR CI,
exact-main CI, and Pages evidence remain pending.

## Context

Quality Zoo already had four C++ sanitizer scenarios and the Python scenario.
The new `cpp.qt-missing-parent-constructor` case exercises clazy's
`ctor-missing-parent-argument` check with a nearby clean `QObject` counterpart.
The released and candidate executables both report the finding at
`src/bad.cpp:3`, but the released expectation retains `maintainability` while
the candidate expectation applies the new `tool-rule-v1` policy and records
`resource`.

## Changes Made

### 1. Qt 6 bad/clean fixture

- `quality-zoo/scenarios/cpp/qt-missing-parent-constructor/CMakeLists.txt` uses
  Qt6 Core and builds `src/bad.cpp` and `src/clean.cpp` as an object fixture.
- `src/bad.cpp` defines `MissingParent()` without a `QObject *parent` argument;
  the expected finding is at line 3.
- `src/clean.cpp` defines `ParentAware(QObject *parent = nullptr)` and forwards
  the parent to `QObject`; a lint finding there is forbidden.
- `ici.toml` requires `g++`, `cmake`, and `clazy`, with the exact
  `ctor-missing-parent-argument` check.

### 2. Digest-bound expectations

| Channel | Executable SHA-256 | Expected / observed | Evidence / confidence | Rule / tool rule | Category | Location |
|---|---|---|---|---|---|---|
| released ici `v0.10.2` | `8e6237302ff3b6198cad86c97dd6bcd666ecab9204e9e19209e2e310c7fd18f4` | `WARN` / `WARN` | `MEASURED` / `exact` | `ici.legacy.lint.target` / `clazy-ctor-missing-parent-argument` | `maintainability` | `src/bad.cpp:3` |
| candidate target `e7a9f55be8893d91497a6e1d0bff6e2e5f4af5f3` | `985c81a63363356619207870cddb0d8cd9854a46925a3e0a745e54bd543d5b51` | `WARN` / `WARN` | `MEASURED` / `exact` | `ici.legacy.lint.target` / `clazy-ctor-missing-parent-argument` | `resource` (`tool-rule-v1`) | `src/bad.cpp:3` |

Both schema-1 expectations forbid a lint finding in `src/clean.cpp`; the
schema-2 scenario selects them by exact executable digest.

### 3. Candidate provenance and local evidence

- Candidate target: `e7a9f55be8893d91497a6e1d0bff6e2e5f4af5f3`.
- Producer [run `33715173073`](https://github.com/jihoon22-lee/ici/actions/runs/33715173073)
  published [artifact `9878317009`](https://github.com/jihoon22-lee/ici/actions/artifacts/9878317009).
- Raw candidate ZIP: `2,288,897` bytes, SHA-256
  `1165312e36344244fe0591e4fbcf869d126a0a7160a21099ee13c34ae8144d5e`.
- Contained `ici.pyz`: `2,287,574` bytes, SHA-256
  `985c81a63363356619207870cddb0d8cd9854a46925a3e0a745e54bd543d5b51`.
- The provenance binds the candidate to successful main `Merge Gate`
  [run `33714515219`](https://github.com/jihoon22-lee/ici/actions/runs/33714515219).
- Local Quality Zoo unit validation passed `57/57`; the candidate run with Qt
  excluded returned contract `5/5 PASS` for the existing five scenarios.

## Code Examples

```cpp
// src/bad.cpp — expected clazy finding at line 3
class MissingParent final : public QObject {
public:
    MissingParent() = default;
};

// src/clean.cpp — forbidden-finding counterpart
class ParentAware final : public QObject {
public:
    explicit ParentAware(QObject *parent = nullptr) : QObject(parent) {}
};
```

## Verification Results

```text
(cd quality-zoo && python3.10 -m unittest discover -s tests -v)
57 tests passed

candidate with cpp.qt-missing-parent-constructor excluded
existing five-scenario contract: 5/5 PASS

git diff --check
PASS
```

## Scope Boundary and Next Steps

This evidence adds the fixture and local digest-bound contract only. Qt
candidate remote acceptance, PR CI, exact-main CI, and Pages verification are
pending. The candidate remains non-stable; no toy or ici version/release was
changed.
