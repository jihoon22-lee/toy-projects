"""Runtime checker option, helper, and failure-boundary tests."""

from __future__ import annotations

import sys
from pathlib import Path
from unittest.mock import patch

import pytest

from envlens import runtime
from envlens.probe import ProbeError


def _project(path: Path, entry_value: str = "sample:main") -> None:
    (path / "pyproject.toml").write_text(
        "[project]\n"
        'name = "runtime-boundaries"\n'
        'version = "1.0"\n'
        "[project.scripts]\n"
        f'run = "{entry_value}"\n',
        encoding="utf-8",
    )


def test_runtime_helpers_cache_and_compatibility_aliases(tmp_path: Path) -> None:
    cache = tmp_path / "cache"
    environment = runtime._runtime_env(tmp_path, cache)
    assert environment["PYTHONPATH"] == str(tmp_path)
    assert environment["PYTHONPYCACHEPREFIX"] == str(cache)
    assert len(runtime._bounded_text(b"x" * (runtime.MAX_OUTPUT_CHARS + 1))) == (
        runtime.MAX_OUTPUT_CHARS + 1
    )
    assert runtime._failed_process_status(-999, "", "check")["signal_name"] == "SIG999"
    assert runtime._failed_process_status(1, "", "check")["status"] == "failed"

    first = runtime.runtime_check(
        tmp_path,
        interpreters=[sys.executable],
        imports=[],
        compile_paths=[],
    )
    second = runtime.run_smoke(
        tmp_path,
        interpreters=[sys.executable],
        imports=[],
        compile_paths=[],
    )
    assert first["schema_version"] == second["schema_version"] == "envlens.runtime/v1"


def test_runtime_rejects_invalid_options_and_individual_path_bounds(tmp_path: Path) -> None:
    with pytest.raises(runtime.RuntimeCheckError, match="positive integer"):
        runtime.run_runtime_checks(tmp_path, timeout_seconds=0)
    with pytest.raises(runtime.RuntimeCheckError, match="must be boolean"):
        runtime.run_runtime_checks(tmp_path, execute_entry_points=1)  # type: ignore[arg-type]
    with pytest.raises(runtime.RuntimeCheckError, match="imports exceeds"):
        runtime.run_runtime_checks(tmp_path, imports=["json"] * (runtime.MAX_IMPORTS + 1))
    with pytest.raises(runtime.RuntimeCheckError, match="entry_points exceeds"):
        runtime.run_runtime_checks(
            tmp_path,
            entry_points=["run"] * (runtime.MAX_IMPORTS + 1),
        )
    with pytest.raises(runtime.RuntimeCheckError, match="oversized path"):
        runtime.run_runtime_checks(
            tmp_path,
            compile_paths=["x" * (runtime.MAX_PATH_LENGTH + 1)],
        )
    with pytest.raises(runtime.RuntimeCheckError, match="interpreters exceeds"):
        runtime.run_runtime_checks(
            tmp_path,
            interpreters=[sys.executable] * (runtime.MAX_ENTRY_POINTS + 1),
        )


def test_runtime_marks_unsupported_entry_point_when_execution_is_opted_in(tmp_path: Path) -> None:
    _project(tmp_path, "not-a-target")
    result = runtime.run_runtime_checks(
        tmp_path,
        interpreters=[sys.executable],
        imports=[],
        compile_paths=[],
        entry_points=["run"],
        execute_entry_points=True,
    )
    check = result["interpreters"][0]["checks"][-1]
    assert check["status"] == "unsupported"
    assert check["action"] == "execution-skipped"
    assert result["summary"]["status"] == "unknown"


def test_runtime_selection_preserves_qualified_matches_and_orders_missing_requests() -> None:
    entries = [
        {"group": "console_scripts", "name": "run"},
        {"group": "sample.plugins", "name": "run"},
    ]

    selected = runtime._select_entry_points(
        entries,
        ["sample.plugins/run", "run", "missing-z", "missing-a"],
        location_path="pyproject.toml",
    )
    assert runtime._select_entry_points(entries, ["sample.plugins/run"]) == [entries[1]]

    assert selected[:2] == entries
    missing = selected[2:]
    assert [item["requested"] for item in missing] == ["missing-a", "missing-z"]
    assert missing[0]["group"] == ""
    assert missing[1]["group"] == ""
    assert all(item["error_code"] == "missing-entry-point" for item in missing)
    assert all(item["location"] == {"path": "pyproject.toml", "line": 0} for item in missing)


def test_runtime_missing_entry_point_is_a_failure_check(tmp_path: Path) -> None:
    _project(tmp_path)

    result = runtime.run_runtime_checks(
        tmp_path,
        interpreters=[sys.executable],
        imports=[],
        compile_paths=[],
        entry_points=["missing"],
    )

    check = result["interpreters"][0]["checks"][-1]
    assert check["status"] == "failed"
    assert check["error_code"] == "missing-entry-point"
    assert check["requested"] == "missing"
    assert check["location"] == {"path": str(tmp_path / "pyproject.toml"), "line": 0}
    assert result["summary"]["status"] == "failed"
    assert result["summary"]["failure_count"] == 1


def test_runtime_distinguishes_probe_start_errors(tmp_path: Path) -> None:
    with patch.object(
        runtime,
        "_run_bounded",
        side_effect=ProbeError("invalid-interpreter", "cannot start child"),
    ):
        result = runtime.run_runtime_checks(
            tmp_path,
            interpreters=[sys.executable],
            imports=["json"],
            compile_paths=[],
        )
    check = result["interpreters"][0]["checks"][1]
    assert check["status"] == "start-error"
    assert check["error_code"] == "invalid-interpreter"


def test_runtime_preserves_project_inspection_errors_and_rejects_bad_configuration(
    tmp_path: Path,
) -> None:
    # A malformed project is reported as failure evidence while runtime checks
    # remain bounded and usable.
    (tmp_path / "pyproject.toml").write_text("[project]\nname = bare\n", encoding="utf-8")
    result = runtime.run_runtime_checks(
        tmp_path,
        interpreters=[sys.executable],
        imports=[],
        compile_paths=[],
    )
    assert result["project"]["inspection_error"]["code"] == "unsupported-pyproject"
    project_check = result["interpreters"][0]["checks"][-1]
    assert project_check["kind"] == "project-inspection"
    assert project_check["status"] == "failed"
    assert project_check["error_code"] == "unsupported-pyproject"
    assert project_check["location"] == {
        "path": str(tmp_path / "pyproject.toml"),
        "line": 2,
    }
    assert result["summary"]["status"] == "failed"
    assert result["summary"]["failure_count"] == 1

    project_info = {
        "configuration": {"imports": "not-a-list"},
        "entry_points": [],
    }
    with (
        patch.object(runtime, "inspect_pyproject", return_value=project_info),
        pytest.raises(runtime.RuntimeCheckError, match="imports must be a list"),
    ):
        runtime.run_runtime_checks(tmp_path)

    with (
        patch.object(
            runtime,
            "inspect_pyproject",
            return_value={"configuration": {}, "entry_points": "bad"},
        ),
        pytest.raises(runtime.RuntimeCheckError, match="entry_points must be a list"),
    ):
        runtime.run_runtime_checks(tmp_path)


def test_runtime_missing_default_pyproject_is_a_normal_no_metadata_case(tmp_path: Path) -> None:
    result = runtime.run_runtime_checks(
        tmp_path,
        interpreters=[sys.executable],
        imports=[],
        compile_paths=[],
    )

    assert "inspection_error" not in result["project"]
    assert result["summary"]["status"] == "passed"
    assert not any(
        check.get("kind") == "project-inspection" for check in result["interpreters"][0]["checks"]
    )


def test_runtime_dangling_default_pyproject_is_not_treated_as_absent(tmp_path: Path) -> None:
    pyproject = tmp_path / "pyproject.toml"
    try:
        pyproject.symlink_to(tmp_path / "missing-project-metadata.toml")
    except OSError as error:
        pytest.skip(f"symlinks are unavailable: {error}")

    result = runtime.run_runtime_checks(
        tmp_path,
        interpreters=[sys.executable],
        imports=[],
        compile_paths=[],
    )

    check = result["interpreters"][0]["checks"][-1]
    assert check["kind"] == "project-inspection"
    assert check["status"] == "failed"
    assert check["error_code"] == "project-read-failed"
    assert result["summary"]["status"] == "failed"


def test_runtime_rejects_non_directory_project_root() -> None:
    with pytest.raises(runtime.RuntimeCheckError, match="project root must be a directory"):
        runtime.run_runtime_checks("/path/that/does/not/exist")
