"""Unit tests for the dependency-free BuildScope B5 report checker."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path

CI_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(CI_DIR))

from check_buildscope_b5_report import (
    BuildScopeB5ReportError,
    check_report,
    main,
    validate_report,
)


def _tool_record(name: str) -> dict[str, object]:
    return {
        "name": name,
        "path": f"/usr/bin/{name}",
        "version": f"{name} 18.1.0",
        "argv": [name, "--version"],
        "returncode": 0,
        "timed_out": False,
        "truncated": False,
        "error": "",
    }


def _engine(
    name: str, extra: dict[str, object], *, status: str = "PASS"
) -> dict[str, object]:
    return {
        "schema_version": "ici.result/v3",
        "engine_name": name,
        "status": status,
        "summary": "contract fixture",
        "score": 1.0,
        "max_score": 1.0,
        "duration": 0.1,
        "raw_output": "",
        "extra": extra,
        "required": False,
        "evidence": "MEASURED",
        "cache_hit": False,
        "tool_evidence": [],
        "targets": [],
        "findings": [],
    }


def _valid_report() -> dict[str, object]:
    lint = _engine(
        "lint",
        {
            "qt_codegen_mode": "exact",
            "qt_codegen_inputs_checked": 3,
            "qt_codegen_moc_checked": 1,
            "qt_codegen_ui_checked": 1,
            "qt_codegen_qrc_checked": 1,
            "cpp_analysis_mode": "exact",
            "cpp_configurations_checked": 22,
            "qt5_compile_units": 0,
            "qt6_compile_units": 12,
            "clang_tidy_mode": "exact",
            "clang_tidy_sources_checked": 12,
            "clang_tidy_configurations_checked": 22,
            "clazy_mode": "exact",
            "clazy_sources_checked": 12,
            "clazy_configurations_checked": 22,
            "clazy_provider": "standalone",
        },
    )
    lint["tool_evidence"] = [_tool_record("clang-tidy"), _tool_record("clazy")]
    return {
        "schema_version": "ici.result/v3",
        "analysis_metadata": {"producer_version": "0.10.2"},
        "analysis_context": {"profile": "deep"},
        "results": [
            lint,
            _engine(
                "compile_db",
                {
                    "configurations": 27,
                    "production_units": 12,
                    "covered_units": 12,
                    "coverage_percent": 100.0,
                    "issues_count": 0,
                },
            ),
            _engine(
                "test",
                {"passed_tests": 92, "total_tests": 92, "pass_rate": 1.0},
            ),
        ],
    }


class BuildScopeB5ReportTests(unittest.TestCase):
    def test_valid_deep_uncached_tool_backed_report(self) -> None:
        report = _valid_report()

        self.assertEqual(check_report(report), [])
        validate_report(report)
        validate_report(report, expected_qt_major=6)

    def test_rejects_wrong_qt_major_evidence(self) -> None:
        report = _valid_report()

        errors = check_report(report, expected_qt_major=5)

        self.assertTrue(any("Qt evidence" in error for error in errors))

    def test_accepts_qt5_only_evidence(self) -> None:
        report = _valid_report()
        report["results"][0]["extra"]["qt5_compile_units"] = 12
        report["results"][0]["extra"]["qt6_compile_units"] = 0

        self.assertEqual(check_report(report, expected_qt_major=5), [])

    def test_rejects_non_exact_cpp_analysis_and_short_configuration_set(self) -> None:
        report = _valid_report()
        report["results"][0]["extra"]["cpp_analysis_mode"] = "estimated"
        report["results"][0]["extra"]["cpp_configurations_checked"] = 0

        errors = check_report(report)

        self.assertTrue(any("cpp_analysis_mode" in error for error in errors))
        self.assertTrue(any("cpp_configurations_checked" in error for error in errors))

    def test_rejects_short_compile_database_configuration_set(self) -> None:
        report = _valid_report()
        report["results"][1]["extra"]["configurations"] = 26

        errors = check_report(report)

        self.assertTrue(
            any("compile_db.extra.configurations" in error for error in errors)
        )
        self.assertTrue(any("at least 27" in error for error in errors))

    def test_rejects_schema_profile_and_producer_mismatch(self) -> None:
        report = _valid_report()
        report["schema_version"] = "ici.result/v2"
        report["analysis_metadata"]["producer_version"] = "0.9.1"
        report["analysis_context"]["profile"] = "standard"

        errors = check_report(report)

        self.assertTrue(any("report.schema_version" in error for error in errors))
        self.assertTrue(any("producer_version" in error for error in errors))
        self.assertTrue(any("analysis_context.profile" in error for error in errors))

    def test_rejects_cached_engine(self) -> None:
        report = _valid_report()
        report["results"][0]["cache_hit"] = True

        errors = check_report(report)

        self.assertTrue(any("uncached" in error for error in errors))

    def test_rejects_incomplete_qt_codegen_inventory(self) -> None:
        report = _valid_report()
        report["results"][0]["extra"]["qt_codegen_qrc_checked"] = 0

        errors = check_report(report)

        self.assertTrue(any("qt_codegen_qrc_checked" in error for error in errors))

    def test_rejects_unbacked_static_analysis(self) -> None:
        report = _valid_report()
        report["results"][0]["extra"]["clazy_mode"] = "unavailable"
        report["results"][0]["tool_evidence"][1]["returncode"] = None

        errors = check_report(report)

        self.assertTrue(any("clazy_mode" in error for error in errors))
        self.assertTrue(
            any("clazy" in error and "successful" in error for error in errors)
        )

    def test_rejects_incomplete_compile_database(self) -> None:
        report = _valid_report()
        report["results"][1]["extra"]["covered_units"] = 11
        report["results"][1]["extra"]["coverage_percent"] = 91.7

        errors = check_report(report)

        self.assertTrue(any("coverage is incomplete" in error for error in errors))
        self.assertTrue(any("coverage_percent" in error for error in errors))

    def test_rejects_failed_tests(self) -> None:
        report = _valid_report()
        report["results"][2]["status"] = "FAIL"
        report["results"][2]["extra"]["passed_tests"] = 91

        errors = check_report(report)

        self.assertTrue(any("test.status" in error for error in errors))
        self.assertTrue(any("test result is not complete" in error for error in errors))

    def test_validate_report_raises_with_all_violations(self) -> None:
        report = _valid_report()
        report["results"][1]["extra"]["issues_count"] = 1

        with self.assertRaisesRegex(BuildScopeB5ReportError, "issues_count"):
            validate_report(report)

    def test_cli_loads_json_and_returns_status(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            report_path = Path(temporary) / "verify_report.json"
            report_path.write_text(json.dumps(_valid_report()), encoding="utf-8")
            stdout = StringIO()
            stderr = StringIO()
            with redirect_stdout(stdout), redirect_stderr(stderr):
                result = main([str(report_path)])

        self.assertEqual(result, 0)
        self.assertIn("contract: PASS", stdout.getvalue())
        self.assertEqual(stderr.getvalue(), "")

    def test_cli_rejects_non_object_json(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            report_path = Path(temporary) / "verify_report.json"
            report_path.write_text("[]", encoding="utf-8")
            stderr = StringIO()
            with redirect_stderr(stderr):
                result = main([str(report_path)])

        self.assertEqual(result, 1)
        self.assertIn("must contain a JSON object", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
