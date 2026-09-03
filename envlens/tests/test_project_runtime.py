"""E3 project inspection and runtime smoke tests."""

from __future__ import annotations

import os
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


def test_pyproject_rejects_unsupported_bare_toml_values(tmp_path: Path) -> None:
    (tmp_path / "pyproject.toml").write_text(
        '[project]\nname = bare-name\nversion = "1.0"\n',
        encoding="utf-8",
    )

    with pytest.raises(project.ProjectError, match="unsupported bare TOML value"):
        project.inspect_pyproject(tmp_path / "pyproject.toml")


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


def test_runtime_uses_optional_tool_configuration_when_options_are_omitted(
    tmp_path: Path,
) -> None:
    _write_project(tmp_path)
    with (tmp_path / "pyproject.toml").open("a", encoding="utf-8") as stream:
        stream.write(
            "\n[tool.envlens]\n"
            f"interpreters = [{sys.executable!r}]\n"
            'imports = ["samplemod"]\n'
            'compile_paths = ["src"]\n'
            'entry_points = ["hello"]\n'
        )

    result = runtime.run_runtime_checks(tmp_path)

    assert result["project"]["configuration"]["compile_paths"] == ["src"]
    assert result["interpreters"][0]["requested_executable"] == sys.executable
    assert [item["status"] for item in result["interpreters"][0]["checks"]] == [
        "passed",
        "passed",
        "inspected",
    ]


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


def test_runtime_entrypoint_resets_argv_to_console_name(tmp_path: Path) -> None:
    source = _write_project(tmp_path)
    (source / "samplemod.py").write_text(
        "import pathlib\n"
        "import sys\n"
        "def main():\n"
        "    pathlib.Path('argv.txt').write_text(repr(sys.argv), encoding='utf-8')\n",
        encoding="utf-8",
    )

    result = runtime.run_runtime_checks(
        tmp_path,
        interpreters=[sys.executable],
        imports=[],
        compile_paths=[],
        entry_points=["hello"],
        execute_entry_points=True,
    )

    assert result["interpreters"][0]["checks"][-1]["status"] == "passed"
    assert (tmp_path / "argv.txt").read_text(encoding="utf-8") == "['hello']"


def test_runtime_environment_replaces_host_pythonpath(tmp_path: Path) -> None:
    _write_project(tmp_path)
    with patch.dict(os.environ, {"PYTHONPATH": "/host/contamination"}):
        environment = runtime._runtime_env(tmp_path)

    assert environment["PYTHONPATH"] == os.pathsep.join([str(tmp_path), str(tmp_path / "src")])


def test_runtime_rejects_oversized_compile_command(tmp_path: Path) -> None:
    source = _write_project(tmp_path)
    with (
        patch.object(runtime, "MAX_COMPILE_COMMAND_CHARS", 1),
        pytest.raises(runtime.RuntimeCheckError, match="compileall command exceeds"),
    ):
        runtime.run_runtime_checks(
            tmp_path,
            interpreters=[sys.executable],
            imports=[],
            compile_paths=[source],
        )


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


def test_runtime_rejects_oversized_path_configuration(tmp_path: Path) -> None:
    _write_project(tmp_path)
    with pytest.raises(runtime.RuntimeCheckError, match="compile_paths exceeds"):
        runtime.run_runtime_checks(
            tmp_path,
            interpreters=[sys.executable],
            imports=[],
            compile_paths=[tmp_path] * (runtime.MAX_PATHS + 1),
        )


def test_runtime_stops_recursive_source_enumeration_at_bound(tmp_path: Path) -> None:
    _write_project(tmp_path)
    with (
        patch.object(runtime, "MAX_SOURCE_ENTRIES", 1),
        pytest.raises(runtime.RuntimeCheckError, match="source tree exceeds"),
    ):
        runtime.run_runtime_checks(
            tmp_path,
            interpreters=[sys.executable],
            imports=[],
        )
