"""CLI parser, stdin, and user-facing error boundary tests."""

from __future__ import annotations

import argparse
import io
from contextlib import redirect_stderr
from pathlib import Path
from unittest.mock import patch

import pytest

from envlens import __main__ as cli
from envlens.project import ProjectError
from envlens.report import ReportError
from envlens.runtime import RuntimeCheckError


def test_cli_timestamp_and_same_file_helpers_cover_invalid_inputs(tmp_path: Path) -> None:
    with pytest.raises(argparse.ArgumentTypeError, match="ISO-8601"):
        cli._parse_captured_at("not-a-timestamp")
    assert cli._same_file_if_present(tmp_path / "missing", tmp_path / "other") is False
    with patch.object(cli.os, "path") as path:
        path.samefile.side_effect = OSError("samefile failed")
        assert cli._same_file_if_present(tmp_path / "left", tmp_path / "right") is False


def test_cli_loads_snapshot_from_stdin_and_reports_exit_statuses() -> None:
    payload = (
        '{"schema_version":"envlens.snapshot/v1",'
        '"source":{"identity":{"version":"3"}},"distributions":[]}'
    )
    with patch.object(cli.sys, "stdin", io.StringIO(payload)):
        assert cli._load_snapshot_argument(Path("-"))["schema_version"] == ("envlens.snapshot/v1")
    assert cli._report_exit_code({}) == 0
    assert cli._report_exit_code({"summary": {"status": "failed"}}) == 1


@pytest.mark.parametrize(
    ("error", "message"),
    [
        (ProjectError("project-read-failed", "bad project"), "project-read-failed"),
        (RuntimeCheckError("invalid-timeout", "bad timeout"), "invalid-timeout"),
        (ReportError("invalid-format", "bad format"), "invalid-format"),
    ],
)
def test_cli_maps_library_errors_to_exit_two(error: Exception, message: str) -> None:
    stderr = io.StringIO()
    if isinstance(error, ProjectError):
        failing = patch.object(cli, "inspect_pyproject", side_effect=error)
        arguments = ["diff", "--before", "-", "--after", "-", "--project", "project.toml"]
    elif isinstance(error, RuntimeCheckError):
        failing = patch.object(cli, "run_runtime_checks", side_effect=error)
        arguments = ["runtime"]
    else:
        failing = patch.object(cli, "compare_snapshots", side_effect=error)
        arguments = ["diff", "--before", "-", "--after", "-"]
    with (
        patch.object(cli, "_load_snapshot_argument", return_value={}),
        failing,
        redirect_stderr(stderr),
        pytest.raises(SystemExit) as caught,
    ):
        cli.main(arguments)
    assert caught.value.code == 2
    assert message in stderr.getvalue()
