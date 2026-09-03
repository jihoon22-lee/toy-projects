"""E2/E3 command-line integration tests."""

from __future__ import annotations

import io
import json
import sys
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch

import pytest

from envlens import __main__ as cli


def _snapshot(version: str) -> dict[str, object]:
    return {
        "schema_version": "envlens.snapshot/v1",
        "producer": {"name": "envlens", "version": "0.1.0"},
        "captured_at": "2026-09-03T00:00:00Z",
        "redaction": {"policy": "envlens-redaction/v1", "enabled": True},
        "source": {
            "kind": "python-interpreter",
            "requested_executable": "/usr/bin/python",
            "resolved_executable": "/usr/bin/python",
            "identity": {
                "implementation": "cpython",
                "version": "3.10.12",
                "version_info": [3, 10, 12, "final", 0],
                "cache_tag": "cpython-310",
                "platform": "linux",
                "machine": "x86_64",
            },
        },
        "environment": {"variables": {}},
        "distributions": [
            {
                "name": "sample",
                "normalized_name": "sample",
                "version": version,
                "metadata": {"requires_python": ">=3.10", "requires_dist": []},
                "entry_points": [],
                "location": "/site/sample",
                "status": "ok",
                "errors": [],
            }
        ],
        "collection": {"status": "complete", "distribution_count": 1, "error_count": 0},
    }


def test_cli_diff_emits_json_and_returns_change_status(tmp_path: Path) -> None:
    before = tmp_path / "before.json"
    after = tmp_path / "after.json"
    before.write_text(json.dumps(_snapshot("1.0")), encoding="utf-8")
    after.write_text(json.dumps(_snapshot("2.0")), encoding="utf-8")
    output = io.StringIO()

    with redirect_stdout(output):
        result = cli.main(
            [
                "diff",
                "--before",
                str(before),
                "--after",
                str(after),
                "--format",
                "json",
            ]
        )

    assert result == 1
    report = json.loads(output.getvalue())
    assert report["schema_version"] == "envlens.diff/v1"
    assert report["upgraded"][0]["to_version"] == "2.0"


def test_cli_runtime_forwards_matrix_and_writes_markdown(tmp_path: Path) -> None:
    output_path = tmp_path / "runtime.md"
    fake = {
        "schema_version": "envlens.runtime/v1",
        "interpreters": [],
        "summary": {
            "status": "passed",
            "interpreter_count": 0,
            "check_count": 0,
            "failure_count": 0,
            "unknown_count": 0,
        },
    }
    with patch.object(cli, "run_runtime_checks", return_value=fake) as run:
        result = cli.main(
            [
                "smoke",
                "--project-root",
                str(tmp_path),
                "--interpreter",
                sys.executable,
                "--import",
                "json",
                "--compile-path",
                str(tmp_path),
                "--entry-point",
                "hello",
                "--execute",
                "--format",
                "markdown",
                "--output",
                str(output_path),
            ]
        )

    assert result == 0
    run.assert_called_once_with(
        tmp_path,
        interpreters=[Path(sys.executable)],
        imports=["json"],
        compile_paths=[tmp_path],
        entry_points=["hello"],
        pyproject=None,
        timeout_seconds=10,
        execute_entry_points=True,
    )
    assert output_path.read_text(encoding="utf-8").startswith("# EnvLens runtime — PASSED")


def test_cli_diff_rejects_missing_input_as_exit_two(tmp_path: Path) -> None:
    with pytest.raises(SystemExit) as caught:
        cli.main(["diff", "--before", str(tmp_path / "missing"), "--after", "-"])
    assert caught.value.code == 2
