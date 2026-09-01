#!/usr/bin/env python3
"""Check the BuildScope B5 ici-report contract.

The checker intentionally uses only the Python standard library.  It is run
after a public ``ici v0.10.2`` deep/no-cache verification and fails closed when
the report does not contain the evidence required by the B5 release gate.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import Any

EXPECTED_ICI_VERSION = "0.10.2"
EXPECTED_SCHEMA_VERSION = "ici.result/v3"
EXPECTED_PROFILE = "deep"
EXPECTED_CODEGEN_COUNTS = {
    "qt_codegen_inputs_checked": 3,
    "qt_codegen_moc_checked": 1,
    "qt_codegen_ui_checked": 1,
    "qt_codegen_qrc_checked": 1,
}
REQUIRED_ENGINES = ("lint", "compile_db", "test")
REQUIRED_TOOL_NAMES = ("clang-tidy", "clazy")


class BuildScopeB5ReportError(ValueError):
    """Raised when a report cannot satisfy the B5 release contract."""


def _mapping(
    value: object, location: str, errors: list[str]
) -> Mapping[str, Any] | None:
    if not isinstance(value, Mapping):
        errors.append(f"{location} must be an object")
        return None
    return value


def _exact(value: object, expected: object, location: str, errors: list[str]) -> None:
    if value != expected:
        errors.append(f"{location} must be {expected!r}, got {value!r}")


def _positive_int(value: object, location: str, errors: list[str]) -> int | None:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        errors.append(f"{location} must be a positive integer, got {value!r}")
        return None
    return value


def _nonnegative_int(value: object, location: str, errors: list[str]) -> int | None:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        errors.append(f"{location} must be a non-negative integer, got {value!r}")
        return None
    return value


def _engine_results(
    report: Mapping[str, Any], errors: list[str]
) -> dict[str, Mapping[str, Any]]:
    raw_results = report.get("results")
    if not isinstance(raw_results, Sequence) or isinstance(
        raw_results, (str, bytes, bytearray)
    ):
        errors.append("results must be an array")
        return {}

    results: dict[str, Mapping[str, Any]] = {}
    for index, raw_result in enumerate(raw_results):
        result = _mapping(raw_result, f"results[{index}]", errors)
        if result is None:
            continue
        name = result.get("engine_name")
        if not isinstance(name, str) or not name:
            errors.append(f"results[{index}].engine_name must be a non-empty string")
            continue
        if name in results:
            errors.append(f"results contains duplicate engine {name!r}")
            continue
        _exact(
            result.get("schema_version"),
            EXPECTED_SCHEMA_VERSION,
            f"results[{index}].schema_version",
            errors,
        )
        if result.get("cache_hit") is not False:
            errors.append(
                f"results[{index}] ({name}) must be uncached (cache_hit=false)"
            )
        results[name] = result

    for name in REQUIRED_ENGINES:
        if name not in results:
            errors.append(f"results is missing required engine {name!r}")
    return results


def _tool_backed(result: Mapping[str, Any], errors: list[str]) -> None:
    extra = _mapping(result.get("extra"), "lint.extra", errors)
    if extra is None:
        return

    _exact(extra.get("clang_tidy_mode"), "exact", "lint.extra.clang_tidy_mode", errors)
    _exact(extra.get("clazy_mode"), "exact", "lint.extra.clazy_mode", errors)
    for field in ("clang_tidy_sources_checked", "clazy_sources_checked"):
        _positive_int(extra.get(field), f"lint.extra.{field}", errors)
    _positive_int(
        extra.get("clang_tidy_configurations_checked"),
        "lint.extra.clang_tidy_configurations_checked",
        errors,
    )
    _positive_int(
        extra.get("clazy_configurations_checked"),
        "lint.extra.clazy_configurations_checked",
        errors,
    )
    provider = extra.get("clazy_provider")
    if not isinstance(provider, str) or not provider or provider == "none":
        errors.append(
            f"lint.extra.clazy_provider must identify a provider, got {provider!r}"
        )

    raw_evidence = result.get("tool_evidence")
    if not isinstance(raw_evidence, Sequence) or isinstance(
        raw_evidence, (str, bytes, bytearray)
    ):
        errors.append("lint.tool_evidence must be an array")
        return

    for tool_name in REQUIRED_TOOL_NAMES:
        records: list[Mapping[str, Any]] = []
        for index, raw_record in enumerate(raw_evidence):
            record = _mapping(raw_record, f"lint.tool_evidence[{index}]", errors)
            if record is not None and record.get("name") == tool_name:
                records.append(record)
        if not records:
            errors.append(f"lint.tool_evidence has no {tool_name!r} execution record")
            continue
        if not any(
            isinstance(record.get("path"), str)
            and bool(record["path"])
            and isinstance(record.get("version"), str)
            and bool(record["version"])
            and isinstance(record.get("argv"), Sequence)
            and not isinstance(record.get("argv"), (str, bytes, bytearray))
            and bool(record["argv"])
            and record.get("returncode") == 0
            and record.get("timed_out") is False
            and record.get("truncated") is False
            and record.get("error") == ""
            for record in records
        ):
            errors.append(
                f"lint.tool_evidence {tool_name!r} has no successful tool-backed execution"
            )


def _check_lint(
    result: Mapping[str, Any], errors: list[str], expected_qt_major: int | None
) -> None:
    status = result.get("status")
    if status not in {"PASS", "WARN"}:
        errors.append(
            f"lint.status must be PASS or WARN after tool-backed analysis, got {status!r}"
        )
    _tool_backed(result, errors)

    extra = result.get("extra")
    if not isinstance(extra, Mapping):
        return
    _exact(
        extra.get("cpp_analysis_mode"), "exact", "lint.extra.cpp_analysis_mode", errors
    )
    _positive_int(
        extra.get("cpp_configurations_checked"),
        "lint.extra.cpp_configurations_checked",
        errors,
    )
    for field, expected in EXPECTED_CODEGEN_COUNTS.items():
        _exact(extra.get(field), expected, f"lint.extra.{field}", errors)
    _exact(extra.get("qt_codegen_mode"), "exact", "lint.extra.qt_codegen_mode", errors)
    qt5_units = _nonnegative_int(
        extra.get("qt5_compile_units"), "lint.extra.qt5_compile_units", errors
    )
    qt6_units = _nonnegative_int(
        extra.get("qt6_compile_units"), "lint.extra.qt6_compile_units", errors
    )
    if expected_qt_major == 5 and qt5_units is not None and qt6_units is not None:
        if qt5_units <= 0 or qt6_units != 0:
            errors.append(
                "lint Qt evidence must select Qt5 only: "
                f"qt5_compile_units={qt5_units}, qt6_compile_units={qt6_units}"
            )
    elif (
        expected_qt_major == 6
        and qt5_units is not None
        and qt6_units is not None
        and (qt6_units <= 0 or qt5_units != 0)
    ):
        errors.append(
            "lint Qt evidence must select Qt6 only: "
            f"qt5_compile_units={qt5_units}, qt6_compile_units={qt6_units}"
        )


def _check_compile_db(result: Mapping[str, Any], errors: list[str]) -> None:
    _exact(result.get("status"), "PASS", "compile_db.status", errors)
    extra = _mapping(result.get("extra"), "compile_db.extra", errors)
    if extra is None:
        return
    configurations = _positive_int(
        extra.get("configurations"), "compile_db.extra.configurations", errors
    )
    if configurations is not None and configurations < 27:
        errors.append(
            f"compile_db.extra.configurations must be at least 27, got {configurations}"
        )
    production = _positive_int(
        extra.get("production_units"), "compile_db.extra.production_units", errors
    )
    covered = _positive_int(
        extra.get("covered_units"), "compile_db.extra.covered_units", errors
    )
    if production is not None and covered is not None and covered != production:
        errors.append(
            f"compile_db coverage is incomplete: {covered}/{production} production units"
        )
    coverage = extra.get("coverage_percent")
    if (
        isinstance(coverage, bool)
        or not isinstance(coverage, (int, float))
        or not math.isclose(float(coverage), 100.0)
    ):
        errors.append(
            f"compile_db.extra.coverage_percent must be 100.0, got {coverage!r}"
        )
    _exact(extra.get("issues_count"), 0, "compile_db.extra.issues_count", errors)


def _check_tests(result: Mapping[str, Any], errors: list[str]) -> None:
    _exact(result.get("status"), "PASS", "test.status", errors)
    extra = _mapping(result.get("extra"), "test.extra", errors)
    if extra is None:
        return
    passed = _positive_int(extra.get("passed_tests"), "test.extra.passed_tests", errors)
    total = _positive_int(extra.get("total_tests"), "test.extra.total_tests", errors)
    if passed is not None and total is not None and passed != total:
        errors.append(f"test result is not complete: {passed}/{total} tests passed")
    if "pass_rate" in extra:
        rate = extra["pass_rate"]
        if (
            isinstance(rate, bool)
            or not isinstance(rate, (int, float))
            or not math.isclose(float(rate), 1.0)
        ):
            errors.append(f"test.extra.pass_rate must be 1.0, got {rate!r}")


def check_report(report: object, expected_qt_major: int | None = None) -> list[str]:
    """Return all B5 contract violations in deterministic order."""

    errors: list[str] = []
    root = _mapping(report, "report", errors)
    if root is None:
        return errors

    _exact(
        root.get("schema_version"),
        EXPECTED_SCHEMA_VERSION,
        "report.schema_version",
        errors,
    )
    metadata = _mapping(root.get("analysis_metadata"), "analysis_metadata", errors)
    if metadata is not None:
        _exact(
            metadata.get("producer_version"),
            EXPECTED_ICI_VERSION,
            "analysis_metadata.producer_version",
            errors,
        )
    context = _mapping(root.get("analysis_context"), "analysis_context", errors)
    if context is not None:
        _exact(
            context.get("profile"), EXPECTED_PROFILE, "analysis_context.profile", errors
        )

    results = _engine_results(root, errors)
    lint = results.get("lint")
    if lint is not None:
        _check_lint(lint, errors, expected_qt_major)
    compile_db = results.get("compile_db")
    if compile_db is not None:
        _check_compile_db(compile_db, errors)
    tests = results.get("test")
    if tests is not None:
        _check_tests(tests, errors)
    return errors


def validate_report(report: object, expected_qt_major: int | None = None) -> None:
    """Raise :class:`BuildScopeB5ReportError` unless ``report`` is valid."""

    if expected_qt_major not in (None, 5, 6):
        raise ValueError(
            f"expected_qt_major must be 5, 6, or None, got {expected_qt_major!r}"
        )
    errors = check_report(report, expected_qt_major)
    if errors:
        details = "\n".join(f"- {error}" for error in errors)
        raise BuildScopeB5ReportError(
            f"BuildScope B5 report contract failed:\n{details}"
        )


def load_report(path: Path) -> Mapping[str, Any]:
    """Load one JSON report and reject non-object roots."""

    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise BuildScopeB5ReportError(
            f"could not read report {path}: {error}"
        ) from error
    if not isinstance(payload, Mapping):
        raise BuildScopeB5ReportError(f"report {path} must contain a JSON object")
    return payload


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path, help="BuildScope verify_report.json")
    parser.add_argument(
        "--expected-qt-major",
        type=int,
        choices=(5, 6),
        help="Require the report to contain compile units for only this Qt major",
    )
    args = parser.parse_args(argv)
    try:
        validate_report(load_report(args.report), args.expected_qt_major)
    except BuildScopeB5ReportError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(f"BuildScope B5 report contract: PASS ({args.report})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
