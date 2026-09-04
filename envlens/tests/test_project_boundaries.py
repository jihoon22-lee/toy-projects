"""Bounded TOML parser and project metadata edge cases."""

from __future__ import annotations

from pathlib import Path
from unittest.mock import patch

import pytest

from envlens import project


def _write(path: Path, text: str) -> Path:
    path.write_text(text, encoding="utf-8")
    return path


def test_parser_handles_quotes_nested_values_and_tool_aliases(tmp_path: Path) -> None:
    path = _write(
        tmp_path / "pyproject.toml",
        """
[project]
name = "sample#project" # comment outside a string
version = '1.0'
requires-python = ">=3.10"
dependencies = [
  "one>=1",
  "two>=2",
]

[tool.other]
metadata = {name = "two", enabled = true}

[project.scripts]
cli = "sample:main[arg]"

[project.gui-scripts]
gui = "sample.gui:run"

[project.entry-points."sample.plugins"]
plugin = "sample.plugins:factory.create"

[tool.envlens]
modules = ["json"]
compile = ["src"]
[tool.envlens.runtime]
imports = ["sample"]
entry_points = ["cli"]
interpreters = ["python"]
""",
    )

    result = project.inspect_pyproject(path)

    assert result["project"]["name"] == "sample#project"
    assert result["project"]["dependencies"] == ["one>=1", "two>=2"]
    assert result["entry_points"][0]["target"] == {
        "module": "sample",
        "attribute": "main",
    }
    assert result["entry_points"][0]["location"]["line"] == 15
    assert result["configuration"] == {
        "interpreters": ["python"],
        "imports": ["sample"],
        "compile_paths": ["src"],
        "entry_points": ["cli"],
    }
    assert project.inspect_project(path) == result
    assert project.load_pyproject(path) == result
    assert project.inspect_entry_points(path) == result["entry_points"]


def test_parser_private_boundaries_and_multiline_arrays() -> None:
    assert project._strip_comment('"a#b" # comment') == '"a#b"'
    assert project._strip_comment("'a#b' # comment") == "'a#b'"
    assert project._split_quoted('one."two.three".four', ".") == [
        "one",
        '"two.three"',
        "four",
    ]
    assert project._split_dotted('one."two.three"') == ["one", "two.three"]
    assert project._bracket_balance('["["]') == 0
    assert project._bracket_step("[", "", False, 0) == ("", False, 1)
    assert project._bracket_step("]", "", False, 1) == ("", False, 0)
    assert project._bracket_step("x", '"', True, 0) == ('"', False, 0)
    assert project._structure_transition("{", 0, 0) == (1, 0, False)
    assert project._structure_transition("}", 1, 0) == (0, 0, False)
    assert project._structure_transition("[", 0, 0) == (0, 1, False)
    assert project._structure_transition("]", 0, 1) == (0, 0, False)
    assert project._structure_transition(",", 0, 0) == (0, 0, True)
    assert project._structure_transition(",", 1, 0) == (1, 0, False)
    assert project._split_top_level('{a = [1, 2], b = "x,y"}, tail') == [
        '{a = [1, 2], b = "x,y"}',
        "tail",
    ]
    assert project._parse_inline_table('{a = 1, b = "two"}', line=1) == {
        "a": 1,
        "b": "two",
    }
    assert project._parse_array('["one", "two"]', line=1) == ["one", "two"]
    assert project._parse_array("[1, {ok = true}]", line=1) == [
        1,
        {"ok": True},
    ]
    assert project._parse_value("false", line=1) is False
    assert project._parse_value("3.5", line=1) == 3.5


@pytest.mark.parametrize(
    ("value", "pattern"),
    [
        ("{a = 1", "invalid inline TOML table"),
        ("{a}", "invalid inline TOML table"),
        ("{a = 1, a = 2}", "duplicate inline TOML key"),
        ("[1", "unterminated TOML array"),
    ],
)
def test_parser_rejects_malformed_structures(value: str, pattern: str) -> None:
    with pytest.raises(project.ProjectError, match=pattern):
        if value.startswith("{"):
            project._parse_inline_table(value, line=4)
        else:
            project._parse_toml(f"[project]\nitems = {value}\n")


def test_parser_rejects_unsupported_values_and_duplicate_tables() -> None:
    with pytest.raises(project.ProjectError, match="invalid bare TOML key"):
        project._decode_key('"unterminated')
    with pytest.raises(project.ProjectError, match="invalid bare TOML key"):
        project._decode_key("bad/key")
    with pytest.raises(project.ProjectError, match="unsupported bare TOML value"):
        project._parse_bare("2024-01-01", line=2)
    with pytest.raises(project.ProjectError, match="TOML string expected"):
        project._parse_string("1", line=2)
    with pytest.raises(project.ProjectError, match="array tables are unsupported"):
        project._table_header("[[project]]", 3)
    with pytest.raises(project.ProjectError, match="invalid TOML table"):
        project._table_header("[project]]", 3)
    with pytest.raises(project.ProjectError, match="expected key/value"):
        project._parse_toml("[project]\nnot-an-assignment\n")

    root: dict[str, object] = {}
    locations: dict[tuple[str, ...], int] = {}
    project._set_nested(root, ["project", "name"], "one", line=1, locations=locations)
    with pytest.raises(project.ProjectError, match="duplicates TOML key"):
        project._set_nested(root, ["project", "name"], "two", line=2, locations=locations)
    with pytest.raises(project.ProjectError, match="redefines a TOML table"):
        project._set_nested(root, ["project", "name", "child"], "x", line=3, locations=locations)
    project._ensure_table(root, ["new", "table"], 4)
    with pytest.raises(project.ProjectError, match="redefines a TOML table"):
        project._ensure_table({"project": "scalar"}, ["project", "name"], 5)


def test_metadata_and_entry_point_validation_boundaries() -> None:
    assert project._get_mapping({}, "missing") == {}
    assert project._get_mapping({"value": "scalar"}, "value") == {}
    assert project._string_field(None, "optional") == ""
    assert project._string_field("ok", "name", required=True) == "ok"
    with pytest.raises(project.ProjectError, match="is required"):
        project._string_field(None, "name", required=True)
    with pytest.raises(project.ProjectError, match="must be a string"):
        project._string_field(1, "name")
    with pytest.raises(project.ProjectError, match="must not be empty"):
        project._string_field("", "name", required=True)
    assert project._string_list(None, "items") == []
    assert project._string_list(["b", "a"], "items") == ["a", "b"]
    with pytest.raises(project.ProjectError, match="bounded string array"):
        project._string_list("items", "items")
    with pytest.raises(project.ProjectError, match="non-empty string"):
        project._string_list([""], "items")
    with pytest.raises(project.ProjectError, match="exceeds 65536"):
        project._string_list(["x" * (project.MAX_STRING_LENGTH + 1)], "items")

    assert project._entry_point_target("sample:main[flag]") == ("sample", "main")
    assert project._entry_point_target("sample") is None
    assert project._entry_point_target("bad-module:main") is None
    assert project._entry_point_target("sample:bad attr") is None


def test_project_read_errors_and_size_limits(tmp_path: Path) -> None:
    with pytest.raises(project.ProjectError, match="project-read-failed"):
        project.inspect_pyproject(tmp_path / "missing.toml")
    directory = tmp_path / "directory"
    directory.mkdir()
    with pytest.raises(project.ProjectError, match="regular file"):
        project.inspect_pyproject(directory)
    invalid = tmp_path / "invalid.toml"
    invalid.write_bytes(b"[project]\nname = \xff\n")
    with pytest.raises(project.ProjectError, match="project-read-failed"):
        project.inspect_pyproject(invalid)
    oversized = _write(tmp_path / "large.toml", "x" * 10)
    with (
        patch.object(project, "MAX_PYPROJECT_BYTES", 1),
        pytest.raises(project.ProjectError, match="project-too-large"),
    ):
        project.inspect_pyproject(oversized)
