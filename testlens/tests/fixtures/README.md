# Fixture provenance

These reports were produced on 2026-10-03 with the repository's existing executables.
Workspace root occurrences were replaced with `/workspace/toy-projects` for portability.
Test names, case structure, counts, measurements and output were otherwise retained.

- `qt-diskmap.xml`: `diskmap/build/cmake-qt6/tests/test_format -o ...,junitxml`.
- `qt-buildscope.xml`: `buildscope/build/gui/test_contract -o ...,junitxml`.
- `qt-loglens.xml`: `loglens/build/gui/test_log_parser -o ...,junitxml`.
- `pytest-envlens.xml`: actual `envlens/tests/test_redaction.py`, pytest 9.1.1 xunit2.
- `ctest-repo.xml`: a TestLens-owned scratch CTest directory registering those three
  existing repository executables, emitted with `--output-junit`.
- `ctest-dashboard.xml`: the same registration, `ctest -T Test` dashboard artifact.

Qt reports use Qt 6.10.2. Tests register the actual CTest target output separately from
inner Qt cases. Synthetic error, unsafe XML, retry and incomplete cases live inline in
unit tests so their intended mutation is visible to reviewers.
