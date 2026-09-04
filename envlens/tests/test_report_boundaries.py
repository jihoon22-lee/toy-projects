"""Report rendering boundary and compatibility-alias tests."""

from __future__ import annotations

import math
from pathlib import Path
from unittest.mock import patch

import pytest

from envlens import report


def _diff_report() -> dict[str, object]:
    change = {
        "name": "sample",
        "normalized_name": "sample",
        "version": "1.0",
        "from_version": "1.0",
        "to_version": "2.0",
        "certainty": "unknown",
        "import_names": ["sample", "sample.cli"],
    }
    return {
        "schema_version": "envlens.diff/v1",
        "status": "unknown",
        "summary": {
            "added": 1,
            "removed": 1,
            "upgraded": 1,
            "downgraded": 1,
            "changed": 1,
            "compatibility_issues": 1,
            "dependency_issues": 1,
        },
        "added": [change, "ignored"],
        "removed": [change],
        "upgraded": [change],
        "downgraded": [change],
        "changed": [change],
        "compatibility": [
            {
                "kind": "requires-python",
                "name": "sample",
                "status": "unknown",
                "certainty": "unknown",
                "reason": "line | needs review",
            },
            "ignored",
        ],
        "dependencies": [
            {
                "kind": "missing",
                "name": "missing",
                "requirement": "missing>=1",
                "certainty": "unknown",
                "reason": "not captured",
            },
            "ignored",
        ],
        "import_name_changes": [
            {"normalized_project_name": "sample", "before": [], "after": ["sample"]},
            "ignored",
        ],
    }


def _runtime_report() -> dict[str, object]:
    return {
        "schema_version": "envlens.runtime/v1",
        "summary": {"status": "unknown", "interpreter_count": 1, "check_count": 2},
        "interpreters": [
            {
                "requested_executable": "/tmp/python",
                "status": "ready",
                "checks": [
                    {"kind": "compileall", "name": "src", "status": "passed", "reason": "ok"},
                    "ignored",
                ],
            },
            "ignored",
            {"requested_executable": "/tmp/other", "checks": "malformed"},
        ],
        "project": {
            "entry_points": [
                {
                    "group": "console_scripts",
                    "name": "sample",
                    "value": "sample:main",
                    "location": {"path": "pyproject.toml", "line": 4},
                },
                "ignored",
            ]
        },
    }


def test_text_renderers_cover_all_sections_and_malformed_items() -> None:
    diff = _diff_report()
    rendered = report.render_text(diff)
    assert "Added (2):" in rendered
    assert "↑ sample 1.0 -> 2.0" in rendered
    assert "Compatibility:" in rendered
    assert "Dependency issues:" in rendered
    assert "Project/import-name changes:" in rendered

    runtime = report.render_text(_runtime_report())
    assert "envlens runtime: UNKNOWN" in runtime
    assert "Interpreter /tmp/python" in runtime
    assert "Entry points (dry inspection):" in runtime
    assert "console_scripts/sample" in runtime


def test_markdown_renderers_cover_runtime_diff_and_safe_cells() -> None:
    diff = _diff_report()
    markdown = report.render_markdown(diff)
    assert "# EnvLens diff — UNKNOWN" in markdown
    assert "line \\| needs review" in markdown
    assert "| missing | missing | missing>=1 | unknown | not captured |" in markdown

    runtime = report.render_markdown(_runtime_report())
    assert "# EnvLens runtime — UNKNOWN" in runtime
    assert "| /tmp/python | compileall: src | passed | ok |" in runtime

    empty = report.render_markdown(
        {
            "status": "unchanged",
            "added": "malformed",
            "compatibility": "malformed",
            "dependencies": [],
        }
    )
    assert "No dependency issues recorded." in empty


def test_report_aliases_and_json_boundaries(tmp_path: Path) -> None:
    diff = _diff_report()
    assert report.report_to_text(diff) == report.render_text(diff)
    assert report.report_to_markdown(diff) == report.render_markdown(diff)
    assert report.dumps_diff(diff, pretty=True).startswith("{\n")
    runtime = _runtime_report()
    assert report.dumps_runtime(runtime) == report.dumps_report(runtime)
    assert report.render_diff(diff, format="md") == report.render_markdown(diff)

    with pytest.raises(report.ReportError, match="invalid-format"):
        report.render_report(diff, format="xml")
    with pytest.raises(report.ReportError, match="report-serialization-failed"):
        report.dumps_report({"bad": object()})
    with pytest.raises(report.ReportError, match="report-serialization-failed"):
        report.dumps_report({"bad": math.nan})
    with (
        patch.object(report, "MAX_REPORT_BYTES", 1),
        pytest.raises(report.ReportError, match="report-too-large"),
    ):
        report.dumps_report({"ok": True})
