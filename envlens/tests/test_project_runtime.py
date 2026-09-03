"""E3 project inspection and runtime smoke tests."""

from __future__ import annotations

import signal
import sys
from pathlib import Path
from unittest.mock import patch

import pytest

from envlens import project, runtime
from envlens.probe import ProbeError

PYPROJECT = """
[project]
name = "sample-project"
version = "1.2.3"
requires-python = ">=3.10"
dependencies = ["dep>=1"]

[project.scripts]
hello = "samplemod:main"

[project.entry-points."sample.plugins"]
plugin = "samplemod:main"
"""


def _write_project(tmp_path: Path) -> Path:
    (tmp_path / "pyproject.toml").write_text(PYPROJECT, encoding="utf-8")
    source = tmp_path / "src"
    source.mkdir()
    (source / "samplemod.py").write_text("def main():\n    return 0\n", encoding="utf-8")
    return source


def test_pyproject_inspection_is_dry_and_preserves_locations(tmp_path: Path) -> None:
    _write_project(tmp_path)

    result = project.inspect_pyproject(tmp_path / "pyproject.toml")

    assert result["project"]["normalized_name"] == "sample-project"
    assert result["project"]["dependencies"] == ["dep>=1"]
    assert [(item["group"], item["name"]) for item in result["entry_points"]] == [
        ("console_scripts", "hello"),
        ("sample.plugins", "plugin"),
    ]
    assert result["entry_points"][0]["target"] == {
        "module": "samplemod",
        "attribute": "main",
    }
    assert result["entry_points"][0]["location"]["line"] > 0
    assert "dry" in result["warnings"][0]


def test_runtime_compile_import_and_dry_entrypoint_e2e(tmp_path: Path) -> None:
    source = _write_project(tmp_path)

    result = runtime.run_runtime_checks(
        tmp_path,
        interpreters=[sys.executable],
        imports=["samplemod"],
        compile_paths=[source],
        entry_points=["hello"],
        timeout_seconds=10,
    )

    checks = result["interpreters"][0]["checks"]
    assert result["summary"]["status"] == "passed"
    assert [item["status"] for item in checks] == ["passed", "passed", "inspected"]
    assert checks[-1]["action"] == "dry-inspection"


def test_runtime_entrypoint_execution_requires_explicit_opt_in(tmp_path: Path) -> None:
    source = _write_project(tmp_path)
    with patch.object(runtime, "_run_bounded", wraps=runtime._run_bounded) as run:
        result = runtime.run_runtime_checks(
            tmp_path,
            interpreters=[sys.executable],
            imports=[],
            compile_paths=[],
            entry_points=["hello"],
        )
    assert result["interpreters"][0]["checks"][-1]["status"] == "inspected"
    run.assert_not_called()

    result = runtime.run_runtime_checks(
        tmp_path,
        interpreters=[sys.executable],
        imports=[],
        compile_paths=[source],
        entry_points=["hello"],
        execute_entry_points=True,
    )
    assert result["interpreters"][0]["checks"][-1]["status"] == "passed"


def test_runtime_distinguishes_missing_interpreter_and_import(tmp_path: Path) -> None:
    _write_project(tmp_path)
    result = runtime.run_runtime_checks(
        tmp_path,
        interpreters=[tmp_path / "does-not-exist"],
        imports=["missing_module"],
        compile_paths=[],
    )
    assert result["interpreters"][0]["status"] == "missing-interpreter"
    assert result["interpreters"][0]["checks"][0]["status"] == "missing-interpreter"

    result = runtime.run_runtime_checks(
        tmp_path,
        interpreters=[sys.executable],
        imports=["missing_module"],
        compile_paths=[],
    )
    assert result["interpreters"][0]["checks"][1]["status"] == "missing-import"
    assert result["interpreters"][0]["checks"][1]["missing_import"] == "missing_module"


@pytest.mark.parametrize(
    ("return_value", "expected_status"),
    [((b"", b"", -signal.SIGTERM), "signal"), ((b"", b"", 1), "failed")],
)
def test_runtime_preserves_signal_and_exit_distinctions(
    tmp_path: Path, return_value: tuple[bytes, bytes, int], expected_status: str
) -> None:
    _write_project(tmp_path)
    with patch.object(runtime, "_run_bounded", return_value=return_value):
        result = runtime.run_runtime_checks(
            tmp_path,
            interpreters=[sys.executable],
            imports=["samplemod"],
            compile_paths=[],
        )
    check = result["interpreters"][0]["checks"][1]
    assert check["status"] == expected_status
    if expected_status == "signal":
        assert check["signal_name"] == "SIGTERM"


def test_runtime_maps_timeout_probe_error(tmp_path: Path) -> None:
    _write_project(tmp_path)
    with patch.object(
        runtime,
        "_run_bounded",
        side_effect=ProbeError("probe-timeout", "interpreter exceeded 1 seconds"),
    ):
        result = runtime.run_runtime_checks(
            tmp_path,
            interpreters=[sys.executable],
            imports=["samplemod"],
            compile_paths=[],
            timeout_seconds=1,
        )
    assert result["interpreters"][0]["checks"][1]["status"] == "timeout"
